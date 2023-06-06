#define BOOST_TEST_DYN_LINK
#include <boost/test/unit_test.hpp>

#include "rusttest.hh"

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

BOOST_AUTO_TEST_SUITE_END()
