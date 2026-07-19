#include <decimal/decimal.h>

#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

static_assert(std::is_same_v<std::underlying_type_t<round_mode>, std::int32_t>);
static_assert(std::is_same_v<decltype(decimal_detail::count_trailing_zeros(std::uint32_t{})), int>);
static_assert(std::is_same_v<decltype(decimal_detail::count_leading_zeros(std::uint64_t{})), int>);
static_assert(std::is_same_v<decltype(decimal_detail::population_count(std::uint32_t{})), int>);
static_assert(std::is_same_v<decltype(std::declval<const math_context&>().precision()), std::int32_t>);
static_assert(std::is_same_v<decltype(std::declval<const math_context&>().hash_code()), std::int32_t>);
static_assert(std::is_same_v<decltype(std::declval<const jarray<std::uint32_t>&>().length()), std::int32_t>);

int main() {
  for (std::int32_t bit = 0; bit < 32; ++bit) {
    const std::uint32_t value = UINT32_C(1) << bit;
    if (decimal_detail::count_trailing_zeros(value) != bit || decimal_detail::count_leading_zeros(value) != 31 - bit ||
        decimal_detail::population_count(value) != 1) {
      return 1;
    }
  }
  for (std::int32_t bit = 0; bit < 64; ++bit) {
    const std::uint64_t value = UINT64_C(1) << bit;
    if (decimal_detail::count_leading_zeros(value) != 63 - bit) {
      return 2;
    }
  }
  if (decimal_detail::count_trailing_zeros(std::uint32_t{0}) != 32 ||
      decimal_detail::count_leading_zeros(std::uint64_t{0}) != 64 ||
      decimal_detail::population_count(UINT32_MAX) != 32) {
    return 3;
  }

  std::int64_t result = 0;
  const std::int64_t maximum = (std::numeric_limits<std::int64_t>::max)();
  const std::int64_t minimum = (std::numeric_limits<std::int64_t>::min)();
  if (!decimal_detail::add_overflow(maximum, 1, &result) || decimal_detail::add_overflow(maximum, -1, &result) ||
      result != maximum - 1 || !decimal_detail::add_overflow(minimum, -1, &result)) {
    return 4;
  }
  if (!decimal_detail::multiply_overflow(maximum, 2, &result) || decimal_detail::multiply_overflow(-3, -7, &result) ||
      result != 21 || !decimal_detail::multiply_overflow(minimum, -1, &result)) {
    return 5;
  }

  const decimal_detail::uint128_words full_product = decimal_detail::multiply_64x64(UINT64_MAX, UINT64_MAX);
  if (full_product.high != UINT64_MAX - 1 || full_product.low != 1) {
    return 6;
  }
  const decimal_detail::uint128_words carry_product =
      decimal_detail::multiply_64x64(UINT64_C(1) << 32, UINT64_C(1) << 32);
  if (carry_product.high != 1 || carry_product.low != 0) {
    return 7;
  }

#if defined(__SIZEOF_INT128__) && !defined(_MSC_VER)
  __extension__ using native_uint128 = unsigned __int128;
  std::uint64_t state = UINT64_C(0x9e3779b97f4a7c15);
  for (std::int32_t i = 0; i < 10000; ++i) {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    const std::uint64_t lhs_word = state;
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    const std::uint64_t rhs_word = state;
    const decimal_detail::uint128_words actual = decimal_detail::multiply_64x64(lhs_word, rhs_word);
    const native_uint128 expected = static_cast<native_uint128>(lhs_word) * rhs_word;
    if (actual.high != static_cast<std::uint64_t>(expected >> 64) ||
        actual.low != static_cast<std::uint64_t>(expected)) {
      return 8;
    }
  }
#endif

  const decimal lhs = decimal::value_of(std::int64_t{922337203685477580});
  const decimal rhs = decimal::value_of(std::int64_t{10});
  const decimal product = lhs.multiply(rhs);

  if (product.to_plain_string() != "9223372036854775800") {
    return 9;
  }

  const decimal rounded_product = lhs.multiply(rhs, math_context(10, round_mode::HALF_UP));
  if (rounded_product.to_plain_string() != "9223372037000000000") {
    return 10;
  }

  const decimal quotient = product.divide(decimal::value_of(std::int64_t{3}), 0, round_mode::HALF_UP);
  if (quotient.to_plain_string() != "3074457345618258600") {
    return 11;
  }

  const decimal fast_dividend = decimal::value_of(std::int64_t{92233720368547758});
  const decimal fast_divisor = decimal::value_of(std::int64_t{99999999999999999});
  const decimal fast_quotient = fast_dividend.divide(fast_divisor, math_context(17, round_mode::HALF_UP));
  return fast_quotient.to_plain_string() == "0.92233720368547759" ? 0 : 12;
}
