#include <test_support.h>

#include <decimal/decimal.h>

#include <array>

namespace {

bool check_division(const bigint& dividend, const bigint& divisor) {
  const auto qr = dividend.divide_and_remainder(divisor);
  CHECK_EQ(qr.first.multiply(divisor).add(qr.second).compare_to(dividend), 0);
  CHECK(qr.second.abs().compare_to(divisor.abs()) < 0);
  CHECK(qr.second.is_zero() || qr.second.signum() == dividend.signum());
  CHECK_EQ(dividend.divide(divisor).compare_to(qr.first), 0);
  CHECK_EQ(dividend.remainder(divisor).compare_to(qr.second), 0);
  return true;
}

bool test_fixed_division() {
  const bigint dividend("123456789012345678901234567890");
  const bigint divisor("9876543210");
  CHECK_EQ(dividend.divide(divisor).to_string(), "12499999887343749990");
  CHECK_EQ(dividend.remainder(divisor).to_string(), "1562499990");
  CHECK(check_division(dividend, divisor));
  CHECK(check_division(dividend.negate(), divisor));
  CHECK(check_division(dividend, divisor.negate()));
  CHECK(check_division(dividend.negate(), divisor.negate()));
  CHECK(decimal_test::throws<std::runtime_error>([&dividend] { static_cast<void>(dividend.divide(bigint::ZERO)); }));
  return true;
}

bool test_knuth_and_burnikel_ziegler() {
  // TEST_COVERS: add_one mul_add impl_mul_add impl_mul_add_check sub_n int_array_cmp_to_len
  const bigint small_divisor = bigint::ONE.shift_left(255).add(bigint("123456789"));
  const bigint small_dividend = small_divisor.multiply(bigint("9876543210123456789")).add(bigint("7654321"));
  CHECK(check_division(small_dividend, small_divisor));
  CHECK_EQ(small_dividend.divide_knuth(small_divisor).to_string(), "9876543210123456789");
  CHECK_EQ(small_dividend.remainder_knuth(small_divisor).to_string(), "7654321");
  const auto small_qr = small_dividend.divide_and_remainder_knuth(small_divisor);
  CHECK_EQ(small_qr.first.to_string(), "9876543210123456789");
  CHECK_EQ(small_qr.second.to_string(), "7654321");

  const bigint large_divisor = bigint::ONE.shift_left(32 * 82).add(bigint::ONE.shift_left(100)).add(bigint("17"));
  const bigint quotient = bigint::ONE.shift_left(32 * 45).add(bigint("123456789"));
  const bigint remainder("987654321");
  const bigint large_dividend = large_divisor.multiply(quotient).add(remainder);
  const auto qr = large_dividend.divide_and_remainder_burnikel_ziegler(large_divisor);
  CHECK_EQ(qr.first.compare_to(quotient), 0);
  CHECK_EQ(qr.second.compare_to(remainder), 0);
  CHECK_EQ(large_dividend.divide_burnikel_ziegler(large_divisor).compare_to(quotient), 0);
  CHECK_EQ(large_dividend.remainder_burnikel_ziegler(large_divisor).compare_to(remainder), 0);
  CHECK(check_division(large_dividend, large_divisor));
  return true;
}

}  // namespace

int main() {
  if (!test_fixed_division()) {
    return 1;
  }
  return test_knuth_and_burnikel_ziegler() ? 0 : 2;
}
