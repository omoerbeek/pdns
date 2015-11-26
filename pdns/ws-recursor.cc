/*
    PowerDNS Versatile Database Driven Nameserver
    Copyright (C) 2003 - 2014  PowerDNS.COM BV

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2
    as published by the Free Software Foundation

    Additionally, the license of this program contains a special
    exception which allows to distribute the program in binary form when
    it is linked against OpenSSL.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "ws-recursor.hh"
#include "json.hh"
#include <boost/foreach.hpp>
#include <string>
#include "namespaces.hh"
#include <iostream>
#include "iputils.hh"
#include "rec_channel.hh"
#include "arguments.hh"
#include "misc.hh"
#include "syncres.hh"
#include "dnsparser.hh"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "webserver.hh"
#include "ws-api.hh"
#include "logger.hh"

extern __thread FDMultiplexer* t_fdm;

using namespace rapidjson;

void productServerStatisticsFetch(map<string,string>& out)
{
  map<string,string> stats = getAllStatsMap();
  out.swap(stats);
}

static void apiWriteConfigFile(const string& filebasename, const string& content)
{
  if (::arg()["experimental-api-config-dir"].empty()) {
    throw ApiException("Config Option \"experimental-api-config-dir\" must be set");
  }

  string filename = ::arg()["experimental-api-config-dir"] + "/" + filebasename + ".conf";
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
  if (req->method == "PUT" && !::arg().mustDo("experimental-api-readonly")) {
    Document document;
    req->json(document);
    const Value &jlist = document["value"];

    if (!document.IsObject()) {
      throw ApiException("Supplied JSON must be an object");
    }

    if (!jlist.IsArray()) {
      throw ApiException("'value' must be an array");
    }

    for (SizeType i = 0; i < jlist.Size(); ++i) {
      try {
        Netmask(jlist[i].GetString());
      } catch (NetmaskException &e) {
        throw ApiException(e.reason);
      }
    }

    ostringstream ss;

    // Clear allow-from-file if set, so our changes take effect
    ss << "allow-from-file=" << endl;

    // Clear allow-from, and provide a "parent" value
    ss << "allow-from=" << endl;
    for (SizeType i = 0; i < jlist.Size(); ++i) {
      ss << "allow-from+=" << jlist[i].GetString() << endl;
    }

    apiWriteConfigFile("allow-from", ss.str());

    parseACLs();

    // fall through to GET
  } else if (req->method != "GET") {
    throw HttpMethodNotAllowedException();
  }

  // Return currently configured ACLs
  Document document;
  document.SetObject();

  Value jlist;
  jlist.SetArray();

  vector<string> entries;
  t_allowFrom->toStringVector(&entries);

  for(const string& entry :  entries) {
    Value jentry(entry.c_str(), document.GetAllocator()); // copy
    jlist.PushBack(jentry, document.GetAllocator());
  }

  document.AddMember("name", "allow-from", document.GetAllocator());
  document.AddMember("value", jlist, document.GetAllocator());

  resp->setBody(document);
}

static void fillZone(const DNSName& zonename, HttpResponse* resp)
{
  auto iter = t_sstorage->domainmap->find(zonename);
  if (iter == t_sstorage->domainmap->end())
    throw ApiException("Could not find domain '"+zonename.toString()+"'");

  Document doc;
  doc.SetObject();

  const SyncRes::AuthDomain& zone = iter->second;

  // id is the canonical lookup key, which doesn't actually match the name (in some cases)
  string zoneId = apiZoneNameToId(iter->first);
  Value jzoneid(zoneId.c_str(), doc.GetAllocator()); // copy
  doc.AddMember("id", jzoneid, doc.GetAllocator());
  string url = "/servers/localhost/zones/" + zoneId;
  Value jurl(url.c_str(), doc.GetAllocator()); // copy
  doc.AddMember("url", jurl, doc.GetAllocator());
  Value jname(iter->first.toString().c_str(), doc.GetAllocator()); // copy
  doc.AddMember("name", jname, doc.GetAllocator());
  doc.AddMember("kind", zone.d_servers.empty() ? "Native" : "Forwarded", doc.GetAllocator());
  Value servers;
  servers.SetArray();
  for(const ComboAddress& server :  zone.d_servers) {
    Value value(server.toStringWithPort().c_str(), doc.GetAllocator());
    servers.PushBack(value, doc.GetAllocator());
  }
  doc.AddMember("servers", servers, doc.GetAllocator());
  bool rd = zone.d_servers.empty() ? false : zone.d_rdForward;
  doc.AddMember("recursion_desired", rd, doc.GetAllocator());

  Value records;
  records.SetArray();
  for(const SyncRes::AuthDomain::records_t::value_type& dr :  zone.d_records) {
    Value object;
    object.SetObject();
    Value jname(dr.d_name.toString().c_str(), doc.GetAllocator()); // copy
    object.AddMember("name", jname, doc.GetAllocator());
    Value jtype(DNSRecordContent::NumberToType(dr.d_type).c_str(), doc.GetAllocator()); // copy
    object.AddMember("type", jtype, doc.GetAllocator());
    object.AddMember("ttl", dr.d_ttl, doc.GetAllocator());
    Value jcontent(dr.d_content->getZoneRepresentation().c_str(), doc.GetAllocator()); // copy
    object.AddMember("content", jcontent, doc.GetAllocator());
    records.PushBack(object, doc.GetAllocator());
  }
  doc.AddMember("records", records, doc.GetAllocator());

  resp->setBody(doc);
}

static void doCreateZone(const Value& document)
{
  if (::arg()["experimental-api-config-dir"].empty()) {
    throw ApiException("Config Option \"experimental-api-config-dir\" must be set");
  }

  if(stringFromJson(document, "name").empty())
    throw ApiException("Zone name empty");
  
  DNSName zonename(stringFromJson(document, "name"));

  string singleIPTarget = stringFromJson(document, "single_target_ip", "");
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
    string zonefilename = ::arg()["experimental-api-config-dir"] + "/" + confbasename + ".zone";
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
    const Value &servers = document["servers"];
    if (kind == "FORWARDED" && (!servers.IsArray() || servers.Size() == 0))
      throw ApiException("Need at least one upstream server when forwarding");

    string serverlist;
    if (servers.IsArray()) {
      for (SizeType i = 0; i < servers.Size(); ++i) {
        if (!serverlist.empty()) {
          serverlist += ";";
        }
        serverlist += servers[i].GetString();
      }
    }

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
  if (::arg()["experimental-api-config-dir"].empty()) {
    throw ApiException("Config Option \"experimental-api-config-dir\" must be set");
  }

  string filename;

  // this one must exist
  filename = ::arg()["experimental-api-config-dir"] + "/zone-" + apiZoneNameToId(zonename) + ".conf";
  if (unlink(filename.c_str()) != 0) {
    return false;
  }

  // .zone file is optional
  filename = ::arg()["experimental-api-config-dir"] + "/zone-" + apiZoneNameToId(zonename) + ".zone";
  unlink(filename.c_str());

  return true;
}

static void apiServerZones(HttpRequest* req, HttpResponse* resp)
{
  if (req->method == "POST" && !::arg().mustDo("experimental-api-readonly")) {
    if (::arg()["experimental-api-config-dir"].empty()) {
      throw ApiException("Config Option \"experimental-api-config-dir\" must be set");
    }

    Document document;
    req->json(document);

    DNSName zonename(stringFromJson(document, "name"));

    auto iter = t_sstorage->domainmap->find(zonename);
    if (iter != t_sstorage->domainmap->end())
      throw ApiException("Zone already exists");

    doCreateZone(document);
    reloadAuthAndForwards();
    fillZone(zonename, resp);
    resp->status = 201;
    return;
  }

  if(req->method != "GET")
    throw HttpMethodNotAllowedException();

  Document doc;
  doc.SetArray();

  for(const SyncRes::domainmap_t::value_type& val :  *t_sstorage->domainmap) {
    const SyncRes::AuthDomain& zone = val.second;
    Value jdi;
    jdi.SetObject();
    // id is the canonical lookup key, which doesn't actually match the name (in some cases)
    string zoneId = apiZoneNameToId(val.first);
    Value jzoneid(zoneId.c_str(), doc.GetAllocator()); // copy
    jdi.AddMember("id", jzoneid, doc.GetAllocator());
    string url = "/servers/localhost/zones/" + zoneId;
    Value jurl(url.c_str(), doc.GetAllocator()); // copy
    jdi.AddMember("url", jurl, doc.GetAllocator());
    jdi.AddMember("name", val.first.toString().c_str(), doc.GetAllocator());
    jdi.AddMember("kind", zone.d_servers.empty() ? "Native" : "Forwarded", doc.GetAllocator());
    Value servers;
    servers.SetArray();
    for(const ComboAddress& server :  zone.d_servers) {
      Value value(server.toStringWithPort().c_str(), doc.GetAllocator());
      servers.PushBack(value, doc.GetAllocator());
    }
    jdi.AddMember("servers", servers, doc.GetAllocator());
    bool rd = zone.d_servers.empty() ? false : zone.d_rdForward;
    jdi.AddMember("recursion_desired", rd, doc.GetAllocator());
    doc.PushBack(jdi, doc.GetAllocator());
  }
  resp->setBody(doc);
}

static void apiServerZoneDetail(HttpRequest* req, HttpResponse* resp)
{
  DNSName zonename = apiZoneIdToName(req->parameters["id"]);

  SyncRes::domainmap_t::const_iterator iter = t_sstorage->domainmap->find(zonename);
  if (iter == t_sstorage->domainmap->end())
    throw ApiException("Could not find domain '"+zonename.toString()+"'");

  if(req->method == "PUT" && !::arg().mustDo("experimental-api-readonly")) {
    Document document;
    req->json(document);

    doDeleteZone(zonename);
    doCreateZone(document);
    reloadAuthAndForwards();
    fillZone(DNSName(stringFromJson(document, "name")), resp);
  }
  else if(req->method == "DELETE" && !::arg().mustDo("experimental-api-readonly")) {
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

  Document doc;
  doc.SetArray();

  for(const SyncRes::domainmap_t::value_type& val :  *t_sstorage->domainmap) {
    string zoneId = apiZoneNameToId(val.first);
    if (pdns_ci_find(val.first.toString(), q) != string::npos) {
      Value object;
      object.SetObject();
      object.AddMember("type", "zone", doc.GetAllocator());
      Value jzoneId(zoneId.c_str(), doc.GetAllocator()); // copy
      object.AddMember("zone_id", jzoneId, doc.GetAllocator());
      Value jzoneName(val.first.toString().c_str(), doc.GetAllocator()); // copy
      object.AddMember("name", jzoneName, doc.GetAllocator());
      doc.PushBack(object, doc.GetAllocator());
    }

    // if zone name is an exact match, don't bother with returning all records/comments in it
    if (val.first == DNSName(q)) {
      continue;
    }

    const SyncRes::AuthDomain& zone = val.second;

    for(const SyncRes::AuthDomain::records_t::value_type& rr :  zone.d_records) {
      if (pdns_ci_find(rr.d_name.toString(), q) == string::npos && pdns_ci_find(rr.d_content->getZoneRepresentation(), q) == string::npos)
        continue;

      Value object;
      object.SetObject();
      object.AddMember("type", "record", doc.GetAllocator());
      Value jzoneId(zoneId.c_str(), doc.GetAllocator()); // copy
      object.AddMember("zone_id", jzoneId, doc.GetAllocator());
      Value jzoneName(val.first.toString().c_str(), doc.GetAllocator()); // copy
      object.AddMember("zone_name", jzoneName, doc.GetAllocator());
      Value jname(rr.d_name.toString().c_str(), doc.GetAllocator()); // copy
      object.AddMember("name", jname, doc.GetAllocator());
      Value jcontent(rr.d_content->getZoneRepresentation().c_str(), doc.GetAllocator()); // copy
      object.AddMember("content", jcontent, doc.GetAllocator());

      doc.PushBack(object, doc.GetAllocator());
    }
  }
  resp->setBody(doc);
}

static void apiServerFlushCache(HttpRequest* req, HttpResponse* resp) {
  if(req->method != "PUT")
    throw HttpMethodNotAllowedException();

  DNSName canon(req->getvars["domain"]);
  int count = broadcastAccFunction<uint64_t>(boost::bind(pleaseWipeCache, canon, false));
  count += broadcastAccFunction<uint64_t>(boost::bind(pleaseWipeAndCountNegCache, canon, false));
  map<string, string> object;
  object["count"] = lexical_cast<string>(count);
  object["result"] = "Flushed cache.";
  resp->body = returnJsonObject(object);
}

RecursorWebServer::RecursorWebServer(FDMultiplexer* fdm)
{
  RecursorControlParser rcp; // inits

  d_ws = new AsyncWebServer(fdm, arg()["experimental-webserver-address"], arg().asNum("experimental-webserver-port"));
  d_ws->bind();

  // legacy dispatch
  d_ws->registerApiHandler("/jsonstat", boost::bind(&RecursorWebServer::jsonstat, this, _1, _2));
  d_ws->registerApiHandler("/servers/localhost/flush-cache", &apiServerFlushCache);
  d_ws->registerApiHandler("/servers/localhost/config/allow-from", &apiServerConfigAllowFrom);
  d_ws->registerApiHandler("/servers/localhost/config", &apiServerConfig);
  d_ws->registerApiHandler("/servers/localhost/search-log", &apiServerSearchLog);
  d_ws->registerApiHandler("/servers/localhost/search-data", &apiServerSearchData);
  d_ws->registerApiHandler("/servers/localhost/statistics", &apiServerStatistics);
  d_ws->registerApiHandler("/servers/localhost/zones/<id>", &apiServerZoneDetail);
  d_ws->registerApiHandler("/servers/localhost/zones", &apiServerZones);
  d_ws->registerApiHandler("/servers/localhost", &apiServerDetail);
  d_ws->registerApiHandler("/servers", &apiServer);

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

    Document doc;
    doc.SetObject();
    Value entries;
    entries.SetArray();
    unsigned int tot=0, totIncluded=0;
    for(const rcounts_t::value_type& q :  rcounts) {
      Value arr;

      arr.SetArray();
      totIncluded-=q.first;
      arr.PushBack(-q.first, doc.GetAllocator());
      arr.PushBack(q.second.first.toString().c_str(), doc.GetAllocator());
      arr.PushBack(DNSRecordContent::NumberToType(q.second.second).c_str(), doc.GetAllocator());
      entries.PushBack(arr, doc.GetAllocator());
      if(tot++>=100)
	break;
    }
    if(queries.size() != totIncluded) {
      Value arr;
      arr.SetArray();
      arr.PushBack((unsigned int)(queries.size()-totIncluded), doc.GetAllocator());
      arr.PushBack("", doc.GetAllocator());
      arr.PushBack("", doc.GetAllocator());
      entries.PushBack(arr, doc.GetAllocator());
    }
    doc.AddMember("entries", entries, doc.GetAllocator());
    resp->setBody(doc);
    return;
  }
  else if(command == "get-remote-ring") {
    vector<ComboAddress> queries;
    if(req->getvars["name"]=="remotes")
      queries=broadcastAccFunction<vector<ComboAddress> >(pleaseGetRemotes);
    else if(req->getvars["name"]=="servfail-remotes")
      queries=broadcastAccFunction<vector<ComboAddress> >(pleaseGetServfailRemotes);
    else if(req->getvars["name"]=="large-answer-remotes")
      queries=broadcastAccFunction<vector<ComboAddress> >(pleaseGetLargeAnswerRemotes);

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


    Document doc;
    doc.SetObject();
    Value entries;
    entries.SetArray();
    unsigned int tot=0, totIncluded=0;
    for(const rcounts_t::value_type& q :  rcounts) {
      totIncluded-=q.first;
      Value arr;

      arr.SetArray();
      arr.PushBack(-q.first, doc.GetAllocator());
      Value jname(q.second.toString().c_str(), doc.GetAllocator()); // copy
      arr.PushBack(jname, doc.GetAllocator());
      entries.PushBack(arr, doc.GetAllocator());
      if(tot++>=100)
	break;
    }
    if(queries.size() != totIncluded) {
      Value arr;
      arr.SetArray();
      arr.PushBack((unsigned int)(queries.size()-totIncluded), doc.GetAllocator());
      arr.PushBack("", doc.GetAllocator());
      entries.PushBack(arr, doc.GetAllocator());
    }

    doc.AddMember("entries", entries, doc.GetAllocator());
    resp->setBody(doc);
    return;
  } else {
    resp->status = 404;
    resp->body = returnJsonError("Command '"+command+"' not found");
  }
}


void AsyncServerNewConnectionMT(void *p) {
  AsyncServer *server = (AsyncServer*)p;
  try {
    Socket* socket = server->accept();
    server->d_asyncNewConnectionCallback(socket);
    delete socket;
  } catch (NetworkError &e) {
    // we're running in a shared process/thread, so can't just terminate/abort.
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
  MT->makeThread(&AsyncServerNewConnectionMT, this);
}


void AsyncWebServer::serveConnection(Socket *client)
{
  HttpRequest req;
  YaHTTP::AsyncRequestLoader yarl;
  yarl.initialize(&req);
  client->setNonBlocking();

  string data;
  try {
    while(!req.complete) {
      data.clear();
      int bytes = arecvtcp(data, 16384, client, true);
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

  HttpResponse resp = handleRequest(req);
  ostringstream ss;
  resp.write(ss);
  data = ss.str();

  // now send the reply
  if (asendtcp(data, client) == -1 || data.empty()) {
    L<<Logger::Error<<"Failed sending reply to HTTP client"<<endl;
  }
}

void AsyncWebServer::go() {
  if (!d_server)
    return;
  ((AsyncServer*)d_server)->asyncWaitForConnections(d_fdm, boost::bind(&AsyncWebServer::serveConnection, this, _1));
}
