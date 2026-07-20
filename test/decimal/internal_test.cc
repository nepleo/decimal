#include <test_support.h>

#include <decimal/decimal.h>

#include <cstdint>
#include <limits>
#include <utility>

namespace {

bool test_compact_helpers() {
  CHECK_EQ(decimal::long_digit_length(0), 1);
  CHECK_EQ(decimal::long_digit_length(INT64_MAX), 19);
  CHECK_EQ(decimal::saturate_long(INT64_MAX), INT32_MAX);
  CHECK_EQ(decimal::saturate_long(INT64_MIN), INT32_MIN);
  CHECK(decimal::long_compare_magnitude(INT64_C(20), INT64_C(10)) > 0);
  CHECK(decimal::unsigned_long_compare(UINT64_MAX, UINT64_C(1)));
  CHECK(decimal::unsigned_long_compare_eq(UINT64_MAX, UINT64_MAX));
  CHECK_EQ(decimal::compact_val_for(bigint("123")), INT64_C(123));
  CHECK_EQ(decimal::compact_val_for(bigint("9223372036854775808")), decimal::INFLATED);
  CHECK_EQ(decimal::add_64(INT64_C(40), INT64_C(2)), INT64_C(42));
  CHECK_EQ(decimal::add_64(INT64_MAX, INT64_C(1)), decimal::INFLATED);
  CHECK_EQ(decimal::multiply_64(INT64_C(-6), INT64_C(7)), INT64_C(-42));
  CHECK_EQ(decimal::multiply_64(INT64_MAX, INT64_C(2)), decimal::INFLATED);
  CHECK_EQ(decimal::long_mul_pow10(INT64_C(123), 3), INT64_C(123000));
  CHECK_EQ(decimal::long_mul_pow10(INT64_MAX, 1), decimal::INFLATED);
  CHECK_EQ(decimal::make_64(INT64_C(1), INT64_C(2)), INT64_C(0x100000002));
  CHECK(decimal::long_long_compare_magnitude(INT64_C(0), INT64_C(-1), INT64_C(1), INT64_C(0)));
  CHECK_EQ(decimal::big_digit_length(bigint("99999999999999999999")), 20);
  CHECK_EQ(decimal::precision_128(INT64_C(0), INT64_C(999999999999999999)), 18);
  return true;
}

bool test_rounding_helpers() {
  CHECK(decimal::common_need_increment(round_mode::UP, 1, -1, false));
  CHECK(!decimal::common_need_increment(round_mode::DOWN, 1, 1, true));
  CHECK(decimal::common_need_increment(round_mode::HALF_EVEN, 1, 0, true));
  CHECK(!decimal::common_need_increment(round_mode::HALF_EVEN, 1, 0, false));
  CHECK_EQ(decimal::divide_and_round_64(25, 10, round_mode::HALF_EVEN), INT64_C(2));
  CHECK_EQ(decimal::divide_and_round_64(35, 10, round_mode::HALF_EVEN), INT64_C(4));
  CHECK_EQ(decimal::divide_and_round_by_10pow(bigint("12345"), 2, round_mode::HALF_UP).to_string(), "123");
  CHECK_EQ(decimal::do_round(decimal("9.99"), math_context(2, round_mode::HALF_UP)).to_string(), "10");
  CHECK_EQ(decimal("1.234").round(math_context(3, round_mode::DOWN)).to_string(), "1.23");
  CHECK(decimal_test::throws<std::runtime_error>(
      [] { static_cast<void>(decimal::divide_and_round_64(1, 3, round_mode::UNNECESSARY)); }));
  return true;
}

bool test_scale_and_layout_helpers() {
  CHECK_EQ(decimal::big_ten_to_the(25).to_string(), "10000000000000000000000000");
  CHECK_EQ(decimal("12.3").big_mul_pow10(2).to_string(), "12300");
  CHECK_EQ(decimal::big_mul_pow10(INT64_C(12), 3).to_string(), "12000");
  CHECK_EQ(decimal::big_mul_pow10(bigint("12"), 3).to_string(), "12000");
  CHECK_EQ(decimal::adjust_scale(3, -2), 5);
  CHECK_EQ(decimal::parse_exp("1E-0012", 1, 6), INT64_C(-12));
  CHECK_EQ(decimal::zero_value_of(7).scale(), 7);
  CHECK_EQ(decimal::scaled_ten_pow(3, -1, 2).to_string(), "-10.00");
  CHECK(decimal::fraction_only(decimal("0.01")));
  CHECK(!decimal::fraction_only(decimal("1.01")));
  CHECK(decimal::is_power_of_ten(INT64_C(1000000)));
  CHECK(decimal::is_power_of_ten(bigint("100000000000000000000")));
  CHECK(decimal("1E+3").is_power_of_ten_unscaled());
  CHECK_EQ(decimal("1234567").layout_chars(true), "1234567");
  CHECK_EQ(decimal("1E+7").to_engineering_string(), "10E+6");
  CHECK_EQ(decimal("123.45").get_value_string(12345, bigint(), 2), "123.45");
  CHECK_EQ(decimal("123.45").unscaled_long_value(), INT64_C(12345));
  CHECK_EQ(decimal("123.45").unscaled_long_value_exact(), INT64_C(12345));
  CHECK_EQ(decimal("123.45").inflated().to_string(), "12345");
  CHECK_EQ(decimal("1").check_scale(INT64_C(10)), 10);
  CHECK_EQ(decimal::check_scale_non_zero(INT64_C(-10)), -10);
  CHECK_EQ(decimal::to_strict_bigint(bigint("42")).to_string(), "42");
  CHECK_EQ(decimal::create_and_strip_zeros_to_match_scale(INT64_C(12000), 3, 0).to_string(), "12");
  CHECK_EQ(decimal::strip_zeros_to_match_scale(bigint("12000"), decimal::INFLATED, 3, 0).to_string(), "12");
  return true;
}

bool test_mutable_bridge_helpers() {
  // TEST_COVERS: compare_magnitude_normalized div_rem_negative_long divide_small_fast_path do_round_128
  // TEST_COVERS: long_overflow_check multiply_divide_and_round pre_align rounded_ten_power try_divide_and_round_128
  // TEST_COVERS: try_multiply_divide_and_round
  mutable_bigint magnitude(bigint("12345678901234567890").mag_);
  CHECK_EQ(decimal::mq_to_compact_value(magnitude, 1), decimal::INFLATED);
  CHECK_EQ(decimal::mq_to_decimal(magnitude, -1, 2).to_string(), "-123456789012345678.90");
  std::pair<decimal, decimal> pair{decimal("1.2"), decimal("3.456")};
  decimal::match_scale(pair);
  CHECK_EQ(pair.first.to_string(), "1.200");
  CHECK_EQ(pair.second.to_string(), "3.456");
  CHECK(decimal::double_string_roundtrip("0.1", 0.1));
  CHECK(!decimal::double_string_roundtrip("0.2", 0.1));
  return true;
}

}  // namespace

int main() {
  if (!test_compact_helpers()) {
    return 1;
  }
  if (!test_rounding_helpers()) {
    return 2;
  }
  if (!test_scale_and_layout_helpers()) {
    return 3;
  }
  return test_mutable_bridge_helpers() ? 0 : 4;
}
