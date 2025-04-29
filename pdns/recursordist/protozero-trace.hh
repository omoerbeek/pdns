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

#include <array>
#include <variant>
#include <vector>

#include <protozero/pbf_reader.hpp>
#include <protozero/pbf_writer.hpp>

// See https://github.com/open-telemetry/opentelemetry-proto/tree/main/opentelemetry/proto

namespace pdns::trace
{

struct AnyValue;
struct ArrayValue;
struct KeyValue;
struct KeyValueList;

void encode(protozero::pbf_writer& writer, uint8_t field, bool value, bool always = false)
{
  if (always || value) {
    writer.add_bool(field, value);
  }
}

void encode(protozero::pbf_writer& writer, uint8_t field, uint32_t value, bool always = false)
{
  if (always || value != 0) {
    writer.add_uint32(field, value);
  }
}

void encodeFixed(protozero::pbf_writer& writer, uint8_t field, uint32_t value)
{
  writer.add_fixed32(field, value);
}

void encode(protozero::pbf_writer& writer, uint8_t field, int64_t value, bool always = false)
{
  if (always || value != 0) {
    writer.add_int64(field, value);
  }
}

void encode(protozero::pbf_writer& writer, uint8_t field, uint64_t value, bool always = false)
{
  if (always || value != 0) {
    writer.add_uint64(field, value);
  }
}

void encodeFixed(protozero::pbf_writer& writer, uint8_t field, uint64_t value)
{
  writer.add_fixed64(field, value);
}

void encode(protozero::pbf_writer& writer, uint8_t field, double value, bool always = false)
{
  if (always || value != 0.0) {
    writer.add_double(field, value);
  }
}

void encode(protozero::pbf_writer& writer, uint8_t field, const std::string& value, bool always = false)
{
  if (always || !value.empty()) {
    writer.add_string(field, value);
  }
}

void encode(protozero::pbf_writer& writer, uint8_t field, const std::vector<uint8_t>& value, bool always = false)
{
  if (always || !value.empty()) {
    writer.add_bytes(field, reinterpret_cast<const char*>(value.data()), value.size()); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) it's the API
  }
}

template <typename T>
void encode(protozero::pbf_writer& writer, const std::vector<T>& vec)
{
  for (auto const& element : vec) {
    element.encode(writer);
  }
}

template <typename T>
void encode(protozero::pbf_writer& writer, uint8_t field, const std::vector<T>& vec)
{
  for (auto const& element : vec) {
    protozero::pbf_writer sub{writer, field};
    element.encode(sub);
  }
}

template <typename T, typename E>
T decode(protozero::pbf_reader& reader)
{
  std::vector<E> vec;
  while (reader.next()) {
    if (reader.tag() == 1) {
      protozero::pbf_reader sub = reader.get_message();
      vec.emplace_back(E::decode(sub));
    }
  }
  return {vec};
}

struct ArrayValue
{
  std::vector<AnyValue> values; // = 1

  void encode(protozero::pbf_writer& writer) const
  {
    pdns::trace::encode(writer, 1, values);
  }

  static ArrayValue decode(protozero::pbf_reader& reader);

  bool operator==(const ArrayValue& rhs) const
  {
    return values == rhs.values;
  }
};

struct KeyValueList
{
  std::vector<KeyValue> values; // = 1

  void encode(protozero::pbf_writer& writer) const
  {
    pdns::trace::encode(writer, 1, values);
  }

  static KeyValueList decode(protozero::pbf_reader& reader);

  bool operator==(const KeyValueList& rhs) const
  {
    return values == rhs.values;
  }
};

struct AnyValue : public std::variant<char, std::string, bool, int64_t, double, ArrayValue, KeyValueList, std::vector<uint8_t>>
{
  void encode(protozero::pbf_writer& writer) const
  {
    if (std::holds_alternative<std::string>(*this)) {
      pdns::trace::encode(writer, 1, std::get<std::string>(*this), true);
    }
    else if (std::holds_alternative<bool>(*this)) {
      pdns::trace::encode(writer, 2, std::get<bool>(*this), true);
    }
    else if (std::holds_alternative<int64_t>(*this)) {
      pdns::trace::encode(writer, 3, std::get<int64_t>(*this), true);
    }
    else if (std::holds_alternative<double>(*this)) {
      pdns::trace::encode(writer, 4, std::get<double>(*this), true);
    }
    else if (std::holds_alternative<ArrayValue>(*this)) {
      protozero::pbf_writer sub{writer, 5};
      std::get<ArrayValue>(*this).encode(sub);
    }
    else if (std::holds_alternative<KeyValueList>(*this)) {
      protozero::pbf_writer sub{writer, 6};
      std::get<KeyValueList>(*this).encode(sub);
    }
    else if (std::holds_alternative<std::vector<uint8_t>>(*this)) {
      pdns::trace::encode(writer, 7, std::get<std::vector<uint8_t>>(*this), true);
    }
  }

