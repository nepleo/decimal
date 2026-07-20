#include <test_support.h>

#include <decimal/decimal.h>

#include <array>
#include <cstdint>

namespace {

struct rounding_case {
  const char* input;
  std::array<const char*, 7> expected;
};

bool test_rounding_modes() {
  // TEST_COVERS: divide_and_round need_increment mulsub
  const std::array<round_mode, 7> modes = {round_mode::UP,       round_mode::DOWN,      round_mode::CEILING,
                                           round_mode::FLOOR,    round_mode::HALF_UP,   round_mode::HALF_DOWN,
                                           round_mode::HALF_EVEN};
  const std::array<rounding_case, 8> cases = {{
      {"5.5", {"6", "5", "6", "5", "6", "5", "6"}},
      {"2.5", {"3", "2", "3", "2", "3", "2", "2"}},
      {"1.6", {"2", "1", "2", "1", "2", "2", "2"}},
      {"1.1", {"2", "1", "2", "1", "1", "1", "1"}},
      {"-1.1", {"-2", "-1", "-1", "-2", "-1", "-1", "-1"}},
      {"-1.6", {"-2", "-1", "-1", "-2", "-2", "-2", "-2"}},
      {"-2.5", {"-3", "-2", "-2", "-3", "-3", "-2", "-2"}},
      {"-5.5", {"-6", "-5", "-5", "-6", "-6", "-5", "-6"}},
  }};

  for (const rounding_case& item : cases) {
    for (std::size_t i = 0; i < modes.size(); ++i) {
      CHECK_EQ(decimal(item.input).set_scale(0, modes[i]).to_string(), item.expected[i]);
    }
    CHECK(decimal_test::throws<std::runtime_error>(
        [&item] { static_cast<void>(decimal(item.input).set_scale(0, round_mode::UNNECESSARY)); }));
  }
  CHECK_EQ(decimal("1.0").set_scale(0, round_mode::UNNECESSARY).to_string(), "1");
  return true;
}

bool test_divide_overloads() {
  CHECK_EQ(decimal("1").divide(decimal("8")).to_string(), "0.125");
  CHECK_EQ(decimal("10.00").divide(decimal("4"), round_mode::HALF_UP).to_string(), "2.50");
  CHECK_EQ(decimal("1").divide(decimal("8"), 2, round_mode::HALF_UP).to_string(), "0.13");
  CHECK_EQ(decimal("1").divide(decimal("8"), 2, static_cast<std::int32_t>(round_mode::HALF_EVEN)).to_string(),
           "0.12");
  CHECK_EQ(decimal("1").divide(decimal("3"), math_context(5, round_mode::HALF_UP)).to_string(), "0.33333");
  CHECK_EQ(decimal("12345678901234567890").divide(decimal("7"), math_context(8, round_mode::DOWN)).to_string(),
           "1.7636684E+18");
  CHECK(decimal_test::throws<std::runtime_error>([] { static_cast<void>(decimal("1").divide(decimal("3"))); }));
  CHECK(decimal_test::throws<std::runtime_error>([] { static_cast<void>(decimal("1").divide(decimal("0"))); }));
  CHECK(decimal_test::throws<std::runtime_error>([] { static_cast<void>(decimal("0").divide(decimal("0"))); }));
  return true;
}

bool test_quotient_and_remainder() {
  CHECK_EQ(decimal("17.5").divide_to_integral_value(decimal("3")).to_string(), "5.0");
  CHECK_EQ(decimal("17.5").remainder(decimal("3")).to_string(), "2.5");
  CHECK_EQ(decimal("-17.5").remainder(decimal("3")).to_string(), "-2.5");

  const decimal dividend("12345678901234567890.25");
  const decimal divisor("97.5");
  const auto result = dividend.divide_and_remainder(divisor);
  CHECK_EQ(result.first.multiply(divisor).add(result.second).compare_to(dividend), 0);
  CHECK(result.second.abs().compare_to(divisor.abs()) < 0);

  const auto contextual = decimal("12345").divide_and_remainder(decimal("12"), math_context(4));
  CHECK_EQ(contextual.first.to_string(), "1028");
  CHECK_EQ(contextual.second.to_string(), "9");
  CHECK(decimal_test::throws<std::runtime_error>(
      [] { static_cast<void>(decimal("12345").divide_to_integral_value(decimal("12"), math_context(3))); }));
  return true;
}

}  // namespace

int main() {
  if (!test_rounding_modes()) {
    return 1;
  }
  if (!test_divide_overloads()) {
    return 2;
  }
  return test_quotient_and_remainder() ? 0 : 3;
}
