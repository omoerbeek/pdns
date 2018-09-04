/*
 * This file is part of PowerDNS or dnsdist.
 * Copyright -- PowerDNS.COM B.V. and its contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * In addition, for the avoidance of any doubt, permission is granted to
 * link this program with OpenSSL and to (re)distribute the binaries
 * produced as the result of such linking.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "ws-recursor.hh"
#include "json.hh"

#include <string>
#include "namespaces.hh"
#include <iostream>
#include "iputils.hh"
#include "rec_channel.hh"
#include "arguments.hh"
#include "misc.hh"
#include "syncres.hh"
#include "dnsparser.hh"
#include "json11.hpp"
#include "webserver.hh"
#include "ws-api.hh"
#include "logger.hh"
#include "ext/incbin/incbin.h"
#include "rec-lua-conf.hh"
#include "rpzloader.hh"

extern thread_local FDMultiplexer* t_fdm;

using json11::Json;

void productServerStatisticsFetch(map<string,string>& out)
{
  map<string,string> stats = getAllStatsMap();
  out.swap(stats);
}

static void apiWriteConfigFile(const string& filebasename, const string& content)
{
  if (::arg()["api-config-dir"].empty()) {
    throw ApiException("Config Option \"api-config-dir\" must be set");
  }

  string filename = ::arg()["api-config-dir"] + "/" + filebasename + ".conf";
  ofstream ofconf(filename.c_str());
  if (!ofconf) {
    throw ApiException("Could not open config fragment file '"+filename+"' for writing: "+stringerror());
  }
  ofconf << "# Generated by pdns-recursor REST API, DO NOT EDIT" << endl;
  ofconf << content << endl;
  ofconf.close();
}

static void apiServerConfigAllowFrom(HttpRequest* req, HttpResponse* resp)
{
  if (req->method == "PUT") {
    Json document = req->json();

    auto jlist = document["value"];
    if (!jlist.is_array()) {
      throw ApiException("'value' must be an array");
    }

    NetmaskGroup nmg;
    for (auto value : jlist.array_items()) {
      try {
        nmg.addMask(value.string_value());
      } catch (const NetmaskException &e) {
        throw ApiException(e.reason);
      }
    }

    ostringstream ss;

    // Clear allow-from-file if set, so our changes take effect
    ss << "allow-from-file=" << endl;

    // Clear allow-from, and provide a "parent" value
    ss << "allow-from=" << endl;
    ss << "allow-from+=" << nmg.toString() << endl;

    apiWriteConfigFile("allow-from", ss.str());

    parseACLs();

    // fall through to GET
  } else if (req->method != "GET") {
    throw HttpMethodNotAllowedException();
  }

  // Return currently configured ACLs
  vector<string> entries;
  t_allowFrom->toStringVector(&entries);

  resp->setBody(Json::object {
    { "name", "allow-from" },
    { "value", entries },
  });
}

static void fillZone(const DNSName& zonename, HttpResponse* resp)
{
  auto iter = SyncRes::t_sstorage.domainmap->find(zonename);
  if (iter == SyncRes::t_sstorage.domainmap->end())
    throw ApiException("Could not find domain '"+zonename.toLogString()+"'");

  const SyncRes::AuthDomain& zone = iter->second;

  Json::array servers;
  for(const ComboAddress& server : zone.d_servers) {
    servers.push_back(server.toStringWithPort());
  }

  Json::array records;
  for(const SyncRes::AuthDomain::records_t::value_type& dr : zone.d_records) {
    records.push_back(Json::object {
      { "name", dr.d_name.toString() },
      { "type", DNSRecordContent::NumberToType(dr.d_type) },
      { "ttl", (double)dr.d_ttl },
      { "content", dr.d_content->getZoneRepresentation() }
    });
  }

  // id is the canonical lookup key, which doesn't actually match the name (in some cases)
  string zoneId = apiZoneNameToId(iter->first);
  Json::object doc = {
    { "id", zoneId },
    { "url", "/api/v1/servers/localhost/zones/" + zoneId },
    { "name", iter->first.toString() },
    { "kind", zone.d_servers.empty() ? "Native" : "Forwarded" },
    { "servers", servers },
    { "recursion_desired", zone.d_servers.empty() ? false : zone.d_rdForward },
    { "records", records }
  };

  resp->setBody(doc);
}

static void doCreateZone(const Json document)
{
  if (::arg()["api-config-dir"].empty()) {
    throw ApiException("Config Option \"api-config-dir\" must be set");
  }

  DNSName zonename = apiNameToDNSName(stringFromJson(document, "name"));
  apiCheckNameAllowedCharacters(zonename.toString());

  string singleIPTarget = document["single_target_ip"].string_value();
  string kind = toUpper(stringFromJson(document, "kind"));
  bool rd = boolFromJson(document, "recursion_desired");
  string confbasename = "zone-" + apiZoneNameToId(zonename);

  if (kind == "NATIVE") {
    if (rd)
      throw ApiException("kind=Native and recursion_desired are mutually exclusive");
    if(!singleIPTarget.empty()) {
      try {
	ComboAddress rem(singleIPTarget);
	if(rem.sin4.sin_family != AF_INET)
	  throw ApiException("");
	singleIPTarget = rem.toString();
      }
      catch(...) {
	throw ApiException("Single IP target '"+singleIPTarget+"' is invalid");
      }
    }
    string zonefilename = ::arg()["api-config-dir"] + "/" + confbasename + ".zone";
    ofstream ofzone(zonefilename.c_str());
    if (!ofzone) {
      throw ApiException("Could not open '"+zonefilename+"' for writing: "+stringerror());
    }
    ofzone << "; Generated by pdns-recursor REST API, DO NOT EDIT" << endl;
    ofzone << zonename << "\tIN\tSOA\tlocal.zone.\thostmaster."<<zonename<<" 1 1 1 1 1" << endl;
    if(!singleIPTarget.empty()) {
      ofzone <<zonename << "\t3600\tIN\tA\t"<<singleIPTarget<<endl;
      ofzone <<"*."<<zonename << "\t3600\tIN\tA\t"<<singleIPTarget<<endl;
    }
    ofzone.close();

    apiWriteConfigFile(confbasename, "auth-zones+=" + zonename.toString() + "=" + zonefilename);
  } else if (kind == "FORWARDED") {
    string serverlist;
    for (auto value : document["servers"].array_items()) {
      string server = value.string_value();
      if (server == "") {
        throw ApiException("Forwarded-to server must not be an empty string");
      }
      try {
        ComboAddress ca = parseIPAndPort(server, 53);
        if (!serverlist.empty()) {
          serverlist += ";";
        }
        serverlist += ca.toStringWithPort();
      } catch (const PDNSException &e) {
        throw ApiException(e.reason);
      }
    }
    if (serverlist == "")
      throw ApiException("Need at least one upstream server when forwarding");

    if (rd) {
      apiWriteConfigFile(confbasename, "forward-zones-recurse+=" + zonename.toString() + "=" + serverlist);
    } else {
      apiWriteConfigFile(confbasename, "forward-zones+=" + zonename.toString() + "=" + serverlist);
    }
  } else {
    throw ApiException("invalid kind");
  }
}

static bool doDeleteZone(const DNSName& zonename)
{
  if (::arg()["api-config-dir"].empty()) {
    throw ApiException("Config Option \"api-config-dir\" must be set");
  }

  string filename;

  // this one must exist
  filename = ::arg()["api-config-dir"] + "/zone-" + apiZoneNameToId(zonename) + ".conf";
  if (unlink(filename.c_str()) != 0) {
    return false;
  }

  // .zone file is optional
  filename = ::arg()["api-config-dir"] + "/zone-" + apiZoneNameToId(zonename) + ".zone";
  unlink(filename.c_str());

  return true;
}

static void apiServerZones(HttpRequest* req, HttpResponse* resp)
{
  if (req->method == "POST") {
    if (::arg()["api-config-dir"].empty()) {
      throw ApiException("Config Option \"api-config-dir\" must be set");
    }

    Json document = req->json();

    DNSName zonename = apiNameToDNSName(stringFromJson(document, "name"));

    auto iter = SyncRes::t_sstorage.domainmap->find(zonename);
    if (iter != SyncRes::t_sstorage.domainmap->end())
      throw ApiException("Zone already exists");

    doCreateZone(document);
    reloadAuthAndForwards();
    fillZone(zonename, resp);
    resp->status = 201;
    return;
  }

  if(req->method != "GET")
    throw HttpMethodNotAllowedException();

  Json::array doc;
  for(const SyncRes::domainmap_t::value_type& val :  *SyncRes::t_sstorage.domainmap) {
    const SyncRes::AuthDomain& zone = val.second;
    Json::array servers;
    for(const ComboAddress& server : zone.d_servers) {
      servers.push_back(server.toStringWithPort());
    }
    // id is the canonical lookup key, which doesn't actually match the name (in some cases)
    string zoneId = apiZoneNameToId(val.first);
    doc.push_back(Json::object {
      { "id", zoneId },
      { "url", "/api/v1/servers/localhost/zones/" + zoneId },
      { "name", val.first.toString() },
      { "kind", zone.d_servers.empty() ? "Native" : "Forwarded" },
      { "servers", servers },
      { "recursion_desired", zone.d_servers.empty() ? false : zone.d_rdForward }
    });
  }
  resp->setBody(doc);
}

static void apiServerZoneDetail(HttpRequest* req, HttpResponse* resp)
{
  DNSName zonename = apiZoneIdToName(req->parameters["id"]);

  SyncRes::domainmap_t::const_iterator iter = SyncRes::t_sstorage.domainmap->find(zonename);
  if (iter == SyncRes::t_sstorage.domainmap->end())
    throw ApiException("Could not find domain '"+zonename.toLogString()+"'");

  if(req->method == "PUT") {
    Json document = req->json();

    doDeleteZone(zonename);
    doCreateZone(document);
    reloadAuthAndForwards();
    resp->body = "";
    resp->status = 204; // No Content, but indicate success
  }
  else if(req->method == "DELETE") {
    if (!doDeleteZone(zonename)) {
      throw ApiException("Deleting domain failed");
    }

    reloadAuthAndForwards();
    // empty body on success
    resp->body = "";
    resp->status = 204; // No Content: declare that the zone is gone now
  } else if(req->method == "GET") {
    fillZone(zonename, resp);
  } else {
    throw HttpMethodNotAllowedException();
  }
}

static void apiServerSearchData(HttpRequest* req, HttpResponse* resp) {
  if(req->method != "GET")
    throw HttpMethodNotAllowedException();

  string q = req->getvars["q"];
  if (q.empty())
    throw ApiException("Query q can't be blank");

  Json::array doc;
  for(const SyncRes::domainmap_t::value_type& val : *SyncRes::t_sstorage.domainmap) {
    string zoneId = apiZoneNameToId(val.first);
    string zoneName = val.first.toString();
    if (pdns_ci_find(zoneName, q) != string::npos) {
      doc.push_back(Json::object {
        { "type", "zone" },
        { "zone_id", zoneId },
        { "name", zoneName }
      });
    }

    // if zone name is an exact match, don't bother with returning all records/comments in it
    if (val.first == DNSName(q)) {
      continue;
    }

    const SyncRes::AuthDomain& zone = val.second;

    for(const SyncRes::AuthDomain::records_t::value_type& rr : zone.d_records) {
      if (pdns_ci_find(rr.d_name.toString(), q) == string::npos && pdns_ci_find(rr.d_content->getZoneRepresentation(), q) == string::npos)
        continue;

      doc.push_back(Json::object {
        { "type", "record" },
        { "zone_id", zoneId },
        { "zone_name", zoneName },
        { "name", rr.d_name.toString() },
        { "content", rr.d_content->getZoneRepresentation() }
      });
    }
  }
  resp->setBody(doc);
}

static void apiServerCacheFlush(HttpRequest* req, HttpResponse* resp) {
  if(req->method != "PUT")
    throw HttpMethodNotAllowedException();

  DNSName canon = apiNameToDNSName(req->getvars["domain"]);
  bool subtree = (req->getvars.count("subtree") > 0 && req->getvars["subtree"].compare("true") == 0);

  int count = broadcastAccFunction<uint64_t>(boost::bind(pleaseWipeCache, canon, subtree));
  count += broadcastAccFunction<uint64_t>(boost::bind(pleaseWipePacketCache, canon, subtree));
  count += broadcastAccFunction<uint64_t>(boost::bind(pleaseWipeAndCountNegCache, canon, subtree));
  resp->setBody(Json::object {
    { "count", count },
    { "result", "Flushed cache." }
  });
}

static void apiServerRPZStats(HttpRequest* req, HttpResponse* resp) {
  if(req->method != "GET")
    throw HttpMethodNotAllowedException();

  auto luaconf = g_luaconfs.getLocal();
  auto numZones = luaconf->dfe.size();

  Json::object ret;

  for (size_t i=0; i < numZones; i++) {
    auto zone = luaconf->dfe.getZone(i);
    if (zone == nullptr)
      continue;
    auto name = zone->getName();
    auto stats = getRPZZoneStats(*name);
    if (stats == nullptr)
      continue;
    Json::object zoneInfo = {
      {"transfers_failed", (double)stats->d_failedTransfers},
      {"transfers_success", (double)stats->d_successfulTransfers},
      {"transfers_full", (double)stats->d_fullTransfers},
      {"records", (double)stats->d_numberOfRecords},
      {"last_update", (double)stats->d_lastUpdate},
      {"serial", (double)stats->d_serial},
    };
    ret[*name] = zoneInfo;
  }
  resp->setBody(ret);
}

#include "htmlfiles.h"

static void serveStuff(HttpRequest* req, HttpResponse* resp)
{
  resp->headers["Cache-Control"] = "max-age=86400";

  if(req->url.path == "/")
    req->url.path = "/index.html";

  const string charset = "; charset=utf-8";
  if(boost::ends_with(req->url.path, ".html"))
    resp->headers["Content-Type"] = "text/html" + charset;
  else if(boost::ends_with(req->url.path, ".css"))
    resp->headers["Content-Type"] = "text/css" + charset;
  else if(boost::ends_with(req->url.path,".js"))
    resp->headers["Content-Type"] = "application/javascript" + charset;
  else if(boost::ends_with(req->url.path, ".png"))
    resp->headers["Content-Type"] = "image/png";

  resp->headers["X-Content-Type-Options"] = "nosniff";
  resp->headers["X-Frame-Options"] = "deny";
  resp->headers["X-Permitted-Cross-Domain-Policies"] = "none";

  resp->headers["X-XSS-Protection"] = "1; mode=block";
  //  resp->headers["Content-Security-Policy"] = "default-src 'self'; style-src 'self' 'unsafe-inline'";

  resp->body = g_urlmap[req->url.path.c_str()+1];
  resp->status = 200;
}


RecursorWebServer::RecursorWebServer(FDMultiplexer* fdm)
{
  registerAllStats();

  d_ws = new AsyncWebServer(fdm, arg()["webserver-address"], arg().asNum("webserver-port"));
  d_ws->bind();

  // legacy dispatch
  d_ws->registerApiHandler("/jsonstat", boost::bind(&RecursorWebServer::jsonstat, this, _1, _2));
  d_ws->registerApiHandler("/api/v1/servers/localhost/cache/flush", &apiServerCacheFlush);
  d_ws->registerApiHandler("/api/v1/servers/localhost/config/allow-from", &apiServerConfigAllowFrom);
  d_ws->registerApiHandler("/api/v1/servers/localhost/config", &apiServerConfig);
  d_ws->registerApiHandler("/api/v1/servers/localhost/rpzstatistics", &apiServerRPZStats);
  d_ws->registerApiHandler("/api/v1/servers/localhost/search-log", &apiServerSearchLog);
  d_ws->registerApiHandler("/api/v1/servers/localhost/search-data", &apiServerSearchData);
  d_ws->registerApiHandler("/api/v1/servers/localhost/statistics", &apiServerStatistics);
  d_ws->registerApiHandler("/api/v1/servers/localhost/zones/<id>", &apiServerZoneDetail);
  d_ws->registerApiHandler("/api/v1/servers/localhost/zones", &apiServerZones);
  d_ws->registerApiHandler("/api/v1/servers/localhost", &apiServerDetail);
  d_ws->registerApiHandler("/api/v1/servers", &apiServer);
  d_ws->registerApiHandler("/api", &apiDiscovery);

  for(const auto& u : g_urlmap) 
    d_ws->registerWebHandler("/"+u.first, serveStuff);
  d_ws->registerWebHandler("/", serveStuff);
  d_ws->go();
}

void RecursorWebServer::jsonstat(HttpRequest* req, HttpResponse *resp)
{
  string command;

  if(req->getvars.count("command")) {
    command = req->getvars["command"];
    req->getvars.erase("command");
  }

  map<string, string> stats;
  if(command == "get-query-ring") {
    typedef pair<DNSName,uint16_t> query_t;
    vector<query_t> queries;
    bool filter=!req->getvars["public-filtered"].empty();

    if(req->getvars["name"]=="servfail-queries")
      queries=broadcastAccFunction<vector<query_t> >(pleaseGetServfailQueryRing);
    if(req->getvars["name"]=="bogus-queries")
      queries=broadcastAccFunction<vector<query_t> >(pleaseGetBogusQueryRing);
    else if(req->getvars["name"]=="queries")
      queries=broadcastAccFunction<vector<query_t> >(pleaseGetQueryRing);

    typedef map<query_t,unsigned int> counts_t;
    counts_t counts;
    unsigned int total=0;
    for(const query_t& q :  queries) {
      total++;
      if(filter)
	counts[make_pair(getRegisteredName(q.first), q.second)]++;
      else
	counts[make_pair(q.first, q.second)]++;
    }

    typedef std::multimap<int, query_t> rcounts_t;
    rcounts_t rcounts;

    for(counts_t::const_iterator i=counts.begin(); i != counts.end(); ++i)
      rcounts.insert(make_pair(-i->second, i->first));

    Json::array entries;
    unsigned int tot=0, totIncluded=0;
    for(const rcounts_t::value_type& q :  rcounts) {
      totIncluded-=q.first;
      entries.push_back(Json::array {
        -q.first, q.second.first.toString(), DNSRecordContent::NumberToType(q.second.second)
      });
      if(tot++>=100)
	break;
    }
    if(queries.size() != totIncluded) {
      entries.push_back(Json::array {
        (int)(queries.size() - totIncluded), "", ""
      });
    }
    resp->setBody(Json::object { { "entries", entries } });
    return;
  }
  else if(command == "get-remote-ring") {
    vector<ComboAddress> queries;
    if(req->getvars["name"]=="remotes")
      queries=broadcastAccFunction<vector<ComboAddress> >(pleaseGetRemotes);
    else if(req->getvars["name"]=="servfail-remotes")
      queries=broadcastAccFunction<vector<ComboAddress> >(pleaseGetServfailRemotes);
    else if(req->getvars["name"]=="bogus-remotes")
      queries=broadcastAccFunction<vector<ComboAddress> >(pleaseGetBogusRemotes);
    else if(req->getvars["name"]=="large-answer-remotes")
      queries=broadcastAccFunction<vector<ComboAddress> >(pleaseGetLargeAnswerRemotes);
    else if(req->getvars["name"]=="timeouts")
      queries=broadcastAccFunction<vector<ComboAddress> >(pleaseGetTimeouts);

    typedef map<ComboAddress,unsigned int,ComboAddress::addressOnlyLessThan> counts_t;
    counts_t counts;
    unsigned int total=0;
    for(const ComboAddress& q :  queries) {
      total++;
      counts[q]++;
    }

    typedef std::multimap<int, ComboAddress> rcounts_t;
    rcounts_t rcounts;

    for(counts_t::const_iterator i=counts.begin(); i != counts.end(); ++i)
      rcounts.insert(make_pair(-i->second, i->first));

    Json::array entries;
    unsigned int tot=0, totIncluded=0;
    for(const rcounts_t::value_type& q :  rcounts) {
      totIncluded-=q.first;
      entries.push_back(Json::array {
        -q.first, q.second.toString()
      });
      if(tot++>=100)
	break;
    }
    if(queries.size() != totIncluded) {
      entries.push_back(Json::array {
        (int)(queries.size() - totIncluded), ""
      });
    }

    resp->setBody(Json::object { { "entries", entries } });
    return;
  } else {
    resp->setErrorResult("Command '"+command+"' not found", 404);
  }
}


void AsyncServerNewConnectionMT(void *p) {
  AsyncServer *server = (AsyncServer*)p;
  
  try {
    auto socket = server->accept(); // this is actually a shared_ptr
    if (socket) {
      server->d_asyncNewConnectionCallback(socket);
    }
  } catch (NetworkError &e) {
    // we're running in a shared process/thread, so can't just terminate/abort.
    g_log<<Logger::Warning<<"Network error in web thread: "<<e.what()<<endl;
    return;
  }
  catch (...) {
    g_log<<Logger::Warning<<"Unknown error in web thread"<<endl;

    return;
  }

}

void AsyncServer::asyncWaitForConnections(FDMultiplexer* fdm, const newconnectioncb_t& callback)
{
  d_asyncNewConnectionCallback = callback;
  fdm->addReadFD(d_server_socket.getHandle(), boost::bind(&AsyncServer::newConnection, this));
}

void AsyncServer::newConnection()
{
  getMT()->makeThread(&AsyncServerNewConnectionMT, this);
}

// This is an entry point from FDM, so it needs to catch everything.
void AsyncWebServer::serveConnection(std::shared_ptr<Socket> client) const
try {
  HttpRequest req;
  YaHTTP::AsyncRequestLoader yarl;
  yarl.initialize(&req);
  client->setNonBlocking();

  string data;
  try {
    while(!req.complete) {
      int bytes = arecvtcp(data, 16384, client.get(), true);
      if (bytes > 0) {
        req.complete = yarl.feed(data);
      } else {
        // read error OR EOF
        break;
      }
    }
    yarl.finalize();
  } catch (YaHTTP::ParseError &e) {
    // request stays incomplete
  }

  HttpResponse resp;
  handleRequest(req, resp);
  ostringstream ss;
  resp.write(ss);
  data = ss.str();

  // now send the reply
  if (asendtcp(data, client.get()) == -1 || data.empty()) {
    g_log<<Logger::Error<<"Failed sending reply to HTTP client"<<endl;
  }
}
catch(PDNSException &e) {
  g_log<<Logger::Error<<"HTTP Exception: "<<e.reason<<endl;
}
catch(std::exception &e) {
  if(strstr(e.what(), "timeout")==0)
    g_log<<Logger::Error<<"HTTP STL Exception: "<<e.what()<<endl;
}
catch(...) {
  g_log<<Logger::Error<<"HTTP: Unknown exception"<<endl;
}

void AsyncWebServer::go() {
  if (!d_server)
    return;
  auto server = std::dynamic_pointer_cast<AsyncServer>(d_server);
  if (!server)
    return;
  server->asyncWaitForConnections(d_fdm, boost::bind(&AsyncWebServer::serveConnection, this, _1));
}