  static AnyValue decode(protozero::pbf_reader& reader)
  {
    while (reader.next()) {
      switch (reader.tag()) {
      case 1:
        return AnyValue{reader.get_string()};
        break;
      case 2:
        return AnyValue{reader.get_bool()};
        break;
      case 3:
        return AnyValue{reader.get_int64()};
        break;
      case 4:
        return AnyValue{reader.get_double()};
        break;
      case 5: {
        protozero::pbf_reader arrayvalue = reader.get_message();
        return AnyValue{ArrayValue::decode(arrayvalue)};
        break;
      }
      case 6: {
        protozero::pbf_reader kvlist = reader.get_message();
        return AnyValue{KeyValueList::decode(kvlist)};
        break;
      }
      case 7: {
        auto value = reader.get_view();
        std::vector<uint8_t> data{};
        data.reserve(value.size());
        for (size_t i = 0; i < value.size(); ++i) {
          data.push_back(static_cast<uint8_t>(value.data()[i]));
        }
        return AnyValue{std::move(data)};
        break;
      }
      default:
        break;
      }
    }

    return {};
  }
};

struct EntityRef
{
  std::string schema_url; // == 1
  std::string type; // == 2
  std::vector<std::string> id_keys; // == 3
  std::vector<std::string> description_keys; // == 4

  void encode(protozero::pbf_writer& writer) const
  {
    pdns::trace::encode(writer, 1, schema_url);
    pdns::trace::encode(writer, 2, type);
    for (auto const& element : id_keys) {
      pdns::trace::encode(writer, 3, element);
    }
    for (auto const& element : description_keys) {
      pdns::trace::encode(writer, 4, element);
    }
  }

  static EntityRef decode(protozero::pbf_reader& reader)
  {
    EntityRef ret;
    while (reader.next()) {
      switch (reader.tag()) {
      case 1:
        ret.schema_url = reader.get_string();
        break;
      case 2:
        ret.type = reader.get_string();
        break;
      case 3:
        ret.id_keys.emplace_back(reader.get_string());
        break;
      case 4:
        ret.description_keys.emplace_back(reader.get_string());
        break;
      default:
        break;
      }
    }
    return ret;
  }
};

struct KeyValue
{
  std::string key; // = 1
  AnyValue value; // = 2
  void encode(protozero::pbf_writer& writer) const
  {
    pdns::trace::encode(writer, 1, key);
    {
      protozero::pbf_writer val_sub{writer, 2};
      value.encode(val_sub);
    }
  }

  static KeyValue decode(protozero::pbf_reader& reader);

  bool operator==(const KeyValue& rhs) const
  {
    return key == rhs.key && value == rhs.value;
  }
};

struct Resource
{
  std::vector<KeyValue> attributes; // = 1
  uint32_t dropped_attributes_count{0}; // = 2;
  std::vector<EntityRef> entity_refs; // = 3

  void encode(protozero::pbf_writer& writer) const
  {
    pdns::trace::encode(writer, 1, attributes);
    pdns::trace::encode(writer, 2, dropped_attributes_count);
    pdns::trace::encode(writer, 3, entity_refs);
  }

  static Resource decode(protozero::pbf_reader& reader)
  {
    Resource ret;
    while (reader.next()) {
      switch (reader.tag()) {
      case 1: {
        auto sub = reader.get_message();
        ret.attributes.emplace_back(KeyValue::decode(sub));
        break;
      }
      case 2:
        ret.dropped_attributes_count = reader.get_uint32();
        break;
      case 3: {
        auto sub = reader.get_message();
        ret.entity_refs.emplace_back(EntityRef::decode(sub));
        break;
      }
      default:
        break;
      }
    }
    return ret;
  }
};

