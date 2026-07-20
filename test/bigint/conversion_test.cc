#include <test_support.h>

#include <decimal/decimal.h>

#include <cmath>
#include <cstdint>
#include <limits>

namespace {

bool test_radix_and_byte_round_trip() {
  // TEST_COVERS: get_radix_conversion_cache log_cache long_radix_magnitude long_radix_value
  // TEST_COVERS: pad_with_zeros small_to_string to_string_recursive
  const bigint values[] = {bigint("0"), bigint("1"), bigint("-1"), bigint("127"), bigint("128"), bigint("-128"),
                           bigint("-129"), bigint("123456789012345678901234567890")};
  for (const bigint& value : values) {
    for (std::int32_t radix : {2, 8, 10, 16, 36}) {
      CHECK_EQ(bigint(value.to_string(radix), radix).compare_to(value), 0);
    }
    const jarray<std::uint8_t> bytes = value.to_byte_array();
    CHECK_EQ(bigint(bytes).compare_to(value), 0);
  }
  CHECK_EQ(bigint("255").mag_serialized_form().length(), 1);
  CHECK_EQ(bigint::ZERO.mag_serialized_form().length(), 0);
  return true;
}

bool test_primitive_conversions() {
  CHECK_EQ(bigint("4294967295").to_int(), -1);
  CHECK_EQ(bigint("4294967295").int_value(), -1);
  CHECK_EQ(bigint("18446744073709551615").to_long(), INT64_C(-1));
  CHECK_EQ(bigint("18446744073709551615").long_value(), INT64_C(-1));
  CHECK_EQ(bigint("9223372036854775807").to_long_exact(), INT64_MAX);
  CHECK_EQ(bigint("-9223372036854775808").long_value_exact(), INT64_MIN);
  CHECK_EQ(bigint("2147483647").int_value_exact(), INT32_MAX);
  CHECK_EQ(bigint("32767").short_value_exact(), INT16_MAX);
  CHECK_EQ(bigint("127").byte_value_exact(), INT8_MAX);
  CHECK(decimal_test::throws<std::runtime_error>(
      [] { static_cast<void>(bigint("9223372036854775808").to_long_exact()); }));
  CHECK(decimal_test::throws<std::runtime_error>([] { static_cast<void>(bigint("2147483648").to_int_exact()); }));
  CHECK(decimal_test::throws<std::runtime_error>([] { static_cast<void>(bigint("32768").to_short_exact()); }));
  CHECK(decimal_test::throws<std::runtime_error>([] { static_cast<void>(bigint("128").to_byte_exact()); }));
  return true;
}

bool test_floating_conversions() {
  CHECK(decimal_test::same_double(bigint("9007199254740992").to_double(), 9007199254740992.0));
  CHECK(decimal_test::same_double(bigint("9007199254740993").to_double(), 9007199254740992.0));
  CHECK_EQ(bigint("16777217").to_float(), 16777216.0f);
  CHECK(std::isinf(bigint::ONE.shift_left(2000).to_double()));
  CHECK(std::isinf(bigint::ONE.shift_left(200).to_float()));
  CHECK_EQ(bigint("42").double_value(), 42.0);
  CHECK_EQ(bigint("42").float_value(), 42.0f);
  return true;
}

}  // namespace

int main() {
  if (!test_radix_and_byte_round_trip()) {
    return 1;
  }
  if (!test_primitive_conversions()) {
    return 2;
  }
  return test_floating_conversions() ? 0 : 3;
}
