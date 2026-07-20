#include <test_support.h>

#include <decimal/decimal.h>

#include <cstdint>
#include <random>
#include <string>

namespace {

bool test_representation_helpers() {
  const bigint value("18446744073709551617");
  CHECK(value.compare_magnitude(bigint("18446744073709551616")) > 0);
  CHECK(value.compare_magnitude(INT64_MAX) > 0);
  CHECK(value.is_odd());
  CHECK(!value.is_even());
  CHECK_EQ(value.int_length(), 3);
  CHECK_EQ(value.sign_bit(), 0);
  CHECK_EQ(bigint("-1").sign_int(), UINT32_MAX);
  CHECK_EQ(value.get_int(0), UINT32_C(1));
  CHECK_EQ(value.get_int(2), UINT32_C(1));
  CHECK_EQ(value.first_nonzero_int_num(), 0);
  CHECK_EQ(value.get_lower(1).to_string(), "1");
  CHECK_EQ(value.get_upper(1).to_string(), "4294967296");
  CHECK_EQ(bigint("123456789").exact_divide_by3().to_string(), "41152263");
  CHECK_EQ(bigint::uint64_to_string(UINT64_MAX, 16), "ffffffffffffffff");
  CHECK_EQ(bigint::digit('z', 36), 35);
  CHECK_EQ(bigint::digit('z', 10), -1);
  return true;
}

bool test_array_helpers() {
  const jarray<std::uint32_t> leading{UINT32_C(0), UINT32_C(0), UINT32_C(5)};
  CHECK_EQ(bigint::strip_leading_zero_limbs(leading).length(), 1);
  CHECK_EQ(bigint::trusted_strip_leading_zero_limbs(leading).length(), 1);
  CHECK_EQ(bigint::from_twos_complement(jarray<std::uint32_t>{UINT32_MAX}).to_string(), "-1");

  jarray<std::uint32_t> increment{UINT32_MAX};
  increment = bigint::java_increment(std::move(increment));
  CHECK_EQ(increment.length(), 2);
  CHECK_EQ(increment[0], UINT32_C(1));
  CHECK_EQ(increment[1], UINT32_C(0));

  const jarray<std::uint32_t> x{UINT32_C(2)};
  CHECK_EQ(bigint::multiply_by_int(x, UINT32_C(3), 1).to_string(), "6");
  CHECK_EQ(bigint(1, bigint::add_magnitude(x, UINT64_C(5))).to_string(), "7");
  CHECK_EQ(bigint(1, bigint::subtract_magnitude(UINT64_C(9), x)).to_string(), "7");
  CHECK(decimal_test::throws<std::out_of_range>([] { bigint::check_from_index_size(2, 1, 2); }));
  bigint("1").check_range();
  CHECK(decimal_test::throws<std::runtime_error>([] { bigint::report_overflow(); }));
  return true;
}

bool test_random_and_mutable_helpers() {
  std::mt19937_64 random(UINT64_C(0xdec1a1));
  CHECK_EQ(bigint::random_bits(0, random).length(), 0);
  CHECK(bigint::random_magnitude(65, random).length() <= 3);
  CHECK(bigint::random_bigint(96, random).bit_length() <= 96);

  mutable_bigint mutable_value = bigint("12345678901234567890").to_mutable();
  CHECK_EQ(bigint::from_mutable(std::move(mutable_value), -1).to_string(), "-12345678901234567890");
  return true;
}

bool test_algorithm_helpers() {
  // TEST_COVERS: passes_lucas_lehmer
  CHECK_EQ(bigint("5").odd_mod_pow(bigint("117"), bigint("19")).to_string(), "1");
  CHECK_EQ(bigint("255").mod2(5).to_string(), "31");
  CHECK_EQ(bigint("3").mod_pow2(bigint("5"), 8).to_string(), "243");
  CHECK_EQ(bigint::jacobi_symbol(5, bigint("11")), 1);
  CHECK(bigint("104729").passes_miller_rabin(8));
  CHECK(bigint("104729").prime_to_certainty(50));
  CHECK_EQ(bigint("12345678901234567890").mod_small_prime_product(), UINT64_C(115958090967720));
  return true;
}

}  // namespace

int main() {
  if (!test_representation_helpers()) {
    return 1;
  }
  if (!test_array_helpers()) {
    return 2;
  }
  if (!test_random_and_mutable_helpers()) {
    return 3;
  }
  return test_algorithm_helpers() ? 0 : 4;
}
