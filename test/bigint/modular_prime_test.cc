#include <test_support.h>

#include <decimal/decimal.h>

#include <cstdint>
#include <random>

namespace {

bool test_gcd_and_modular_arithmetic() {
  // TEST_COVERS: montgomery_multiply montgomery_multiply_into montgomery_square montgomery_square_into
  // TEST_COVERS: impl_montgomery_multiply impl_montgomery_square impl_montgomery_multiply_checks
  // TEST_COVERS: mont_reduce mont_reduce_in_place materialize
  CHECK_EQ(bigint("48").gcd(bigint("18")).to_string(), "6");
  CHECK_EQ(bigint("-48").gcd(bigint("18")).to_string(), "6");
  CHECK_EQ(bigint("-13").mod(bigint("5")).to_string(), "2");
  CHECK_EQ(bigint("3").mod_inverse(bigint("11")).to_string(), "4");
  CHECK_EQ(bigint("4").mod_pow(bigint("13"), bigint("497")).to_string(), "445");
  CHECK_EQ(bigint("2").mod_pow(bigint("-1"), bigint("5")).to_string(), "3");
  CHECK_EQ(bigint("12345678901234567890").mod_uint32(UINT32_C(97)), UINT32_C(3));
  CHECK(decimal_test::throws<std::runtime_error>([] { static_cast<void>(bigint("2").mod(bigint("-5"))); }));
  CHECK(decimal_test::throws<std::runtime_error>([] { static_cast<void>(bigint("2").mod_inverse(bigint("4"))); }));
  CHECK(decimal_test::throws<std::runtime_error>(
      [] { static_cast<void>(bigint("2").mod_pow(bigint("3"), bigint("0"))); }));
  return true;
}

bool test_primes() {
  // TEST_COVERS: lucas_lehmer_sequence
  CHECK(bigint("2").is_probable_prime(100));
  CHECK(bigint("104729").is_probable_prime(100));
  CHECK(!bigint("104730").is_probable_prime(100));
  CHECK_EQ(bigint("100").next_probable_prime().to_string(), "101");
  CHECK_EQ(bigint("104729").next_probable_prime().to_string(), "104743");

  std::mt19937_64 random(UINT64_C(0x5eed));
  const bigint prime = bigint::probable_prime(80, random);
  CHECK_EQ(prime.bit_length(), 80);
  CHECK(prime.is_probable_prime(100));
  CHECK(decimal_test::throws<std::runtime_error>(
      [&random] { static_cast<void>(bigint::probable_prime(1, random)); }));
  CHECK(decimal_test::throws<std::runtime_error>([] { static_cast<void>(bigint("-2").next_probable_prime()); }));
  return true;
}

}  // namespace

int main() {
  if (!test_gcd_and_modular_arithmetic()) {
    return 1;
  }
  return test_primes() ? 0 : 2;
}