struct InstrumentationScope
{
  std::string name; // = 1
  std::string version; // = 2
  std::vector<KeyValue> attributes; // = 3
  uint32_t dropped_attributes_count{0}; // = 4

  void encode(protozero::pbf_writer& writer) const
  {
    pdns::trace::encode(writer, 1, name);
    pdns::trace::encode(writer, 2, version);
    pdns::trace::encode(writer, 3, attributes);
    pdns::trace::encode(writer, 4, dropped_attributes_count);
  }

  static InstrumentationScope decode(protozero::pbf_reader& reader)
  {
    InstrumentationScope ret;
    while (reader.next()) {
      switch (reader.tag()) {
      case 1:
        ret.name = reader.get_string();
        break;
      case 2:
        ret.version = reader.get_string();
        break;
      case 3: {
        auto sub = reader.get_message();
        ret.attributes.emplace_back(KeyValue::decode(sub));
        break;
      }
      case 4:
        ret.dropped_attributes_count = reader.get_uint32();
        break;
      default:
        break;
      }
    }
    return ret;
  }
};

using TraceID = std::array<uint8_t, 16>;
using SpanID = std::array<uint8_t, 8>;

void encode(protozero::pbf_writer& writer, uint8_t field, const TraceID& value)
{
  writer.add_bytes(field, reinterpret_cast<const char*>(value.data()), value.size()); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) it's the API
}

TraceID decodeTraceID(protozero::pbf_reader& reader)
{
  TraceID bytes;
  auto [data, len] = reader.get_data();
  memcpy(bytes.data(), data, std::min(bytes.size(), static_cast<size_t>(len)));
  return bytes;
}

void encode(protozero::pbf_writer& writer, uint8_t field, const SpanID& value)
{
  writer.add_bytes(field, reinterpret_cast<const char*>(value.data()), value.size()); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) it's the API
}

SpanID decodeSpanID(protozero::pbf_reader& reader)
{
  SpanID bytes;
  auto [data, len] = reader.get_data();
  memcpy(bytes.data(), data, std::min(bytes.size(), static_cast<size_t>(len)));
  return bytes;
}

struct Status
{
  std::string message; // = 2;

  // For the semantics of status codes see
  // https://github.com/open-telemetry/opentelemetry-specification/blob/main/specification/trace/api.md#set-status
  enum class StatusCode : uint8_t
  {
    STATUS_CODE_UNSET = 0,
    STATUS_CODE_OK = 1,
    STATUS_CODE_ERROR = 2,
  };

  // The status code.
  StatusCode code{StatusCode::STATUS_CODE_UNSET}; //  = 3;

  void encode(protozero::pbf_writer& writer) const
  {
    pdns::trace::encode(writer, 2, message);
    pdns::trace::encode(writer, 3, uint32_t(code));
  }

  static Status decode(protozero::pbf_reader& reader)
  {
    Status ret;
    while (reader.next()) {
      switch (reader.tag()) {
      case 2:
        ret.message = reader.get_string();
        break;
      case 3:
        ret.code = static_cast<StatusCode>(reader.get_uint32());
        break;
      default:
        break;
      }
    }
    return ret;
  }
};

struct Span
{
  TraceID trace_id; // = 1
  SpanID span_id; // = 2
  std::string trace_state; // = 3
  SpanID parent_span_id; // = 4
  std::string name; // = 5
  enum class SpanKind : uint8_t
  {
    SPAN_KINUNSPECIFIED = 0,
    SPAN_KININTERNAL = 1,
    SPAN_KINSERVER = 2,
    SPAN_KINCLIENT = 3,
    SPAN_KINPRODUCER = 4,
    SPAN_KINCONSUMER = 5,
  };
  SpanKind kind{Span::SpanKind::SPAN_KINUNSPECIFIED}; // = 6
  uint64_t start_time_unix_nano{0}; // = 7
  uint64_t end_time_unix_nano{0}; // = 8
  std::vector<KeyValue> attributes; // = 9
  uint32_t dropped_attribute_count{0}; // = 10
  struct Event
  {
    uint64_t time_unix_nano; // = 1
    std::string name; // = 2
    std::vector<KeyValue> attributes; // = 3
    uint32_t dropped_attribute_count{0}; // = 4

