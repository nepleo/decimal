#include <test_support.h>

#include <decimal/decimal.h>

#include <array>

namespace {

bool test_fixed_arithmetic() {
  const bigint a("123456789012345678901234567890");
  const bigint b("98765432109876543210987654321");
  CHECK_EQ(a.add(b).to_string(), "222222221122222222112222222211");
  CHECK_EQ(a.subtract(b).to_string(), "24691356902469135690246913569");
  CHECK_EQ(a.multiply(b).to_string(), "12193263113702179522618503273362292333223746380111126352690");
  CHECK_EQ(a.add(INT64_C(-10)).to_string(), "123456789012345678901234567880");
  CHECK_EQ(bigint("-12").multiply(INT64_C(-7)).to_string(), "84");
  CHECK_EQ(bigint("-12").abs().to_string(), "12");
  CHECK_EQ(bigint("12").negate().to_string(), "-12");
  CHECK_EQ(bigint("-12").min(bigint("3")).to_string(), "-12");
  CHECK_EQ(bigint("-12").max(bigint("3")).to_string(), "3");
  CHECK_EQ(bigint("12").pow(10).to_string(), "61917364224");
  CHECK_EQ(bigint("123456789").square().to_string(), "15241578750190521");
  CHECK_EQ(bigint("15241578750190521").sqrt().to_string(), "123456789");
  const auto sqrt_rem = bigint("200").sqrt_and_remainder();
  CHECK_EQ(sqrt_rem.first.to_string(), "14");
  CHECK_EQ(sqrt_rem.second.to_string(), "4");
  CHECK(decimal_test::throws<std::runtime_error>([] { static_cast<void>(bigint("2").pow(-1)); }));
  CHECK(decimal_test::throws<std::runtime_error>([] { static_cast<void>(bigint("-1").sqrt()); }));
  return true;
}

bool test_arithmetic_properties() {
  const std::array<const char*, 7> values = {"0", "1", "-1", "4294967296", "-9223372036854775808",
                                              "123456789012345678901234567890", "-999999999999999999999999"};
  for (const char* lhs_text : values) {
    const bigint lhs(lhs_text);
    CHECK_EQ(lhs.add(bigint::ZERO).compare_to(lhs), 0);
    CHECK_EQ(lhs.multiply(bigint::ONE).compare_to(lhs), 0);
    CHECK_EQ(lhs.square().compare_to(lhs.multiply(lhs)), 0);
    for (const char* rhs_text : values) {
      const bigint rhs(rhs_text);
      CHECK_EQ(lhs.add(rhs).compare_to(rhs.add(lhs)), 0);
      CHECK_EQ(lhs.multiply(rhs).compare_to(rhs.multiply(lhs)), 0);
      CHECK_EQ(lhs.subtract(rhs).add(rhs).compare_to(lhs), 0);
    }
  }
  return true;
}

bool test_large_algorithm_paths() {
  // TEST_COVERS: multiply_karatsuba multiply_toom_cook3 get_toom_slice
  // TEST_COVERS: square_karatsuba square_toom_cook3 square_to_len impl_square_to_len
  // TEST_COVERS: impl_square_to_len_checks impl_square_to_len_into multiply_to_len
  // TEST_COVERS: multiply_to_len_check impl_multiply_to_len impl_multiply_to_len_into
  const bigint x = bigint::ONE.shift_left(32 * 250).add(bigint("12345678901234567890"));
  const bigint y = bigint::ONE.shift_left(32 * 241).add(bigint("9876543210987654321"));
  const bigint product = x.multiply(y);
  CHECK_EQ(product.divide(x).compare_to(y), 0);
  CHECK_EQ(product.divide(y).compare_to(x), 0);
  CHECK_EQ(x.square().compare_to(x.multiply(x)), 0);
  return true;
}

}  // namespace

int main() {
  if (!test_fixed_arithmetic()) {
    return 1;
  }
  if (!test_arithmetic_properties()) {
    return 2;
  }
  return test_large_algorithm_paths() ? 0 : 3;
}
