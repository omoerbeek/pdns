#define BOOST_TEST_DYN_LINK
#include <boost/test/unit_test.hpp>

#include "rusttest.hh"
#include "rust/experiment/lib.rs.h"

BOOST_AUTO_TEST_SUITE(syncres_cc1)

BOOST_AUTO_TEST_CASE(test_rust_basic)
{
  rustHello();
  int32_t rustTest = 0;
  rustIncrement(&rustTest);
  BOOST_CHECK_EQUAL(rustTest, 1);
  std::string rustStr = "foo";
  rustString(rustStr.data());
  BOOST_CHECK_EQUAL(rustStr, "goo");
}

BOOST_AUTO_TEST_CASE(test_rust_parse)
{
  const std::string yaml = R"EOT(
  record_cache:
    size: 10000
  dnssec:
    validation: validate
  incoming:
    pdns_distributes_queries: false
)EOT";

  auto config = parse_yaml_config_in_rust(yaml);
  BOOST_CHECK_EQUAL(config.record_cache.size, 10000U);
  BOOST_CHECK_EQUAL(std::string(config.dnssec.validation), "validate");
  BOOST_CHECK_EQUAL(config.incoming.pdns_distributes_queries, false);
  BOOST_REQUIRE(config.incoming.listen.size() == 1U);
  BOOST_CHECK_EQUAL(std::string(config.incoming.listen[0]), "127.0.0.1");
}

BOOST_AUTO_TEST_SUITE_END()
