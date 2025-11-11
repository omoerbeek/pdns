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
#pragma once

#include <set>
#include <boost/variant.hpp>

#include "sholder.hh"
#include "sortlist.hh"
#include "filterpo.hh"
#include "validate.hh"
#include "rec-zonetocache.hh"
#include "rpzloader.hh"

struct ProtobufExportConfig
{
  std::set<uint16_t> exportTypes = {QType::A, QType::AAAA, QType::CNAME};
  std::vector<ComboAddress> servers;
  uint64_t maxQueuedEntries{100};
  uint16_t timeout{2};
  uint16_t reconnectWaitTime{1};
  bool asyncConnect{false};
  bool enabled{false};
  bool logQueries{true};
  bool logResponses{true};
  bool taggedOnly{false};
  bool logMappedFrom{false};
};

bool operator==(const ProtobufExportConfig& configA, const ProtobufExportConfig& configB);
bool operator!=(const ProtobufExportConfig& configA, const ProtobufExportConfig& configB);

struct FrameStreamExportConfig
{
  std::vector<string> servers;
  bool enabled{false};
  bool logQueries{true};
  bool logResponses{true};
  bool logNODs{true};
  bool logUDRs{false};
  unsigned bufferHint{0};
  unsigned flushTimeout{0};
  unsigned inputQueueSize{0};
  unsigned outputQueueSize{0};
  unsigned queueNotifyThreshold{0};
  unsigned reopenInterval{0};
};

bool operator==(const FrameStreamExportConfig& configA, const FrameStreamExportConfig& configB);
bool operator!=(const FrameStreamExportConfig& configA, const FrameStreamExportConfig& configB);

struct TrustAnchorFileInfo
{
  uint32_t interval{24};
  std::string fname;
};

enum class AdditionalMode : uint8_t
{
  Ignore,
  CacheOnly,
  CacheOnlyRequireAuth,
  ResolveImmediately,
  ResolveDeferred
};

struct ProxyMappingCounts
{
  uint64_t netmaskMatches{};
  uint64_t suffixMatches{};
};

struct ProxyByTableValue
{
  ComboAddress address;
  std::optional<SuffixMatchNode> suffixMatchNode;
  mutable ProxyMappingCounts stats{};
};

using ProxyMapping = NetmaskTree<ProxyByTableValue, Netmask>;

struct OpenTelemetryTraceCondition
{
  std::optional<SuffixMatchNode> d_qnames;
  std::optional<std::unordered_set<QType>> d_qtypes;
  std::optional<uint16_t> d_qid;
  bool d_edns_option_required{false};
  bool d_traceid_only{false};
};

using OpenTelemetryTraceConditions = NetmaskTree<OpenTelemetryTraceCondition>;

using rpzOptions_t = std::unordered_map<std::string, boost::variant<bool, uint32_t, std::string, std::vector<std::pair<int, std::string>>>>;

class LuaConfigItems
{
public:
  struct NTAInfo
  {
  public:
    void clear()
    {
      d_staticConfig.clear();
      d_runtimeMods.clear();
      d_mergedConfig.clear();
    }
    void insertStatic(const DNSName& who, const std::string& why)
    {
      d_staticConfig[who] = why;
      recompute();
    }
    void insertRuntime(const DNSName& who, const std::string& why)
    {
      d_runtimeMods[who] = why;
      recompute();
    }
    void clearRuntime(const DNSName& who)
    {
      d_runtimeMods[who] = true;
      recompute();
    }
    void clearAll()
    {
      d_runtimeMods.clear();
      d_allCleared = true;
      recompute();
    }

    void recompute()
    {
      std::map<DNSName, std::string> merged;
      if (!d_allCleared) {
        merged = d_staticConfig;
      }
      for (const auto& [name, mod] : d_runtimeMods) {
        if (std::holds_alternative<bool>(mod)) {
          merged.erase(name);
        }
        else {
          merged[name] = std::get<std::string>(mod);
        }
      }
      d_mergedConfig = merged;
    }

    [[nodiscard]] const std::map<DNSName, std::string>& getMerged() const
    {
      return d_mergedConfig;
    }

    [[nodiscard]] std::string toString() const
    {
      std::stringstream str;
      str << "STATIC: " << endl;
      for (const auto& entry : d_staticConfig) {
        str << entry.first << ' ' << entry.second << endl;
      }
      str << "RUNTIME: " << d_allCleared << endl;
      for (const auto& entry : d_runtimeMods) {
        str << entry.first << ' ';
        if (std::holds_alternative<bool>(entry.second)) {
          str << std::get<bool>(entry.second);
        }
        else {
          str << std::get<string>(entry.second);
        }
        str << endl;
      }
      return str.str();
    }

    void setRuntimeKeepers(const NTAInfo& old)
    {
      d_runtimeMods = old.d_runtimeMods;
      d_allCleared = old.d_allCleared;
      recompute();
    }
  private:
    std::map<DNSName, std::string> d_staticConfig;
    std::map<DNSName, std::variant<std::string, bool>> d_runtimeMods; // name for why, bool for clear
    std::map<DNSName, std::string> d_mergedConfig;
    bool d_allCleared{false};
  };

  LuaConfigItems();
  void setRuntimeKeepers(const LuaConfigItems&);
  SortList sortlist;
  DNSFilterEngine dfe;
  vector<RPZTrackerParams> rpzs;
  vector<FWCatalogZone> catalogzones;
  TrustAnchorFileInfo trustAnchorFileInfo; // Used to update the Trust Anchors from file periodically
  map<DNSName, dsset_t> dsAnchors;
  NTAInfo d_ntas;
  map<DNSName, RecZoneToCache::Config> ztcConfigs;
  std::map<QType, std::pair<std::set<QType>, AdditionalMode>> allowAdditionalQTypes;
  ProtobufExportConfig protobufExportConfig;
  ProtobufExportConfig outgoingProtobufExportConfig;
  FrameStreamExportConfig frameStreamExportConfig;
  FrameStreamExportConfig nodFrameStreamExportConfig;
  std::shared_ptr<Logr::Logger> d_slog;
  /* we need to increment this every time the configuration
     is reloaded, so we know if we need to reload the protobuf
     remote loggers */
  uint64_t generation{0};
  uint8_t protobufMaskV4{32};
  uint8_t protobufMaskV6{128};
};

extern GlobalStateHolder<LuaConfigItems> g_luaconfs;

void loadRecursorLuaConfig(const std::string& fname, ProxyMapping&, LuaConfigItems& newLuaConfig);
