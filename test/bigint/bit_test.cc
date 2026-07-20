#include <test_support.h>

#include <decimal/decimal.h>

#include <cstdint>

namespace {

bool test_shift_operations() {
  // TEST_COVERS: left_shift shift_left_mag shift_left_impl_worker shift_right_impl shift_right_impl_worker
  // TEST_COVERS: primitive_left_shift primitive_right_shift
  CHECK_EQ(bigint("1").shift_left(65).to_string(), "36893488147419103232");
  CHECK_EQ(bigint("36893488147419103232").shift_right(65).to_string(), "1");
  CHECK_EQ(bigint("-9").shift_right(1).to_string(), "-5");
  CHECK_EQ(bigint("3").shift_left(-1).to_string(), "1");
  CHECK_EQ(bigint("3").shift_right(-2).to_string(), "12");
  return true;
}

bool test_logical_operations() {
  CHECK_EQ(bigint("12").and_op(bigint("10")).to_string(), "8");
  CHECK_EQ(bigint("12").or_op(bigint("10")).to_string(), "14");
  CHECK_EQ(bigint("12").xor_op(bigint("10")).to_string(), "6");
  CHECK_EQ(bigint("12").not_op().to_string(), "-13");
  CHECK_EQ(bigint("15").and_not(bigint("10")).to_string(), "5");
  CHECK_EQ(bigint("-1").and_op(bigint("123456789")).to_string(), "123456789");
  CHECK_EQ(bigint("-1").xor_op(bigint("123456789")).to_string(), "-123456790");
  return true;
}

bool test_individual_bits() {
  const bigint value("40");
  CHECK(value.test_bit(3));
  CHECK(value.test_bit(5));
  CHECK(!value.test_bit(4));
  CHECK_EQ(value.set_bit(4).to_string(), "56");
  CHECK_EQ(value.clear_bit(3).to_string(), "32");
  CHECK_EQ(value.flip_bit(5).to_string(), "8");
  CHECK_EQ(value.get_lowest_set_bit(), 3);
  CHECK_EQ(value.bit_length(), 6);
  CHECK_EQ(value.bit_count(), 2);
  CHECK_EQ(bigint::ZERO.get_lowest_set_bit(), -1);
  CHECK(bigint("-8").test_bit(100));
  CHECK_EQ(bigint("-8").bit_length(), 3);
  CHECK(decimal_test::throws<std::runtime_error>([&value] { static_cast<void>(value.test_bit(-1)); }));
  CHECK(decimal_test::throws<std::runtime_error>([&value] { static_cast<void>(value.set_bit(-1)); }));
  return true;
}

}  // namespace

int main() {
  if (!test_shift_operations()) {
    return 1;
  }
  if (!test_logical_operations()) {
    return 2;
  }
  return test_individual_bits() ? 0 : 3;
}
