#include <test_support.h>

#include <decimal/decimal.h>

#include <cstdint>

namespace {

bool test_scale_operations() {
  CHECK_EQ(decimal("1.23").set_scale(4).to_string(), "1.2300");
  CHECK_EQ(decimal("1.235").set_scale(2, round_mode::HALF_EVEN).to_string(), "1.24");
  CHECK_EQ(decimal("1.23").move_point_left(3).to_string(), "0.00123");
  CHECK_EQ(decimal("1.23").move_point_right(3).to_string(), "1230");
  CHECK_EQ(decimal("1.23").scale_by_power_of_ten(3).to_string(), "1.23E+3");
  CHECK_EQ(decimal("1200.00").strip_trailing_zeros().to_string(), "1.2E+3");
  CHECK_EQ(decimal("0.000").strip_trailing_zeros().to_string(), "0");
  CHECK_EQ(decimal("123.45").scale(), 2);
  CHECK_EQ(decimal("123.45").precision(), 5);
  CHECK_EQ(decimal("123.45").unscaled_value().to_string(), "12345");
  CHECK_EQ(decimal("123.45").ulp().to_string(), "0.01");
  return true;
}

bool test_power_and_square_root() {
  CHECK_EQ(decimal("1.20").pow(3).to_string(), "1.728000");
  CHECK_EQ(decimal("2").pow(10).to_string(), "1024");
  CHECK_EQ(decimal("2").pow(-3, math_context(10, round_mode::HALF_EVEN)).to_string(), "0.125");
  CHECK_EQ(decimal("4.00").sqrt(math_context::UNLIMITED).to_string(), "2.0");
  CHECK_EQ(decimal("2").sqrt(math_context(10, round_mode::HALF_EVEN)).to_string(), "1.414213562");
  CHECK_EQ(decimal("0.0000").sqrt(math_context(10)).to_string(), "0.00");
  CHECK(decimal_test::throws<std::invalid_argument>([] { static_cast<void>(decimal("2").pow(-1)); }));
  CHECK(decimal_test::throws<std::runtime_error>(
      [] { static_cast<void>(decimal("-1").sqrt(math_context::DECIMAL64)); }));
  CHECK(decimal_test::throws<std::runtime_error>(
      [] { static_cast<void>(decimal("2").sqrt(math_context::UNLIMITED)); }));
  return true;
}

bool test_exact_conversions() {
  CHECK_EQ(decimal("123.99").to_int(), 123);
  CHECK_EQ(decimal("-123.99").to_long(), INT64_C(-123));
  CHECK_EQ(decimal("123.000").to_long_exact(), INT64_C(123));
  CHECK_EQ(decimal("2147483647").to_int_exact(), INT32_MAX);
  CHECK_EQ(decimal("32767").to_short_exact(), INT16_MAX);
  CHECK_EQ(decimal("127").to_byte_exact(), INT8_MAX);
  CHECK_EQ(decimal("123.99").to_big_integer().to_string(), "123");
  CHECK_EQ(decimal("123.000").to_big_integer_exact().to_string(), "123");
  CHECK(decimal_test::throws<std::runtime_error>([] { static_cast<void>(decimal("1.1").to_long_exact()); }));
  CHECK(decimal_test::throws<std::runtime_error>([] { static_cast<void>(decimal("2147483648").to_int_exact()); }));
  CHECK(decimal_test::throws<std::runtime_error>([] { static_cast<void>(decimal("32768").to_short_exact()); }));
  CHECK(decimal_test::throws<std::runtime_error>([] { static_cast<void>(decimal("128").to_byte_exact()); }));
  CHECK(decimal_test::throws<std::runtime_error>([] { static_cast<void>(decimal("1.1").to_big_integer_exact()); }));
  return true;
}

}  // namespace

int main() {
  if (!test_scale_operations()) {
    return 1;
  }
  if (!test_power_and_square_root()) {
    return 2;
  }
  return test_exact_conversions() ? 0 : 3;
}
