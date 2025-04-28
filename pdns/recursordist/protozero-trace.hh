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

void encode(protozero::pbf_writer& writer, uint8_t field, bool value)
{
  if (value) {
    writer.add_bool(field, value);
  }
}

void encode(protozero::pbf_writer& writer, uint8_t field, uint32_t value)
{
  if (value != 0) {
    writer.add_uint32(field, value);
  }
}

void encodeFixed(protozero::pbf_writer& writer, uint8_t field, uint32_t value)
{
  if (value != 0) {
    writer.add_fixed32(field, value);
  }
}

void encode(protozero::pbf_writer& writer, uint8_t field, int64_t value)
{
  if (value != 0) {
    writer.add_int64(field, value);
  }
}

void encode(protozero::pbf_writer& writer, uint8_t field, uint64_t value)
{
  if (value != 0) {
    writer.add_uint64(field, value);
  }
}

void encodeFixed(protozero::pbf_writer& writer, uint8_t field, uint64_t value)
{
  writer.add_fixed64(field, value);
}

void encode(protozero::pbf_writer& writer, uint8_t field, double value)
{
  if (value != 0.0) {
    writer.add_double(field, value);
  }
}

void encode(protozero::pbf_writer& writer, uint8_t field, const std::string& value)
{
  if (!value.empty()) {
    writer.add_string(field, value);
  }
}

void encode(protozero::pbf_writer& writer, uint8_t field, const std::vector<uint8_t>& value)
{
  if (!value.empty()) {
    writer.add_bytes(field, reinterpret_cast<const char*>(value.data()), value.size()); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) it's the API
  }
}

template <typename T>
void encode(protozero::pbf_writer& writer, const std::vector<T>& vec);

struct ArrayValue
{
  std::vector<AnyValue> values; // = 1

  void encode(protozero::pbf_writer& writer) const
  {
    if (values.size() > 0) {
      protozero::pbf_writer sub{writer, 1};
      pdns::trace::encode(writer, values);
    }
  }

  void decode(protozero::pbf_reader& reader)
  {
  }
};

struct KeyValueList
{
  std::vector<KeyValue> values; // = 1

  void encode(protozero::pbf_writer& writer) const
  {
    if (values.size() > 0) {
      protozero::pbf_writer sub{writer, 1};
      pdns::trace::encode(writer, values);
    }
  }

  void decode(protozero::pbf_reader& reader)
  {
  }
};

struct AnyValue : public std::variant<std::string, bool, int64_t, double, ArrayValue, KeyValueList, std::vector<uint8_t>>
{
  void encode(protozero::pbf_writer& writer) const
  {
    if (std::holds_alternative<std::string>(*this)) {
      pdns::trace::encode(writer, 1, std::get<std::string>(*this));
    }
    else if (std::holds_alternative<bool>(*this)) {
      pdns::trace::encode(writer, 2, std::get<bool>(*this));
    }
    else if (std::holds_alternative<int64_t>(*this)) {
      pdns::trace::encode(writer, 3, std::get<int64_t>(*this));
    }
    else if (std::holds_alternative<double>(*this)) {
      pdns::trace::encode(writer, 4, std::get<double>(*this));
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
      pdns::trace::encode(writer, 7, std::get<std::vector<uint8_t>>(*this));
    }
  }

  void decode(protozero::pbf_reader& reader)
  {
    while (reader.next()) {
      switch (reader.tag()) {
      case 1:
        *this = AnyValue{reader.get_string()};
        break;
      case 2:
        *this = AnyValue{reader.get_bool()};
        break;
      case 3:
        *this = AnyValue{reader.get_int64()};
        break;
      case 4:
        *this = AnyValue{reader.get_double()};
        break;
      case 5: {
        protozero::pbf_reader arrayvalue = reader.get_message();
        ArrayValue value;
        value.decode(reader);
        *this = AnyValue{std::move(value)};
        break;
      }
      case 6: {
        protozero::pbf_reader kvlist = reader.get_message();
        KeyValueList value;
        value.decode(reader);
        *this = AnyValue{std::move(value)};
        break;
      }
      case 7: {
        auto value = reader.get_view();
        std::vector<uint8_t> data(value.size());
        for (size_t i = 0; i < value.size(); ++i) {
          data.push_back(static_cast<uint8_t>(value.data()[i]));
        }
        *this = AnyValue{std::move(data)};
        break;
      }
      default:
        break;
      }
    }
  }
};

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
};

using TraceID = std::array<uint8_t, 16>;
using SpanID = std::array<uint8_t, 8>;

void encode(protozero::pbf_writer& writer, uint8_t field, const TraceID& value)
{
  writer.add_bytes(field, reinterpret_cast<const char*>(value.data()), value.size()); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) it's the API
}

void encode(protozero::pbf_writer& writer, uint8_t field, const SpanID& value)
{
  writer.add_bytes(field, reinterpret_cast<const char*>(value.data()), value.size()); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) it's the API
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
};

struct Span
{
  TraceID trace_id; // = 1
  SpanID span_id; // =2
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
};

struct TracesData
{
  std::vector<ResourceSpans> resource_spans; // = 1

  void encode(protozero::pbf_writer& writer) const
  {
    pdns::trace::encode(writer, 1, resource_spans);
  }
};
}