    void encode(protozero::pbf_writer& writer) const
    {
      pdns::trace::encodeFixed(writer, 1, time_unix_nano);
      pdns::trace::encode(writer, 2, name);
      pdns::trace::encode(writer, 3, attributes);
      pdns::trace::encode(writer, 4, dropped_attribute_count);
    }
    static Event decode(protozero::pbf_reader& reader)
    {
      Event ret;
      while (reader.next()) {
        switch (reader.tag()) {
        case 1:
          ret.time_unix_nano = reader.get_fixed64();
          break;
        case 2:
          ret.name = reader.get_string();
          break;
        case 3: {
          auto sub = reader.get_message();
          ret.attributes.emplace_back(KeyValue::decode(sub));
          break;
        }
        case 4:
          ret.dropped_attribute_count = reader.get_uint32();
        default:
          break;
        }
      }
      return ret;
    }
  };
  std::vector<Event> events; // = 11
  uint32_t dropped_events_count; // = 12
  struct Link
  {
    TraceID trace_id; // = 1
    SpanID span_id; // = 2
    std::string trace_state; // = 3
    std::vector<KeyValue> attributes; // = 4
    uint32_t dropped_attribute_count{0}; // = 5
    uint32_t flags{0}; // = 6

    void encode(protozero::pbf_writer& writer) const
    {
      pdns::trace::encode(writer, 1, trace_id);
      pdns::trace::encode(writer, 2, span_id);
      pdns::trace::encode(writer, 3, trace_state);
      pdns::trace::encode(writer, 4, attributes);
      pdns::trace::encode(writer, 5, dropped_attribute_count);
      pdns::trace::encodeFixed(writer, 6, flags);
    }
    static Link decode(protozero::pbf_reader& reader)
    {
      Link ret;
      while (reader.next()) {
        switch (reader.tag()) {
        case 1:
          ret.trace_id = decodeTraceID(reader);
          break;
        case 2:
          ret.span_id = decodeSpanID(reader);
          break;
        case 3:
          ret.trace_state = reader.get_string();
          break;
        case 4: {
          auto sub = reader.get_message();
          ret.attributes.emplace_back(KeyValue::decode(sub));
          break;
        }
        case 5:
          ret.dropped_attribute_count = reader.get_uint32();
          break;
        case 6:
          ret.flags = reader.get_uint32();
        default:
          break;
        }
      }
      return ret;
    }
  };
  std::vector<Link> links; // = 13
  uint32_t dropped_links_count{0}; // = 14
  Status status; // = 15

  void encode(protozero::pbf_writer& writer) const
  {
    pdns::trace::encode(writer, 1, trace_id);
    pdns::trace::encode(writer, 2, span_id);
    pdns::trace::encode(writer, 3, trace_state);
    pdns::trace::encode(writer, 4, parent_span_id);
    pdns::trace::encode(writer, 5, name);
    pdns::trace::encode(writer, 6, uint32_t(kind));
    pdns::trace::encodeFixed(writer, 7, start_time_unix_nano);
    pdns::trace::encodeFixed(writer, 8, end_time_unix_nano);
    pdns::trace::encode(writer, 9, attributes);
    pdns::trace::encode(writer, 10, dropped_attribute_count);
    pdns::trace::encode(writer, 11, events);
    pdns::trace::encode(writer, 12, dropped_events_count);
    pdns::trace::encode(writer, 13, links);
    pdns::trace::encode(writer, 14, dropped_links_count);
    if (status.code != Status::StatusCode::STATUS_CODE_UNSET || !status.message.empty()) {
      protozero::pbf_writer sub{writer, 15};
      status.encode(sub);
    }
  }

