#ifndef DECIMAL_TEST_SUPPORT_H_
#define DECIMAL_TEST_SUPPORT_H_

#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

namespace decimal_test {

inline bool check(bool condition, const char* expression, const char* file, int line) {
  if (condition) {
    return true;
  }
  std::cerr << file << ':' << line << ": check failed: " << expression << '\n';
  return false;
}

template <typename T>
std::string value_text(const T& value) {
  std::ostringstream stream;
  stream << value;
  return stream.str();
}

template <typename Actual, typename Expected>
bool check_equal(const Actual& actual, const Expected& expected, const char* actual_expression,
                 const char* expected_expression, const char* file, int line) {
  if (actual == expected) {
    return true;
  }
  std::cerr << file << ':' << line << ": expected " << actual_expression << " == " << expected_expression
            << ", actual=" << value_text(actual) << ", expected=" << value_text(expected) << '\n';
  return false;
}

template <typename Exception, typename Function>
bool throws(Function&& function) {
  try {
    std::forward<Function>(function)();
  } catch (const Exception&) {
    return true;
  } catch (...) {
    return false;
  }
  return false;
}

inline std::uint64_t double_bits(double value) {
  std::uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

inline bool same_double(double lhs, double rhs) {
  return double_bits(lhs) == double_bits(rhs);
}

}  // namespace decimal_test

#define CHECK(condition)                                                                                               \
  do {                                                                                                                 \
    if (!decimal_test::check(static_cast<bool>(condition), #condition, __FILE__, __LINE__)) {                          \
      return false;                                                                                                    \
    }                                                                                                                  \
  } while (false)

#define CHECK_EQ(actual, expected)                                                                                     \
  do {                                                                                                                 \
    const auto& decimal_test_actual = (actual);                                                                        \
    const auto& decimal_test_expected = (expected);                                                                    \
    if (!decimal_test::check_equal(decimal_test_actual, decimal_test_expected, #actual, #expected, __FILE__,           \
                                   __LINE__)) {                                                                        \
      return false;                                                                                                    \
    }                                                                                                                  \
  } while (false)

#endif  // DECIMAL_TEST_SUPPORT_H_
