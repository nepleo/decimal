#include <test_support.h>

#include <decimal/decimal.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace {

bool test_default_constructor() {
  const decimal value;
  CHECK_EQ(value.signum(), 0);
  CHECK_EQ(value.scale(), 0);
  CHECK_EQ(value.precision(), 1);
  CHECK_EQ(value.to_string(), "0");
  return true;
}

bool test_string_constructors() {
  const char text[] = "xx-123.450E+2yy";
  CHECK_EQ(decimal(text, 2, 11).to_string(), "-12345.0");
  CHECK_EQ(decimal("+001.2300").to_string(), "1.2300");
  CHECK_EQ(decimal(std::string("123456789012345678901234567890.25")).to_string(),
           "123456789012345678901234567890.25");
  CHECK_EQ(decimal(std::string_view("0.00000125")).to_string(), "0.00000125");
  CHECK_EQ(decimal("9.999", math_context(3, round_mode::HALF_UP)).to_string(), "10.0");
  CHECK_EQ(decimal(std::string("1.2345"), math_context(3, round_mode::DOWN)).to_string(), "1.23");
  CHECK_EQ(decimal(std::string_view("1.2355"), math_context(4, round_mode::HALF_EVEN)).to_string(), "1.236");
  return true;
}

bool test_numeric_constructors() {
  CHECK_EQ(decimal(INT32_MIN).to_string(), "-2147483648");
  CHECK_EQ(decimal(INT64_MAX).to_string(), "9223372036854775807");
  CHECK_EQ(decimal(INT64_MIN).to_string(), "-9223372036854775808");
  CHECK_EQ(decimal(INT64_MAX, math_context(5, round_mode::HALF_UP)).to_string(), "9.2234E+18");

  const bigint huge("123456789012345678901234567890");
  CHECK_EQ(decimal(huge).to_string(), "123456789012345678901234567890");
  CHECK_EQ(decimal(huge, 4).to_string(), "12345678901234567890123456.7890");
  CHECK_EQ(decimal(huge, math_context(6, round_mode::DOWN)).to_string(), "1.23456E+29");
  CHECK_EQ(decimal(huge, 4, math_context(6, round_mode::HALF_UP)).to_string(), "1.23457E+25");

  CHECK_EQ(decimal(0.5).to_string(), "0.5");
  CHECK_EQ(decimal(0.1).to_string(), "0.1000000000000000055511151231257827021181583404541015625");
  CHECK_EQ(decimal(0.1, math_context(6, round_mode::HALF_EVEN)).to_string(), "0.100000");
  CHECK_EQ(decimal::value_of(0.1).to_string(), "0.1");
  return true;
}

bool test_invalid_constructors() {
  CHECK(decimal_test::throws<std::runtime_error>([] { static_cast<void>(decimal("")); }));
  CHECK(decimal_test::throws<std::runtime_error>([] { static_cast<void>(decimal("+")); }));
  CHECK(decimal_test::throws<std::runtime_error>([] { static_cast<void>(decimal(".")); }));
  CHECK(decimal_test::throws<std::runtime_error>([] { static_cast<void>(decimal("1.2.3")); }));
  CHECK(decimal_test::throws<std::runtime_error>([] { static_cast<void>(decimal("1E")); }));
  CHECK(decimal_test::throws<std::runtime_error>([] { static_cast<void>(decimal("1E+")); }));
  CHECK(decimal_test::throws<std::runtime_error>([] { static_cast<void>(decimal("1x")); }));
  CHECK(decimal_test::throws<std::runtime_error>([] { static_cast<void>(decimal("1E2147483648")); }));
  CHECK(decimal_test::throws<std::runtime_error>(
      [] { static_cast<void>(decimal(std::numeric_limits<double>::infinity())); }));
  CHECK(decimal_test::throws<std::runtime_error>(
      [] { static_cast<void>(decimal(std::numeric_limits<double>::quiet_NaN())); }));
  return true;
}

}  // namespace

int main() {
  if (!test_default_constructor()) {
    return 1;
  }
  if (!test_string_constructors()) {
    return 2;
  }
  if (!test_numeric_constructors()) {
    return 3;
  }
  return test_invalid_constructors() ? 0 : 4;
}