  static Span decode(protozero::pbf_reader& reader)
  {
    Span ret;
    while (reader.next()) {
      switch (reader.tag()) {
      case 1:
        ret.trace_id = decodeTraceID(reader);
        break;
      case 2:
        ret.span_id = decodeSpanID(reader);
        break;
      case 3:
        ret.trace_state = reader.get_string();
        break;
      case 4:
        ret.parent_span_id = decodeSpanID(reader);
        break;
      case 5:
        ret.name = reader.get_string();
        break;
      case 6:
        ret.kind = static_cast<Span::SpanKind>(reader.get_uint32());
        break;
      case 7:
        ret.start_time_unix_nano = reader.get_fixed64();
        break;
      case 8:
        ret.end_time_unix_nano = reader.get_fixed64();
        break;
      case 9: {
        auto sub = reader.get_message();
        ret.attributes.emplace_back(KeyValue::decode(sub));
        break;
      }
      case 10:
        ret.dropped_attribute_count = reader.get_uint32();
        break;
      case 11: {
        auto sub = reader.get_message();
        ret.events.emplace_back(Span::Event::decode(sub));
        break;
      }
      case 12:
        ret.dropped_events_count = reader.get_uint32();
        break;
      case 13: {
        auto sub = reader.get_message();
        ret.links.emplace_back(Span::Link::decode(sub));
        break;
      }
      case 14:
        ret.dropped_links_count = reader.get_uint32();
        break;
      case 15: {
        auto sub = reader.get_message();
        ret.status = Status::decode(sub);
        break;
      }
      default:
        break;
      }
    }
    return ret;
  }
};

struct ScopeSpans
{
  InstrumentationScope scope; // = 1
  std::vector<Span> spans; // = 2
  std::string schema_url; // = 3

  void encode(protozero::pbf_writer& writer) const
  {
    {
      protozero::pbf_writer sub{writer, 1};
      scope.encode(sub);
    }
    pdns::trace::encode(writer, 2, spans);
    pdns::trace::encode(writer, 3, schema_url);
  }

  static ScopeSpans decode(protozero::pbf_reader& reader)
  {
    ScopeSpans ret;
    while (reader.next()) {
      switch (reader.tag()) {
      case 1: {
        auto sub = reader.get_message();
        ret.scope = InstrumentationScope::decode(sub);
        break;
      }
      case 2: {
        auto sub = reader.get_message();
        ret.spans.emplace_back(Span::decode(sub));
        break;
      }
      case 3:
        ret.schema_url = reader.get_string();
      default:
        break;
      }
    }
    return ret;
  }
};

struct ResourceSpans
{
  Resource resource; // = 1
  std::vector<ScopeSpans> scope_spans; // = 2
  std::string schema_url; // = 3

  void encode(protozero::pbf_writer& writer) const
  {
    {
      protozero::pbf_writer sub{writer, 1};
      resource.encode(sub);
    }
    pdns::trace::encode(writer, 2, scope_spans);
    pdns::trace::encode(writer, 3, schema_url);
  }

  static ResourceSpans decode(protozero::pbf_reader& reader)
  {
    ResourceSpans ret;
    while (reader.next()) {
      switch (reader.tag()) {
      case 1: {
        protozero::pbf_reader sub = reader.get_message();
        ret.resource = Resource::decode(sub);
        break;
      }
      case 2: {
        protozero::pbf_reader sub = reader.get_message();
        ret.scope_spans.emplace_back(ScopeSpans::decode(sub));
        break;
      }
      case 3:
        ret.schema_url = reader.get_string();
      default:
        break;
      }
    }
    return ret;
  }
};

struct TracesData
{
  std::vector<ResourceSpans> resource_spans; // = 1

  void encode(protozero::pbf_writer& writer) const
  {
    pdns::trace::encode(writer, 1, resource_spans);
  }

  static TracesData decode(protozero::pbf_reader& reader)
  {
    TracesData ret;
    while (reader.next()) {
      switch (reader.tag()) {
      case 1: {
        auto sub = reader.get_message();
        ret.resource_spans.emplace_back(ResourceSpans::decode(sub));
        break;
      }
      default:
        break;
      }
    }
    return ret;
  }
};

inline ArrayValue ArrayValue::decode(protozero::pbf_reader& reader)
{
  return pdns::trace::decode<ArrayValue, AnyValue>(reader);
}

inline KeyValue KeyValue::decode(protozero::pbf_reader& reader)
{
  KeyValue value;
  while (reader.next()) {
    switch (reader.tag()) {
    case 1:
      value.key = reader.get_string();
      break;
    case 2: {
      protozero::pbf_reader sub = reader.get_message();
      value.value = AnyValue::decode(sub);
      break;
    }
    default:
      break;
    }
  }
  return value;
}

inline KeyValueList KeyValueList::decode(protozero::pbf_reader& reader)
{
  return pdns::trace::decode<KeyValueList, KeyValue>(reader);
}

}
