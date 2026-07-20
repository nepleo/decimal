#include <test_support.h>

#include <decimal/decimal.h>

#include <array>
#include <cstdint>

namespace {

bool test_add_subtract_multiply() {
  // TEST_COVERS: multiply_and_round do_round_value
  CHECK_EQ(decimal("1.20").add(decimal("3.4")).to_string(), "4.60");
  CHECK_EQ(decimal("1.20").subtract(decimal("3.4")).to_string(), "-2.20");
  CHECK_EQ(decimal("-12.5").multiply(decimal("0.08")).to_string(), "-1.000");
  CHECK_EQ(decimal("999999999999999999").add(decimal("1")).to_string(), "1000000000000000000");
  CHECK_EQ(decimal("9223372036854775807").add(decimal("1")).to_string(), "9223372036854775808");
  CHECK_EQ(decimal("9223372036854775807").multiply(decimal("2")).to_string(), "18446744073709551614");
  CHECK_EQ(decimal("10000000000000000000").subtract(decimal("9999999999999999999")).to_string(), "1");

  const math_context mc(3, round_mode::HALF_UP);
  CHECK_EQ(decimal("1.2345").add(decimal("9.8765"), mc).to_string(), "11.1");
  CHECK_EQ(decimal("9.8765").subtract(decimal("1.2345"), mc).to_string(), "8.64");
  CHECK_EQ(decimal("12.34").multiply(decimal("5.678"), mc).to_string(), "70.1");
  CHECK_EQ(decimal("1E+100").add(decimal("1"), math_context(5, round_mode::HALF_EVEN)).to_string(), "1.0000E+100");
  return true;
}

bool test_representation_combinations() {
  const bigint huge("123456789012345678901234567890");
  CHECK_EQ(decimal::add_compact(12, 30, 1).to_string(), "4.2");
  CHECK_EQ(decimal::add_compact_unaligned(12, 1, 34, 2).to_string(), "1.54");
  CHECK_EQ(decimal::add_mixed(12, 1, huge, 2).to_string(), "1234567890123456789012345680.10");
  CHECK_EQ(decimal::add_inflated(huge, 2, bigint("10"), 1).to_string(), "1234567890123456789012345679.90");
  CHECK_EQ(decimal::multiply_compact(125, 8, 3).to_string(), "1.000");
  CHECK_EQ(decimal::multiply_mixed(2, huge, 2).to_string(), "2469135780246913578024691357.80");
  CHECK_EQ(decimal::multiply_inflated(huge, bigint("3"), 1).to_string(), "37037036703703703670370370367.0");
  return true;
}

bool test_comparison_and_unary_operations() {
  CHECK_EQ(decimal("2.0").compare_to(decimal("2.00")), 0);
  CHECK(!decimal("2.0").equals(decimal("2.00")));
  CHECK(decimal("2.00").equals(decimal("2.00")));
  CHECK_EQ(decimal("-3").min(decimal("2")).to_string(), "-3");
  CHECK_EQ(decimal("-3").max(decimal("2")).to_string(), "2");
  CHECK_EQ(decimal("-12.30").abs().to_string(), "12.30");
  CHECK_EQ(decimal("12.30").negate().to_string(), "-12.30");
  CHECK_EQ(decimal("1.2345").plus(math_context(3, round_mode::HALF_EVEN)).to_string(), "1.23");
  CHECK_EQ(decimal("-1.2355").abs(math_context(4, round_mode::HALF_EVEN)).to_string(), "1.236");
  CHECK(decimal("0.000").is_zero());
  CHECK(!decimal("0.001").is_zero());
  CHECK_EQ(decimal("2.0").hash_code(), decimal("2.0").hash_code());
  return true;
}

bool test_arithmetic_properties() {
  const std::array<const char*, 7> values = {"0", "1.25", "-7.5", "99999999999999999999", "1E-20",
                                              "-123456789.0001", "3.1415926535897932384626"};
  for (const char* lhs_text : values) {
    const decimal lhs(lhs_text);
    CHECK(lhs.add(decimal::ZERO).equals(lhs));
    CHECK(lhs.multiply(decimal::ONE).equals(lhs));
    for (const char* rhs_text : values) {
      const decimal rhs(rhs_text);
      CHECK_EQ(lhs.add(rhs).compare_to(rhs.add(lhs)), 0);
      CHECK_EQ(lhs.multiply(rhs).compare_to(rhs.multiply(lhs)), 0);
      CHECK_EQ(lhs.subtract(rhs).add(rhs).compare_to(lhs), 0);
    }
  }
  return true;
}

}  // namespace

int main() {
  if (!test_add_subtract_multiply()) {
    return 1;
  }
  if (!test_representation_combinations()) {
    return 2;
  }
  if (!test_comparison_and_unary_operations()) {
    return 3;
  }
  return test_arithmetic_properties() ? 0 : 4;
}
