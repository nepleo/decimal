#include <test_support.h>

#include <decimal/decimal.h>

#include <cstdint>
#include <random>
#include <string>

namespace {

bool test_integer_and_string_constructors() {
  // TEST_COVERS: destructive_mul_add parse_group parse_int_decimal strip_leading_zero_bytes
  // TEST_COVERS: make_positive_bytes make_positive_limbs
  CHECK_EQ(bigint().to_string(), "0");
  CHECK_EQ(bigint(INT64_MIN).to_string(), "-9223372036854775808");
  CHECK_EQ(bigint(INT64_MAX).to_string(), "9223372036854775807");
  CHECK_EQ(bigint("123456789012345678901234567890").to_string(), "123456789012345678901234567890");
  CHECK_EQ(bigint("-ffffffffffffffff", 16).to_string(16), "-ffffffffffffffff");
  CHECK_EQ(bigint("1010101010101010", 2).to_string(), "43690");
  CHECK_EQ(bigint("zzzzzz", 36).to_string(36), "zzzzzz");
  CHECK_EQ(bigint::value_of(-16).to_string(), "-16");
  return true;
}

bool test_binary_constructors() {
  const jarray<std::uint8_t> positive{UINT8_C(0x01), UINT8_C(0x00)};
  const jarray<std::uint8_t> negative{UINT8_C(0xff), UINT8_C(0x00)};
  const jarray<std::uint8_t> magnitude{UINT8_C(0x00), UINT8_C(0x80), UINT8_C(0x00)};
  CHECK_EQ(bigint(positive).to_string(), "256");
  CHECK_EQ(bigint(negative).to_string(), "-256");
  CHECK_EQ(bigint(-1, magnitude, 1, 2).to_string(), "-32768");
  CHECK_EQ(bigint(1, magnitude).to_string(), "32768");

  const jarray<std::uint32_t> positive_limbs{UINT32_C(0), UINT32_C(0x80000000)};
  const jarray<std::uint32_t> negative_limbs{UINT32_C(0xffffffff), UINT32_C(0xfffffffe)};
  CHECK_EQ(bigint(positive_limbs).to_string(), "2147483648");
  CHECK_EQ(bigint(negative_limbs).to_string(), "-2");
  CHECK_EQ(bigint(1, jarray<std::uint32_t>{UINT32_C(1), UINT32_C(0)}).to_string(), "4294967296");
  return true;
}

bool test_random_constructors() {
  std::mt19937_64 random(UINT64_C(0x123456789abcdef0));
  const bigint zero_bits(0, random);
  const bigint random_127(127, random);
  const bigint prime_64(64, 50, random);
  CHECK(zero_bits.is_zero());
  CHECK(random_127.signum() >= 0);
  CHECK(random_127.bit_length() <= 127);
  CHECK_EQ(prime_64.bit_length(), 64);
  CHECK(prime_64.is_probable_prime(50));
  return true;
}

bool test_invalid_constructors() {
  CHECK(decimal_test::throws<std::invalid_argument>([] { static_cast<void>(bigint("")); }));
  CHECK(decimal_test::throws<std::invalid_argument>([] { static_cast<void>(bigint("+")); }));
  CHECK(decimal_test::throws<std::invalid_argument>([] { static_cast<void>(bigint("12z", 10)); }));
  CHECK(decimal_test::throws<std::invalid_argument>([] { static_cast<void>(bigint("10", 1)); }));
  CHECK(decimal_test::throws<std::invalid_argument>([] { static_cast<void>(bigint("10", 37)); }));
  CHECK(decimal_test::throws<std::invalid_argument>([] { static_cast<void>(bigint(jarray<std::uint8_t>{})); }));
  CHECK(decimal_test::throws<std::invalid_argument>(
      [] { static_cast<void>(bigint(0, jarray<std::uint8_t>{UINT8_C(1)})); }));
  CHECK(decimal_test::throws<std::out_of_range>(
      [] { static_cast<void>(bigint(jarray<std::uint8_t>{UINT8_C(1)}, 1, 1)); }));
  return true;
}

}  // namespace

int main() {
  if (!test_integer_and_string_constructors()) {
    return 1;
  }
  if (!test_binary_constructors()) {
    return 2;
  }
  if (!test_random_constructors()) {
    return 3;
  }
  return test_invalid_constructors() ? 0 : 4;
}
