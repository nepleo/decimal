#include <test_support.h>

#include <decimal/decimal.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

namespace {

double double_from_bits(std::uint64_t bits) {
  double value = 0;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

bool test_fixed_double_format() {
  struct format_case {
    std::uint64_t bits;
    const char* expected;
  };
  constexpr std::array<format_case, 14> cases = {{
      {UINT64_C(0x0000000000000000), "0.0"},
      {UINT64_C(0x8000000000000000), "-0.0"},
      {UINT64_C(0x3ff0000000000000), "1.0"},
      {UINT64_C(0x3f50624dd2f1a9fc), "0.001"},
      {UINT64_C(0x3f1a36e2eb1c432d), "1.0E-4"},
      {UINT64_C(0x416312cfe0000000), "9999999.0"},
      {UINT64_C(0x416312d000000000), "1.0E7"},
      {UINT64_C(0x44b52d02c7e14af6), "1.0E23"},
      {UINT64_C(0x0000000000000001), "4.9E-324"},
      {UINT64_C(0x7fefffffffffffff), "1.7976931348623157E308"},
      {UINT64_C(0x43da1c1272807ea1), "7.525596109301515E18"},
      {UINT64_C(0xc39b9400b0eafc2e), "-4.968035238584677E17"},
      {UINT64_C(0xc38b20417d31f0ea), "-2.443292805304231E17"},
      {UINT64_C(0x43b5ed64b744462e), "1.580029782564155E18"},
  }};
  for (const format_case& item : cases) {
    CHECK_EQ(decimal::double_to_format_string(double_from_bits(item.bits)), item.expected);
  }
  return true;
}

bool test_shortest_double_layout() {
  CHECK_EQ(decimal::normalize_shortest_double("1"), "1.0");
  CHECK_EQ(decimal::normalize_shortest_double("-0"), "-0.0");
  CHECK_EQ(decimal::normalize_shortest_double("0.001"), "0.001");
  CHECK_EQ(decimal::normalize_shortest_double("0.0001"), "1.0E-4");
  CHECK_EQ(decimal::normalize_shortest_double("1e+23"), "1.0E23");
  CHECK_EQ(decimal::normalize_shortest_double("9999999"), "9999999.0");
  CHECK_EQ(decimal::normalize_shortest_double("10000000"), "1.0E7");
  CHECK_EQ(decimal::normalize_shortest_double("4.9e-324"), "4.9E-324");
  return true;
}

bool test_random_double_round_trip() {
  std::uint64_t state = UINT64_C(0x9e3779b97f4a7c15);
  for (std::int32_t i = 0; i < 20000; ++i) {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    const double value = double_from_bits(state);
    if (!std::isfinite(value)) {
      continue;
    }
    const std::string text = decimal::double_to_format_string(value);
    char* end = nullptr;
    const double parsed = std::strtod(text.c_str(), &end);
    CHECK(end != nullptr && *end == '\0');
    CHECK(decimal_test::same_double(parsed, value));
  }
  return true;
}

}  // namespace

int main() {
  if (!test_fixed_double_format()) {
    return 1;
  }
  if (!test_shortest_double_layout()) {
    return 2;
  }
  return test_random_double_round_trip() ? 0 : 3;
}
