#ifndef DECIMAL_H_
#define DECIMAL_H_

#if defined(_MSVC_LANG)
#if _MSVC_LANG < 201703L
#error "decimal requires C++17 or later"
#endif
#elif __cplusplus < 201703L
#error "decimal requires C++17 or later"
#endif

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// 前向声明
namespace decimal_detail {
struct uint128_words;
}
struct math_context;
struct mutable_bigint;
struct signed_mutable_bigint;
struct bigint;
struct bit_sieve;
struct decimal;
template <typename T>
struct jarray;

// 编译器能力选择如下. portable 表示使用不依赖编译器扩展的纯 C++ 实现.
//
// +-------------+---------------+---------------+------------------+------------------+
// | Compiler    | Bit ops       | Overflow      | 128-bit multiply | 128-bit divide   |
// +-------------+---------------+---------------+------------------+------------------+
// | GCC         | __builtin_*   | __builtin_*   | __int128         | __int128         |
// | Clang       | __builtin_*   | __builtin_*   | __int128         | __int128         |
// | MSVC x64    | _BitScan*     | portable      | _umul128         | _udiv128         |
// | clang-cl    | __builtin_*   | __builtin_*   | _umul128         | portable         |
// | MSVC ARM64  | _BitScan*     | portable      | __umulh          | portable         |
// | Other C++17 | portable      | portable      | portable         | portable         |
// +-------------+---------------+---------------+------------------+------------------+
//
// 定义 DECIMAL_DISABLE_INTRINSICS=1 时,所有编译器都强制使用 portable 实现.

#if defined(DECIMAL_DISABLE_INTRINSICS) && DECIMAL_DISABLE_INTRINSICS
#define DECIMAL_DETAIL_INTRINSICS_DISABLED 1
#else
#define DECIMAL_DETAIL_INTRINSICS_DISABLED 0
#endif

#if defined(__has_builtin)
#define DECIMAL_DETAIL_HAS_BUILTIN(x) __has_builtin(x)
#else
#define DECIMAL_DETAIL_HAS_BUILTIN(x) 0
#endif

#if !DECIMAL_DETAIL_INTRINSICS_DISABLED && (DECIMAL_DETAIL_HAS_BUILTIN(__builtin_clz) || defined(__GNUC__))
#define DECIMAL_DETAIL_HAS_BIT_BUILTINS 1
#else
#define DECIMAL_DETAIL_HAS_BIT_BUILTINS 0
#endif

#if !DECIMAL_DETAIL_INTRINSICS_DISABLED && (DECIMAL_DETAIL_HAS_BUILTIN(__builtin_add_overflow) || defined(__GNUC__))
#define DECIMAL_DETAIL_HAS_OVERFLOW_BUILTINS 1
#else
#define DECIMAL_DETAIL_HAS_OVERFLOW_BUILTINS 0
#endif

#if !DECIMAL_DETAIL_INTRINSICS_DISABLED && defined(__SIZEOF_INT128__) && !defined(_MSC_VER)
#define DECIMAL_DETAIL_HAS_NATIVE_INT128 1
#else
#define DECIMAL_DETAIL_HAS_NATIVE_INT128 0
#endif

// MSVC 和 clang-cl 都可以包含 <intrin.h>,但其中可用的 intrinsic 不完全相同.
#if !DECIMAL_DETAIL_INTRINSICS_DISABLED && defined(_MSC_VER)
#define DECIMAL_DETAIL_HAS_MSVC_INTRINSICS 1
#include <intrin.h>
#else
#define DECIMAL_DETAIL_HAS_MSVC_INTRINSICS 0
#endif

// _udiv128 仅在真正的 MSVC x64 环境中可用. clang-cl 的 <intrin.h> 提供
// _umul128,但不提供 _udiv128,所以 clang-cl 的 128 位除法必须走通用实现.
#if DECIMAL_DETAIL_HAS_MSVC_INTRINSICS && defined(_M_X64) && !defined(__clang__) && !defined(__GNUC__) && \
    _MSC_VER >= 1920
#define DECIMAL_DETAIL_HAS_MSVC_DIV128 1
#else
#define DECIMAL_DETAIL_HAS_MSVC_DIV128 0
#endif

#if DECIMAL_DETAIL_HAS_NATIVE_INT128 || DECIMAL_DETAIL_HAS_MSVC_DIV128
#define DECIMAL_DETAIL_HAS_FAST_DIV128 1
#else
#define DECIMAL_DETAIL_HAS_FAST_DIV128 0
#endif

#if DECIMAL_DETAIL_INTRINSICS_DISABLED && (DECIMAL_DETAIL_HAS_BIT_BUILTINS || DECIMAL_DETAIL_HAS_OVERFLOW_BUILTINS || \
                                           DECIMAL_DETAIL_HAS_NATIVE_INT128 || DECIMAL_DETAIL_HAS_MSVC_INTRINSICS)
#error "DECIMAL_DISABLE_INTRINSICS must disable all compiler intrinsics"
#endif

namespace decimal_detail {

struct uint128_words {
  std::uint64_t high;
  std::uint64_t low;
};

inline int count_trailing_zeros(std::uint32_t value) noexcept {
  if (value == 0) {
    return 32;
  }
#if DECIMAL_DETAIL_HAS_BIT_BUILTINS
  return __builtin_ctz(value);
#elif DECIMAL_DETAIL_HAS_MSVC_INTRINSICS
  unsigned long index = 0;
  _BitScanForward(&index, value);
  return static_cast<int>(index);
#else
  int count = 0;
  while ((value & 1U) == 0) {
    value >>= 1;
    ++count;
  }
  return count;
#endif
}

inline int count_leading_zeros(std::uint32_t value) noexcept {
  if (value == 0) {
    return 32;
  }
#if DECIMAL_DETAIL_HAS_BIT_BUILTINS
  return __builtin_clz(value);
#elif DECIMAL_DETAIL_HAS_MSVC_INTRINSICS
  unsigned long index = 0;
  _BitScanReverse(&index, value);
  return 31 - static_cast<int>(index);
#else
  int count = 0;
  for (std::uint32_t bit = UINT32_C(1) << 31; (value & bit) == 0; bit >>= 1) {
    ++count;
  }
  return count;
#endif
}

inline int count_leading_zeros(std::uint64_t value) noexcept {
  if (value == 0) {
    return 64;
  }
#if DECIMAL_DETAIL_HAS_BIT_BUILTINS
  return __builtin_clzll(static_cast<unsigned long long>(value));
#elif DECIMAL_DETAIL_HAS_MSVC_INTRINSICS && (defined(_M_X64) || defined(_M_ARM64))
  unsigned long index = 0;
  _BitScanReverse64(&index, value);
  return 63 - static_cast<int>(index);
#elif DECIMAL_DETAIL_HAS_MSVC_INTRINSICS
  const std::uint32_t high = static_cast<std::uint32_t>(value >> 32);
  return high != 0 ? count_leading_zeros(high) : 32 + count_leading_zeros(static_cast<std::uint32_t>(value));
#else
  int count = 0;
  for (std::uint64_t bit = UINT64_C(1) << 63; (value & bit) == 0; bit >>= 1) {
    ++count;
  }
  return count;
#endif
}

inline int population_count(std::uint32_t value) noexcept {
#if DECIMAL_DETAIL_HAS_BIT_BUILTINS
  return __builtin_popcount(value);
#else
  value -= (value >> 1) & UINT32_C(0x55555555);
  value = (value & UINT32_C(0x33333333)) + ((value >> 2) & UINT32_C(0x33333333));
  value = (value + (value >> 4)) & UINT32_C(0x0f0f0f0f);
  return static_cast<int>((value * UINT32_C(0x01010101)) >> 24);
#endif
}

inline bool add_overflow(std::int64_t lhs, std::int64_t rhs, std::int64_t* result) noexcept {
#if DECIMAL_DETAIL_HAS_OVERFLOW_BUILTINS
  return __builtin_add_overflow(lhs, rhs, result);
#else
  if ((rhs > 0 && lhs > (std::numeric_limits<std::int64_t>::max)() - rhs) ||
      (rhs < 0 && lhs < (std::numeric_limits<std::int64_t>::min)() - rhs)) {
    return true;
  }
  *result = lhs + rhs;
  return false;
#endif
}

inline bool multiply_overflow(std::int64_t lhs, std::int64_t rhs, std::int64_t* result) noexcept {
#if DECIMAL_DETAIL_HAS_OVERFLOW_BUILTINS
  return __builtin_mul_overflow(lhs, rhs, result);
#else
  if (lhs == 0 || rhs == 0) {
    *result = 0;
    return false;
  }
  const std::int64_t maximum = (std::numeric_limits<std::int64_t>::max)();
  const std::int64_t minimum = (std::numeric_limits<std::int64_t>::min)();
  const bool overflow = lhs > 0 ? (rhs > 0 ? lhs > maximum / rhs : rhs < minimum / lhs)
                                : (rhs > 0 ? lhs < minimum / rhs : rhs < maximum / lhs);
  if (overflow) {
    return true;
  }
  *result = lhs * rhs;
  return false;
#endif
}

inline std::uint64_t unsigned_magnitude(std::int64_t value) noexcept {
  return value < 0 ? static_cast<std::uint64_t>(-(value + 1)) + 1 : static_cast<std::uint64_t>(value);
}

inline uint128_words multiply_64x64(std::uint64_t lhs, std::uint64_t rhs) noexcept {
#if DECIMAL_DETAIL_HAS_NATIVE_INT128
  __extension__ using native_uint128 = unsigned __int128;
  const native_uint128 product = static_cast<native_uint128>(lhs) * rhs;
  return {static_cast<std::uint64_t>(product >> 64), static_cast<std::uint64_t>(product)};
#elif DECIMAL_DETAIL_HAS_MSVC_INTRINSICS && defined(_M_X64)
  std::uint64_t high = 0;
  const std::uint64_t low = _umul128(lhs, rhs, &high);
  return {high, low};
#elif DECIMAL_DETAIL_HAS_MSVC_INTRINSICS && defined(_M_ARM64)
  return {__umulh(lhs, rhs), lhs * rhs};
#else
  const std::uint64_t lhs_high = lhs >> 32;
  const std::uint64_t lhs_low = lhs & UINT32_MAX;
  const std::uint64_t rhs_high = rhs >> 32;
  const std::uint64_t rhs_low = rhs & UINT32_MAX;
  const std::uint64_t low_product = lhs_low * rhs_low;
  const std::uint64_t middle1 = lhs_high * rhs_low + (low_product >> 32);
  const std::uint64_t middle1_low = middle1 & UINT32_MAX;
  const std::uint64_t middle2 = lhs_low * rhs_high + middle1_low;
  const std::uint64_t high = lhs_high * rhs_high + (middle1 >> 32) + (middle2 >> 32);
  const std::uint64_t low = (middle2 << 32) | (low_product & UINT32_MAX);
  return {high, low};
#endif
}

#if DECIMAL_DETAIL_HAS_FAST_DIV128
inline std::uint64_t divide_128_by_64(std::uint64_t high, std::uint64_t low, std::uint64_t divisor,
                                      std::uint64_t* remainder) noexcept {
#if DECIMAL_DETAIL_HAS_NATIVE_INT128
  __extension__ using native_uint128 = unsigned __int128;
  const native_uint128 dividend = (static_cast<native_uint128>(high) << 64) | low;
  *remainder = static_cast<std::uint64_t>(dividend % divisor);
  return static_cast<std::uint64_t>(dividend / divisor);
#else
  return _udiv128(high, low, divisor, remainder);
#endif
}
#endif

}  // namespace decimal_detail

// round_mode
// 控制缩小精度并丢弃十进制数字时的舍入方向.
//
// "丢弃部分"是缩小精度时删除的尾部数字.只要丢弃部分非零,结果就不再精确.
//
// 各模式的规则和示例:
//
// +-------------+--------------------------------------------------+-------------------------------+
// | Mode        | Rule                                             | Example                       |
// +-------------+--------------------------------------------------+-------------------------------+
// | UP          | Discarded nonzero digits -> away from zero       | 1.1 -> 2; -1.1 -> -2          |
// | DOWN        | Discard digits without incrementing              | 1.9 -> 1; -1.9 -> -1          |
// | CEILING     | Toward +infinity                                 | 1.1 -> 2; -1.1 -> -1          |
// | FLOOR       | Toward -infinity                                 | 1.9 -> 1; -1.1 -> -2          |
// | HALF_UP     | Nearest; midpoint -> away from zero              | 2.5 -> 3; -2.5 -> -3          |
// | HALF_DOWN   | Nearest; midpoint -> toward zero                 | 2.5 -> 2; -2.5 -> -2          |
// | HALF_EVEN   | Nearest; midpoint -> even last digit             | 2.5 -> 2; 5.5 -> 6            |
// | UNNECESSARY | Require exact result; otherwise error            | 1.0 -> 1; 1.1 -> error        |
// +-------------+--------------------------------------------------+-------------------------------+
//
// 下表将两位有效数字舍入为一位. error 表示存在非零丢弃部分,因此拒绝运算.
//
// +-------+----+------+---------+-------+---------+-----------+-----------+-------------+
// | Input | UP | DOWN | CEILING | FLOOR | HALF_UP | HALF_DOWN | HALF_EVEN | UNNECESSARY |
// +-------+----+------+---------+-------+---------+-----------+-----------+-------------+
// |  5.5  | 6  | 5    | 6       | 5     | 6       | 5         | 6         | error       |
// |  2.5  | 3  | 2    | 3       | 2     | 3       | 2         | 2         | error       |
// |  1.6  | 2  | 1    | 2       | 1     | 2       | 2         | 2         | error       |
// |  1.1  | 2  | 1    | 2       | 1     | 1       | 1         | 1         | error       |
// |  1.0  | 1  | 1    | 1       | 1     | 1       | 1         | 1         | 1           |
// | -1.0  | -1 | -1   | -1      | -1    | -1      | -1        | -1        | -1          |
// | -1.1  | -2 | -1   | -1      | -2    | -1      | -1        | -1        | error       |
// | -1.6  | -2 | -1   | -1      | -2    | -2      | -2        | -2        | error       |
// | -2.5  | -3 | -2   | -2      | -3    | -3      | -2        | -2        | error       |
// | -5.5  | -6 | -5   | -5      | -6    | -6      | -5        | -6        | error       |
// +-------+----+------+---------+-------+---------+-----------+-----------+-------------+
//
enum class round_mode : std::int32_t {
  UP = 0,
  DOWN = 1,
  CEILING = 2,
  FLOOR = 3,
  HALF_UP = 4,
  HALF_DOWN = 5,
  HALF_EVEN = 6,
  UNNECESSARY = 7,
};

// 将整型常量转换为 round_mode
//
// 合法范围为 [UP, UNNECESSARY] (含端点), 与上述枚举底层值一致
// 越界时抛出 std::invalid_argument.
inline round_mode value_of(std::int32_t v) {
  if (v < static_cast<std::int32_t>(round_mode::UP) || v > static_cast<std::int32_t>(round_mode::UNNECESSARY)) {
    throw std::invalid_argument("argument out of range");
  }
  return static_cast<round_mode>(v);
}

// math_context
// 控制 decimal 运算精度和舍入行为的值对象
//
// precision 表示运算结果保留的十进制有效位数
// precision == 0 表示不限精度,运算必须返回精确结果,rounding_mode 此时不参与计算
// precision > 0 时先按数学意义计算精确结果,再使用 rounding_mode 舍入到指定有效位数
//
// DECIMAL32、DECIMAL64 和 DECIMAL128 只采用对应 IEEE 754 格式的精度和舍入模式
// 这些预定义上下文不限制指数范围,也不会模拟固定宽度十进制浮点格式的溢出和下溢
//
// 这个 struct 按项目约定保持全公有,调用者应将其作为不可变值使用
struct math_context {
  // 精度为 0,舍入模式为 HALF_UP
  static const math_context UNLIMITED;

  // IEEE 754 decimal32 对应的 7 位精度,舍入模式为 HALF_EVEN
  static const math_context DECIMAL32;

  // IEEE 754 decimal64 对应的 16 位精度,舍入模式为 HALF_EVEN
  static const math_context DECIMAL64;

  // IEEE 754 decimal128 对应的 34 位精度,舍入模式为 HALF_EVEN
  static const math_context DECIMAL128;

  // 有效位数的最小值
  static constexpr std::int32_t min_digits = 0;

  // 默认的舍入方式
  static constexpr round_mode default_rounding_mode = round_mode::HALF_UP;

  // 使用指定 precision 和默认 HALF_UP 舍入模式构造上下文
  // precision 必须非负,0 表示不限精度
  math_context(std::int32_t set_precision) : math_context(set_precision, default_rounding_mode) {
  }

  // 使用指定 precision 和 rounding_mode 构造上下文
  // set_precision 必须非负,否则抛出 std::invalid_argument
  math_context(std::int32_t set_precision, round_mode set_rounding_mode)
      : precision_(set_precision), rounding_mode_(set_rounding_mode) {
    if (set_precision < min_digits) {
      throw std::invalid_argument("digits < 0");
    }
  }

  // 显式声明拷贝构造,避免接口行为依赖编译器隐式生成
  math_context(const math_context& other) = default;

  // 显式声明拷贝赋值,保持值对象语义
  math_context& operator=(const math_context& other) = default;

  // 显式声明移动构造,移动后源对象恢复为 UNLIMITED 状态
  math_context(math_context&& other) noexcept : precision_(other.precision_), rounding_mode_(other.rounding_mode_) {
    other.precision_ = 0;
    other.rounding_mode_ = default_rounding_mode;
  }

  // 显式声明移动赋值,移动后源对象恢复为 UNLIMITED 状态
  math_context& operator=(math_context&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    precision_ = other.precision_;
    rounding_mode_ = other.rounding_mode_;
    other.precision_ = 0;
    other.rounding_mode_ = default_rounding_mode;
    return *this;
  }

  // 返回 precision 设置,始终非负
  std::int32_t precision() const {
    return precision_;
  }

  // 返回 rounding_mode 设置
  round_mode get_rounding_mode() const {
    return rounding_mode_;
  }

  // 判断两个上下文是否具有完全相同的设置
  bool operator==(const math_context& other) const {
    return precision_ == other.precision_ && rounding_mode_ == other.rounding_mode_;
  }

  // 判断两个上下文设置是否不相等
  bool operator!=(const math_context& other) const {
    return !(*this == other);
  }

  // 返回由 precision 和 rounding_mode 共同计算的稳定哈希值
  std::int32_t hash_code() const {
    return precision_ + static_cast<std::int32_t>(rounding_mode_) * 59;
  }

  std::int32_t precision_{0};
  round_mode rounding_mode_{default_rounding_mode};
};

inline const math_context math_context::UNLIMITED{0, round_mode::HALF_UP};
inline const math_context math_context::DECIMAL32{7, round_mode::HALF_EVEN};
inline const math_context math_context::DECIMAL64{16, round_mode::HALF_EVEN};
inline const math_context math_context::DECIMAL128{34, round_mode::HALF_EVEN};

// jarray
template <typename T>
struct jarray {
  static_assert(std::is_trivially_copyable_v<T>, "jarray<T> only support trivially copyable types");

  // 默认构造空数组
  jarray() = default;

  // 分配长度为 len 的数组; len 为 0 时不分配.
  jarray(std::int32_t len) : data_(len > 0 ? new T[len]() : nullptr), len_(len) {
    assert(len >= 0);
  }

  // 从初始化列表构造 jarray
  jarray(std::initializer_list<T> init)
      : data_(init.size() > 0 ? new T[init.size()]() : nullptr), len_(static_cast<std::int32_t>(init.size())) {
    if (len_ > 0) {
      std::memcpy(data_, init.begin(), static_cast<size_t>((len_)) * sizeof(T));
    }
  }

  // 拷贝构造 jarray
  jarray(const jarray& other) : data_(other.len_ > 0 ? new T[other.len_]() : nullptr), len_(other.len_) {
    if (len_ > 0) {
      std::memcpy(data_, other.data_, std::size_t(len_) * sizeof(T));
    }
  }

  // 拷贝赋值 jarray
  jarray& operator=(const jarray& other) {
    if (this == &other) {
      return *this;
    }

    jarray tmp(other);
    swap(tmp);
    return *this;
  }

  // 移动构造 jarray
  jarray(jarray&& other) noexcept : data_(other.data_), len_(other.len_) {
    other.data_ = nullptr;
    other.len_ = 0;
  }

  // 移动赋值 jarray
  jarray& operator=(jarray&& other) noexcept {
    if (this == &other) {
      return *this;
    }

    delete[] data_;

    data_ = other.data_;
    len_ = other.len_;

    other.data_ = nullptr;
    other.len_ = 0;

    return *this;
  }

  // 析构并释放数组存储
  ~jarray() {
    delete[] data_;
  }

  // 返回数组长度
  std::int32_t length() const {
    return len_;
  }

  // 数组长度是否为 0
  bool empty() const {
    return len_ == 0;
  }

  // 返回指定下标的元素引用
  T& operator[](std::int32_t index) {
    assert(index >= 0 && index < len_);
    return data_[index];
  }

  // 返回指定下标的 const 元素引用
  const T& operator[](std::int32_t index) const {
    assert(index >= 0 && index < len_);
    return data_[index];
  }

  // 返回底层数组指针
  T* data() {
    return data_;
  }

  // 返回底层 const 数组指针
  const T* data() const {
    return data_;
  }

  // 重新分配数组存储
  void alloc(std::int32_t len) {
    assert(len >= 0);
    T* new_data = len > 0 ? new T[len]() : nullptr;
    delete[] data_;
    data_ = new_data;
    len_ = len;
  }

  // 交换两个 jarray 的内容
  void swap(jarray& other) noexcept {
    std::swap(data_, other.data_);
    std::swap(len_, other.len_);
  }

  // 用 val 填充整个数组
  void fill(T val) {
    if (len_ == 0) {
      return;
    }

    std::fill(data_, data_ + len_, val);
  }

  // 用 val 填充 [from, to) 区间
  void fill(std::int32_t from, std::int32_t to, T val) {
    assert(from >= 0);
    assert(from <= to);
    assert(to <= len_);
    if (from == to) {
      return;
    }
    std::fill(data_ + from, data_ + to, val);
  }

  // 拷贝为指定长度的新数组
  jarray copy_of(std::int32_t new_len) const {
    assert(new_len >= 0);
    jarray result(new_len);
    const std::int32_t copy_len = std::min(len_, new_len);
    if (copy_len > 0) {
      std::memcpy(result.data_, data_, std::size_t(copy_len) * sizeof(T));
    }
    return result;
  }

  // 拷贝指定区间为新数组
  jarray copy_of_range(std::int32_t from, std::int32_t to) const {
    assert(from >= 0);
    assert(from <= to);
    assert(from <= len_);
    const std::int32_t new_len = to - from;
    jarray result(new_len);
    const std::int32_t available = len_ - from;
    const std::int32_t copy_len = std::min(available, new_len);
    if (copy_len > 0) {
      std::memcpy(result.data(), data_ + from, std::size_t(copy_len) * sizeof(T));
    }
    return result;
  }

  // 返回数组的深拷贝
  jarray clone() const {
    return jarray(*this);
  }

  // 返回指向首元素的迭代器
  T* begin() {
    return data_;
  }
  // 返回指向尾后位置的迭代器
  T* end() {
    return len_ == 0 ? nullptr : data_ + len_;
  }
  // 返回 const 首元素迭代器
  const T* begin() const {
    return data_;
  }
  // 返回 const 尾后迭代器
  const T* end() const {
    return len_ == 0 ? nullptr : data_ + len_;
  }

  T* data_{nullptr};
  std::int32_t len_{0};
};

template <typename T>
void jarray_copy(const jarray<T>& src, std::int32_t src_pos, jarray<T>& dst, std::int32_t dst_pos, std::int32_t len) {
  if (src_pos < 0 || dst_pos < 0 || len < 0) {
    throw std::out_of_range("jarray_copy: negative index or length");
  }

  const std::int64_t src_end = static_cast<std::int64_t>(src_pos) + len;
  const std::int64_t dst_end = static_cast<std::int64_t>(dst_pos) + len;
  if (src_end > src.length() || dst_end > dst.length()) {
    throw std::out_of_range("jarray_copy: range out of bounds");
  }

  if (len == 0) {
    return;
  }

  std::memmove(dst.data() + dst_pos, src.data() + src_pos, static_cast<std::size_t>(len) * sizeof(T));
}

// mutable_bigint
// bigint 和 decimal 内部运算使用的可变非负多精度整数
//
// 当前值只保存绝对值而不保存符号,所有 limb 均按无符号 32 位数解释
// value_ 使用大端顺序,有效 magnitude 位于 [offset_, offset_ + int_len_)
// 有效区间可以只占底层数组的一部分,从而在迭代运算中复用已有存储并减少重新分配
//
// int_len_ == 0 表示 0,规范非零值的第一个有效 limb 必须非零
// normalize() 用于移除有效区间的前导零并恢复规范形式
// 算术、移位和除法等操作会原地修改当前对象或显式传入的结果对象
//
// 这个 struct 按项目约定保持全公有,仅供内部算法使用
struct mutable_bigint {
  // 大端 limb 数组,存放绝对值;有效区间由 offset_ 与 int_len_ 界定
  // limb = 大整数绝对值在数组里的一个 std::uint32_t 存储单元
  jarray<std::uint32_t> value_;
  // value_ 数组中当前用于保存本数绝对值的 32 位单元数量.数值从 offset_ 开始,
  // offset_ + int_len_ 可以小于 value_.length()
  std::int32_t int_len_{0};
  // 本数绝对值在 value_ 数组中的起始偏移量
  std::int32_t offset_{0};

  // 常量声明
  static constexpr std::int32_t KNUTH_POW2_THRESH_LEN = 6;
  static constexpr std::int32_t KNUTH_POW2_THRESH_ZEROS = 3;
  static constexpr std::int32_t BURNIKEL_ZIEGLER_THRESHOLD = 80;
  static constexpr std::int32_t BURNIKEL_ZIEGLER_OFFSET = 40;
  static const mutable_bigint ONE;

  // 构造容量为一个 limb 的规范零值
  mutable_bigint() {
    value_ = jarray<std::uint32_t>(1);
    int_len_ = 0;
  }

  // 使用单个无符号 32 位 limb 作为 magnitude 构造对象
  // 构造函数不执行 normalize(),val == 0 时有效区间仍包含一个零 limb
  mutable_bigint(std::uint32_t val) {
    value_ = jarray<std::uint32_t>(1);
    int_len_ = 1;
    value_[0] = val;
  }

  // 复制大端 magnitude 数组并将完整数组作为有效区间
  // 构造函数不移除前导零,调用方在需要规范形式时应执行 normalize()
  mutable_bigint(const jarray<std::uint32_t>& val) {
    value_ = jarray<std::uint32_t>(val);
    int_len_ = val.length();
  }

  // 接管大端 magnitude 数组并将完整数组作为有效区间
  // 构造函数不移除前导零,调用方在需要规范形式时应执行 normalize()
  mutable_bigint(jarray<std::uint32_t>&& val) noexcept : value_(std::move(val)), int_len_(value_.length()) {
  }

  // 复制 bigint 的规范 magnitude 构造非负可变值,忽略原值符号
  explicit mutable_bigint(const bigint& val);

  // 复制 val 的有效区间并将新对象的 offset_ 设为 0
  mutable_bigint(const mutable_bigint& val) {
    int_len_ = val.int_len_;
    value_ = val.value_.copy_of_range(val.offset_, val.offset_ + int_len_);
  }

  // 用 val 的有效区间替换当前值,自赋值时保持不变
  // 赋值后 offset_ 为 0,不会复制 val 有效区间之外的备用容量
  mutable_bigint& operator=(const mutable_bigint& val) {
    if (this == &val) {
      return *this;
    }
    int_len_ = val.int_len_;
    offset_ = 0;
    value_ = val.value_.copy_of_range(val.offset_, val.offset_ + int_len_);
    return *this;
  }

  // 接管 val 的底层存储、有效长度和偏移量
  // 移动后 val 变为 int_len_ == 0 且 offset_ == 0 的零值
  mutable_bigint(mutable_bigint&& val) noexcept
      : value_(std::move(val.value_)), int_len_(val.int_len_), offset_(val.offset_) {
    val.int_len_ = 0;
    val.offset_ = 0;
  }

  // 用 val 的底层存储、有效长度和偏移量替换当前对象
  // 自移动时保持不变,移动后 val 重置为零
  mutable_bigint& operator=(mutable_bigint&& val) noexcept {
    if (this == &val) {
      return *this;
    }
    value_ = std::move(val.value_);
    int_len_ = val.int_len_;
    offset_ = val.offset_;
    val.int_len_ = 0;
    val.offset_ = 0;
    return *this;
  }

  // 将当前值重置为规范零,并清零底层数组的全部 limb
  // 保留已分配容量以供后续运算复用
  void clear() {
    offset_ = 0;
    int_len_ = 0;
    value_.fill(0);
  }

  // 仅将 int_len_ 和 offset_ 重置为 0,逻辑上把当前值设为零
  // 底层数组内容和容量保持不变,旧 limb 不再属于有效区间
  void reset() {
    offset_ = 0;
    int_len_ = 0;
  }

  // 将有效区间内下标 index 的 limb 设置为 val
  // 调用方负责保证 index 位于 [0, int_len_)
  void set_int(std::int32_t index, std::uint32_t val) {
    value_[offset_ + index] = val;
  }

  // 复制大端数组 val,并将前 length 个 limb 设为从 offset_ == 0 开始的有效区间
  // length 必须位于 [0, val.length()],函数不执行 normalize()
  void set_value(const jarray<std::uint32_t>& val, std::int32_t length) {
    assert(length >= 0);
    assert(length <= val.length());
    value_ = val;
    int_len_ = length;
    offset_ = 0;
  }

  // 接管大端数组 val,并将前 length 个 limb 设为从 offset_ == 0 开始的有效区间
  // length 必须位于 [0, val.length()],函数不执行 normalize()
  void set_value(jarray<std::uint32_t>&& val, std::int32_t length) noexcept {
    assert(length >= 0);
    assert(length <= val.length());
    value_ = std::move(val);
    int_len_ = length;
    offset_ = 0;
  }

  // 将 src 的有效区间复制到当前对象并把 offset_ 设为 0
  // 现有容量足够时复用底层数组,src 保持不变
  void copy_value(const mutable_bigint& src) {
    const std::int32_t len = src.int_len_;
    if (value_.length() < len) {
      value_.alloc(len);
    }
    jarray_copy(src.value_, src.offset_, value_, 0, len);
    int_len_ = len;
    offset_ = 0;
  }

  // 将完整大端数组 val 复制为当前有效区间并把 offset_ 设为 0
  // 现有容量足够时复用底层数组,函数不移除前导零
  void copy_value(const jarray<std::uint32_t>& val) {
    const std::int32_t len = val.length();
    if (value_.length() < len) {
      value_.alloc(len);
    }
    jarray_copy(val, 0, value_, 0, len);
    int_len_ = len;
    offset_ = 0;
  }

  // 返回仅包含当前有效区间的独立大端 magnitude 数组
  // 返回数组长度等于 int_len_,不包含备用容量
  jarray<std::uint32_t> to_int_array() const {
    jarray<std::uint32_t> result(int_len_);
    for (std::int32_t i = 0; i < int_len_; ++i) {
      result[i] = value_[offset_ + i];
    }
    return result;
  }

  // 使用 sign 和当前 magnitude 构造规范 bigint
  // 当前值为零或 sign == 0 时返回 bigint::ZERO,非零时 sign 应为 -1 或 1
  bigint to_bigint(std::int32_t sign);

  // 规范化当前对象并将其转换为非负 bigint
  bigint to_bigint();

  // 使用 sign * magnitude 作为 unscaled value、使用 scale 构造 decimal
  // magnitude 可放入 std::int64_t 时使用 compact 表示,否则使用 bigint 表示
  decimal to_decimal(std::int32_t sign, std::int32_t scale);

  // 返回带 sign 的 std::int64_t compact 值
  // magnitude 无法精确表示为 compact 值时返回 decimal::INFLATED
  std::int64_t to_compact_value(std::int32_t sign);

  // 将当前非负 magnitude 转换为十进制字符串
  std::string to_string();

  // 返回有效区间是否为空
  // 调用前应保证对象已规范化,否则单个零 limb 不会被识别为零
  bool is_zero() const {
    return int_len_ == 0;
  }

  // 返回规范 magnitude 是否恰好等于 1
  bool is_one() const {
    return int_len_ == 1 && value_[offset_] == 1;
  }

  // 返回当前 magnitude 是否为偶数,规范零值视为偶数
  bool is_even() const {
    return int_len_ == 0 || ((value_[offset_ + int_len_ - 1] & 1U) == 0);
  }

  // 返回当前 magnitude 是否为奇数,规范零值返回 false
  bool is_odd() const {
    return !is_zero() && ((value_[offset_ + int_len_ - 1] & 1U) == 1);
  }

  // 从有效区间移除所有前导零 limb
  // 规范化后非零值的首个有效 limb 非零,零值的 int_len_ 和 offset_ 均为 0
  void normalize() {
    if (int_len_ == 0) {
      offset_ = 0;
      return;
    }

    std::int32_t index = offset_;
    if (value_[index] != 0) {
      return;
    }

    const std::int32_t index_bound = index + int_len_;
    do {
      ++index;
    } while (index < index_bound && value_[index] == 0);

    const std::int32_t num_zeros = index - offset_;
    int_len_ -= num_zeros;
    offset_ = int_len_ == 0 ? 0 : offset_ + num_zeros;
  }

  // 返回当前对象是否满足有效区间和前导零不变量
  // 空有效区间视为规范,非空区间必须位于 value_ 内且首 limb 非零
  bool is_normal() const {
    if (int_len_ + offset_ > value_.length()) {
      return false;
    }
    if (int_len_ == 0) {
      return true;
    }
    return value_[offset_] != 0;
  }

  // 返回 32 位 limb 从最低位开始连续零 bit 的数量
  // limb == 0 时返回 32
  static std::int32_t number_of_trailing_zeros(std::uint32_t limb) {
    return decimal_detail::count_trailing_zeros(limb);
  }

  // 返回 32 位 limb 从最高位开始连续零 bit 的数量
  // limb == 0 时返回 32
  static std::int32_t number_of_leading_zeros(std::uint32_t limb) {
    return decimal_detail::count_leading_zeros(limb);
  }

  // 返回 limb 的有效 bit 长度,limb == 0 时返回 0
  static std::int32_t bit_length_for_limb(std::uint32_t limb) {
    return 32 - number_of_leading_zeros(limb);
  }

  // 将无符号 64 位 n 除以无符号 32 位 d
  // 返回值高 32 bit 保存余数,低 32 bit 保存商,d 必须非零且商必须能放入 32 bit
  static std::uint64_t div_word(std::uint64_t n, std::uint32_t d) {
    const std::uint64_t d_long = d & 0xffffffffULL;
    std::uint64_t q = 0;
    std::uint64_t r = 0;
    if (d_long == 1) {
      q = static_cast<std::uint32_t>(n);
      return (r << 32) | (q & 0xffffffffULL);
    }

    q = n / d_long;
    r = n - q * d_long;
    return (r << 32) | (q & 0xffffffffULL);
  }

  // 按无符号 64 位数值比较 one 和 two,one > two 时返回 true
  static bool unsigned_long_compare(std::uint64_t one, std::uint64_t two) {
    return one > two;
  }

  // 从 q 的指定位置减去无符号 64 位数 (dh, dl) 与单 limb x 的乘积
  // 原地更新 q 并返回最高借位,用于双 limb 除数的商估计校正
  std::int32_t mulsub_long(jarray<std::uint32_t>& q, std::uint32_t dh, std::uint32_t dl, std::uint32_t x,
                           std::int32_t offset) {
    const std::uint64_t x_long = x & 0xffffffffULL;
    offset += 2;

    std::uint64_t product = (dl & 0xffffffffULL) * x_long;
    std::int64_t difference =
        static_cast<std::int64_t>(static_cast<std::int32_t>(q[offset])) - static_cast<std::int64_t>(product);
    q[offset--] = static_cast<std::uint32_t>(difference);
    std::uint64_t carry = (product >> 32) + (((static_cast<std::uint64_t>(difference) & 0xffffffffULL) >
                                              ((~static_cast<std::uint32_t>(product)) & 0xffffffffULL))
                                                 ? 1
                                                 : 0);

    product = (dh & 0xffffffffULL) * x_long + carry;
    difference = static_cast<std::int64_t>(static_cast<std::int32_t>(q[offset])) - static_cast<std::int64_t>(product);
    q[offset--] = static_cast<std::uint32_t>(difference);
    carry = (product >> 32) + (((static_cast<std::uint64_t>(difference) & 0xffffffffULL) >
                                ((~static_cast<std::uint32_t>(product)) & 0xffffffffULL))
                                   ? 1
                                   : 0);
    return static_cast<std::int32_t>(carry);
  }

  // 从 q 的指定位置减去 a[0, len) 与单 limb x 的乘积
  // 原地更新 q 并返回最高借位,用于 Knuth Algorithm D
  std::int32_t mulsub(jarray<std::uint32_t>& q, const jarray<std::uint32_t>& a, std::uint32_t x, std::int32_t len,
                      std::int32_t offset) {
    const std::uint64_t x_long = x & 0xffffffffULL;
    std::uint64_t carry = 0;
    offset += len;

    for (std::int32_t j = len - 1; j >= 0; --j) {
      const std::uint64_t product = (a[j] & 0xffffffffULL) * x_long + carry;
      const std::int64_t difference =
          static_cast<std::int64_t>(static_cast<std::int32_t>(q[offset])) - static_cast<std::int64_t>(product);
      q[offset--] = static_cast<std::uint32_t>(difference);
      carry = (product >> 32) + (((static_cast<std::uint64_t>(difference) & 0xffffffffULL) >
                                  ((~static_cast<std::uint32_t>(product)) & 0xffffffffULL))
                                     ? 1
                                     : 0);
    }

    return static_cast<std::int32_t>(carry);
  }

  // 计算从 q 的指定位置减去 a[0, len) 与单 limb x 的乘积会产生的最高借位
  // 不修改 q,用于不需要保存最终余数的除法路径
  std::int32_t mulsub_borrow(const jarray<std::uint32_t>& q, const jarray<std::uint32_t>& a, std::uint32_t x,
                             std::int32_t len, std::int32_t offset) {
    const std::uint64_t x_long = x & 0xffffffffULL;
    std::uint64_t carry = 0;
    offset += len;

    for (std::int32_t j = len - 1; j >= 0; --j) {
      const std::uint64_t product = (a[j] & 0xffffffffULL) * x_long + carry;
      const std::int64_t difference =
          static_cast<std::int64_t>(static_cast<std::int32_t>(q[offset--])) - static_cast<std::int64_t>(product);
      carry = (product >> 32) + (((static_cast<std::uint64_t>(difference) & 0xffffffffULL) >
                                  ((~static_cast<std::uint32_t>(product)) & 0xffffffffULL))
                                     ? 1
                                     : 0);
    }

    return static_cast<std::int32_t>(carry);
  }

  // 将大端 magnitude a 加回 result 的指定位置并返回最高进位
  // 用于商估计过大时撤销一次 Knuth 乘减
  std::int32_t divadd(const jarray<std::uint32_t>& a, jarray<std::uint32_t>& result, std::int32_t offset) {
    std::uint64_t carry = 0;

    for (std::int32_t j = a.length() - 1; j >= 0; --j) {
      const std::uint64_t sum = (a[j] & 0xffffffffULL) + (result[j + offset] & 0xffffffffULL) + carry;
      result[j + offset] = static_cast<std::uint32_t>(sum);
      carry = sum >> 32;
    }

    return static_cast<std::int32_t>(carry);
  }

  // 将无符号 64 位数 (dh, dl) 加回 result 的指定位置并返回最高进位
  // 用于双 limb 除数的商估计校正
  std::int32_t divadd_long(std::uint32_t dh, std::uint32_t dl, jarray<std::uint32_t>& result, std::int32_t offset) {
    std::uint64_t carry = 0;

    std::uint64_t sum = (dl & 0xffffffffULL) + (result[1 + offset] & 0xffffffffULL);
    result[1 + offset] = static_cast<std::uint32_t>(sum);

    sum = (dh & 0xffffffffULL) + (result[offset] & 0xffffffffULL) + carry;
    result[offset] = static_cast<std::uint32_t>(sum);
    carry = sum >> 32;
    return static_cast<std::int32_t>(carry);
  }

  // 将 src[src_from, src_from + src_len) 左移 shift bit 后写入 dst 的指定位置
  // 调用方必须保证 0 < shift < 32 且源、目标区间有效
  static void copy_and_shift(const jarray<std::uint32_t>& src, std::int32_t src_from, std::int32_t src_len,
                             jarray<std::uint32_t>& dst, std::int32_t dst_from, std::int32_t shift) {
    const std::int32_t n2 = 32 - shift;
    std::uint32_t c = src[src_from];
    for (std::int32_t i = 0; i < src_len - 1; ++i) {
      const std::uint32_t b = c;
      c = src[++src_from];
      dst[dst_from + i] = (b << shift) | (c >> n2);
    }
    dst[dst_from + src_len - 1] = c << shift;
  }

  // 将当前非负 magnitude 除以单 limb 非零 divisor
  // 商覆盖写入 quotient 并规范化,返回小于 divisor 的无符号余数
  std::uint32_t divide_one_word(std::uint32_t divisor, mutable_bigint& quotient) const {
    const std::uint64_t divisor_long = divisor & 0xffffffffULL;

    if (int_len_ == 1) {
      const std::uint64_t dividend_value = value_[offset_] & 0xffffffffULL;
      const std::uint32_t q = static_cast<std::uint32_t>((dividend_value / divisor_long));
      const std::uint32_t r = static_cast<std::uint32_t>((dividend_value - (q & 0xffffffffULL) * divisor_long));
      if (quotient.value_.length() < 1) {
        quotient.value_ = jarray<std::uint32_t>(1);
      }
      quotient.value_[0] = q;
      quotient.int_len_ = q == 0 ? 0 : 1;
      quotient.offset_ = 0;
      return r;
    }

    if (quotient.value_.length() < int_len_) {
      quotient.value_ = jarray<std::uint32_t>(int_len_);
    }
    quotient.offset_ = 0;
    quotient.int_len_ = int_len_;

    const std::int32_t shift = number_of_leading_zeros(divisor);

    std::uint32_t rem = value_[offset_];
    std::uint64_t rem_long = rem & 0xffffffffULL;
    if (rem_long < divisor_long) {
      quotient.value_[0] = 0;
    } else {
      quotient.value_[0] = static_cast<std::uint32_t>((rem_long / divisor_long));
      rem = static_cast<std::uint32_t>((rem_long - (quotient.value_[0] & 0xffffffffULL) * divisor_long));
      rem_long = rem & 0xffffffffULL;
    }

    std::int32_t xlen = int_len_;
    while (--xlen > 0) {
      const std::uint64_t dividend_estimate = (rem_long << 32) | (value_[offset_ + int_len_ - xlen] & 0xffffffffULL);
      std::uint32_t q = 0;
      if ((dividend_estimate >> 63) == 0) {
        q = static_cast<std::uint32_t>((dividend_estimate / divisor_long));
        rem = static_cast<std::uint32_t>((dividend_estimate - (q & 0xffffffffULL) * divisor_long));
      } else {
        const std::uint64_t tmp = div_word(dividend_estimate, divisor);
        q = static_cast<std::uint32_t>((tmp & 0xffffffffULL));
        rem = static_cast<std::uint32_t>((tmp >> 32));
      }
      quotient.value_[int_len_ - xlen] = q;
      rem_long = rem & 0xffffffffULL;
    }

    quotient.normalize();
    return shift > 0 ? static_cast<std::uint32_t>((rem % divisor)) : rem;
  }

  // 使用 Knuth Algorithm D 将当前 magnitude 除以至少两个 limb 的 div
  // 商覆盖写入 quotient,need_remainder 为 true 时返回规范余数,否则返回零工作对象
  mutable_bigint divide_magnitude(const mutable_bigint& div, mutable_bigint& quotient, bool need_remainder) {
    const std::int32_t shift = number_of_leading_zeros(div.value_[div.offset_]);
    const std::int32_t dlen = div.int_len_;
    jarray<std::uint32_t> divisor;
    mutable_bigint rem(jarray<std::uint32_t>{});

    if (shift > 0) {
      divisor = jarray<std::uint32_t>(dlen);
      copy_and_shift(div.value_, div.offset_, dlen, divisor, 0, shift);
      if (number_of_leading_zeros(value_[offset_]) >= shift) {
        jarray<std::uint32_t> remarr(int_len_ + 1);
        copy_and_shift(value_, offset_, int_len_, remarr, 1, shift);
        rem.set_value(std::move(remarr), int_len_);
        rem.offset_ = 1;
      } else {
        jarray<std::uint32_t> remarr(int_len_ + 2);
        std::int32_t r_from = offset_;
        std::uint32_t c = 0;
        const std::int32_t n2 = 32 - shift;
        for (std::int32_t i = 1; i < int_len_ + 1; ++i, ++r_from) {
          const std::uint32_t b = c;
          c = value_[r_from];
          remarr[i] = (b << shift) | (c >> n2);
        }
        remarr[int_len_ + 1] = c << shift;
        rem.set_value(std::move(remarr), int_len_ + 1);
        rem.offset_ = 1;
      }
    } else {
      divisor = div.value_.copy_of_range(div.offset_, div.offset_ + div.int_len_);
      rem.set_value(jarray<std::uint32_t>(int_len_ + 1), int_len_);
      jarray_copy(value_, offset_, rem.value_, 1, int_len_);
      rem.offset_ = 1;
    }

    const std::int32_t nlen = rem.int_len_;
    const std::int32_t limit = nlen - dlen + 1;
    if (quotient.value_.length() < limit) {
      quotient.value_ = jarray<std::uint32_t>(limit);
      quotient.offset_ = 0;
    }
    quotient.int_len_ = limit;
    jarray<std::uint32_t>& q = quotient.value_;

    if (rem.int_len_ == nlen) {
      rem.offset_ = 0;
      rem.value_[0] = 0;
      ++rem.int_len_;
    }

    const std::uint32_t dh = divisor[0];
    const std::uint64_t dh_long = dh & 0xffffffffULL;
    const std::uint32_t dl = divisor[1];

    for (std::int32_t j = 0; j < limit; ++j) {
      std::uint32_t qhat = 0;
      std::uint32_t qrem = 0;
      bool skip_correction = false;
      const std::uint32_t nh = rem.value_[j + rem.offset_];
      const std::int32_t nh2 = static_cast<std::int32_t>((nh + 0x80000000U));
      const std::uint32_t nm = rem.value_[j + 1 + rem.offset_];

      if (nh == dh) {
        qhat = ~0U;
        qrem = nh + nm;
        skip_correction = static_cast<std::int32_t>((qrem + 0x80000000U)) < nh2;
      } else {
        const std::uint64_t n_chunk = (static_cast<std::uint64_t>(nh) << 32) | (nm & 0xffffffffULL);
        if ((n_chunk >> 63) == 0) {
          qhat = static_cast<std::uint32_t>((n_chunk / dh_long));
          qrem = static_cast<std::uint32_t>((n_chunk - (qhat & 0xffffffffULL) * dh_long));
        } else {
          const std::uint64_t tmp = div_word(n_chunk, dh);
          qhat = static_cast<std::uint32_t>((tmp & 0xffffffffULL));
          qrem = static_cast<std::uint32_t>((tmp >> 32));
        }
      }

      if (qhat == 0) {
        continue;
      }

      if (!skip_correction) {
        const std::uint64_t nl = rem.value_[j + 2 + rem.offset_] & 0xffffffffULL;
        std::uint64_t rs = ((qrem & 0xffffffffULL) << 32) | nl;
        std::uint64_t est_product = (dl & 0xffffffffULL) * (qhat & 0xffffffffULL);

        if (unsigned_long_compare(est_product, rs)) {
          --qhat;
          qrem = static_cast<std::uint32_t>(((qrem & 0xffffffffULL) + dh_long));
          if ((qrem & 0xffffffffULL) >= dh_long) {
            est_product -= (dl & 0xffffffffULL);
            rs = ((qrem & 0xffffffffULL) << 32) | nl;
            if (unsigned_long_compare(est_product, rs)) {
              --qhat;
            }
          }
        }
      }

      const bool last = j == limit - 1;
      rem.value_[j + rem.offset_] = 0;
      std::int32_t borrow = 0;
      if (!last || need_remainder) {
        borrow = mulsub(rem.value_, divisor, qhat, dlen, j + rem.offset_);
      } else {
        borrow = mulsub_borrow(rem.value_, divisor, qhat, dlen, j + rem.offset_);
      }

      if (static_cast<std::int32_t>((borrow + 0x80000000U)) > nh2) {
        if (!last || need_remainder) {
          divadd(divisor, rem.value_, j + 1 + rem.offset_);
        }
        --qhat;
      }

      q[j] = qhat;
    }

    if (need_remainder) {
      if (shift > 0) {
        rem.right_shift(shift);
      }
      rem.normalize();
    }
    quotient.normalize();
    if (need_remainder) {
      return std::move(rem);
    }
    return mutable_bigint();
  }

  // 使用 Knuth 长除法计算当前值除以 b 的商和余数
  // 商覆盖写入 quotient 并返回余数,b 为零时抛出 std::runtime_error
  mutable_bigint divide_knuth(const mutable_bigint& b, mutable_bigint& quotient) {
    return divide_knuth(b, quotient, true);
  }

  // 使用 Knuth 长除法计算当前值除以 b 的商
  // need_remainder 为 true 时返回余数,否则允许跳过余数写入,b 为零时抛出异常
  mutable_bigint divide_knuth(const mutable_bigint& b, mutable_bigint& quotient, bool need_remainder) {
    if (b.int_len_ == 0) {
      throw std::runtime_error("divide by zero");
    }

    if (int_len_ == 0) {
      quotient.int_len_ = 0;
      quotient.offset_ = 0;
      return mutable_bigint();
    }

    const std::int32_t cmp = compare(b);
    if (cmp < 0) {
      quotient.int_len_ = 0;
      quotient.offset_ = 0;
      return need_remainder ? mutable_bigint(*this) : mutable_bigint();
    }
    if (cmp == 0) {
      if (quotient.value_.length() < 1) {
        quotient.value_ = jarray<std::uint32_t>(1);
      }
      quotient.value_[0] = 1;
      quotient.int_len_ = 1;
      quotient.offset_ = 0;
      return mutable_bigint();
    }

    quotient.clear();
    if (b.int_len_ == 1) {
      const std::uint32_t r = divide_one_word(b.value_[b.offset_], quotient);
      if (!need_remainder || r == 0) {
        return mutable_bigint();
      }
      return mutable_bigint(r);
    }

    if (int_len_ >= KNUTH_POW2_THRESH_LEN) {
      const std::int32_t trailing_zero_bits = std::min(get_lowest_set_bit(), b.get_lowest_set_bit());
      if (trailing_zero_bits >= KNUTH_POW2_THRESH_ZEROS * 32) {
        mutable_bigint a(*this);
        mutable_bigint b_shifted(b);
        a.right_shift(trailing_zero_bits);
        b_shifted.right_shift(trailing_zero_bits);
        mutable_bigint r = a.divide_knuth(b_shifted, quotient);
        r.left_shift(trailing_zero_bits);
        return r;
      }
    }

    return divide_magnitude(b, quotient, need_remainder);
  }

  // 将当前值设为由 n 个 0xffffffff limb 组成的规范 magnitude
  // 现有容量不足时扩容,否则复用底层数组
  void ones(std::int32_t n) {
    if (n > value_.length()) {
      value_ = jarray<std::uint32_t>(n);
    }
    value_.fill(0xffffffffU);
    offset_ = 0;
    int_len_ = n;
  }

  // 原地丢弃当前值高于最低 n 个 limb 的部分
  // n 不小于 int_len_ 时保持不变
  void keep_lower(std::int32_t n) {
    if (int_len_ >= n) {
      offset_ += int_len_ - n;
      int_len_ = n;
    }
  }

  // 返回当前值最低 n 个 limb 组成的规范 mutable_bigint 副本
  // 当前值为零时返回零,n 大于有效长度时返回当前值的完整副本
  mutable_bigint get_lower_mutable(std::int32_t n) const {
    if (is_zero()) {
      return mutable_bigint();
    }
    if (int_len_ < n) {
      return mutable_bigint(*this);
    }

    std::int32_t len = n;
    while (len > 0 && value_[offset_ + int_len_ - len] == 0) {
      --len;
    }
    if (len == 0) {
      return mutable_bigint();
    }
    return mutable_bigint(value_.copy_of_range(offset_ + int_len_ - len, offset_ + int_len_));
  }

  // 返回当前值最低 n 个 limb 组成的规范非负 bigint
  bigint get_lower(std::int32_t n);

  // 返回 Burnikel-Ziegler 除法从低位开始编号的第 index 个 limb 块
  // 每块最多包含 block_length 个 limb,超出有效范围时返回零
  mutable_bigint get_block(std::int32_t index, std::int32_t num_blocks, std::int32_t block_length) const {
    const std::int32_t block_start = index * block_length;
    if (block_start >= int_len_) {
      return mutable_bigint();
    }

    std::int32_t block_end = 0;
    if (index == num_blocks - 1) {
      block_end = int_len_;
    } else {
      block_end = (index + 1) * block_length;
    }
    if (block_end > int_len_) {
      return mutable_bigint();
    }

    return mutable_bigint(value_.copy_of_range(offset_ + int_len_ - block_end, offset_ + int_len_ - block_start));
  }

  // 使用 Burnikel-Ziegler 递归算法计算 2n limb 被除数除以 n limb 除数 b
  // 商覆盖写入 quotient 并返回余数,规模不满足条件时回退到 Knuth 除法
  mutable_bigint divide2n1n(const mutable_bigint& b, mutable_bigint& quotient) {
    const std::int32_t n = b.int_len_;

    if (n % 2 != 0 || n < BURNIKEL_ZIEGLER_THRESHOLD) {
      return divide_knuth(b, quotient);
    }

    mutable_bigint a_upper(*this);
    a_upper.safe_right_shift(32 * (n / 2));
    keep_lower(n / 2);

    mutable_bigint q1;
    mutable_bigint r1 = a_upper.divide3n2n(b, q1);

    add_disjoint(r1, n / 2);
    mutable_bigint r2 = divide3n2n(b, quotient);

    quotient.add_disjoint(q1, n / 2);
    return r2;
  }

  // 使用 Burnikel-Ziegler 递归算法计算 3n limb 被除数除以 2n limb 除数 b
  // 商覆盖写入 quotient 并返回余数
  mutable_bigint divide3n2n(const mutable_bigint& b, mutable_bigint& quotient) {
    const std::int32_t n = b.int_len_ / 2;

    mutable_bigint a12(*this);
    a12.safe_right_shift(32 * n);

    mutable_bigint b1(b);
    b1.safe_right_shift(32 * n);
    mutable_bigint b2 = b.get_lower_mutable(n);

    mutable_bigint r;
    mutable_bigint d;
    if (compare_shifted(b, n) < 0) {
      r = a12.divide2n1n(b1, quotient);
      quotient.multiply(b2, d);
    } else {
      quotient.ones(n);
      a12.add(b1);
      b1.left_shift(32 * n);
      a12.subtract(b1);
      r = a12;

      d = b2;
      d.left_shift(32 * n);
      d.subtract(b2);
    }

    r.left_shift(32 * n);
    r.add_lower(*this, n);

    while (r.compare(d) < 0) {
      r.add(b);
      quotient.subtract(ONE);
    }
    r.subtract(d);

    return r;
  }

  // 使用 Burnikel-Ziegler 分块除法计算当前值除以 b 的商和余数
  // 商覆盖写入 quotient 并返回余数,当前对象和 b 保持不变
  mutable_bigint divide_and_remainder_burnikel_ziegler(const mutable_bigint& b, mutable_bigint& quotient) {
    const std::int32_t r = int_len_;
    const std::int32_t s = b.int_len_;

    quotient.offset_ = 0;
    quotient.int_len_ = 0;

    if (r < s) {
      return *this;
    }

    const std::int32_t m = 1 << (32 - number_of_leading_zeros(s / BURNIKEL_ZIEGLER_THRESHOLD));

    const std::int32_t j = (s + m - 1) / m;
    const std::int32_t n = j * m;
    const std::uint64_t n32 = 32ULL * n;
    const std::int32_t sigma = static_cast<std::int32_t>(std::max<std::uint64_t>(0, n32 - b.bit_length()));

    mutable_bigint b_shifted(b);
    b_shifted.safe_left_shift(sigma);
    mutable_bigint a_shifted(*this);
    a_shifted.safe_left_shift(sigma);

    std::int32_t t = static_cast<std::int32_t>(((a_shifted.bit_length() + n32) / n32));
    if (t < 2) {
      t = 2;
    }

    mutable_bigint a1 = a_shifted.get_block(t - 1, t, n);
    mutable_bigint z = a_shifted.get_block(t - 2, t, n);
    z.add_disjoint(a1, n);

    mutable_bigint qi;
    mutable_bigint ri;
    for (std::int32_t i = t - 2; i > 0; --i) {
      ri = z.divide2n1n(b_shifted, qi);

      z = a_shifted.get_block(i - 1, t, n);
      z.add_disjoint(ri, n);
      quotient.add_shifted(qi, i * n);
    }

    ri = z.divide2n1n(b_shifted, qi);
    quotient.add(qi);

    ri.right_shift(sigma);
    return ri;
  }

  // 根据操作数规模计算当前值除以 b 的商和余数
  // 商覆盖写入 quotient 并返回余数
  mutable_bigint divide(const mutable_bigint& b, mutable_bigint& quotient) {
    return divide(b, quotient, true);
  }

  // 根据操作数规模在 Knuth 与 Burnikel-Ziegler 除法之间选择
  // 商覆盖写入 quotient,need_remainder 控制 Knuth 路径是否构造余数
  mutable_bigint divide(const mutable_bigint& b, mutable_bigint& quotient, bool need_remainder) {
    if (b.int_len_ < BURNIKEL_ZIEGLER_THRESHOLD || int_len_ - b.int_len_ < BURNIKEL_ZIEGLER_OFFSET) {
      return divide_knuth(b, quotient, need_remainder);
    }
    return divide_and_remainder_burnikel_ziegler(b, quotient);
  }

  // 将当前值除以非零无符号 64 位 v
  // 商覆盖写入 quotient,返回小于 v 的无符号余数,v == 0 时抛出 std::runtime_error
  std::uint64_t divide(std::uint64_t v, mutable_bigint& quotient) {
    if (v == 0) {
      throw std::runtime_error("divide by zero");
    }

    if (int_len_ == 0) {
      quotient.int_len_ = 0;
      quotient.offset_ = 0;
      return 0;
    }

    const std::uint32_t d = static_cast<std::uint32_t>((v >> 32));
    quotient.clear();
    if (d == 0) {
      return divide_one_word(static_cast<std::uint32_t>(v), quotient) & 0xffffffffULL;
    }
    return divide_long_magnitude(v, quotient).to_long();
  }

  // 将当前值除以非零无符号 64 位 v,复用 quotient 和 remainder 工作区
  // 返回余数的 std::uint64_t 值,v == 0 时抛出 std::runtime_error
  std::uint64_t divide(std::uint64_t v, mutable_bigint& quotient, mutable_bigint& remainder) {
    if (v == 0) {
      throw std::runtime_error("divide by zero");
    }

    if (int_len_ == 0) {
      quotient.int_len_ = 0;
      quotient.offset_ = 0;
      remainder.reset();
      return 0;
    }

    const std::uint32_t high = static_cast<std::uint32_t>((v >> 32));
    quotient.clear();
    if (high == 0) {
      remainder.reset();
      return divide_one_word(static_cast<std::uint32_t>(v), quotient) & 0xffffffffULL;
    }
    divide_long_magnitude(v, quotient, remainder);
    return remainder.to_long();
  }

  // 使用双 limb 长除法将当前值除以非零 ldivisor
  // 商覆盖写入 quotient,并以 mutable_bigint 返回余数
  mutable_bigint divide_long_magnitude(std::uint64_t ldivisor, mutable_bigint& quotient) {
    mutable_bigint rem;
    divide_long_magnitude(ldivisor, quotient, rem);
    return rem;
  }

  // 使用双 limb 长除法将当前值除以非零 ldivisor
  // 商和余数分别覆盖写入 quotient 与 rem,两个结果对象的容量可被复用
  void divide_long_magnitude(std::uint64_t ldivisor, mutable_bigint& quotient, mutable_bigint& rem) {
    if (rem.value_.length() < int_len_ + 1) {
      rem.value_.alloc(int_len_ + 1);
    }
    jarray_copy(value_, offset_, rem.value_, 1, int_len_);
    rem.int_len_ = int_len_;
    rem.offset_ = 1;

    const std::int32_t nlen = rem.int_len_;
    const std::int32_t limit = nlen - 2 + 1;
    if (quotient.value_.length() < limit) {
      quotient.value_ = jarray<std::uint32_t>(limit);
      quotient.offset_ = 0;
    }
    quotient.int_len_ = limit;
    jarray<std::uint32_t>& q = quotient.value_;

    const std::int32_t shift = decimal_detail::count_leading_zeros(ldivisor);
    if (shift > 0) {
      ldivisor <<= shift;
      rem.left_shift(shift);
    }

    if (rem.int_len_ == nlen) {
      rem.offset_ = 0;
      rem.value_[0] = 0;
      ++rem.int_len_;
    }

    const std::uint32_t dh = static_cast<std::uint32_t>((ldivisor >> 32));
    const std::uint64_t dh_long = dh & 0xffffffffULL;
    const std::uint32_t dl = static_cast<std::uint32_t>((ldivisor & 0xffffffffULL));

    for (std::int32_t j = 0; j < limit; ++j) {
      std::uint32_t qhat = 0;
      std::uint32_t qrem = 0;
      bool skip_correction = false;
      const std::uint32_t nh = rem.value_[j + rem.offset_];
      const std::int32_t nh2 = static_cast<std::int32_t>((nh + 0x80000000U));
      const std::uint32_t nm = rem.value_[j + 1 + rem.offset_];

      if (nh == dh) {
        qhat = ~0U;
        qrem = nh + nm;
        skip_correction = static_cast<std::int32_t>((qrem + 0x80000000U)) < nh2;
      } else {
        const std::uint64_t n_chunk = (static_cast<std::uint64_t>(nh) << 32) | (nm & 0xffffffffULL);
        if ((n_chunk >> 63) == 0) {
          qhat = static_cast<std::uint32_t>((n_chunk / dh_long));
          qrem = static_cast<std::uint32_t>((n_chunk - (qhat & 0xffffffffULL) * dh_long));
        } else {
          const std::uint64_t tmp = div_word(n_chunk, dh);
          qhat = static_cast<std::uint32_t>((tmp & 0xffffffffULL));
          qrem = static_cast<std::uint32_t>((tmp >> 32));
        }
      }

      if (qhat == 0) {
        continue;
      }

      if (!skip_correction) {
        const std::uint64_t nl = rem.value_[j + 2 + rem.offset_] & 0xffffffffULL;
        std::uint64_t rs = ((qrem & 0xffffffffULL) << 32) | nl;
        std::uint64_t est_product = (dl & 0xffffffffULL) * (qhat & 0xffffffffULL);

        if (unsigned_long_compare(est_product, rs)) {
          --qhat;
          qrem = static_cast<std::uint32_t>(((qrem & 0xffffffffULL) + dh_long));
          if ((qrem & 0xffffffffULL) >= dh_long) {
            est_product -= (dl & 0xffffffffULL);
            rs = ((qrem & 0xffffffffULL) << 32) | nl;
            if (unsigned_long_compare(est_product, rs)) {
              --qhat;
            }
          }
        }
      }

      rem.value_[j + rem.offset_] = 0;
      const std::int32_t borrow = mulsub_long(rem.value_, dh, dl, qhat, j + rem.offset_);

      if (static_cast<std::int32_t>((borrow + 0x80000000U)) > nh2) {
        divadd_long(dh, dl, rem.value_, j + 1 + rem.offset_);
        --qhat;
      }

      q[j] = qhat;
    }

    if (shift > 0) {
      rem.right_shift(shift);
    }

    quotient.normalize();
    rem.normalize();
  }

  // 返回当前非负 magnitude 的整数平方根
  // 结果是满足 s * s <= this 的最大整数 s,等价于 floor(sqrt(this))
  mutable_bigint sqrt() {
    if (is_zero()) {
      return mutable_bigint(0);
    }
    if (int_len_ == 1 && (value_[offset_] & 0xffffffffULL) < 4) {
      return ONE;
    }

    const std::uint64_t bits = bit_length();
    if (bits <= 63) {
      const std::uint64_t v = to_long();
      std::uint64_t xk = 1ULL << ((bits + 1) / 2);
      while (true) {
        const std::uint64_t xk1 = (xk + v / xk) / 2;
        if (xk1 >= xk) {
          jarray<std::uint32_t> result(2);
          result[0] = static_cast<std::uint32_t>((xk >> 32));
          result[1] = static_cast<std::uint32_t>((xk & 0xffffffffULL));
          mutable_bigint out(std::move(result));
          out.normalize();
          return out;
        }
        xk = xk1;
      }
    }

    if ((bits + 1) / 2 > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
      throw std::runtime_error("bit length overflow");
    }
    mutable_bigint xk(1);
    xk.left_shift(static_cast<std::int32_t>(((bits + 1) / 2)));

    mutable_bigint xk1;
    while (true) {
      divide(xk, xk1, false);
      xk1.add(xk);
      xk1.right_shift(1);

      if (xk1.compare(xk) >= 0) {
        return xk;
      }

      xk.copy_value(xk1);
      xk1.reset();
    }
  }

  // 使用二进制 GCD 算法返回两个无符号 32 位数 a 和 b 的最大公约数
  // 任一参数为零时返回另一参数,两者均为零时返回零
  static std::uint32_t binary_gcd(std::uint32_t a, std::uint32_t b) {
    if (b == 0) {
      return a;
    }
    if (a == 0) {
      return b;
    }

    const std::int32_t a_zeros = number_of_trailing_zeros(a);
    const std::int32_t b_zeros = number_of_trailing_zeros(b);
    a >>= a_zeros;
    b >>= b_zeros;

    const std::int32_t t = a_zeros < b_zeros ? a_zeros : b_zeros;

    while (a != b) {
      if (a > b) {
        a -= b;
        a >>= number_of_trailing_zeros(a);
      } else {
        b -= a;
        b >>= number_of_trailing_zeros(b);
      }
    }
    return a << t;
  }

  // 使用二进制 GCD 算法返回 gcd(this, v)
  // 计算在局部副本上进行,当前对象和 v 保持不变
  mutable_bigint binary_gcd(mutable_bigint& v) {
    mutable_bigint u(*this);
    mutable_bigint vv(v);
    mutable_bigint r;

    const std::int32_t s1 = u.get_lowest_set_bit();
    const std::int32_t s2 = vv.get_lowest_set_bit();
    const std::int32_t k = s1 < s2 ? s1 : s2;
    if (k != 0) {
      u.right_shift(k);
      vv.right_shift(k);
    }

    const bool u_odd = k == s1;
    mutable_bigint* t = u_odd ? &vv : &u;
    std::int32_t tsign = u_odd ? -1 : 1;

    std::int32_t lb = 0;
    while ((lb = t->get_lowest_set_bit()) >= 0) {
      t->right_shift(lb);
      assert((tsign > 0 && t == &u) || (tsign < 0 && t == &vv));

      if (u.int_len_ < 2 && vv.int_len_ < 2) {
        const std::uint32_t x = binary_gcd(u.value_[u.offset_], vv.value_[vv.offset_]);
        r.value_[0] = x;
        r.int_len_ = 1;
        r.offset_ = 0;
        if (k > 0) {
          r.left_shift(k);
        }
        return r;
      }

      tsign = u.difference(vv);
      if (tsign == 0) {
        break;
      }
      t = tsign >= 0 ? &u : &vv;
    }

    if (k > 0) {
      u.left_shift(k);
    }
    return u;
  }

  // 返回 gcd(this, b),先用欧几里得除法缩小长度差较大的操作数
  // 两个操作数长度接近后切换到二进制 GCD,当前对象保持不变
  mutable_bigint hybrid_gcd(mutable_bigint b) {
    mutable_bigint a(*this);
    mutable_bigint q(jarray<std::uint32_t>{});

    while (b.int_len_ != 0) {
      const std::int32_t diff = a.int_len_ > b.int_len_ ? a.int_len_ - b.int_len_ : b.int_len_ - a.int_len_;
      if (diff < 2) {
        return a.binary_gcd(b);
      }

      mutable_bigint r = a.divide(b, q);
      a = std::move(b);
      b = std::move(r);
    }
    return a;
  }

  // 返回奇数 val 在模 2^32 下的乘法逆元
  // 使用 Newton 迭代逐步加倍正确 bit 数,调用方必须保证 val 为奇数
  static std::uint32_t inverse_mod32(std::uint32_t val) {
    std::uint32_t t = val;
    t *= 2 - val * t;
    t *= 2 - val * t;
    t *= 2 - val * t;
    t *= 2 - val * t;
    return t;
  }

  // 返回奇数 val 在模 2^64 下的乘法逆元
  // 使用 Newton 迭代逐步加倍正确 bit 数,调用方必须保证 val 为奇数
  static std::uint64_t inverse_mod64(std::uint64_t val) {
    std::uint64_t t = val;
    t *= 2 - val * t;
    t *= 2 - val * t;
    t *= 2 - val * t;
    t *= 2 - val * t;
    t *= 2 - val * t;
    return t;
  }

  // 使用 Fixup 算法返回 c * 2^(-k) mod p
  // 调用方必须保证 c < p、p 为奇数且 k 非负,输入按值传递并可作为工作区修改
  static mutable_bigint fixup(mutable_bigint c, mutable_bigint p, std::int32_t k) {
    mutable_bigint temp;
    const std::uint32_t r = static_cast<std::uint32_t>((0U - inverse_mod32(p.value_[p.offset_ + p.int_len_ - 1])));

    for (std::int32_t i = 0, num_words = k >> 5; i < num_words; ++i) {
      const std::uint32_t v = r * c.value_[c.offset_ + c.int_len_ - 1];
      p.mul(v, temp);
      c.add(temp);
      --c.int_len_;
    }

    const std::int32_t num_bits = k & 0x1f;
    if (num_bits != 0) {
      std::uint32_t v = r * c.value_[c.offset_ + c.int_len_ - 1];
      v &= (1U << num_bits) - 1;
      p.mul(v, temp);
      c.add(temp);
      c.right_shift(num_bits);
    }

    if (c.compare(p) >= 0) {
      mutable_bigint q;
      c = c.divide(p, q);
    }

    return c;
  }

  // 返回 2^k 在奇数模 mod 下的乘法逆元
  // 等价于 fixup(1, mod, k)
  static mutable_bigint mod_inverse_bp2(const mutable_bigint& mod, std::int32_t k) {
    return fixup(mutable_bigint(1), mutable_bigint(mod), k);
  }

  // 使用扩展欧几里得算法返回当前值在模 2^k 下的乘法逆元
  // 逆元不存在时抛出 std::runtime_error
  mutable_bigint euclid_mod_inverse(std::int32_t k) {
    mutable_bigint b(1);
    b.left_shift(k);
    mutable_bigint mod(b);

    mutable_bigint a(*this);
    mutable_bigint q;
    mutable_bigint r = b.divide(a, q);

    std::swap(b, r);

    mutable_bigint t1(q);
    mutable_bigint t0(1);
    mutable_bigint temp;

    while (!b.is_one()) {
      r = a.divide(b, q);
      if (r.int_len_ == 0) {
        throw std::runtime_error("not invertible");
      }

      a = std::move(r);

      if (q.int_len_ == 1) {
        t1.mul(q.value_[q.offset_], temp);
      } else {
        q.multiply(t1, temp);
      }
      std::swap(q, temp);
      t0.add(q);

      if (a.is_one()) {
        return t0;
      }

      r = b.divide(a, q);
      if (r.int_len_ == 0) {
        throw std::runtime_error("not invertible");
      }

      b = std::move(r);

      if (q.int_len_ == 1) {
        t0.mul(q.value_[q.offset_], temp);
      } else {
        q.multiply(t0, temp);
      }
      std::swap(q, temp);

      t1.add(q);
    }
    mod.subtract(t1);
    return mod;
  }

  // 返回当前奇数在模 2^k 下的乘法逆元
  // 当前值为偶数时抛出 std::runtime_error,k > 64 时使用扩展欧几里得算法
  mutable_bigint mod_inverse_mp2(std::int32_t k) {
    if (is_even()) {
      throw std::runtime_error("non-invertible (gcd != 1)");
    }

    if (k > 64) {
      return euclid_mod_inverse(k);
    }

    std::uint32_t t = inverse_mod32(value_[offset_ + int_len_ - 1]);

    if (k < 33) {
      t = k == 32 ? t : t & ((1U << k) - 1);
      return mutable_bigint(t);
    }

    std::uint64_t p_long = value_[offset_ + int_len_ - 1] & 0xffffffffULL;
    if (int_len_ > 1) {
      p_long |= static_cast<std::uint64_t>(value_[offset_ + int_len_ - 2]) << 32;
    }
    std::uint64_t t_long = t & 0xffffffffULL;
    t_long = t_long * (2 - p_long * t_long);
    t_long = k == 64 ? t_long : t_long & ((1ULL << k) - 1);

    jarray<std::uint32_t> result(2);
    result[0] = static_cast<std::uint32_t>((t_long >> 32));
    result[1] = static_cast<std::uint32_t>(t_long);
    mutable_bigint out(std::move(result));
    out.int_len_ = 2;
    out.normalize();
    return out;
  }

  // 返回当前值在正模 p 下的乘法逆元
  // p 可以为奇数或偶数,当前值与 p 不互素时抛出 std::runtime_error
  mutable_bigint mutable_mod_inverse(const mutable_bigint& p);

  // 使用 Schroeppel 几乎逆算法返回当前值在奇数模 mod 下的乘法逆元
  // 当前值与 mod 不互素时抛出 std::runtime_error
  mutable_bigint mod_inverse(const mutable_bigint& mod);

  // 返回有效区间内从最高位开始编号的第 index 个 limb
  // 调用方负责保证 index 位于 [0, int_len_)
  std::uint32_t get_int(std::int32_t index) const {
    return value_[offset_ + index];
  }

  // 返回有效区间内从最高位开始编号的第 index 个 limb 的无符号 64 位值
  // 调用方负责保证 index 位于 [0, int_len_)
  std::uint64_t get_long(std::int32_t index) const {
    return value_[offset_ + index] & 0xffffffffULL;
  }

  // 确保底层数组至少包含 len 个 limb
  // 发生扩容时分配零数组,并把完整数组设为当前有效区间
  void ensure_capacity(std::int32_t len) {
    if (value_.length() < len) {
      value_.alloc(len);
      offset_ = 0;
      int_len_ = len;
    }
  }

  // 将有效区间压缩为 offset_ == 0 且 value_.length() == int_len_ 的独立数组
  // 必要时替换底层存储,随后返回规范 magnitude 的只读引用
  const jarray<std::uint32_t>& get_magnitude_array() {
    if (offset_ > 0 || value_.length() != int_len_) {
      // 缩减 value 使其恰好等于有效数值
      jarray<std::uint32_t> tmp = value_.copy_of_range(offset_, offset_ + int_len_);
      value_.fill(0);
      offset_ = 0;
      int_len_ = tmp.length();
      value_ = std::move(tmp);
    }
    return value_;
  }

  // 返回当前 magnitude 最低 1 bit 的下标,即其右侧连续零 bit 的数量
  // 最低有效 bit 的下标为 0,当前值为零时返回 -1
  std::int32_t get_lowest_set_bit() const {
    if (int_len_ == 0) {
      return -1;
    }

    std::int32_t j = int_len_ - 1;
    while (j > 0 && value_[j + offset_] == 0) {
      --j;
    }

    const std::uint32_t b = value_[j + offset_];
    if (b == 0) {
      return -1;
    }

    return ((int_len_ - 1 - j) << 5) + number_of_trailing_zeros(b);
  }

  // 将不超过两个 limb 的当前 magnitude 精确转换为 std::uint64_t
  // int_len_ > 2 时通过 assert 拒绝
  std::uint64_t to_long() const {
    assert(int_len_ <= 2);
    if (int_len_ == 0) {
      return 0;
    }
    const std::uint64_t d = value_[offset_] & 0xffffffffULL;
    return int_len_ == 2 ? (d << 32) | (value_[offset_ + 1] & 0xffffffffULL) : d;
  }

  // 将当前非零 magnitude 原地右移 n bit
  // 调用方必须保证 0 < n < 32,函数不调整 int_len_ 或移除前导零
  void primitive_right_shift(std::int32_t n) {
    assert(int_len_ > 0);
    assert(n > 0 && n < 32);

    const std::int32_t n2 = 32 - n;
    std::uint32_t c = value_[offset_ + int_len_ - 1];
    for (std::int32_t i = offset_ + int_len_ - 1; i > offset_; --i) {
      const std::uint32_t b = c;
      c = value_[i - 1];
      value_[i] = (c << n2) | (b >> n);
    }
    value_[offset_] >>= n;
  }

  // 将当前非零 magnitude 原地左移 n bit
  // 调用方必须保证 0 < n < 32 且无需增加最高 limb,函数不调整 int_len_
  void primitive_left_shift(std::int32_t n) {
    assert(int_len_ > 0);
    assert(n > 0 && n < 32);

    const std::int32_t n2 = 32 - n;
    std::uint32_t c = value_[offset_];
    const std::int32_t end = offset_ + int_len_ - 1;
    for (std::int32_t i = offset_; i < end; ++i) {
      const std::uint32_t b = c;
      c = value_[i + 1];
      value_[i] = (b << n) | (c >> n2);
    }
    value_[offset_ + int_len_ - 1] <<= n;
  }

  // 比较 this 与 b,并将 abs(this - b) 原地写回 magnitude 较大的那个对象
  // 返回修改前 this 与 b 的比较结果,相等时返回 0 且不修改任一对象
  std::int32_t difference(mutable_bigint& b) {
    mutable_bigint* a = this;
    mutable_bigint* subtrahend = &b;
    const std::int32_t sign = compare(b);

    if (sign == 0) {
      return 0;
    }
    if (sign < 0) {
      std::swap(a, subtrahend);
    }

    std::int32_t x = a->int_len_;
    std::int32_t y = subtrahend->int_len_;
    std::int32_t borrow = 0;

    while (y > 0) {
      --x;
      --y;
      const std::int64_t diff =
          static_cast<std::int64_t>((a->value_[a->offset_ + x] & 0xffffffffULL)) -
          static_cast<std::int64_t>((subtrahend->value_[subtrahend->offset_ + y] & 0xffffffffULL)) - borrow;
      a->value_[a->offset_ + x] = static_cast<std::uint32_t>(diff);
      borrow = diff < 0 ? 1 : 0;
    }

    while (x > 0) {
      --x;
      const std::int64_t diff = static_cast<std::int64_t>((a->value_[a->offset_ + x] & 0xffffffffULL)) - borrow;
      a->value_[a->offset_ + x] = static_cast<std::uint32_t>(diff);
      borrow = diff < 0 ? 1 : 0;
    }

    a->normalize();
    return sign;
  }

  // 比较两个规范非负 magnitude
  // 当前值小于、等于或大于 b 时分别返回 -1、0 或 1
  std::int32_t compare(const mutable_bigint& b) const {
    if (int_len_ < b.int_len_) {
      return -1;
    }
    if (int_len_ > b.int_len_) {
      return 1;
    }

    for (std::int32_t i = offset_, j = b.offset_; i < offset_ + int_len_; ++i, ++j) {
      if (value_[i] < b.value_[j]) {
        return -1;
      }
      if (value_[i] > b.value_[j]) {
        return 1;
      }
    }
    return 0;
  }

  // 比较当前值与 b * 2^(32 * ints),不构造移位后的临时对象
  // 当前值较小、相等或较大时分别返回 -1、0 或 1
  std::int32_t compare_shifted(const mutable_bigint& b, std::int32_t ints) const {
    const std::int32_t blen = b.int_len_;
    const std::int32_t alen = int_len_ - ints;
    if (alen < blen) {
      return -1;
    }
    if (alen > blen) {
      return 1;
    }

    for (std::int32_t i = offset_, j = b.offset_; i < offset_ + alen; ++i, ++j) {
      if (value_[i] < b.value_[j]) {
        return -1;
      }
      if (value_[i] > b.value_[j]) {
        return 1;
      }
    }
    return 0;
  }

  // 比较当前值与精确值 b / 2,不修改 b
  // 两个操作数必须规范化,返回 -1、0 或 1 表示比较结果
  std::int32_t compare_half(const mutable_bigint& b) const {
    const std::int32_t blen = b.int_len_;
    const std::int32_t len = int_len_;
    if (len <= 0) {
      return blen <= 0 ? 0 : -1;
    }
    if (len > blen) {
      return 1;
    }
    if (len < blen - 1) {
      return -1;
    }

    std::int32_t bstart = b.offset_;
    std::uint32_t carry = 0;
    if (len != blen) {
      if (b.value_[bstart] == 1) {
        ++bstart;
        carry = 0x80000000U;
      } else {
        return -1;
      }
    }

    for (std::int32_t i = offset_, j = bstart; i < offset_ + len;) {
      const std::uint32_t bv = b.value_[j++];
      const std::uint64_t hb = static_cast<std::uint64_t>((bv >> 1)) + carry;
      const std::uint64_t v = value_[i++];
      if (v != hb) {
        return v < hb ? -1 : 1;
      }
      carry = (bv & 1U) << 31;
    }

    return carry == 0 ? 0 : -1;
  }

  // 返回当前非负 magnitude 的有效 bit 长度,零值返回 0
  std::uint64_t bit_length() const {
    if (int_len_ == 0) {
      return 0;
    }
    return static_cast<std::uint64_t>((int_len_)) * 32 - number_of_leading_zeros(value_[offset_]);
  }

  // 将当前非负 magnitude 原地右移 n bit
  // 调用方必须保证 0 <= n < bit_length(),函数通过调整有效区间避免不必要的分配
  void right_shift(std::int32_t n) {
    if (int_len_ == 0) {
      return;
    }

    const std::int32_t n_ints = n >> 5;
    const std::int32_t n_bits = n & 0x1f;
    int_len_ -= n_ints;
    if (n_bits == 0) {
      return;
    }

    const std::int32_t bits_in_high_limb = bit_length_for_limb(value_[offset_]);
    if (n_bits >= bits_in_high_limb) {
      primitive_left_shift(32 - n_bits);
      --int_len_;
    } else {
      primitive_right_shift(n_bits);
    }
  }

  // 将当前非负 magnitude 原地左移非负的 n bit
  // 优先在当前 offset_ 前后或现有容量内移动有效区间,容量不足时才重新分配
  void left_shift(std::int32_t n) {
    if (int_len_ == 0) {
      return;
    }

    const std::int32_t n_ints = n >> 5;
    const std::int32_t n_bits = n & 0x1f;
    const std::int32_t bits_in_high_limb = bit_length_for_limb(value_[offset_]);

    if (n <= 32 - bits_in_high_limb) {
      if (n_bits != 0) {
        primitive_left_shift(n_bits);
      }
      return;
    }

    std::int32_t new_len = int_len_ + n_ints + 1;
    if (n_bits <= 32 - bits_in_high_limb) {
      --new_len;
    }

    if (value_.length() < new_len) {
      jarray<std::uint32_t> result(new_len);
      for (std::int32_t i = 0; i < int_len_; ++i) {
        result[i] = value_[offset_ + i];
      }
      set_value(std::move(result), new_len);
    } else if (value_.length() - offset_ >= new_len) {
      value_.fill(offset_ + int_len_, offset_ + new_len, 0);
    } else {
      for (std::int32_t i = 0; i < int_len_; ++i) {
        value_[i] = value_[offset_ + i];
      }
      value_.fill(int_len_, new_len, 0);
      offset_ = 0;
    }

    int_len_ = new_len;
    if (n_bits == 0) {
      return;
    }
    if (n_bits <= 32 - bits_in_high_limb) {
      primitive_left_shift(n_bits);
    } else {
      primitive_right_shift(32 - n_bits);
    }
  }

  // 安全地将当前值原地右移非负的 n bit
  // n 覆盖全部有效 limb 时重置为零,否则调用 right_shift()
  void safe_right_shift(std::int32_t n) {
    if (n / 32 >= int_len_) {
      reset();
    } else {
      right_shift(n);
    }
  }

  // 安全地将当前值原地左移 n bit,n <= 0 时保持不变
  void safe_left_shift(std::int32_t n) {
    if (n > 0) {
      left_shift(n);
    }
  }

  // 原地计算 this += addend,addend 保持不变
  // 现有容量足够且不存在别名冲突时复用 value_,否则分配结果数组
  void add(const mutable_bigint& addend) {
    std::int32_t x = int_len_;
    std::int32_t y = addend.int_len_;
    std::int32_t result_len = std::max(int_len_, addend.int_len_);
    const bool result_is_value = value_.length() >= result_len;
    jarray<std::uint32_t> local_result;
    jarray<std::uint32_t>* result = &value_;
    if (!result_is_value) {
      local_result = jarray<std::uint32_t>(result_len);
      result = &local_result;
    }

    std::int32_t rstart = result->length() - 1;
    std::uint64_t sum = 0;
    std::uint64_t carry = 0;

    while (x > 0 && y > 0) {
      --x;
      --y;
      sum = (value_[x + offset_] & 0xffffffffULL) + (addend.value_[y + addend.offset_] & 0xffffffffULL) + carry;
      (*result)[rstart--] = static_cast<std::uint32_t>((sum));
      carry = sum >> 32;
    }

    while (x > 0) {
      --x;
      if (carry == 0 && result_is_value && rstart == x + offset_) {
        return;
      }
      sum = (value_[x + offset_] & 0xffffffffULL) + carry;
      (*result)[rstart--] = static_cast<std::uint32_t>((sum));
      carry = sum >> 32;
    }

    while (y > 0) {
      --y;
      sum = (addend.value_[y + addend.offset_] & 0xffffffffULL) + carry;
      (*result)[rstart--] = static_cast<std::uint32_t>((sum));
      carry = sum >> 32;
    }

    if (carry > 0) {
      ++result_len;
      if (result->length() < result_len) {
        jarray<std::uint32_t> temp(result_len);
        jarray_copy(*result, 0, temp, 1, result->length());
        temp[0] = 1;
        local_result = std::move(temp);
        result = &local_result;
      } else {
        (*result)[rstart--] = 1;
      }
    }

    if (result != &value_) {
      value_ = std::move(local_result);
    }
    int_len_ = result_len;
    offset_ = value_.length() - result_len;
  }

  // 原地计算 this += addend * 2^(32 * n),addend 保持不变
  // n 表示在 addend 低位追加的零 limb 数量
  void add_shifted(const mutable_bigint& addend, std::int32_t n) {
    if (addend.is_zero()) {
      return;
    }

    std::int32_t x = int_len_;
    std::int32_t y = addend.int_len_ + n;
    std::int32_t result_len = std::max(int_len_, y);
    const bool result_is_value = value_.length() >= result_len && &addend != this;
    jarray<std::uint32_t> local_result;
    jarray<std::uint32_t>* result = &value_;
    if (!result_is_value) {
      local_result = jarray<std::uint32_t>(result_len);
      result = &local_result;
    }

    std::int32_t rstart = result->length() - 1;
    std::uint64_t sum = 0;
    std::uint64_t carry = 0;

    while (x > 0 && y > 0) {
      --x;
      --y;
      const std::uint32_t bval = y + addend.offset_ < addend.value_.length() ? addend.value_[y + addend.offset_] : 0;
      sum = (value_[x + offset_] & 0xffffffffULL) + (bval & 0xffffffffULL) + carry;
      (*result)[rstart--] = static_cast<std::uint32_t>((sum));
      carry = sum >> 32;
    }

    while (x > 0) {
      --x;
      if (carry == 0 && result_is_value && rstart == x + offset_) {
        return;
      }
      sum = (value_[x + offset_] & 0xffffffffULL) + carry;
      (*result)[rstart--] = static_cast<std::uint32_t>((sum));
      carry = sum >> 32;
    }

    while (y > 0) {
      --y;
      const std::uint32_t bval = y + addend.offset_ < addend.value_.length() ? addend.value_[y + addend.offset_] : 0;
      sum = (bval & 0xffffffffULL) + carry;
      (*result)[rstart--] = static_cast<std::uint32_t>((sum));
      carry = sum >> 32;
    }

    if (carry > 0) {
      ++result_len;
      if (result->length() < result_len) {
        jarray<std::uint32_t> temp(result_len);
        jarray_copy(*result, 0, temp, 1, result->length());
        temp[0] = 1;
        local_result = std::move(temp);
        result = &local_result;
      } else {
        (*result)[rstart--] = 1;
      }
    }

    if (result != &value_) {
      value_ = std::move(local_result);
    }
    int_len_ = result_len;
    offset_ = value_.length() - result_len;
  }

  // 在两个 magnitude 的有效 limb 区间互不重叠时计算 this += addend * 2^(32 * n)
  // 调用方必须保证 int_len_ <= n,函数直接拼接数组以避免逐 limb 加法
  void add_disjoint(const mutable_bigint& addend, std::int32_t n) {
    assert(int_len_ <= n);
    if (addend.is_zero()) {
      return;
    }

    std::int32_t x = int_len_;
    std::int32_t y = addend.int_len_ + n;
    const std::int32_t result_len = std::max(int_len_, y);
    const bool result_is_value = value_.length() >= result_len && &addend != this;
    jarray<std::uint32_t> local_result;
    jarray<std::uint32_t>* result = &value_;
    if (!result_is_value) {
      local_result = jarray<std::uint32_t>(result_len);
      result = &local_result;
    } else {
      result->fill(offset_ + int_len_, result->length(), 0);
    }

    std::int32_t rstart = result->length() - 1;

    jarray_copy(value_, offset_, *result, rstart + 1 - x, x);
    y -= x;
    rstart -= x;

    const std::int32_t len = std::min(y, addend.value_.length() - addend.offset_);
    jarray_copy(addend.value_, addend.offset_, *result, rstart + 1 - y, len);

    result->fill(rstart + 1 - y + len, rstart + 1, 0);

    if (result != &value_) {
      value_ = std::move(local_result);
    }
    int_len_ = result_len;
    offset_ = value_.length() - result_len;
  }

  // 将 abs(this - b) 覆盖写回当前对象
  // 返回修改前 this 与 b 的比较结果,相等时把当前对象重置为零
  std::int32_t subtract(const mutable_bigint& b) {
    const mutable_bigint* a = this;
    const mutable_bigint* subtrahend = &b;
    const std::int32_t sign = compare(b);

    if (sign == 0) {
      reset();
      return 0;
    }
    if (sign < 0) {
      std::swap(a, subtrahend);
    }

    const std::int32_t result_len = a->int_len_;
    jarray<std::uint32_t> local_result;
    jarray<std::uint32_t>* result = &value_;
    if (value_.length() < result_len) {
      local_result = jarray<std::uint32_t>(result_len);
      result = &local_result;
    }

    std::int32_t x = a->int_len_;
    std::int32_t y = subtrahend->int_len_;
    std::int32_t rstart = result->length() - 1;
    std::int32_t borrow = 0;

    while (y > 0) {
      --x;
      --y;
      const std::int64_t diff =
          static_cast<std::int64_t>((a->value_[x + a->offset_] & 0xffffffffULL)) -
          static_cast<std::int64_t>((subtrahend->value_[y + subtrahend->offset_] & 0xffffffffULL)) - borrow;
      (*result)[rstart--] = static_cast<std::uint32_t>(diff);
      borrow = diff < 0 ? 1 : 0;
    }

    while (x > 0) {
      --x;
      const std::int64_t diff = static_cast<std::int64_t>((a->value_[x + a->offset_] & 0xffffffffULL)) - borrow;
      (*result)[rstart--] = static_cast<std::uint32_t>(diff);
      borrow = diff < 0 ? 1 : 0;
    }

    if (result != &value_) {
      value_ = std::move(local_result);
    }
    int_len_ = result_len;
    offset_ = value_.length() - result_len;
    normalize();
    return sign;
  }

  // 原地加上 addend 最低 n 个 limb 组成的规范 magnitude
  // 通过局部副本裁剪 addend,原对象保持不变
  void add_lower(const mutable_bigint& addend, std::int32_t n) {
    mutable_bigint a(addend);
    if (a.offset_ + a.int_len_ >= n) {
      a.offset_ = a.offset_ + a.int_len_ - n;
      a.int_len_ = n;
    }
    a.normalize();
    add(a);
  }

  // 计算当前 magnitude 与单 limb y 的乘积并覆盖写入 z
  // y 为 0 时清零 z,y 为 1 时直接复制当前值
  void mul(std::uint32_t y, mutable_bigint& z) const {
    if (y == 1) {
      z.copy_value(*this);
      return;
    }

    if (y == 0) {
      z.clear();
      return;
    }

    const std::uint64_t ylong = y & 0xffffffffULL;
    jarray<std::uint32_t> local_result;
    jarray<std::uint32_t>* result = &z.value_;
    if (z.value_.length() < int_len_ + 1) {
      local_result = jarray<std::uint32_t>(int_len_ + 1);
      result = &local_result;
    }
    std::uint64_t carry = 0;
    for (std::int32_t i = int_len_ - 1; i >= 0; --i) {
      const std::uint64_t product = ylong * (value_[i + offset_] & 0xffffffffULL) + carry;
      (*result)[i + 1] = static_cast<std::uint32_t>(product);
      carry = product >> 32;
    }

    if (carry == 0) {
      z.offset_ = 1;
      z.int_len_ = int_len_;
    } else {
      z.offset_ = 0;
      z.int_len_ = int_len_ + 1;
      (*result)[0] = static_cast<std::uint32_t>(carry);
    }
    if (result != &z.value_) {
      z.value_ = std::move(local_result);
    }
  }

  // 使用 O(n^2) 多 limb 乘法计算当前 magnitude 与 y 的乘积
  // 结果覆盖写入 z,两个操作数保持不变
  void multiply(const mutable_bigint& y, mutable_bigint& z) const {
    const std::int32_t x_len = int_len_;
    const std::int32_t y_len = y.int_len_;
    if (x_len == 0 || y_len == 0) {
      z.clear();
      return;
    }
    const std::int32_t new_len = x_len + y_len;

    if (z.value_.length() < new_len) {
      z.value_ = jarray<std::uint32_t>(new_len);
    }
    z.offset_ = 0;
    z.int_len_ = new_len;

    std::uint64_t carry = 0;
    for (std::int32_t j = y_len - 1, k = y_len + x_len - 1; j >= 0; --j, --k) {
      const std::uint64_t product =
          (y.value_[j + y.offset_] & 0xffffffffULL) * (value_[x_len - 1 + offset_] & 0xffffffffULL) + carry;
      z.value_[k] = static_cast<std::uint32_t>(product);
      carry = product >> 32;
    }
    z.value_[x_len - 1] = static_cast<std::uint32_t>(carry);

    for (std::int32_t i = x_len - 2; i >= 0; --i) {
      carry = 0;
      for (std::int32_t j = y_len - 1, k = y_len + i; j >= 0; --j, --k) {
        const std::uint64_t product =
            (y.value_[j + y.offset_] & 0xffffffffULL) * (value_[i + offset_] & 0xffffffffULL) +
            (z.value_[k] & 0xffffffffULL) + carry;
        z.value_[k] = static_cast<std::uint32_t>(product);
        carry = product >> 32;
      }
      z.value_[i] = static_cast<std::uint32_t>(carry);
    }

    z.normalize();
  }
};
// static 常量定义
inline const mutable_bigint mutable_bigint::ONE{1};

// signed_mutable_bigint
// 为扩展欧几里得算法提供有符号加减法的可变多精度整数
//
// magnitude 的存储、不变量和原地更新语义与 mutable_bigint 相同
// sign_ 允许取 -1、0 或 1,其中 0 用于有符号运算产生的临时零结果
// signed_add() 和 signed_subtract() 同时更新 magnitude 与 sign_
// 继承的其他运算仍把当前对象视为无符号绝对值,不会自动处理 sign_
//
// 该类型仅用于模逆元等需要临时有符号中间结果的内部算法
struct signed_mutable_bigint : mutable_bigint {
  // 构造容量为一个 limb 的正零值
  signed_mutable_bigint() : mutable_bigint() {
  }

  // 使用一个无符号 32 位 limb 构造正值
  signed_mutable_bigint(std::uint32_t val) : mutable_bigint(val) {
  }

  // 使用指定 mutable_bigint 的绝对值构造数值,符号为正
  signed_mutable_bigint(const mutable_bigint& val) : mutable_bigint(val) {
  }

  // 显式声明拷贝构造,同时复制符号和有效数值
  signed_mutable_bigint(const signed_mutable_bigint& val) : mutable_bigint(val), sign_(val.sign_) {
  }

  // 显式声明拷贝赋值,同时复制符号和有效数值
  signed_mutable_bigint& operator=(const signed_mutable_bigint& val) {
    if (this == &val) {
      return *this;
    }
    mutable_bigint::operator=(val);
    sign_ = val.sign_;
    return *this;
  }

  // 显式声明移动构造,移动后源对象恢复为正零
  signed_mutable_bigint(signed_mutable_bigint&& val) noexcept : mutable_bigint(std::move(val)), sign_(val.sign_) {
    val.sign_ = 1;
  }

  // 显式声明移动赋值,移动后源对象恢复为正零
  signed_mutable_bigint& operator=(signed_mutable_bigint&& val) noexcept {
    if (this == &val) {
      return *this;
    }
    mutable_bigint::operator=(std::move(val));
    sign_ = val.sign_;
    val.sign_ = 1;
    return *this;
  }

  // 返回当前符号,1 表示正,-1 表示负,0 表示有符号运算产生的临时零结果
  std::int32_t sign() const {
    return sign_;
  }

  // 设置当前符号,仅接受 -1、0 或 1
  // 0 用于测试和内部算法的临时状态,非法值通过 assert 拒绝
  void set_sign(std::int32_t sign) {
    assert(sign >= -1 && sign <= 1);
    sign_ = sign;
  }

  // 原地计算 this += addend
  // 同号时 magnitude 相加,异号时以较大 magnitude 的符号作为结果符号
  void signed_add(const signed_mutable_bigint& addend) {
    if (sign_ == addend.sign_) {
      add(addend);
    } else {
      sign_ = sign_ * subtract(addend);
    }
  }

  // 将 addend 作为非负数原地计算 this += addend
  // 根据当前符号选择 magnitude 加法或减法
  void signed_add(const mutable_bigint& addend) {
    if (sign_ == 1) {
      add(addend);
    } else {
      sign_ = sign_ * subtract(addend);
    }
  }

  // 原地计算 this -= addend
  // 同号时 magnitude 相减,异号时 magnitude 相加并保留当前符号
  void signed_subtract(const signed_mutable_bigint& addend) {
    if (sign_ == addend.sign_) {
      sign_ = sign_ * subtract(addend);
    } else {
      add(addend);
    }
  }

  // 将 addend 作为非负数原地计算 this -= addend
  // 结果为零时将 sign_ 恢复为 1
  void signed_subtract(const mutable_bigint& addend) {
    if (sign_ == 1) {
      sign_ = sign_ * subtract(addend);
    } else {
      add(addend);
    }
    if (is_zero()) {
      sign_ = 1;
    }
  }

  std::int32_t sign_{1};
};

// mutable_bigint 类外定义
// 返回当前值在正模 p 下的乘法逆元,p 为奇数时直接使用几乎逆算法
// p 为偶数时分别求奇数部分和 2 的幂部分的逆元,再通过中国剩余定理合并
// 当前值与 p 不互素时抛出 std::runtime_error
inline mutable_bigint mutable_bigint::mutable_mod_inverse(const mutable_bigint& p) {
  if (p.is_odd()) {
    return mod_inverse(p);
  }

  if (is_even()) {
    throw std::runtime_error("not invertible");
  }

  const std::int32_t powers_of_2 = p.get_lowest_set_bit();

  mutable_bigint odd_mod(p);
  odd_mod.right_shift(powers_of_2);

  if (odd_mod.is_one()) {
    return mod_inverse_mp2(powers_of_2);
  }

  mutable_bigint odd_part = mod_inverse(odd_mod);
  mutable_bigint even_part = mod_inverse_mp2(powers_of_2);

  mutable_bigint y1 = mod_inverse_bp2(odd_mod, powers_of_2);
  mutable_bigint y2 = odd_mod.mod_inverse_mp2(powers_of_2);

  mutable_bigint temp1;
  mutable_bigint temp2;
  mutable_bigint result;

  odd_part.left_shift(powers_of_2);
  odd_part.multiply(y1, result);
  odd_part.clear();

  even_part.multiply(odd_mod, temp1);
  temp1.multiply(y2, temp2);

  result.add(temp2);
  return result.divide(p, temp1);
}

// 使用 Schroeppel 几乎逆算法返回当前值在奇数模 mod 下的乘法逆元
// 当前值与 mod 不互素时抛出 std::runtime_error
inline mutable_bigint mutable_bigint::mod_inverse(const mutable_bigint& mod) {
  mutable_bigint p(mod);
  mutable_bigint f(*this);
  mutable_bigint g(p);
  signed_mutable_bigint c(1);
  signed_mutable_bigint d;

  std::int32_t k = 0;
  if (f.is_even()) {
    const std::int32_t trailing_zeros = f.get_lowest_set_bit();
    f.right_shift(trailing_zeros);
    d.left_shift(trailing_zeros);
    k = trailing_zeros;
  }

  while (!f.is_one()) {
    if (f.is_zero()) {
      throw std::runtime_error("not invertible");
    }

    if (f.compare(g) < 0) {
      std::swap(f, g);
      std::swap(c, d);
    }

    if (((f.value_[f.offset_ + f.int_len_ - 1] ^ g.value_[g.offset_ + g.int_len_ - 1]) & 3U) == 0) {
      f.subtract(g);
      c.signed_subtract(d);
    } else {
      f.add(g);
      c.signed_add(d);
    }

    const std::int32_t trailing_zeros = f.get_lowest_set_bit();
    f.right_shift(trailing_zeros);
    d.left_shift(trailing_zeros);
    k += trailing_zeros;
  }

  if (c.compare(p) >= 0) {
    mutable_bigint q;
    mutable_bigint remainder = c.divide(p, q);
    c.copy_value(remainder);
  }

  if (c.sign() < 0) {
    c.signed_add(p);
  }

  return fixup(std::move(c), std::move(p), k);
}

// bigint
// 任意精度有符号整数,对外按不可变值对象使用
//
// 支持基本算术、最大公约数、模运算、素性测试、素数生成、bit 操作和基数转换
// 除实现规定的最大 magnitude 外,运算不会像固定宽度整数一样静默溢出
// 除数为 0 或结果超过支持范围时抛出异常
//
// 算术运算采用有符号整数语义,除法的商向零截断,非零余数与被除数同号
// 移位和按位运算在概念上使用具有无限符号扩展位的二进制补码表示
// 负移位距离表示向相反方向移位,不存在只适用于固定宽度整数的无符号右移
// 模运算始终返回 [0, modulus) 范围内的非负结果
//
// mag_ 使用大端 std::uint32_t limb 保存绝对值并保持规范形式
// 零由 signum_ == 0 且 mag_.length() == 0 表示
// 非零值的 signum_ 为 -1 或 1,且 mag_[0] 非零
//
// 这个 struct 按项目约定保持全公有,调用者仍不得直接修改表示字段和缓存字段
struct bigint {
  static constexpr std::int32_t MIN_RADIX = 2;
  static constexpr std::int32_t MAX_RADIX = 36;
  static constexpr std::int32_t MAX_CONSTANT = 16;
  static constexpr std::int32_t MAX_MAG_LENGTH = std::numeric_limits<std::int32_t>::max() / 32 + 1;
  static constexpr std::int32_t PRIME_SEARCH_BIT_LENGTH_LIMIT = 500000000;
  static constexpr std::int32_t KARATSUBA_THRESHOLD = 80;
  static constexpr std::int32_t TOOM_COOK_THRESHOLD = 240;
  static constexpr std::int32_t KARATSUBA_SQUARE_THRESHOLD = 128;
  static constexpr std::int32_t TOOM_COOK_SQUARE_THRESHOLD = 216;
  static constexpr std::int32_t SMALL_PRIME_THRESHOLD = 95;
  static constexpr std::int32_t DEFAULT_PRIME_CERTAINTY = 100;
  static constexpr std::int32_t MULTIPLY_SQUARE_THRESHOLD = 20;
  static constexpr std::int32_t MONTGOMERY_INTRINSIC_THRESHOLD = 512;
  static constexpr std::int32_t SCHOENHAGE_BASE_CONVERSION_THRESHOLD = 20;
  static constexpr std::int32_t NUM_ZEROS = 63;
  static constexpr std::uint64_t SMALL_PRIME_PRODUCT_MAGNITUDE =
      3ULL * 5 * 7 * 11 * 13 * 17 * 19 * 23 * 29 * 31 * 37 * 41;
  static constexpr double LOG_TWO = 0.693147180559945309417232121458176568;
  static constexpr std::array<std::int32_t, 7> bn_exp_mod_thresh_table = {
      7, 25, 81, 241, 673, 1793, std::numeric_limits<std::int32_t>::max()};
  static const bigint ZERO;
  static const bigint ONE;
  static const bigint TWO;
  static const bigint TEN;
  static const bigint NEGATIVE_ONE;

  static constexpr std::array<std::uint64_t, 37> bits_per_digit = {
      0,    0,    1024, 1624, 2048, 2378, 2648, 2875, 3072, 3247, 3402, 3543, 3672, 3790, 3899, 4001, 4096, 4186, 4271,
      4350, 4426, 4498, 4567, 4633, 4696, 4756, 4814, 4870, 4923, 4975, 5025, 5074, 5120, 5166, 5210, 5253, 5295};

  static constexpr std::array<std::int32_t, 37> digits_per_int = {0, 0, 30, 19, 15, 13, 11, 11, 10, 9, 9, 8, 8,
                                                                  8, 8, 7,  7,  7,  7,  7,  7,  7,  6, 6, 6, 6,
                                                                  6, 6, 6,  6,  6,  6,  6,  6,  6,  6, 5};

  static constexpr std::array<std::int32_t, 37> digits_per_long = {0,  0,  62, 39, 31, 27, 24, 22, 20, 19, 18, 18, 17,
                                                                   17, 16, 16, 15, 15, 15, 14, 14, 14, 14, 13, 13, 13,
                                                                   13, 13, 13, 12, 12, 12, 12, 12, 12, 12, 12};

  static constexpr std::array<std::uint32_t, 37> int_radix = {
      0,           0,           0x40000000U, 0x4546b3dbU, 0x40000000U, 0x48c27395U, 0x159fd800U, 0x75db9c97U,
      0x40000000U, 0x17179149U, 0x3b9aca00U, 0x0cc6db61U, 0x19a10000U, 0x309f1021U, 0x57f6c100U, 0x0a2f1b6fU,
      0x10000000U, 0x18754571U, 0x247dbc80U, 0x3547667bU, 0x4c4b4000U, 0x6b5a6e1dU, 0x06c20a40U, 0x08d2d931U,
      0x0b640000U, 0x0e8d4a51U, 0x1269ae40U, 0x17179149U, 0x1cb91000U, 0x23744899U, 0x2b73a840U, 0x34e63b41U,
      0x40000000U, 0x4cfa3cc1U, 0x5c13d840U, 0x6d91b519U, 0x039aa400U,
  };

  std::int32_t signum_{0};
  jarray<std::uint32_t> mag_{};
  mutable std::int32_t bit_count_plus_one_{0};
  mutable std::int32_t bit_length_plus_one_{0};
  mutable std::int32_t lowest_set_bit_plus_two_{0};
  mutable std::int32_t first_nonzero_int_num_plus_two_{0};

  // 构造数值为 0 的 bigint
  bigint() = default;

  // 使用 sign-magnitude 表示构造 bigint,并移除 magnitude 的前导零 limb
  // signum 必须为 -1、0 或 1,非零 magnitude 不能与 signum == 0 同时出现
  bigint(std::int32_t signum, jarray<std::uint32_t> magnitude)
      : signum_(signum), mag_(trusted_strip_leading_zero_limbs(std::move(magnitude))) {
    if (signum < -1 || signum > 1) {
      throw std::invalid_argument("invalid signum value");
    }
    if (mag_.length() == 0) {
      signum_ = 0;
    } else if (signum == 0) {
      throw std::invalid_argument("signum-magnitude mismatch");
    }
    if (mag_.length() >= MAX_MAG_LENGTH) {
      check_range();
    }
  }

  // 将大端二进制补码 limb 数组解释为有符号整数
  // val[0] 是最高有效 limb,空数组表示 0
  bigint(const jarray<std::uint32_t>& val) {
    if (val.length() == 0) {
      signum_ = 0;
      return;
    }
    if ((val[0] & 0x80000000U) != 0) {
      mag_ = make_positive_limbs(val);
      signum_ = -1;
    } else {
      mag_ = trusted_strip_leading_zero_limbs(val);
      signum_ = mag_.length() == 0 ? 0 : 1;
    }
    if (mag_.length() >= MAX_MAG_LENGTH) {
      check_range();
    }
  }

  // 接管大端二进制补码 limb 临时数组并解释为有符号整数
  // val[0] 是最高有效 limb,空数组表示 0
  bigint(jarray<std::uint32_t>&& val) {
    if (val.length() == 0) {
      signum_ = 0;
      return;
    }
    if ((val[0] & 0x80000000U) != 0) {
      std::uint64_t carry = 1;
      for (std::int32_t i = val.length() - 1; i >= 0; --i) {
        const std::uint64_t sum = static_cast<std::uint64_t>(~val[i]) + carry;
        val[i] = static_cast<std::uint32_t>(sum);
        carry = sum >> 32;
      }
      mag_ = trusted_strip_leading_zero_limbs(std::move(val));
      signum_ = -1;
    } else {
      mag_ = trusted_strip_leading_zero_limbs(std::move(val));
      signum_ = mag_.length() == 0 ? 0 : 1;
    }
    if (mag_.length() >= MAX_MAG_LENGTH) {
      check_range();
    }
  }

  // 构造与 val 数值相等的 bigint
  // std::int64_t 的全部取值均可精确表示,包括最小负数
  bigint(std::int64_t val) {
    if (val == 0) {
      signum_ = 0;
      return;
    }

    std::uint64_t abs_val = 0;
    if (val < 0) {
      signum_ = -1;
      abs_val = std::uint64_t(0) - static_cast<std::uint64_t>((val));
    } else {
      signum_ = 1;
      abs_val = static_cast<std::uint64_t>((val));
    }

    const std::uint32_t high_word = static_cast<std::uint32_t>((abs_val >> 32));
    if (high_word == 0) {
      mag_ = jarray<std::uint32_t>(1);
      mag_[0] = static_cast<std::uint32_t>((abs_val));
    } else {
      mag_ = jarray<std::uint32_t>(2);
      mag_[0] = high_word;
      mag_[1] = static_cast<std::uint32_t>((abs_val));
    }
  }

  // 将 val[off, off + len) 解释为大端二进制补码整数
  // val[off] 是最高有效 byte,len == 0 时构造 0
  // val 本身为空或指定区间越界时抛出异常
  bigint(const jarray<std::uint8_t>& val, std::int32_t off, std::int32_t len) {
    if (val.length() == 0) {
      throw std::invalid_argument("zero length bigint");
    }
    check_from_index_size(off, len, val.length());
    if (len == 0) {
      signum_ = 0;
      return;
    }

    const std::int32_t b = static_cast<std::int8_t>((val[off]));
    if (b < 0) {
      mag_ = make_positive_bytes(b, val, off, len);
      signum_ = -1;
    } else {
      mag_ = strip_leading_zero_bytes(b, val, off, len);
      signum_ = mag_.length() == 0 ? 0 : 1;
    }
    if (mag_.length() >= MAX_MAG_LENGTH) {
      check_range();
    }
  }

  // 将完整 byte 数组解释为大端二进制补码整数
  // val[0] 是最高有效 byte,空数组无有效整数表示并会抛出 std::invalid_argument
  bigint(const jarray<std::uint8_t>& val) : bigint(val, 0, val.length()) {
  }

  // 将 signum 和 magnitude[off, off + len) 组成的 sign-magnitude 表示转换为 bigint
  // magnitude 使用大端 byte 顺序,零长度或全零 magnitude 始终构造 0
  // signum 非法、signum == 0 与非零 magnitude 不匹配或区间越界时抛出异常
  bigint(std::int32_t signum, const jarray<std::uint8_t>& magnitude, std::int32_t off, std::int32_t len) {
    if (signum < -1 || signum > 1) {
      throw std::invalid_argument("invalid signum value");
    }
    check_from_index_size(off, len, magnitude.length());
    mag_ = strip_leading_zero_bytes(magnitude, off, len);
    if (mag_.length() == 0) {
      signum_ = 0;
    } else {
      if (signum == 0) {
        throw std::invalid_argument("signum-magnitude mismatch");
      }
      signum_ = signum;
    }
    if (mag_.length() >= MAX_MAG_LENGTH) {
      check_range();
    }
  }

  // 将 signum 和完整大端 magnitude byte 数组组成的 sign-magnitude 表示转换为 bigint
  bigint(std::int32_t signum, const jarray<std::uint8_t>& magnitude)
      : bigint(signum, magnitude, 0, magnitude.length()) {
  }

  // 内部 sign-magnitude byte 构造入口,magnitude 使用大端顺序
  // 调用方负责保证 signum 与非零 magnitude 一致
  bigint(const jarray<std::uint8_t>& magnitude, std::int32_t signum) {
    signum_ = magnitude.length() == 0 ? 0 : signum;
    mag_ = strip_leading_zero_bytes(magnitude, 0, magnitude.length());
    if (mag_.length() >= MAX_MAG_LENGTH) {
      check_range();
    }
  }

  // 使用 rnd 生成均匀分布在 [0, 2^num_bits) 的非负随机 bigint
  // num_bits 必须非负,为 0 时结果为 0
  bigint(std::int32_t num_bits, std::mt19937_64& rnd) {
    mag_ = random_magnitude(num_bits, rnd);
    signum_ = mag_.length() == 0 ? 0 : 1;
    if (mag_.length() >= MAX_MAG_LENGTH) {
      check_range();
    }
  }

  // 使用 rnd 随机生成 bit_length 恰好等于指定值的正 probable prime
  // certainty 控制可接受的误判概率和测试时间,bit_length < 2 时抛出 std::runtime_error
  bigint(std::int32_t bit_length, std::int32_t certainty, std::mt19937_64& rnd) {
    if (bit_length < 2) {
      throw std::runtime_error("bit length < 2");
    }
    // 随机生成指定 bitLength 的 probable prime; 主要用于较小位数,位数较大时性能下降;
    // 前提 bitLength > 1
    const bigint prime = bit_length < SMALL_PRIME_THRESHOLD ? small_prime(bit_length, certainty, rnd)
                                                            : large_prime(bit_length, certainty, rnd);
    signum_ = 1;
    mag_ = prime.mag_;
  }

  // 将 val 按 radix 解析为 bigint
  // 格式为可选的单个前导正负号,后跟一个或多个 radix 进制数字,不接受空白或其他字符
  // radix 必须位于 [MIN_RADIX, MAX_RADIX],字符串为空或格式非法时抛出 std::invalid_argument
  bigint(const std::string& val, std::int32_t radix) {
    std::int32_t cursor = 0;
    const std::int32_t len = static_cast<std::int32_t>((val.size()));

    if (radix < MIN_RADIX || radix > MAX_RADIX) {
      throw std::invalid_argument("radix out of range");
    }
    if (len == 0) {
      throw std::invalid_argument("zero length bigint");
    }

    std::int32_t sign = 1;
    const std::size_t index1 = val.rfind('-');
    const std::size_t index2 = val.rfind('+');
    if (index1 != std::string::npos) {
      if (index1 != 0 || index2 != std::string::npos) {
        throw std::invalid_argument("illegal embedded sign character");
      }
      sign = -1;
      cursor = 1;
    } else if (index2 != std::string::npos) {
      if (index2 != 0) {
        throw std::invalid_argument("illegal embedded sign character");
      }
      cursor = 1;
    }
    if (cursor == len) {
      throw std::invalid_argument("zero length bigint");
    }

    while (cursor < len && digit(val[static_cast<size_t>((cursor))], radix) == 0) {
      ++cursor;
    }
    if (cursor == len) {
      signum_ = 0;
      return;
    }

    const std::int32_t num_digits = len - cursor;
    signum_ = sign;
    const std::uint64_t num_bits = ((std::uint64_t(num_digits) * bits_per_digit[radix]) >> 10) + 1;
    if (num_bits + 31 >= (1ULL << 32)) {
      report_overflow();
    }
    const std::int32_t num_words = static_cast<std::int32_t>(((num_bits + 31) >> 5));
    jarray<std::uint32_t> magnitude(num_words);

    std::int32_t first_group_len = num_digits % digits_per_int[radix];
    if (first_group_len == 0) {
      first_group_len = digits_per_int[radix];
    }
    magnitude[num_words - 1] = parse_group(val, cursor, cursor + first_group_len, radix);
    cursor += first_group_len;

    const std::uint32_t super_radix = int_radix[radix];
    while (cursor < len) {
      const std::uint32_t group_val = parse_group(val, cursor, cursor + digits_per_int[radix], radix);
      cursor += digits_per_int[radix];
      destructive_mul_add(magnitude, super_radix, group_val);
    }

    mag_ = trusted_strip_leading_zero_limbs(std::move(magnitude));
    if (mag_.length() >= MAX_MAG_LENGTH) {
      check_range();
    }
  }

  // 将十进制字符串解析为 bigint,接受可选的单个前导正负号
  bigint(const std::string& val) : bigint(val, 10) {
  }

  // 使用调用方预先解析的 sign 和字符长度 len 构造十进制 bigint
  // 仅供 decimal 解析路径使用,调用方负责保证字符均为十进制数字且参数一致
  bigint(const std::string& val, std::int32_t sign, std::int32_t len) {
    std::int32_t cursor = 0;
    while (cursor < len && digit(val[static_cast<size_t>((cursor))], 10) == 0) {
      ++cursor;
    }
    if (cursor == len) {
      signum_ = 0;
      mag_ = ZERO.mag_;
      return;
    }

    const std::int32_t num_digits = len - cursor;
    signum_ = sign;
    std::int32_t num_words = 0;
    if (len < 10) {
      num_words = 1;
    } else {
      const std::uint64_t num_bits = ((std::uint64_t(num_digits) * bits_per_digit[10]) >> 10) + 1;
      if (num_bits + 31 >= (std::uint64_t{1} << 32)) {
        report_overflow();
      }
      num_words = static_cast<std::int32_t>((num_bits + 31)) >> 5;
    }

    jarray<std::uint32_t> magnitude(num_words);
    std::int32_t first_group_len = num_digits % digits_per_int[10];
    if (first_group_len == 0) {
      first_group_len = digits_per_int[10];
    }
    magnitude[num_words - 1] = parse_int_decimal(val, cursor, cursor + first_group_len);
    cursor += first_group_len;

    while (cursor < len) {
      const std::uint32_t group_val = parse_int_decimal(val, cursor, cursor + digits_per_int[10]);
      cursor += digits_per_int[10];
      destructive_mul_add(magnitude, int_radix[10], group_val);
    }
    mag_ = trusted_strip_leading_zero_limbs(std::move(magnitude));
    if (mag_.length() >= MAX_MAG_LENGTH) {
      check_range();
    }
  }

  // 返回与 val 数值相等的 bigint,小整数优先复用预构造常量
  static bigint value_of(std::int64_t val) {
    if (val == 0) {
      return ZERO;
    }
    if (val > 0 && val <= MAX_CONSTANT) {
      return pos_const(static_cast<std::int32_t>((val)));
    }
    if (val < 0 && val >= -MAX_CONSTANT) {
      return neg_const(static_cast<std::int32_t>((-val)));
    }
    return bigint(val);
  }

  // 返回缓存中的正小整数 n,调用方必须保证 n 位于 [1, MAX_CONSTANT]
  static bigint pos_const(std::int32_t n);

  // 返回缓存中的负小整数 -n,调用方必须保证 n 位于 [1, MAX_CONSTANT]
  static bigint neg_const(std::int32_t n);

  // 将非空大端二进制补码 limb 数组解释为有符号整数并复制输入
  static bigint value_of(const jarray<std::uint32_t>& val) {
    if (val.length() == 0) {
      throw std::invalid_argument("zero length bigint");
    }
    return (val[0] & 0x80000000U) == 0 ? bigint(1, val.clone()) : from_twos_complement(val);
  }

  // 将非空大端二进制补码临时数组解释为有符号整数
  // 正数路径接管数组存储,负数路径转换为绝对值 magnitude
  static bigint value_of(jarray<std::uint32_t>&& val) {
    if (val.length() == 0) {
      throw std::invalid_argument("zero length bigint");
    }
    if ((val[0] & 0x80000000U) != 0) {
      return from_twos_complement(std::move(val));
    }
    return bigint(1, trusted_strip_leading_zero_limbs(std::move(val)));
  }

  // 将非空大端二进制补码 limb 数组转换为规范 bigint
  // 负数转换为 sign-magnitude,非负数移除前导零 limb
  static bigint from_twos_complement(const jarray<std::uint32_t>& val) {
    if (val.length() == 0) {
      throw std::invalid_argument("zero length bigint");
    }
    if ((val[0] & 0x80000000U) != 0) {
      return bigint(-1, make_positive_limbs(val));
    }
    return bigint(1, trusted_strip_leading_zero_limbs(val));
  }

  // 接管非空大端二进制补码临时数组并转换为规范 bigint
  static bigint from_twos_complement(jarray<std::uint32_t>&& val) {
    if (val.length() == 0) {
      throw std::invalid_argument("zero length bigint");
    }
    return bigint(std::move(val));
  }

  // 返回当前值的符号,负数返回 -1,零返回 0,正数返回 1
  std::int32_t signum() const {
    return signum_;
  }

  // 返回当前值是否等于 0
  bool is_zero() const {
    return signum_ == 0;
  }

  // 返回当前值是否为偶数,0 视为偶数
  bool is_even() const {
    return signum_ == 0 || ((mag_[mag_.length() - 1] & 1U) == 0);
  }

  // 返回当前值是否为奇数
  bool is_odd() const {
    return signum_ != 0 && ((mag_[mag_.length() - 1] & 1U) != 0);
  }

  // 按有符号数值比较当前值与 val
  // 当前值小于、等于或大于 val 时分别返回 -1、0 或 1
  std::int32_t compare_to(const bigint& val) const {
    if (signum_ == val.signum_) {
      if (signum_ > 0) {
        return compare_magnitude(val);
      }
      if (signum_ < 0) {
        return val.compare_magnitude(*this);
      }
      return 0;
    }
    return signum_ > val.signum_ ? 1 : -1;
  }

  // 忽略符号并比较当前值与 val 的绝对值
  // abs(this) 小于、等于或大于 abs(val) 时分别返回 -1、0 或 1
  std::int32_t compare_magnitude(const bigint& val) const {
    const std::int32_t len1 = mag_.length();
    const std::int32_t len2 = val.mag_.length();
    if (len1 < len2) {
      return -1;
    }
    if (len1 > len2) {
      return 1;
    }
    for (std::int32_t i = 0; i < len1; ++i) {
      if (mag_[i] < val.mag_[i]) {
        return -1;
      }
      if (mag_[i] > val.mag_[i]) {
        return 1;
      }
    }
    return 0;
  }

  // 比较 abs(this) 与 abs(val),较小、相等或较大时分别返回 -1、0 或 1
  // 调用方不得传入 std::numeric_limits<std::int64_t>::min()
  std::int32_t compare_magnitude(std::int64_t val) const {
    assert(val != std::numeric_limits<std::int64_t>::min());
    const std::int32_t len = mag_.length();
    if (len > 2) {
      return 1;
    }
    std::uint64_t abs_val =
        val < 0 ? std::uint64_t(0) - static_cast<std::uint64_t>((val)) : static_cast<std::uint64_t>((val));
    const std::uint32_t high_word = static_cast<std::uint32_t>((abs_val >> 32));
    if (high_word == 0) {
      if (len < 1) {
        return -1;
      }
      if (len > 1) {
        return 1;
      }
      const std::uint32_t low = static_cast<std::uint32_t>((abs_val));
      if (mag_[0] != low) {
        return mag_[0] < low ? -1 : 1;
      }
      return 0;
    }
    if (len < 2) {
      return -1;
    }
    if (mag_[0] != high_word) {
      return mag_[0] < high_word ? -1 : 1;
    }
    const std::uint32_t low = static_cast<std::uint32_t>((abs_val));
    if (mag_[1] != low) {
      return mag_[1] < low ? -1 : 1;
    }
    return 0;
  }

  // 返回当前值与 val 中数值较小的一方,相等时允许返回任意一方
  bigint min(const bigint& val) const {
    return compare_to(val) < 0 ? *this : val;
  }

  // 返回当前值与 val 中数值较大的一方,相等时允许返回任意一方
  bigint max(const bigint& val) const {
    return compare_to(val) > 0 ? *this : val;
  }

  // 返回由 signum_ 和 magnitude 计算的稳定哈希值
  // 算法与 Java BigInteger.hashCode() 一致
  std::int32_t hash_code() const {
    std::int32_t hash = 0;
    for (std::int32_t i = 0; i < mag_.length(); ++i) {
      hash = static_cast<std::int32_t>((31 * hash + mag_[i]));
    }
    return hash * signum_;
  }

  // 返回 abs(this)
  bigint abs() const {
    return signum_ >= 0 ? *this : negate();
  }

  // 返回 -this,0 的相反数仍为 0
  bigint negate() const {
    return bigint(-signum_, mag_.clone());
  }

  // 返回 this + val,不修改任一操作数
  bigint add(const bigint& val) const {
    if (val.signum_ == 0) {
      return *this;
    }
    if (signum_ == 0) {
      return val;
    }
    if (val.signum_ == signum_) {
      return bigint(signum_, add_magnitude(mag_, val.mag_));
    }

    const std::int32_t cmp = compare_magnitude(val);
    if (cmp == 0) {
      return bigint();
    }
    jarray<std::uint32_t> result_mag =
        cmp > 0 ? subtract_magnitude(mag_, val.mag_) : subtract_magnitude(val.mag_, mag_);
    result_mag = trusted_strip_leading_zero_limbs(std::move(result_mag));
    return bigint(cmp == signum_ ? 1 : -1, std::move(result_mag));
  }

  // 返回 this + val,其中 val 为 std::int64_t,不修改当前值
  bigint add(std::int64_t val) const {
    if (val == 0) {
      return *this;
    }
    if (signum_ == 0) {
      return value_of(val);
    }
    const std::int32_t val_sign = (val > 0) - (val < 0);
    const std::uint64_t abs_val =
        val < 0 ? std::uint64_t(0) - static_cast<std::uint64_t>((val)) : static_cast<std::uint64_t>((val));
    if (val_sign == signum_) {
      return bigint(signum_, add_magnitude(mag_, abs_val));
    }
    const std::int32_t cmp = compare_magnitude(val);
    if (cmp == 0) {
      return bigint();
    }
    jarray<std::uint32_t> result_mag = cmp > 0 ? subtract_magnitude(mag_, abs_val) : subtract_magnitude(abs_val, mag_);
    result_mag = trusted_strip_leading_zero_limbs(std::move(result_mag));
    return bigint(cmp == signum_ ? 1 : -1, std::move(result_mag));
  }

  // 返回 this - val,不修改任一操作数
  bigint subtract(const bigint& val) const {
    if (val.signum_ == 0) {
      return *this;
    }
    if (signum_ == 0) {
      return val.negate();
    }
    if (val.signum_ != signum_) {
      return bigint(signum_, add_magnitude(mag_, val.mag_));
    }

    const std::int32_t cmp = compare_magnitude(val);
    if (cmp == 0) {
      return bigint();
    }
    jarray<std::uint32_t> result_mag =
        cmp > 0 ? subtract_magnitude(mag_, val.mag_) : subtract_magnitude(val.mag_, mag_);
    result_mag = trusted_strip_leading_zero_limbs(std::move(result_mag));
    return bigint(cmp == signum_ ? 1 : -1, std::move(result_mag));
  }

  // 返回 this * val,根据操作数规模选择普通、Karatsuba 或 Toom-Cook 乘法
  bigint multiply(const bigint& val) const {
    return multiply(val, false);
  }

  // multiply() 的递归实现入口
  // is_recursion 为 true 时跳过仅需在最外层执行的部分范围检查
  bigint multiply(const bigint& val, bool is_recursion) const {
    if (val.signum_ == 0 || signum_ == 0) {
      return ZERO;
    }

    const std::int32_t xlen = mag_.length();
    if (this == &val && xlen > MULTIPLY_SQUARE_THRESHOLD) {
      return square();
    }

    const std::int32_t ylen = val.mag_.length();
    if ((xlen < KARATSUBA_THRESHOLD) || (ylen < KARATSUBA_THRESHOLD)) {
      const std::int32_t result_sign = signum_ == val.signum_ ? 1 : -1;
      if (val.mag_.length() == 1) {
        return multiply_by_int(mag_, val.mag_[0], result_sign);
      }
      if (mag_.length() == 1) {
        return multiply_by_int(val.mag_, mag_[0], result_sign);
      }
      jarray<std::uint32_t> result = multiply_to_len(mag_, xlen, val.mag_, ylen, nullptr);
      result = trusted_strip_leading_zero_limbs(std::move(result));
      return bigint(result_sign, std::move(result));
    }

    if ((xlen < TOOM_COOK_THRESHOLD) && (ylen < TOOM_COOK_THRESHOLD)) {
      return multiply_karatsuba(*this, val);
    }

    if (!is_recursion) {
      if (static_cast<std::int64_t>((bit_length(mag_, mag_.length()))) +
              static_cast<std::int64_t>((bit_length(val.mag_, val.mag_.length()))) >
          32LL * MAX_MAG_LENGTH) {
        report_overflow();
      }
    }
    return multiply_toom_cook3(*this, val);
  }

  // 返回 this * v,其中 v 为 std::int64_t
  bigint multiply(std::int64_t v) const {
    if (v == 0 || signum_ == 0) {
      return ZERO;
    }

    std::int32_t rsign = v > 0 ? signum_ : -signum_;
    std::uint64_t abs_v = v < 0 ? std::uint64_t(0) - static_cast<std::uint64_t>((v)) : static_cast<std::uint64_t>((v));
    const std::uint64_t dh = abs_v >> 32;
    const std::uint64_t dl = abs_v & 0xffffffffULL;

    const std::int32_t xlen = mag_.length();
    const jarray<std::uint32_t>& value = mag_;
    jarray<std::uint32_t> rmag(dh == 0 ? xlen + 1 : xlen + 2);
    std::uint64_t carry = 0;
    std::int32_t rstart = rmag.length() - 1;
    for (std::int32_t i = xlen - 1; i >= 0; --i) {
      const std::uint64_t product = (value[i] & 0xffffffffULL) * dl + carry;
      rmag[rstart--] = static_cast<std::uint32_t>((product));
      carry = product >> 32;
    }
    rmag[rstart] = static_cast<std::uint32_t>((carry));
    if (dh != 0) {
      carry = 0;
      rstart = rmag.length() - 2;
      for (std::int32_t i = xlen - 1; i >= 0; --i) {
        const std::uint64_t product = (value[i] & 0xffffffffULL) * dh + (rmag[rstart] & 0xffffffffULL) + carry;
        rmag[rstart--] = static_cast<std::uint32_t>((product));
        carry = product >> 32;
      }
      rmag[0] = static_cast<std::uint32_t>((carry));
    }
    if (carry == 0) {
      rmag = rmag.copy_of_range(1, rmag.length());
    }
    return bigint(rsign, std::move(rmag));
  }

  // 返回 abs(this) 最低 n 个 limb 组成的非负 bigint
  // n >= mag_.length() 时返回 abs(this),n <= 0 时返回 0
  bigint get_lower(std::int32_t n) const {
    if (n <= 0) {
      return ZERO;
    }
    const std::int32_t len = mag_.length();
    if (len <= n) {
      return abs();
    }

    jarray<std::uint32_t> lower_ints(n);
    jarray_copy(mag_, len - n, lower_ints, 0, n);
    return bigint(1, trusted_strip_leading_zero_limbs(std::move(lower_ints)));
  }

  // 移除 abs(this) 最低 n 个 limb,返回剩余高位组成的非负 bigint
  // n 必须非负,n >= mag_.length() 时返回 0
  bigint get_upper(std::int32_t n) const {
    const std::int32_t len = mag_.length();
    if (len <= n) {
      return ZERO;
    }

    const std::int32_t upper_len = len - n;
    jarray<std::uint32_t> upper_ints(upper_len);
    jarray_copy(mag_, 0, upper_ints, 0, upper_len);
    return bigint(1, trusted_strip_leading_zero_limbs(std::move(upper_ints)));
  }

  // 返回 Toom-Cook 3 路乘法使用的第 slice 个 magnitude 切片
  // lower_size 和 upper_size 指定低位切片与最高切片的 limb 数
  bigint get_toom_slice(std::int32_t lower_size, std::int32_t upper_size, std::int32_t slice,
                        std::int32_t fullsize) const {
    std::int32_t start = 0;
    std::int32_t end = 0;
    std::int32_t slice_size = 0;
    const std::int32_t len = mag_.length();
    const std::int32_t offset = fullsize - len;

    if (slice == 0) {
      start = 0 - offset;
      end = upper_size - 1 - offset;
    } else {
      start = upper_size + (slice - 1) * lower_size - offset;
      end = start + lower_size - 1;
    }

    if (start < 0) {
      start = 0;
    }
    if (end < 0) {
      return ZERO;
    }

    slice_size = (end - start) + 1;
    if (slice_size <= 0) {
      return ZERO;
    }

    if (start == 0 && slice_size >= len) {
      return abs();
    }

    jarray<std::uint32_t> int_slice(slice_size);
    jarray_copy(mag_, start, int_slice, 0, slice_size);
    return bigint(1, trusted_strip_leading_zero_limbs(std::move(int_slice)));
  }

  // 返回 this / 3,调用方必须保证除法无余数
  // 仅供 Toom-Cook 插值阶段使用
  bigint exact_divide_by3() const {
    const std::int32_t len = mag_.length();
    jarray<std::uint32_t> result(len);
    std::uint64_t borrow = 0;
    for (std::int32_t i = len - 1; i >= 0; --i) {
      const std::uint64_t x = mag_[i] & 0xffffffffULL;
      const std::uint64_t w = x - borrow;
      if (borrow > x) {
        borrow = 1;
      } else {
        borrow = 0;
      }

      const std::uint64_t q = (w * 0xaaaaaaabULL) & 0xffffffffULL;
      result[i] = static_cast<std::uint32_t>((q));

      if (q >= 0x55555556ULL) {
        ++borrow;
        if (q >= 0xaaaaaaabULL) {
          ++borrow;
        }
      }
    }
    result = trusted_strip_leading_zero_limbs(std::move(result));
    return bigint(signum_, std::move(result));
  }

  // 返回 this / val,商向零截断
  // val 为零时抛出 std::runtime_error
  bigint divide(const bigint& val) const {
    if (val.signum_ == 0) {
      throw std::runtime_error("divide by zero");
    }
    if (val.mag_.length() < mutable_bigint::BURNIKEL_ZIEGLER_THRESHOLD ||
        mag_.length() - val.mag_.length() < mutable_bigint::BURNIKEL_ZIEGLER_OFFSET) {
      return divide_knuth(val);
    }
    return divide_burnikel_ziegler(val);
  }

  // 使用 Knuth O(n^2) 长除法返回 this / val
  // 调用方负责保证 val 非零
  bigint divide_knuth(const bigint& val) const {
    mutable_bigint q;
    mutable_bigint a(mag_);
    mutable_bigint b(val.mag_);
    a.divide_knuth(b, q, false);
    return from_mutable(std::move(q), signum_ * val.signum_);
  }

  // 同时计算 this / val 和 this % val
  // 返回值 first 为向零截断的商,second 为余数
  // 余数满足 abs(remainder) < abs(val),非零时与 this 同号
  // val 为零时抛出 std::runtime_error
  std::pair<bigint, bigint> divide_and_remainder(const bigint& val) const {
    if (val.signum_ == 0) {
      throw std::runtime_error("divide by zero");
    }
    if (val.mag_.length() < mutable_bigint::BURNIKEL_ZIEGLER_THRESHOLD ||
        mag_.length() - val.mag_.length() < mutable_bigint::BURNIKEL_ZIEGLER_OFFSET) {
      return divide_and_remainder_knuth(val);
    }
    return divide_and_remainder_burnikel_ziegler(val);
  }

  // 使用 Knuth 长除法同时计算商和余数
  // 返回值 first 为商,second 为余数,调用方负责保证 val 非零
  std::pair<bigint, bigint> divide_and_remainder_knuth(const bigint& val) const {
    mutable_bigint q;
    mutable_bigint a(mag_);
    mutable_bigint b(val.mag_);
    mutable_bigint r = a.divide_knuth(b, q);
    return {from_mutable(std::move(q), signum_ == val.signum_ ? 1 : -1), from_mutable(std::move(r), signum_)};
  }

  // 返回 this % val
  // 结果满足 this == (this / val) * val + remainder
  // 非零结果与 this 同号,因此结果可能为负
  // val 为零时抛出 std::runtime_error
  bigint remainder(const bigint& val) const {
    if (val.signum_ == 0) {
      throw std::runtime_error("divide by zero");
    }
    if (val.mag_.length() < mutable_bigint::BURNIKEL_ZIEGLER_THRESHOLD ||
        mag_.length() - val.mag_.length() < mutable_bigint::BURNIKEL_ZIEGLER_OFFSET) {
      return remainder_knuth(val);
    }
    return remainder_burnikel_ziegler(val);
  }

  // 使用 Knuth 长除法返回 this % val,调用方负责保证 val 非零
  bigint remainder_knuth(const bigint& val) const {
    mutable_bigint q;
    mutable_bigint a(mag_);
    mutable_bigint b(val.mag_);
    mutable_bigint r = a.divide_knuth(b, q);
    return from_mutable(std::move(r), signum_);
  }

  // 使用 Burnikel-Ziegler 算法返回 this / val
  // 操作数规模不满足算法阈值时回退到 Knuth 除法
  bigint divide_burnikel_ziegler(const bigint& val) const {
    if (val.mag_.length() < mutable_bigint::BURNIKEL_ZIEGLER_THRESHOLD ||
        mag_.length() - val.mag_.length() < mutable_bigint::BURNIKEL_ZIEGLER_OFFSET) {
      return divide_knuth(val);
    }
    return divide_and_remainder_burnikel_ziegler(val).first;
  }

  // 使用 Burnikel-Ziegler 算法返回 this % val
  // 操作数规模不满足算法阈值时回退到 Knuth 除法
  bigint remainder_burnikel_ziegler(const bigint& val) const {
    if (val.mag_.length() < mutable_bigint::BURNIKEL_ZIEGLER_THRESHOLD ||
        mag_.length() - val.mag_.length() < mutable_bigint::BURNIKEL_ZIEGLER_OFFSET) {
      return remainder_knuth(val);
    }
    return divide_and_remainder_burnikel_ziegler(val).second;
  }

  // 使用 Burnikel-Ziegler 算法同时计算 this / val 和 this % val
  // 返回值 first 为商,second 为余数
  std::pair<bigint, bigint> divide_and_remainder_burnikel_ziegler(const bigint& val) const {
    if (val.mag_.length() < mutable_bigint::BURNIKEL_ZIEGLER_THRESHOLD ||
        mag_.length() - val.mag_.length() < mutable_bigint::BURNIKEL_ZIEGLER_OFFSET) {
      return divide_and_remainder_knuth(val);
    }
    mutable_bigint q;
    mutable_bigint a(mag_);
    mutable_bigint b(val.mag_);
    mutable_bigint r = a.divide_and_remainder_burnikel_ziegler(b, q);
    const bigint q_bigint = q.is_zero() ? ZERO : from_mutable(std::move(q), signum_ * val.signum_);
    const bigint r_bigint = r.is_zero() ? ZERO : from_mutable(std::move(r), signum_);
    return {q_bigint, r_bigint};
  }

  // 返回 abs(this) 除以单 limb 正整数 m 的余数
  // 供 bit_sieve 构造搜索筛使用,m 必须非零
  std::uint32_t mod_uint32(std::uint32_t m) const {
    if (m == 0) {
      throw std::runtime_error("divide by zero");
    }
    std::uint64_t rem = 0;
    for (std::int32_t i = 0; i < mag_.length(); ++i) {
      rem = ((rem << 32) | (mag_[i] & 0xffffffffULL)) % m;
    }
    return static_cast<std::uint32_t>((rem));
  }

  // 返回 abs(this) mod SMALL_PRIME_PRODUCT_MAGNITUDE
  // 用于一次排除可被 3 到 41 之间小素数整除的候选值
  std::uint64_t mod_small_prime_product() const {
    std::uint64_t remainder = 0;
    for (std::int32_t i = 0; i < mag_.length(); ++i) {
      remainder = ((remainder << 16) | (mag_[i] >> 16)) % SMALL_PRIME_PRODUCT_MAGNITUDE;
      remainder = ((remainder << 16) | (mag_[i] & 0xffffU)) % SMALL_PRIME_PRODUCT_MAGNITUDE;
    }
    return remainder;
  }

  // 返回 this 的 exponent 次方,其中 exponent 为普通整数
  // exponent < 0 时抛出 std::runtime_error,0^0 按整数幂约定返回 1
  bigint pow(std::int32_t exponent) const {
    if (exponent < 0) {
      throw std::runtime_error("negative exponent");
    }
    if (exponent == 0) {
      return ONE;
    }
    if (signum_ == 0) {
      return *this;
    }

    bigint part_storage;
    const bigint* part_to_square = this;
    if (signum_ < 0) {
      part_storage = abs();
      part_to_square = &part_storage;
    }

    const std::int32_t powers_of_two = part_to_square->get_lowest_set_bit();
    const std::int64_t bits_to_shift_long = static_cast<std::int64_t>((powers_of_two)) * exponent;
    if (bits_to_shift_long > std::numeric_limits<std::int32_t>::max()) {
      report_overflow();
    }
    const std::int32_t bits_to_shift = static_cast<std::int32_t>((bits_to_shift_long));

    std::int32_t remaining_bits = 0;
    if (powers_of_two > 0) {
      part_storage = part_to_square->shift_right(powers_of_two);
      part_to_square = &part_storage;
      remaining_bits = part_to_square->bit_length();
      if (remaining_bits == 1) {
        if (signum_ < 0 && (exponent & 1) == 1) {
          return NEGATIVE_ONE.shift_left(bits_to_shift);
        }
        return ONE.shift_left(bits_to_shift);
      }
    } else {
      remaining_bits = part_to_square->bit_length();
      if (remaining_bits == 1) {
        if (signum_ < 0 && (exponent & 1) == 1) {
          return NEGATIVE_ONE;
        }
        return ONE;
      }
    }

    const std::int64_t scale_factor = static_cast<std::int64_t>((remaining_bits)) * exponent;

    if (part_to_square->mag_.length() == 1 && scale_factor <= 62) {
      const std::int32_t new_sign = (signum_ < 0 && (exponent & 1) == 1) ? -1 : 1;
      std::int64_t result = 1;
      std::int64_t base_to_pow2 = part_to_square->mag_[0] & 0xffffffffULL;

      std::int32_t working_exponent = exponent;
      while (working_exponent != 0) {
        if ((working_exponent & 1) == 1) {
          result = result * base_to_pow2;
        }
        working_exponent = static_cast<std::int32_t>((static_cast<std::uint32_t>((working_exponent)) >> 1));
        if (working_exponent != 0) {
          base_to_pow2 = base_to_pow2 * base_to_pow2;
        }
      }

      if (powers_of_two > 0) {
        if (bits_to_shift + scale_factor <= 62) {
          return value_of((result << bits_to_shift) * new_sign);
        }
        return value_of(result * new_sign).shift_left(bits_to_shift);
      }
      return value_of(result * new_sign);
    }

    if (static_cast<std::int64_t>((bit_length())) * exponent / 32 > MAX_MAG_LENGTH) {
      report_overflow();
    }

    bigint answer;
    bool has_answer = false;
    std::int32_t working_exponent = exponent;
    while (working_exponent != 0) {
      if ((working_exponent & 1) == 1) {
        if (has_answer) {
          answer = answer.multiply(*part_to_square);
        } else {
          answer = *part_to_square;
          has_answer = true;
        }
      }
      working_exponent = static_cast<std::int32_t>((static_cast<std::uint32_t>((working_exponent)) >> 1));
      if (working_exponent != 0) {
        part_storage = part_to_square->square();
        part_to_square = &part_storage;
      }
    }
    if (powers_of_two > 0) {
      answer = answer.shift_left(bits_to_shift);
    }
    if (signum_ < 0 && (exponent & 1) == 1) {
      return answer.negate();
    }
    return answer;
  }

  // 返回 this 的整数平方根,即满足 s * s <= this 的最大非负整数 s
  // 结果等价于 floor(sqrt(this)),this 为负数时抛出 std::runtime_error
  bigint sqrt() const {
    if (signum_ < 0) {
      throw std::runtime_error("negative bigint");
    }
    mutable_bigint a = to_mutable();
    mutable_bigint s = a.sqrt();
    return from_mutable(std::move(s), 1);
  }

  // 同时计算 this 的整数平方根 s 和余数 this - s * s
  // 返回值 first 为 s,second 为余数,this 为负数时抛出 std::runtime_error
  std::pair<bigint, bigint> sqrt_and_remainder() const {
    bigint s = sqrt();
    bigint r = subtract(s.multiply(s));
    return {s, r};
  }

  // 返回 gcd(abs(this), abs(val))
  // this 和 val 同时为 0 时返回 0
  bigint gcd(const bigint& val) const {
    if (val.signum_ == 0) {
      return abs();
    }
    if (signum_ == 0) {
      return val.abs();
    }
    mutable_bigint a = to_mutable();
    mutable_bigint b = val.to_mutable();
    mutable_bigint g = a.hybrid_gcd(b);
    return from_mutable(std::move(g), 1);
  }

  // 返回 this 在正模 m 下的唯一非负余数,结果位于 [0, m)
  // 与 remainder() 不同,即使 this 为负数,结果也不会为负
  // m <= 0 时抛出 std::runtime_error
  bigint mod(const bigint& m) const {
    if (m.signum_ <= 0) {
      throw std::runtime_error("modulus not positive");
    }
    if (m.mag_.length() == 1) {
      const std::uint32_t modulus = m.mag_[0];
      const std::uint32_t remainder = mod_uint32(modulus);
      if (remainder == 0 || signum_ >= 0) {
        return value_of(static_cast<std::int64_t>(remainder));
      }
      return value_of(static_cast<std::int64_t>(modulus - remainder));
    }
    bigint result = remainder(m);
    return result.signum_ >= 0 ? result : result.add(m);
  }

  // 返回 this 在正模 m 下的乘法逆元,结果位于 [0, m)
  // m <= 0 或 this 与 m 不互素时抛出 std::runtime_error
  bigint mod_inverse(const bigint& m) const {
    if (m.signum_ != 1) {
      throw std::runtime_error("modulus not positive");
    }
    if (m.compare_to(ONE) == 0) {
      return ZERO;
    }
    bigint mod_val = (signum_ < 0 || compare_magnitude(m) >= 0) ? mod(m) : *this;
    if (mod_val.compare_to(ONE) == 0) {
      return ONE;
    }
    mutable_bigint a = mod_val.to_mutable();
    mutable_bigint p = m.to_mutable();
    mutable_bigint inv = a.mutable_mod_inverse(p);
    return from_mutable(std::move(inv), 1);
  }

  // 返回 this^exponent mod m,结果位于 [0, m)
  // exponent 可以为负,此时结果是正指数模幂的乘法逆元
  // m <= 0 或所需乘法逆元不存在时抛出 std::runtime_error
  bigint mod_pow(const bigint& exponent, const bigint& m) const {
    if (m.signum_ <= 0) {
      throw std::runtime_error("modulus not positive");
    }
    if (exponent.signum_ == 0) {
      return m.compare_to(ONE) == 0 ? ZERO : ONE;
    }
    if (compare_to(ONE) == 0) {
      return m.compare_to(ONE) == 0 ? ZERO : ONE;
    }
    if (signum_ == 0 && exponent.signum_ >= 0) {
      return ZERO;
    }
    if (compare_to(NEGATIVE_ONE) == 0 && !exponent.test_bit(0)) {
      return m.compare_to(ONE) == 0 ? ZERO : ONE;
    }

    bool invert_result = exponent.signum_ < 0;
    bigint exp = exponent;
    if (invert_result) {
      exp = exponent.negate();
    }

    const bigint base = (signum_ < 0 || compare_to(m) >= 0 ? mod(m) : *this);
    bigint result;
    if (m.test_bit(0)) {
      result = base.odd_mod_pow(exp, m);
    } else {
      const std::int32_t p = m.get_lowest_set_bit();
      const bigint m1 = m.shift_right(p);
      const bigint m2 = ONE.shift_left(p);

      const bigint base2 = (signum_ < 0 || compare_to(m1) >= 0 ? mod(m1) : *this);
      const bigint a1 = (m1.compare_to(ONE) == 0 ? ZERO : base2.odd_mod_pow(exp, m1));
      const bigint a2 = base.mod_pow2(exp, p);

      const bigint y1 = m2.mod_inverse(m1);
      const bigint y2 = m1.mod_inverse(m2);

      if (m.mag_.length() < MAX_MAG_LENGTH / 2) {
        result = a1.multiply(m2).multiply(y1).add(a2.multiply(m1).multiply(y2)).mod(m);
      } else {
        mutable_bigint t1;
        a1.multiply(m2).to_mutable().multiply(y1.to_mutable(), t1);
        mutable_bigint t2;
        a2.multiply(m1).to_mutable().multiply(y2.to_mutable(), t2);
        t1.add(t2);
        mutable_bigint q;
        result = from_mutable(t1.divide(m.to_mutable(), q), 1);
      }
    }
    return invert_result ? result.mod_inverse(m) : result;
  }

  // 计算 this^y mod z,其中 z 必须为正奇数且 y 必须为正
  // 根据模数规模选择普通模乘或 Montgomery 滑动窗口算法
  bigint odd_mod_pow(const bigint& y, const bigint& z) const {
    if (y.compare_to(ONE) == 0) {
      return *this;
    }
    if (signum_ == 0) {
      return ZERO;
    }

    // 单 limb 模数直接使用 64 位运算,避免构造 Montgomery 工作数组
    if (z.mag_.length() == 1) {
      const std::uint32_t modulus = z.mag_[0];
      std::uint64_t result = 1 % modulus;
      const std::uint64_t base = mod_uint32(modulus);
      for (std::int32_t i = 0; i < y.mag_.length(); ++i) {
        const std::uint32_t word = y.mag_[i];
        const std::int32_t first_bit = i == 0 ? 31 - decimal_detail::count_leading_zeros(word) : 31;
        for (std::int32_t bit = first_bit; bit >= 0; --bit) {
          result = (result * result) % modulus;
          if (((word >> bit) & 1U) != 0) {
            result = (result * base) % modulus;
          }
        }
      }
      return value_of(static_cast<std::int64_t>(result));
    }

#if DECIMAL_DETAIL_HAS_FAST_DIV128
    // 双 limb 模数使用 128/64 位运算,避免小整数素性测试进入 Montgomery 路径
    if (z.mag_.length() == 2) {
      const std::uint64_t modulus = (static_cast<std::uint64_t>(z.mag_[0]) << 32) | z.mag_[1];
      auto reduce = [modulus](const jarray<std::uint32_t>& magnitude) {
        std::uint64_t remainder = 0;
        for (std::int32_t i = 0; i < magnitude.length(); ++i) {
          const std::uint64_t high = remainder >> 32;
          const std::uint64_t low = (remainder << 32) | magnitude[i];
          decimal_detail::divide_128_by_64(high, low, modulus, &remainder);
        }
        return remainder;
      };
      auto multiply_mod = [modulus](std::uint64_t lhs, std::uint64_t rhs) {
        const decimal_detail::uint128_words product = decimal_detail::multiply_64x64(lhs, rhs);
        std::uint64_t remainder = 0;
        decimal_detail::divide_128_by_64(product.high, product.low, modulus, &remainder);
        return remainder;
      };

      std::uint64_t result = 1 % modulus;
      const std::uint64_t base = reduce(mag_);
      for (std::int32_t i = 0; i < y.mag_.length(); ++i) {
        const std::uint32_t word = y.mag_[i];
        const std::int32_t first_bit = i == 0 ? 31 - decimal_detail::count_leading_zeros(word) : 31;
        for (std::int32_t bit = first_bit; bit >= 0; --bit) {
          result = multiply_mod(result, result);
          if (((word >> bit) & 1U) != 0) {
            result = multiply_mod(result, base);
          }
        }
      }
      if (result <= static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())) {
        return value_of(static_cast<std::int64_t>(result));
      }
      return bigint(
          1, jarray<std::uint32_t>{static_cast<std::uint32_t>(result >> 32), static_cast<std::uint32_t>(result)});
    }
#endif

    jarray<std::uint32_t> base = mag_.clone();
    const jarray<std::uint32_t>& exp = y.mag_;
    jarray<std::uint32_t> mod = z.mag_.clone();
    std::int32_t mod_len = mod.length();

    if ((mod_len & 1) != 0) {
      jarray<std::uint32_t> x(mod_len + 1);
      jarray_copy(mod, 0, x, 1, mod_len);
      mod = std::move(x);
      ++mod_len;
    }

    std::int32_t wbits = 0;
    std::int32_t ebits = bit_length(exp, exp.length());
    if ((ebits != 17) || (exp[0] != 65537)) {
      while (ebits > bn_exp_mod_thresh_table[wbits]) {
        ++wbits;
      }
    }

    const std::int32_t tblmask = 1 << wbits;
    std::vector<jarray<std::uint32_t>> table(static_cast<size_t>((tblmask)));
    for (std::int32_t i = 0; i < tblmask; ++i) {
      table[static_cast<size_t>((i))] = jarray<std::uint32_t>(mod_len);
    }

    const std::uint64_t n0 = (mod[mod_len - 1] & 0xffffffffULL) + ((mod[mod_len - 2] & 0xffffffffULL) << 32);
    const std::uint64_t inv = std::uint64_t(0) - mutable_bigint::inverse_mod64(n0);

    jarray<std::uint32_t> a = shift_left_mag(base, mod_len << 5);
    mutable_bigint q;
    mutable_bigint a2(a);
    mutable_bigint b2(mod);
    b2.normalize();
    mutable_bigint r = a2.divide(b2, q);
    table[0] = r.to_int_array();

    if (table[0].length() < mod_len) {
      const std::int32_t offset = mod_len - table[0].length();
      jarray<std::uint32_t> t2(mod_len);
      jarray_copy(table[0], 0, t2, offset, table[0].length());
      table[0] = std::move(t2);
    }

    jarray<std::uint32_t> b = montgomery_square(table[0], mod, mod_len, inv, nullptr);
    jarray<std::uint32_t> t = b.copy_of(mod_len);

    for (std::int32_t i = 1; i < tblmask; ++i) {
      table[static_cast<size_t>((i))] =
          montgomery_multiply(t, table[static_cast<size_t>((i - 1))], mod, mod_len, inv, nullptr);
    }

    std::uint32_t bitpos = std::uint32_t{1} << ((ebits - 1) & (32 - 1));
    std::int32_t buf = 0;
    std::int32_t elen = exp.length();
    std::int32_t e_index = 0;
    for (std::int32_t i = 0; i <= wbits; ++i) {
      buf = (buf << 1) | (((exp[e_index] & bitpos) != 0) ? 1 : 0);
      bitpos >>= 1;
      if (bitpos == 0) {
        ++e_index;
        bitpos = std::uint32_t{1} << (32 - 1);
        --elen;
      }
    }

    std::int32_t multpos = ebits;
    --ebits;
    bool isone = true;

    multpos = ebits - wbits;
    while ((buf & 1) == 0) {
      buf = static_cast<std::int32_t>((static_cast<std::uint32_t>((buf)) >> 1));
      ++multpos;
    }

    const jarray<std::uint32_t>* mult = &table[static_cast<size_t>((static_cast<std::uint32_t>((buf)) >> 1))];

    buf = 0;
    if (multpos == ebits) {
      isone = false;
    }

    while (true) {
      --ebits;
      buf <<= 1;

      if (elen != 0) {
        buf |= ((exp[e_index] & bitpos) != 0) ? 1 : 0;
        bitpos >>= 1;
        if (bitpos == 0) {
          ++e_index;
          bitpos = std::uint32_t{1} << (32 - 1);
          --elen;
        }
      }

      if ((buf & tblmask) != 0) {
        multpos = ebits - wbits;
        while ((buf & 1) == 0) {
          buf = static_cast<std::int32_t>((static_cast<std::uint32_t>((buf)) >> 1));
          ++multpos;
        }
        mult = &table[static_cast<size_t>((static_cast<std::uint32_t>((buf)) >> 1))];
        buf = 0;
      }

      if (ebits == multpos) {
        if (isone) {
          b = mult->clone();
          isone = false;
        } else {
          montgomery_multiply_into(b, *mult, mod, mod_len, inv, a);
          a.swap(b);
        }
      }

      if (ebits == 0) {
        break;
      }

      if (!isone) {
        montgomery_square_into(b, mod, mod_len, inv, a);
        a.swap(b);
      }
    }

    jarray<std::uint32_t> t2(2 * mod_len);
    jarray_copy(b, 0, t2, mod_len, mod_len);
    b = mont_reduce(t2, mod, mod_len, static_cast<std::uint32_t>((inv)));
    t2 = b.copy_of(mod_len);

    return bigint(1, std::move(t2));
  }

  // 返回 this^exponent mod 2^p
  // 调用方保证 exponent 非负且 p 为正
  bigint mod_pow2(const bigint& exponent, std::int32_t p) const {
    if (p > 0 && p <= 64 && exponent.signum_ != 0) {
      std::uint64_t base = mag_.length() == 0 ? 0 : mag_[mag_.length() - 1];
      if (mag_.length() > 1) {
        base |= static_cast<std::uint64_t>(mag_[mag_.length() - 2]) << 32;
      }
      const std::uint64_t mask = p == 64 ? UINT64_MAX : (std::uint64_t{1} << p) - 1;
      base &= mask;

      std::uint64_t result = 1;
      std::int32_t limit = exponent.bit_length();
      if ((base & 1U) != 0) {
        limit = (std::min)(p - 1, limit);
      }
      for (std::int32_t bit = 0; bit < limit; ++bit) {
        if (exponent.test_bit(bit)) {
          result = (result * base) & mask;
        }
        if (bit + 1 < limit) {
          base = (base * base) & mask;
        }
      }
      if (result <= static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())) {
        return value_of(static_cast<std::int64_t>(result));
      }
      return bigint(
          1, jarray<std::uint32_t>{static_cast<std::uint32_t>(result >> 32), static_cast<std::uint32_t>(result)});
    }

    bigint result = ONE;
    bigint base_to_pow2 = mod2(p);
    std::int32_t exp_offset = 0;

    std::int32_t limit = exponent.bit_length();
    if (test_bit(0)) {
      limit = (p - 1) < limit ? (p - 1) : limit;
    }

    while (exp_offset < limit) {
      if (exponent.test_bit(exp_offset)) {
        result = result.multiply(base_to_pow2).mod2(p);
      }
      ++exp_offset;
      if (exp_offset < limit) {
        base_to_pow2 = base_to_pow2.square().mod2(p);
      }
    }
    return result;
  }

  // 返回 this mod 2^p,调用方必须保证 this >= 0 且 p > 0
  bigint mod2(std::int32_t p) const {
    if (bit_length() <= p) {
      return *this;
    }

    const std::int32_t num_ints = static_cast<std::int32_t>((static_cast<std::uint32_t>((p + 31)) >> 5));
    jarray<std::uint32_t> mag(num_ints);
    jarray_copy(mag_, mag_.length() - num_ints, mag, 0, num_ints);

    const std::int32_t excess_bits = (num_ints << 5) - p;
    mag[0] &= static_cast<std::uint32_t>(((std::uint64_t{1} << (32 - excess_bits)) - 1));

    return bigint(1, std::move(mag));
  }

  // 返回 this << n,等价于 floor(this * 2^n)
  // n 可以为负,此时执行算术右移 -n 位
  bigint shift_left(std::int32_t n) const {
    if (signum_ == 0) {
      return ZERO;
    }
    if (n > 0) {
      return bigint(signum_, shift_left_mag(mag_, n));
    } else if (n == 0) {
      return *this;
    } else {
      return shift_right_impl(static_cast<std::int32_t>((-static_cast<std::uint32_t>((n)))));
    }
  }

  // 返回 this >> n,正移位距离执行带符号扩展的算术右移
  // 结果等价于 floor(this / 2^n),n 为负时执行左移 -n 位
  bigint shift_right(std::int32_t n) const {
    if (signum_ == 0) {
      return ZERO;
    }
    if (n > 0) {
      return shift_right_impl(n);
    } else if (n == 0) {
      return *this;
    } else {
      return bigint(signum_, shift_left_mag(mag_, static_cast<std::int32_t>((-static_cast<std::uint32_t>((n))))));
    }
  }

  // 使用按无符号值解释的移位距离 n 执行算术右移
  // 仅供 shift_left() 和 shift_right() 处理极端负移位距离
  bigint shift_right_impl(std::int32_t n) const {
    const std::int32_t n_ints = static_cast<std::int32_t>((static_cast<std::uint32_t>((n)) >> 5));
    const std::int32_t n_bits = n & 0x1f;
    const std::int32_t mag_len = mag_.length();
    jarray<std::uint32_t> new_mag;

    if (n_ints >= mag_len) {
      return signum_ >= 0 ? ZERO : NEGATIVE_ONE;
    }

    if (n_bits == 0) {
      const std::int32_t new_mag_len = mag_len - n_ints;
      new_mag = mag_.copy_of(new_mag_len);
    } else {
      std::int32_t i = 0;
      const std::uint32_t high_bits = mag_[0] >> n_bits;
      if (high_bits != 0) {
        new_mag = jarray<std::uint32_t>(mag_len - n_ints);
        new_mag[i++] = high_bits;
      } else {
        new_mag = jarray<std::uint32_t>(mag_len - n_ints - 1);
      }
      const std::int32_t num_iter = mag_len - n_ints - 1;
      shift_right_impl_worker(new_mag, mag_, i, n_bits, num_iter);
    }

    if (signum_ < 0) {
      bool ones_lost = false;
      for (std::int32_t i = mag_len - 1, j = mag_len - n_ints; i >= j && !ones_lost; --i) {
        ones_lost = mag_[i] != 0;
      }
      if (!ones_lost && n_bits != 0) {
        ones_lost = (mag_[mag_len - n_ints - 1] << (32 - n_bits)) != 0;
      }

      if (ones_lost) {
        new_mag = java_increment(std::move(new_mag));
      }
    }

    return bigint(signum_, std::move(new_mag));
  }

  // 返回 this & val,按具有无限符号扩展位的二进制补码语义执行
  // 结果仅在两个操作数均为负数时为负
  bigint and_op(const bigint& val) const {
    const std::int32_t len = std::max(int_length(), val.int_length());
    jarray<std::uint32_t> result(len);
    for (std::int32_t i = 0; i < len; ++i) {
      result[i] = get_int(len - i - 1) & val.get_int(len - i - 1);
    }
    return value_of(std::move(result));
  }

  // 返回 this | val,按具有无限符号扩展位的二进制补码语义执行
  // 任一操作数为负数时结果为负
  bigint or_op(const bigint& val) const {
    const std::int32_t len = std::max(int_length(), val.int_length());
    jarray<std::uint32_t> result(len);
    for (std::int32_t i = 0; i < len; ++i) {
      result[i] = get_int(len - i - 1) | val.get_int(len - i - 1);
    }
    return value_of(std::move(result));
  }

  // 返回 this ^ val,按具有无限符号扩展位的二进制补码语义执行
  // 恰好一个操作数为负数时结果为负
  bigint xor_op(const bigint& val) const {
    const std::int32_t len = std::max(int_length(), val.int_length());
    jarray<std::uint32_t> result(len);
    for (std::int32_t i = 0; i < len; ++i) {
      result[i] = get_int(len - i - 1) ^ val.get_int(len - i - 1);
    }
    return value_of(std::move(result));
  }

  // 返回 ~this,按具有无限符号扩展位的二进制补码语义执行
  // 结果等价于 -(this + 1),当前值非负时结果为负
  bigint not_op() const {
    const std::int32_t len = int_length();
    jarray<std::uint32_t> result(len);
    for (std::int32_t i = 0; i < len; ++i) {
      result[i] = ~get_int(len - i - 1);
    }
    return value_of(std::move(result));
  }

  // 返回 this & ~val,等价于 and_op(val.not_op())
  // 该操作用于在不构造中间补数的情况下清除一组掩码 bit
  bigint and_not(const bigint& val) const {
    const std::int32_t len = std::max(int_length(), val.int_length());
    jarray<std::uint32_t> result(len);
    for (std::int32_t i = 0; i < len; ++i) {
      result[i] = get_int(len - i - 1) & ~val.get_int(len - i - 1);
    }
    return value_of(std::move(result));
  }

  // 返回二进制补码表示中下标 n 的 bit 是否为 1
  // 最低有效 bit 的下标为 0,必要时按无限符号位扩展
  // n < 0 时抛出 std::runtime_error
  bool test_bit(std::int32_t n) const {
    if (n < 0) {
      throw std::runtime_error("negative bit address");
    }
    return (get_int(n >> 5) & (std::uint32_t(1) << (n & 31))) != 0;
  }

  // 返回将二进制补码表示中下标 n 的 bit 置为 1 后的新 bigint
  // 最低有效 bit 的下标为 0,n < 0 时抛出 std::runtime_error
  bigint set_bit(std::int32_t n) const {
    if (n < 0) {
      throw std::runtime_error("negative bit address");
    }
    const std::int32_t int_num = n >> 5;
    jarray<std::uint32_t> result(std::max(int_length(), ((n + 1) >> 5) + 1));
    for (std::int32_t i = 0; i < result.length(); ++i) {
      result[result.length() - i - 1] = get_int(i);
    }
    result[result.length() - int_num - 1] |= std::uint32_t(1) << (n & 31);
    return value_of(std::move(result));
  }

  // 返回将二进制补码表示中下标 n 的 bit 清为 0 后的新 bigint
  // 最低有效 bit 的下标为 0,n < 0 时抛出 std::runtime_error
  bigint clear_bit(std::int32_t n) const {
    if (n < 0) {
      throw std::runtime_error("negative bit address");
    }
    const std::int32_t int_num = n >> 5;
    jarray<std::uint32_t> result(std::max(int_length(), int_num + 2));
    for (std::int32_t i = 0; i < result.length(); ++i) {
      result[result.length() - i - 1] = get_int(i);
    }
    result[result.length() - int_num - 1] &= ~(std::uint32_t(1) << (n & 31));
    return value_of(std::move(result));
  }

  // 返回将二进制补码表示中下标 n 的 bit 翻转后的新 bigint
  // 最低有效 bit 的下标为 0,n < 0 时抛出 std::runtime_error
  bigint flip_bit(std::int32_t n) const {
    if (n < 0) {
      throw std::runtime_error("negative bit address");
    }
    const std::int32_t int_num = n >> 5;
    jarray<std::uint32_t> result(std::max(int_length(), int_num + 2));
    for (std::int32_t i = 0; i < result.length(); ++i) {
      result[result.length() - i - 1] = get_int(i);
    }
    result[result.length() - int_num - 1] ^= std::uint32_t(1) << (n & 31);
    return value_of(std::move(result));
  }

  // 返回二进制补码表示中最低 1 bit 的下标,即其右侧连续 0 bit 的数量
  // 最低有效 bit 的下标为 0,当前值为 0 时返回 -1
  std::int32_t get_lowest_set_bit() const {
    std::int32_t lsb = lowest_set_bit_plus_two_ - 2;
    if (lsb == -2) {
      lsb = 0;
      if (signum_ == 0) {
        lsb -= 1;
      } else {
        std::int32_t i = mag_.length();
        std::uint32_t b = 0;
        do {
          b = mag_[--i];
        } while (b == 0);
        lsb += ((mag_.length() - i - 1) << 5) + mutable_bigint::number_of_trailing_zeros(b);
      }
      lowest_set_bit_plus_two_ = lsb + 2;
    }
    return lsb;
  }

  // 返回当前值最小二进制补码表示所需的 bit 数,不包含符号位
  // 正数等于普通二进制表示的长度,0 返回 0
  std::int32_t bit_length() const {
    std::int32_t n = bit_length_plus_one_ - 1;
    if (n == -1) {
      if (signum_ == 0) {
        n = 0;
      } else {
        const std::int32_t mag_bit_length = ((mag_.length() - 1) << 5) + mutable_bigint::bit_length_for_limb(mag_[0]);
        if (signum_ < 0) {
          bool pow2 = (mag_[0] & (mag_[0] - 1)) == 0;
          for (std::int32_t i = 1; i < mag_.length() && pow2; ++i) {
            pow2 = mag_[i] == 0;
          }
          n = pow2 ? mag_bit_length - 1 : mag_bit_length;
        } else {
          n = mag_bit_length;
        }
      }
      bit_length_plus_one_ = n + 1;
    }
    return n;
  }

  // 返回二进制补码表示中与符号位不同的 bit 数量
  // 该定义对负数统计其无限符号扩展表示中的有限非符号部分
  std::int32_t bit_count() const {
    std::int32_t bc = bit_count_plus_one_ - 1;
    if (bc == -1) {
      bc = 0;
      for (std::int32_t i = 0; i < mag_.length(); ++i) {
        bc += decimal_detail::population_count(mag_[i]);
      }
      if (signum_ < 0) {
        std::int32_t mag_trailing_zero_count = 0;
        std::int32_t j = mag_.length() - 1;
        while (j >= 0 && mag_[j] == 0) {
          mag_trailing_zero_count += 32;
          --j;
        }
        if (j >= 0) {
          mag_trailing_zero_count += mutable_bigint::number_of_trailing_zeros(mag_[j]);
        }
        bc += mag_trailing_zero_count - 1;
      }
      bit_count_plus_one_ = bc + 1;
    }
    return bc;
  }

  // 返回容纳最小二进制补码表示所需的 32 位 limb 数
  std::int32_t int_length() const {
    return (bit_length() >> 5) + 1;
  }

  // 返回无限二进制补码表示的符号 bit,负数返回 1,非负数返回 0
  std::int32_t sign_bit() const {
    return signum_ < 0 ? 1 : 0;
  }

  // 返回无限符号扩展使用的 32 位 limb,负数返回 0xffffffff,非负数返回 0
  std::uint32_t sign_int() const {
    return signum_ < 0 ? 0xffffffffU : 0U;
  }

  // 返回二进制补码小端视图中下标 n 的 32 位 limb
  // 超出实际 magnitude 时执行无限符号扩展,n < 0 时返回 0
  std::uint32_t get_int(std::int32_t n) const {
    if (n < 0) {
      return 0;
    }
    if (n >= mag_.length()) {
      return sign_int();
    }
    const std::uint32_t mag_int = mag_[mag_.length() - n - 1];
    if (signum_ >= 0) {
      return mag_int;
    }
    return n <= first_nonzero_int_num() ? std::uint32_t(0U - mag_int) : ~mag_int;
  }

  // 返回 magnitude 小端视图中最低非零 limb 的下标
  // 结果用于按需生成负数的二进制补码 limb
  std::int32_t first_nonzero_int_num() const {
    std::int32_t fn = first_nonzero_int_num_plus_two_ - 2;
    if (fn == -2) {
      std::int32_t i = mag_.length() - 1;
      while (i >= 0 && mag_[i] == 0) {
        --i;
      }
      fn = mag_.length() - i - 1;
      first_nonzero_int_num_plus_two_ = fn + 2;
    }
    return fn;
  }

  // 判断 abs(this) 是否为 probable prime
  // 返回 false 表示一定是合数,返回 true 时为素数的概率大于 1 - 2^(-certainty)
  // certainty <= 0 时无条件返回 true,执行时间随 certainty 增大
  bool is_probable_prime(std::int32_t certainty) const {
    if (certainty <= 0) {
      return true;
    }
    bigint n = abs();
    if (n.compare_to(TWO) == 0) {
      return true;
    }
    if (n.signum_ == 0 || n.compare_to(ONE) == 0 || n.is_even()) {
      return false;
    }
    return n.prime_to_certainty(certainty);
  }

  // 使用 rnd 返回 bit_length 恰好等于指定值的正 probable prime
  // 使用 DEFAULT_PRIME_CERTAINTY 控制误判概率,bit_length < 2 时抛出异常
  static bigint probable_prime(std::int32_t bit_length, std::mt19937_64& rnd);

  // 搜索 bit_length 小于 SMALL_PRIME_THRESHOLD 的随机 probable prime
  // 先排除小素数因子,再按 certainty 执行概率素性测试
  static bigint small_prime(std::int32_t bit_length, std::int32_t certainty, std::mt19937_64& rnd);

  // 搜索 bit_length 较大的随机 probable prime
  // 使用 bit_sieve 批量排除合数后对剩余候选执行概率素性测试
  static bigint large_prime(std::int32_t bit_length, std::int32_t certainty, std::mt19937_64& rnd);

  // 返回 next_probable_prime() 对指定 bit_length 使用的筛选区间长度
  // bit_length 超过实现允许的搜索上限时抛出 std::runtime_error
  static std::int32_t get_prime_search_len(std::int32_t bit_length);

  // 返回 3、5、7 到 41 的连续小素数乘积
  static bigint small_prime_product();

  // 返回严格大于当前值的第一个 probable prime
  // 当前值为负数时抛出 std::runtime_error
  bigint next_probable_prime() const;

  // 使用内部随机源执行 certainty 对应强度的 probable-prime 测试
  // 根据位数选择 Miller-Rabin 轮数,必要时再执行 Lucas 测试
  bool prime_to_certainty(std::int32_t certainty) const {
    return prime_to_certainty(certainty, nullptr);
  }

  // 使用可选 random 随机源执行 certainty 对应强度的 probable-prime 测试
  // random 为空时使用线程局部随机源
  bool prime_to_certainty(std::int32_t certainty, std::mt19937_64* random) const {
    std::int32_t rounds = 0;
    const std::int32_t n = (std::min(certainty, std::numeric_limits<std::int32_t>::max() - 1) + 1) / 2;

    const std::int32_t size_in_bits = bit_length();
    if (size_in_bits < 100) {
      rounds = 50;
      rounds = n < rounds ? n : rounds;
      return passes_miller_rabin(rounds, random);
    }

    if (size_in_bits < 256) {
      rounds = 27;
    } else if (size_in_bits < 512) {
      rounds = 15;
    } else if (size_in_bits < 768) {
      rounds = 8;
    } else if (size_in_bits < 1024) {
      rounds = 4;
    } else {
      rounds = 2;
    }
    rounds = n < rounds ? n : rounds;

    return passes_miller_rabin(rounds, random) && passes_lucas_lehmer();
  }

  // 对当前正奇数执行 Lucas-Lehmer probable-prime 测试
  bool passes_lucas_lehmer() const {
    const bigint this_plus_one = add(ONE);

    std::int32_t d = 5;
    while (jacobi_symbol(d, *this) != -1) {
      d = (d < 0) ? std::abs(d) + 2 : -(d + 2);
    }

    const bigint u = lucas_lehmer_sequence(d, this_plus_one, *this);
    return u.mod(*this).is_zero();
  }

  // 返回 Jacobi 符号 (p / n)
  // 调用方必须保证 n 为不小于 3 的正奇数
  static std::int32_t jacobi_symbol(std::int32_t p, const bigint& n) {
    if (p == 0) {
      return 0;
    }

    std::int32_t j = 1;
    std::int32_t u = static_cast<std::int32_t>((n.mag_[n.mag_.length() - 1]));

    if (p < 0) {
      p = -p;
      const std::int32_t n8 = u & 7;
      if ((n8 == 3) || (n8 == 7)) {
        j = -j;
      }
    }

    while ((p & 3) == 0) {
      p >>= 2;
    }
    if ((p & 1) == 0) {
      p >>= 1;
      if (((u ^ (u >> 1)) & 2) != 0) {
        j = -j;
      }
    }
    if (p == 1) {
      return j;
    }
    if ((p & u & 2) != 0) {
      j = -j;
    }
    u = static_cast<std::int32_t>(n.mod_uint32(static_cast<std::uint32_t>(p)));

    while (u != 0) {
      while ((u & 3) == 0) {
        u >>= 2;
      }
      if ((u & 1) == 0) {
        u >>= 1;
        if (((p ^ (p >> 1)) & 2) != 0) {
          j = -j;
        }
      }
      if (u == 1) {
        return j;
      }
      const std::int32_t t = u;
      u = p;
      p = t;
      if ((u & p & 2) != 0) {
        j = -j;
      }
      u %= p;
    }
    return 0;
  }

  // 计算 Lucas probable-prime 测试所需的序列项
  // z 为判别式,k 为序列下标,n 为正模数
  static bigint lucas_lehmer_sequence(std::int32_t z, const bigint& k, const bigint& n) {
#if DECIMAL_DETAIL_HAS_FAST_DIV128
    if (n.mag_.length() <= 2) {
      const std::uint64_t modulus =
          n.mag_.length() == 1 ? n.mag_[0] : (static_cast<std::uint64_t>(n.mag_[0]) << 32) | n.mag_[1];
      auto multiply_mod = [modulus](std::uint64_t lhs, std::uint64_t rhs) {
        const decimal_detail::uint128_words product = decimal_detail::multiply_64x64(lhs, rhs);
        std::uint64_t remainder = 0;
        decimal_detail::divide_128_by_64(product.high, product.low, modulus, &remainder);
        return remainder;
      };
      auto add_mod = [modulus](std::uint64_t lhs, std::uint64_t rhs) {
        return lhs >= modulus - rhs ? lhs - (modulus - rhs) : lhs + rhs;
      };
      auto subtract_mod = [modulus](std::uint64_t lhs, std::uint64_t rhs) {
        return lhs >= rhs ? lhs - rhs : modulus - (rhs - lhs);
      };
      auto halve_mod = [modulus](std::uint64_t value) {
        return (value & 1U) == 0 ? value >> 1 : (value >> 1) + (modulus >> 1) + 1;
      };
      auto from_unsigned = [](std::uint64_t value) {
        if (value <= static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)())) {
          return value_of(static_cast<std::int64_t>(value));
        }
        return bigint(
            1, jarray<std::uint32_t>{static_cast<std::uint32_t>(value >> 32), static_cast<std::uint32_t>(value)});
      };

      const bool negative_d = z < 0;
      const std::uint64_t magnitude_d =
          negative_d ? static_cast<std::uint64_t>(-static_cast<std::int64_t>(z)) : static_cast<std::uint64_t>(z);
      std::uint64_t u = 1;
      std::uint64_t v = 1;
      for (std::int32_t i = k.bit_length() - 2; i >= 0; --i) {
        std::uint64_t u2 = multiply_mod(u, v);
        const std::uint64_t du2 = multiply_mod(multiply_mod(u, u), magnitude_d % modulus);
        std::uint64_t v2 = multiply_mod(v, v);
        v2 = negative_d ? subtract_mod(v2, du2) : add_mod(v2, du2);
        v2 = halve_mod(v2);

        u = u2;
        v = v2;
        if (k.test_bit(i)) {
          u2 = halve_mod(add_mod(u, v));
          const std::uint64_t du = multiply_mod(u, magnitude_d % modulus);
          v2 = negative_d ? subtract_mod(v, du) : add_mod(v, du);
          v2 = halve_mod(v2);
          u = u2;
          v = v2;
        }
      }
      return from_unsigned(u);
    }
#endif

    const bigint d = value_of(z);
    bigint u = ONE;
    bigint v = ONE;

    for (std::int32_t i = k.bit_length() - 2; i >= 0; --i) {
      bigint u2 = u.multiply(v).mod(n);

      bigint v2 = v.square().add(d.multiply(u.square())).mod(n);
      if (v2.test_bit(0)) {
        v2 = v2.subtract(n);
      }
      v2 = v2.shift_right(1);

      u = u2;
      v = v2;
      if (k.test_bit(i)) {
        u2 = u.add(v).mod(n);
        if (u2.test_bit(0)) {
          u2 = u2.subtract(n);
        }
        u2 = u2.shift_right(1);
        v2 = v.add(d.multiply(u)).mod(n);
        if (v2.test_bit(0)) {
          v2 = v2.subtract(n);
        }
        v2 = v2.shift_right(1);

        u = u2;
        v = v2;
      }
    }
    return u;
  }

  // 使用内部随机源对当前正奇数执行指定轮数的 Miller-Rabin 测试
  bool passes_miller_rabin(std::int32_t iterations) const {
    return passes_miller_rabin(iterations, nullptr);
  }

  // 使用可选 random 随机源执行指定轮数的 Miller-Rabin 测试
  // random 为空时使用线程局部随机源
  bool passes_miller_rabin(std::int32_t iterations, std::mt19937_64* random) const {
    const bigint this_minus_one = subtract(ONE);
    bigint m = this_minus_one;
    const std::int32_t a = m.get_lowest_set_bit();
    m = m.shift_right(a);

    static thread_local std::mt19937_64 rnd{std::random_device{}()};
    std::mt19937_64& source = random == nullptr ? rnd : *random;
    for (std::int32_t i = 0; i < iterations; ++i) {
      bigint b;
      do {
        b = random_bigint(bit_length(), source);
      } while (b.compare_to(ONE) <= 0 || b.compare_to(*this) >= 0);

      std::int32_t j = 0;
      bigint z = b.mod_pow(m, *this);
      while (!((j == 0 && z.compare_to(ONE) == 0) || z.compare_to(this_minus_one) == 0)) {
        if ((j > 0 && z.compare_to(ONE) == 0) || ++j == a) {
          return false;
        }
        z = z.square().mod(*this);
      }
    }
    return true;
  }

  // 使用 rnd 生成 num_bits 个随机 bit 的大端 byte 数组
  // 未使用的最高 bit 会被清零,num_bits 必须非负
  static jarray<std::uint8_t> random_bits(std::int32_t num_bits, std::mt19937_64& rnd) {
    if (num_bits < 0) {
      throw std::invalid_argument("num bits must be non-negative");
    }
    const std::int32_t num_bytes = static_cast<std::int32_t>(((static_cast<std::int64_t>((num_bits)) + 7) / 8));
    jarray<std::uint8_t> bytes(num_bytes);
    for (std::int32_t i = 0; i < num_bytes;) {
      std::uint64_t random_word = rnd();
      const std::int32_t end = (std::min)(num_bytes, i + 8);
      while (i < end) {
        bytes[i++] = static_cast<std::uint8_t>(random_word);
        random_word >>= 8;
      }
    }
    if (num_bytes > 0) {
      const std::int32_t excess_bits = 8 * num_bytes - num_bits;
      bytes[0] &= static_cast<std::uint8_t>(((1U << (8 - excess_bits)) - 1U));
    }
    return bytes;
  }

  // 使用 rnd 生成均匀分布在 [0, 2^num_bits) 的规范大端 magnitude
  // num_bits 必须非负,为 0 时返回空数组
  static jarray<std::uint32_t> random_magnitude(std::int32_t num_bits, std::mt19937_64& rnd) {
    if (num_bits < 0) {
      throw std::invalid_argument("num bits must be non-negative");
    }
    const std::int32_t length = (num_bits + 31) / 32;
    jarray<std::uint32_t> magnitude(length);
    for (std::int32_t i = 0; i < length;) {
      const std::uint64_t random_word = rnd();
      magnitude[i++] = static_cast<std::uint32_t>(random_word);
      if (i < length) {
        magnitude[i++] = static_cast<std::uint32_t>(random_word >> 32);
      }
    }
    if (length > 0) {
      const std::int32_t excess_bits = 32 * length - num_bits;
      magnitude[0] &= UINT32_MAX >> excess_bits;
    }
    return trusted_strip_leading_zero_limbs(std::move(magnitude));
  }

  // 使用 rnd 生成均匀分布在 [0, 2^num_bits) 的非负 bigint
  static bigint random_bigint(std::int32_t num_bits, std::mt19937_64& rnd) {
    return bigint(num_bits, rnd);
  }

  // 返回当前值在 radix 进制下的字符串表示
  // 使用 0-9 和小写 a-z 表示数字,负数带前导负号,radix 越界时按十进制处理
  std::string to_string(std::int32_t radix) const {
    if (signum_ == 0) {
      return "0";
    }
    if (radix < MIN_RADIX || radix > MAX_RADIX) {
      radix = 10;
    }

    const bigint abs_value = abs();
    const std::int32_t b = abs_value.bit_length();
    const std::int32_t num_chars =
        static_cast<std::int32_t>((std::floor(b * LOG_TWO / log_cache(radix)) + 1)) + (signum_ < 0 ? 1 : 0);
    std::string sb;
    sb.reserve(static_cast<size_t>((num_chars)));

    if (signum_ < 0) {
      sb.push_back('-');
    }

    to_string_recursive(abs_value, sb, radix, 0);
    return sb;
  }

  // 返回当前值的十进制字符串表示
  std::string to_string() const {
    return to_string(10);
  }

  // 当 num_zeros > 0 时向 buf 追加指定数量的字符 '0'
  static void pad_with_zeros(std::string& buf, std::int32_t num_zeros) {
    static const std::string zeros(NUM_ZEROS, '0');
    while (num_zeros >= NUM_ZEROS) {
      buf.append(zeros);
      num_zeros -= NUM_ZEROS;
    }
    if (num_zeros > 0) {
      buf.append(zeros, 0, static_cast<size_t>((num_zeros)));
    }
  }

  // 将当前非负值按 radix 转换并追加到 buf
  // 用于小规模 magnitude,digits > 0 时在左侧补零到至少指定宽度
  void small_to_string(std::int32_t radix, std::string& buf, std::int32_t digits) const {
    assert(signum_ >= 0);

    if (signum_ == 0) {
      pad_with_zeros(buf, digits);
      return;
    }

    const std::int32_t max_num_digit_groups = (4 * mag_.length() + 6) / 7;
    constexpr std::size_t stack_group_capacity = (4 * SCHOENHAGE_BASE_CONVERSION_THRESHOLD + 6) / 7;
    std::array<std::uint64_t, stack_group_capacity> stack_groups{};
    std::vector<std::uint64_t> heap_groups;
    std::uint64_t* digit_groups = stack_groups.data();
    if (max_num_digit_groups > static_cast<std::int32_t>(stack_group_capacity)) {
      heap_groups.resize(static_cast<size_t>((max_num_digit_groups)));
      digit_groups = heap_groups.data();
    }

    const std::uint64_t divisor = long_radix_magnitude(radix);
    std::int32_t num_groups = 0;
#if DECIMAL_DETAIL_HAS_FAST_DIV128
    jarray<std::uint32_t> magnitude = mag_.clone();
    std::int32_t offset = 0;
    while (offset < magnitude.length()) {
      std::uint64_t remainder = 0;
      for (std::int32_t i = offset; i < magnitude.length(); ++i) {
        const std::uint64_t high = remainder >> 32;
        const std::uint64_t low = (remainder << 32) | magnitude[i];
        magnitude[i] = static_cast<std::uint32_t>(decimal_detail::divide_128_by_64(high, low, divisor, &remainder));
      }
      digit_groups[num_groups++] = remainder;
      while (offset < magnitude.length() && magnitude[offset] == 0) {
        ++offset;
      }
    }
#else
    mutable_bigint tmp(mag_);
    mutable_bigint quotient;
    mutable_bigint remainder;
    while (!tmp.is_zero()) {
      digit_groups[num_groups++] = tmp.divide(divisor, quotient, remainder);
      std::swap(tmp, quotient);
      quotient.reset();
    }
#endif

    std::string s = uint64_to_string(digit_groups[num_groups - 1], radix);
    pad_with_zeros(buf, digits - (static_cast<std::int32_t>((s.size())) + (num_groups - 1) * digits_per_long[radix]));
    buf.append(s);

    for (std::int32_t i = num_groups - 2; i >= 0; --i) {
      s = uint64_to_string(digit_groups[i], radix);
      const std::int32_t num_leading_zeros = digits_per_long[radix] - static_cast<std::int32_t>((s.size()));
      if (num_leading_zeros != 0) {
        pad_with_zeros(buf, num_leading_zeros);
      }
      buf.append(s);
    }
  }

  // 使用 Schoenhage 递归基数转换将非负 u 追加到 sb
  // digits 指定当前递归分段所需的最小输出位数
  static void to_string_recursive(const bigint& u, std::string& sb, std::int32_t radix, std::int32_t digits) {
    assert(u.signum() >= 0);

    if (u.mag_.length() <= SCHOENHAGE_BASE_CONVERSION_THRESHOLD) {
      u.small_to_string(radix, sb, digits);
      return;
    }

    const std::int32_t b = u.bit_length();
    const std::int32_t n =
        static_cast<std::int32_t>((std::round(std::log(b * LOG_TWO / log_cache(radix)) / LOG_TWO - 1.0)));

    const bigint v = get_radix_conversion_cache(radix, n);
    auto results = u.divide_and_remainder(v);

    const std::int32_t expected_digits = 1 << n;
    to_string_recursive(results.first, sb, radix, digits - expected_digits);
    to_string_recursive(results.second, sb, radix, expected_digits);
  }

  // 返回 radix^(2^exponent),并按 radix 和 exponent 缓存计算结果
  static bigint get_radix_conversion_cache(std::int32_t radix, std::int32_t exponent) {
    static std::vector<std::vector<bigint>> power_cache;
    if (power_cache.empty()) {
      power_cache.resize(MAX_RADIX + 1);
      for (std::int32_t i = MIN_RADIX; i <= MAX_RADIX; ++i) {
        power_cache[static_cast<size_t>((i))].push_back(value_of(i));
      }
    }

    std::vector<bigint>& cache_line = power_cache[static_cast<size_t>((radix))];
    if (exponent < static_cast<std::int32_t>((cache_line.size()))) {
      return cache_line[static_cast<size_t>((exponent))];
    }

    const std::int32_t old_length = static_cast<std::int32_t>((cache_line.size()));
    cache_line.resize(static_cast<size_t>((exponent + 1)));
    for (std::int32_t i = old_length; i <= exponent; ++i) {
      cache_line[static_cast<size_t>((i))] = cache_line[static_cast<size_t>((i - 1))].pow(2);
    }
    return cache_line[static_cast<size_t>((exponent))];
  }

  // 返回 log(radix) 的缓存值,用于估算字符串位数和递归切分位置
  static double log_cache(std::int32_t radix) {
    static std::array<double, MAX_RADIX + 1> cache{};
    static bool initialized = false;
    if (!initialized) {
      for (std::int32_t i = MIN_RADIX; i <= MAX_RADIX; ++i) {
        cache[static_cast<size_t>((i))] = std::log(static_cast<double>((i)));
      }
      initialized = true;
    }
    return cache[static_cast<size_t>((radix))];
  }

  // 返回不超过 std::uint64_t 范围的最大 radix 幂
  // 该值用于按多个 radix 数字为一组执行基数转换
  static std::uint64_t long_radix_magnitude(std::int32_t radix) {
    static const std::array<std::uint64_t, 37> values = {0,
                                                         0,
                                                         0x4000000000000000ULL,
                                                         0x383d9170b85ff80bULL,
                                                         0x4000000000000000ULL,
                                                         0x6765c793fa10079dULL,
                                                         0x41c21cb8e1000000ULL,
                                                         0x3642798750226111ULL,
                                                         0x1000000000000000ULL,
                                                         0x12bf307ae81ffd59ULL,
                                                         0x0de0b6b3a7640000ULL,
                                                         0x4d28cb56c33fa539ULL,
                                                         0x1eca170c00000000ULL,
                                                         0x780c7372621bd74dULL,
                                                         0x1e39a5057d810000ULL,
                                                         0x5b27ac993df97701ULL,
                                                         0x1000000000000000ULL,
                                                         0x27b95e997e21d9f1ULL,
                                                         0x5da0e1e53c5c8000ULL,
                                                         0x0b16a458ef403f19ULL,
                                                         0x16bcc41e90000000ULL,
                                                         0x2d04b7fdd9c0ef49ULL,
                                                         0x05658597bcaa24000ULL,
                                                         0x06feb266931a75b7ULL,
                                                         0x0c29e98000000000ULL,
                                                         0x14adf4b7320334b9ULL,
                                                         0x226ed36478bfa000ULL,
                                                         0x383d9170b85ff80bULL,
                                                         0x5a3c23e39c000000ULL,
                                                         0x04e900abb53e6b71ULL,
                                                         0x07600ec618141000ULL,
                                                         0x0aee5720ee830681ULL,
                                                         0x1000000000000000ULL,
                                                         0x172588ad4f5f0981ULL,
                                                         0x211e44f7d02c1000ULL,
                                                         0x2ee56725f06e5c71ULL,
                                                         0x41c21cb8e1000000ULL};
    return values[static_cast<size_t>((radix))];
  }

  // 以正 bigint 返回 long_radix_magnitude(radix)
  static bigint long_radix_value(std::int32_t radix) {
    return value_of(static_cast<std::int64_t>((long_radix_magnitude(radix))));
  }

  // 将无符号 64 位 value 转换为 radix 进制字符串
  // radix 必须位于 [MIN_RADIX, MAX_RADIX]
  static std::string uint64_to_string(std::uint64_t value, std::int32_t radix) {
    static constexpr char chars[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    if (value == 0) {
      return "0";
    }
    std::string out;
    while (value != 0) {
      const std::uint64_t q = value / static_cast<std::uint64_t>((radix));
      const std::uint64_t r = value - q * static_cast<std::uint64_t>((radix));
      out.push_back(chars[static_cast<size_t>((r))]);
      value = q;
    }
    std::reverse(out.begin(), out.end());
    return out;
  }

  // 返回当前值的最短大端二进制补码 byte 数组
  // 数组至少保留一个符号 bit,输出可由对应 byte 数组构造函数无损还原
  jarray<std::uint8_t> to_byte_array() const {
    const std::int32_t byte_len = bit_length() / 8 + 1;
    jarray<std::uint8_t> byte_array(byte_len);
    std::int32_t byte_num = byte_len;
    std::int32_t bytes_copied = 4;
    std::uint32_t next_int = 0;
    std::int32_t int_index = 0;
    while (byte_num > 0) {
      if (bytes_copied == 4) {
        next_int = get_int(int_index++);
        bytes_copied = 1;
      } else {
        next_int >>= 8;
        ++bytes_copied;
      }
      byte_array[--byte_num] = static_cast<std::uint8_t>((next_int));
    }
    return byte_array;
  }

  // 返回 mag_ 的最短大端无符号 byte 序列
  // 不包含符号 byte,数值为 0 时返回空数组
  jarray<std::uint8_t> mag_serialized_form() const {
    const std::int32_t len = mag_.length();
    const std::int32_t bit_len = len == 0 ? 0 : ((len - 1) << 5) + mutable_bigint::bit_length_for_limb(mag_[0]);
    const std::int32_t byte_len = static_cast<std::int32_t>((static_cast<std::uint32_t>((bit_len + 7)) >> 3));
    jarray<std::uint8_t> result(byte_len);

    std::int32_t bytes_copied = 4;
    std::int32_t int_index = len - 1;
    std::uint32_t next_int = 0;
    for (std::int32_t i = byte_len - 1; i >= 0; --i) {
      if (bytes_copied == 4) {
        next_int = mag_[int_index--];
        bytes_copied = 1;
      } else {
        next_int >>= 8;
        ++bytes_copied;
      }
      result[i] = static_cast<std::uint8_t>((next_int));
    }
    return result;
  }

  // 返回当前值二进制补码表示的低 32 bit
  // 高位被截断,结果可能丢失 magnitude 信息或具有不同符号
  std::int32_t to_int() const {
    return static_cast<std::int32_t>((get_int(0)));
  }

  // 将当前值转换为 std::int32_t,无法完整表示时仅保留低 32 bit
  std::int32_t int_value() const {
    return to_int();
  }

  // 返回当前值二进制补码表示的低 64 bit
  // 高位被截断,结果可能丢失 magnitude 信息或具有不同符号
  std::int64_t to_long() const {
    std::uint64_t result = 0;
    for (std::int32_t i = 1; i >= 0; --i) {
      result = (result << 32) | (get_int(i) & 0xffffffffULL);
    }
    return static_cast<std::int64_t>((result));
  }

  // 将当前值转换为 std::int64_t,无法完整表示时仅保留低 64 bit
  std::int64_t long_value() const {
    return to_long();
  }

  // 将当前值转换为 float,按 IEEE 754 nearest-even 规则舍入
  // magnitude 过大时返回具有相同符号的 infinity,有限结果也可能丢失精度
  float to_float() const {
    if (signum_ == 0) {
      return 0.0f;
    }

    constexpr std::int32_t SIGNIFICAND_WIDTH = 24;
    constexpr std::int32_t EXP_BIAS = 127;
    constexpr std::int32_t MAX_EXPONENT = 127;
    constexpr std::uint32_t SIGNIF_BIT_MASK = 0x007fffffU;
    constexpr std::uint32_t SIGN_BIT_MASK = 0x80000000U;

    const std::int32_t exponent = ((mag_.length() - 1) << 5) + mutable_bigint::bit_length_for_limb(mag_[0]) - 1;
    if (exponent < 63) {
      return static_cast<float>((long_value()));
    }
    if (exponent > MAX_EXPONENT) {
      return signum_ > 0 ? std::numeric_limits<float>::infinity() : -std::numeric_limits<float>::infinity();
    }

    const std::int32_t shift = exponent - SIGNIFICAND_WIDTH;
    const std::int32_t n_bits = shift & 0x1f;
    const std::int32_t n_bits2 = 32 - n_bits;

    std::uint32_t twice_signif_floor = 0;
    if (n_bits == 0) {
      twice_signif_floor = mag_[0];
    } else {
      twice_signif_floor = mag_[0] >> n_bits;
      if (twice_signif_floor == 0) {
        twice_signif_floor = (mag_[0] << n_bits2) | (mag_[1] >> n_bits);
      }
    }

    std::uint32_t signif_floor = twice_signif_floor >> 1;
    signif_floor &= SIGNIF_BIT_MASK;

    const bool increment =
        (twice_signif_floor & 1U) != 0 && ((signif_floor & 1U) != 0 || abs().get_lowest_set_bit() < shift);
    const std::uint32_t signif_rounded = increment ? signif_floor + 1U : signif_floor;
    std::uint32_t bits = static_cast<std::uint32_t>((exponent + EXP_BIAS)) << (SIGNIFICAND_WIDTH - 1);
    bits += signif_rounded;
    bits |= static_cast<std::uint32_t>((signum_)) & SIGN_BIT_MASK;

    float result = 0.0f;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
  }

  // 将当前值转换为 float,语义与 to_float() 相同
  float float_value() const {
    return to_float();
  }

  // 将当前值转换为 double,按 IEEE 754 nearest-even 规则舍入
  // magnitude 过大时返回具有相同符号的 infinity,有限结果也可能丢失精度
  double to_double() const {
    if (signum_ == 0) {
      return 0.0;
    }

    constexpr std::int32_t SIGNIFICAND_WIDTH = 53;
    constexpr std::int32_t EXP_BIAS = 1023;
    constexpr std::int32_t MAX_EXPONENT = 1023;
    constexpr std::uint64_t SIGNIF_BIT_MASK = 0x000fffffffffffffULL;
    constexpr std::uint64_t SIGN_BIT_MASK = 0x8000000000000000ULL;

    const std::int32_t exponent = ((mag_.length() - 1) << 5) + mutable_bigint::bit_length_for_limb(mag_[0]) - 1;
    if (exponent < 63) {
      return static_cast<double>((long_value()));
    }
    if (exponent > MAX_EXPONENT) {
      return signum_ > 0 ? std::numeric_limits<double>::infinity() : -std::numeric_limits<double>::infinity();
    }

    const std::int32_t shift = exponent - SIGNIFICAND_WIDTH;
    const std::int32_t n_bits = shift & 0x1f;
    const std::int32_t n_bits2 = 32 - n_bits;

    std::uint32_t high_bits = 0;
    std::uint32_t low_bits = 0;
    if (n_bits == 0) {
      high_bits = mag_[0];
      low_bits = mag_[1];
    } else {
      high_bits = mag_[0] >> n_bits;
      low_bits = (mag_[0] << n_bits2) | (mag_[1] >> n_bits);
      if (high_bits == 0) {
        high_bits = low_bits;
        low_bits = (mag_[1] << n_bits2) | (mag_[2] >> n_bits);
      }
    }

    const std::uint64_t twice_signif_floor = (static_cast<std::uint64_t>((high_bits)) << 32) | low_bits;
    std::uint64_t signif_floor = twice_signif_floor >> 1;
    signif_floor &= SIGNIF_BIT_MASK;

    const bool increment =
        (twice_signif_floor & 1ULL) != 0 && ((signif_floor & 1ULL) != 0 || abs().get_lowest_set_bit() < shift);
    const std::uint64_t signif_rounded = increment ? signif_floor + 1ULL : signif_floor;
    std::uint64_t bits = static_cast<std::uint64_t>((exponent + EXP_BIAS)) << (SIGNIFICAND_WIDTH - 1);
    bits += signif_rounded;
    bits |= static_cast<std::uint64_t>((static_cast<std::int64_t>((signum_)))) & SIGN_BIT_MASK;

    double result = 0.0;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
  }

  // 将当前值转换为 double,语义与 to_double() 相同
  double double_value() const {
    return to_double();
  }

  // 将当前值精确转换为 std::int64_t
  // 超出 std::int64_t 可表示范围时抛出 std::runtime_error
  std::int64_t to_long_exact() const {
    if (mag_.length() <= 2 && bit_length() <= 63) {
      return to_long();
    }
    throw std::runtime_error("out of long range");
  }

  // 将当前值精确转换为 std::int64_t,语义与 to_long_exact() 相同
  std::int64_t long_value_exact() const {
    return to_long_exact();
  }

  // 将当前值精确转换为 std::int32_t
  // 超出 std::int32_t 可表示范围时抛出 std::runtime_error
  std::int32_t to_int_exact() const {
    if (mag_.length() <= 1 && bit_length() <= 31) {
      return to_int();
    }
    throw std::runtime_error("out of int range");
  }

  // 将当前值精确转换为 std::int32_t,语义与 to_int_exact() 相同
  std::int32_t int_value_exact() const {
    return to_int_exact();
  }

  // 将当前值精确转换为 std::int16_t
  // 超出 std::int16_t 可表示范围时抛出 std::runtime_error
  std::int16_t to_short_exact() const {
    if (mag_.length() <= 1 && bit_length() <= 31) {
      const std::int32_t value = to_int();
      if (value >= std::numeric_limits<std::int16_t>::min() && value <= std::numeric_limits<std::int16_t>::max()) {
        return static_cast<std::int16_t>((value));
      }
    }
    throw std::runtime_error("out of short range");
  }

  // 将当前值精确转换为 std::int16_t,语义与 to_short_exact() 相同
  std::int16_t short_value_exact() const {
    return to_short_exact();
  }

  // 将当前值精确转换为 std::int8_t
  // 超出 std::int8_t 可表示范围时抛出 std::runtime_error
  std::int8_t to_byte_exact() const {
    if (mag_.length() <= 1 && bit_length() <= 31) {
      const std::int32_t value = to_int();
      if (value >= std::numeric_limits<std::int8_t>::min() && value <= std::numeric_limits<std::int8_t>::max()) {
        return static_cast<std::int8_t>((value));
      }
    }
    throw std::runtime_error("out of byte range");
  }

  // 将当前值精确转换为 std::int8_t,语义与 to_byte_exact() 相同
  std::int8_t byte_value_exact() const {
    return to_byte_exact();
  }

  // 返回当前绝对值的 mutable_bigint 副本,不保留符号
  mutable_bigint to_mutable() const {
    return mutable_bigint(mag_);
  }

  // 接管 mutable_bigint 的 magnitude 并使用 sign 构造规范 bigint
  // 零 magnitude 始终返回 ZERO,非零时调用方负责保证 sign 为 -1 或 1
  static bigint from_mutable(mutable_bigint&& value, std::int32_t sign) {
    value.normalize();
    if (value.is_zero()) {
      return ZERO;
    }
    value.get_magnitude_array();
    return bigint(sign, std::move(value.value_));
  }

  // 检查 [off, off + len) 是否为 array_len 范围内的有效子区间
  // 任一参数为负或区间越界时抛出 std::out_of_range
  static void check_from_index_size(std::int32_t off, std::int32_t len, std::int32_t array_len) {
    if (off < 0 || len < 0 || off > array_len || len > array_len - off) {
      throw std::out_of_range("index out of bounds");
    }
  }

  // 检查当前 magnitude 是否超过 bigint 支持的最大 bit 长度
  // 超出范围时通过 report_overflow() 抛出异常
  void check_range() const {
    if (mag_.length() > MAX_MAG_LENGTH || (mag_.length() == MAX_MAG_LENGTH && (mag_[0] & 0x80000000U) != 0)) {
      report_overflow();
    }
  }

  // 抛出表示 bigint 超出支持范围的 std::runtime_error
  static void report_overflow() {
    throw std::runtime_error("would overflow supported range");
  }

  // 将 ASCII 数字或字母转换为 radix 进制数字值
  // ch 不是有效数字或数值不小于 radix 时返回 -1
  static std::int32_t digit(char ch, std::int32_t radix) {
    std::int32_t value = -1;
    const unsigned char c = static_cast<unsigned char>((ch));
    if (c >= '0' && c <= '9') {
      value = c - '0';
    } else if (c >= 'a' && c <= 'z') {
      value = c - 'a' + 10;
    } else if (c >= 'A' && c <= 'Z') {
      value = c - 'A' + 10;
    }
    return value >= 0 && value < radix ? value : -1;
  }

  // 将 source[start, end) 解析为一个不超过 std::uint32_t 的 radix 进制数字组
  // 遇到非法字符时抛出 std::invalid_argument
  static std::uint32_t parse_group(const std::string& source, std::int32_t start, std::int32_t end,
                                   std::int32_t radix) {
    std::uint32_t result = 0;
    for (std::int32_t i = start; i < end; ++i) {
      const std::int32_t next = digit(source[static_cast<size_t>((i))], radix);
      if (next < 0) {
        throw std::invalid_argument("illegal digit");
      }
      result = result * static_cast<std::uint32_t>((radix)) + static_cast<std::uint32_t>((next));
    }
    return result;
  }

  // 将 source[start, end) 解析为一个不超过 std::uint32_t 的十进制数字组
  // 遇到非十进制字符时抛出 std::invalid_argument
  static std::uint32_t parse_int_decimal(const std::string& source, std::int32_t start, std::int32_t end) {
    std::int32_t result = digit(source[static_cast<size_t>((start++))], 10);
    if (result == -1) {
      throw std::invalid_argument("illegal digit");
    }
    for (std::int32_t index = start; index < end; ++index) {
      const std::int32_t next_val = digit(source[static_cast<size_t>((index))], 10);
      if (next_val == -1) {
        throw std::invalid_argument("illegal digit");
      }
      result = 10 * result + next_val;
    }
    return static_cast<std::uint32_t>((result));
  }

  // 复制大端 limb 数组并移除所有前导零
  // 输入全为零时返回空数组
  static jarray<std::uint32_t> strip_leading_zero_limbs(const jarray<std::uint32_t>& val) {
    std::int32_t keep = 0;
    while (keep < val.length() && val[keep] == 0) {
      ++keep;
    }
    return val.copy_of_range(keep, val.length());
  }

  // 从可信大端 limb 数组构造规范 magnitude
  // 即使输入已规范化也返回独立副本,以保持 bigint 的不可变值语义
  static jarray<std::uint32_t> trusted_strip_leading_zero_limbs(const jarray<std::uint32_t>& val) {
    std::int32_t keep = 0;
    while (keep < val.length() && val[keep] == 0) {
      ++keep;
    }
    return keep == 0 ? val.clone() : val.copy_of_range(keep, val.length());
  }

  // 从可信大端临时数组构造规范 magnitude
  // 已规范化时直接接管存储,否则返回去除前导零后的子数组
  static jarray<std::uint32_t> trusted_strip_leading_zero_limbs(jarray<std::uint32_t>&& val) {
    std::int32_t keep = 0;
    while (keep < val.length() && val[keep] == 0) {
      ++keep;
    }
    return keep == 0 ? std::move(val) : val.copy_of_range(keep, val.length());
  }

  // 将大端无符号 byte 子数组 a[from, from + len) 转换为规范 magnitude
  // 前导零 byte 被忽略,输入为零时返回空数组
  static jarray<std::uint32_t> strip_leading_zero_bytes(const jarray<std::uint8_t>& a, std::int32_t from,
                                                        std::int32_t len) {
    return strip_leading_zero_bytes(-129, a, from, len);
  }

  // 将大端无符号 byte 子数组转换为规范 magnitude
  // b 表示已读取的首 byte,b < -128 时由函数读取 a[from]
  static jarray<std::uint32_t> strip_leading_zero_bytes(std::int32_t b, const jarray<std::uint8_t>& a,
                                                        std::int32_t from, std::int32_t len) {
    if (len == 0) {
      return jarray<std::uint32_t>();
    }
    const std::int32_t to = from + len;
    if (b < -128) {
      b = static_cast<std::int8_t>((a[from]));
    }
    ++from;

    for (; b == 0 && from < to; b = static_cast<std::int8_t>((a[from++]))) {
    }
    if (b == 0) {
      return jarray<std::uint32_t>();
    }

    jarray<std::uint32_t> result(((to - from) >> 2) + 1);

    std::uint32_t d0 = static_cast<std::uint32_t>((b)) & 0xffU;
    while (((to - from) & 0x3) != 0) {
      d0 = (d0 << 8) | (a[from++] & 0xffU);
    }
    result[0] = d0;

    std::int32_t i = 1;
    while (from < to) {
      const std::uint32_t b0 = a[from++];
      const std::uint32_t b1 = a[from++];
      const std::uint32_t b2 = a[from++];
      const std::uint32_t b3 = a[from++];
      result[i++] = (b0 << 24) | ((b1 & 0xffU) << 16) | ((b2 & 0xffU) << 8) | (b3 & 0xffU);
    }
    return result;
  }

  // 将负数的大端二进制补码 byte 子数组转换为规范的绝对值 magnitude
  static jarray<std::uint32_t> make_positive_bytes(const jarray<std::uint8_t>& a, std::int32_t from, std::int32_t len) {
    return make_positive_bytes(static_cast<std::int8_t>((a[from])), a, from, len);
  }

  // 将负数的大端二进制补码 byte 子数组转换为规范的绝对值 magnitude
  // b 是调用方已读取并完成符号扩展的 a[from]
  static jarray<std::uint32_t> make_positive_bytes(std::int32_t b, const jarray<std::uint8_t>& a, std::int32_t from,
                                                   std::int32_t len) {
    const std::int32_t to = from + len;
    ++from;

    for (; b == -1 && from < to; b = static_cast<std::int8_t>((a[from++]))) {
    }

    std::uint32_t d0 = (0xffffffffU << 8) | (static_cast<std::uint32_t>((b)) & 0xffU);
    while (((to - from) & 0x3) != 0) {
      b = static_cast<std::int8_t>((a[from++]));
      d0 = (d0 << 8) | (static_cast<std::uint32_t>((b)) & 0xffU);
    }
    const std::int32_t f = from;

    for (; b == 0 && from < to; b = static_cast<std::int8_t>((a[from++]))) {
    }

    std::uint32_t d = static_cast<std::uint32_t>((b)) & 0xffU;
    while (((to - from) & 0x3) != 0) {
      d = (d << 8) | (a[from++] & 0xffU);
    }

    const std::int32_t c =
        ((to - from) | static_cast<std::int32_t>((d0)) | static_cast<std::int32_t>((d))) == 0 ? 1 : 0;
    jarray<std::uint32_t> result(c + 1 + ((to - f) >> 2));
    result[0] = c == 0 ? d0 : 0xffffffffU;

    std::int32_t i = result.length() - ((to - from) >> 2);
    if (i > 1) {
      result[i - 1] = d;
    }

    while (from < to) {
      const std::uint32_t b0 = a[from++];
      const std::uint32_t b1 = a[from++];
      const std::uint32_t b2 = a[from++];
      const std::uint32_t b3 = a[from++];
      result[i++] = (b0 << 24) | ((b1 & 0xffU) << 16) | ((b2 & 0xffU) << 8) | (b3 & 0xffU);
    }

    while (--i >= 0 && result[i] == 0) {
    }
    result[i] = std::uint32_t(0U - result[i]);
    while (--i >= 0) {
      result[i] = ~result[i];
    }
    return result;
  }

  // 将负数的大端二进制补码 limb 数组转换为规范的绝对值 magnitude
  static jarray<std::uint32_t> make_positive_limbs(const jarray<std::uint32_t>& a) {
    std::int32_t keep = 0;
    std::int32_t j = 0;

    for (keep = 0; keep < a.length() && a[keep] == 0xffffffffU; ++keep) {
    }

    for (j = keep; j < a.length() && a[j] == 0; ++j) {
    }
    const std::int32_t extra_int = j == a.length() ? 1 : 0;
    jarray<std::uint32_t> result(a.length() - keep + extra_int);

    for (std::int32_t i = keep; i < a.length(); ++i) {
      result[i - keep + extra_int] = ~a[i];
    }

    for (std::int32_t i = result.length() - 1; ++result[i] == 0; --i) {
    }

    return result;
  }

  // 原地计算 x = x * y + z,其中 x 是大端无符号 magnitude
  // 调用方必须保证结果能够放入 x 的现有长度
  static void destructive_mul_add(jarray<std::uint32_t>& x, std::uint32_t y, std::uint32_t z) {
    const std::uint64_t ylong = y & 0xffffffffULL;
    const std::uint64_t zlong = z & 0xffffffffULL;
    const std::int32_t len = x.length();
    std::uint64_t carry = 0;
    for (std::int32_t i = len - 1; i >= 0; --i) {
      const std::uint64_t product = ylong * (x[i] & 0xffffffffULL) + carry;
      x[i] = static_cast<std::uint32_t>((product));
      carry = product >> 32;
    }

    std::uint64_t sum = (x[len - 1] & 0xffffffffULL) + zlong;
    x[len - 1] = static_cast<std::uint32_t>((sum));
    carry = sum >> 32;
    for (std::int32_t i = len - 2; i >= 0; --i) {
      sum = (x[i] & 0xffffffffULL) + carry;
      x[i] = static_cast<std::uint32_t>((sum));
      carry = sum >> 32;
    }
  }

  // 将非负 magnitude x 乘以单 limb y,并使用 sign 构造结果
  // y == 0 时返回 ZERO
  static bigint multiply_by_int(const jarray<std::uint32_t>& x, std::uint32_t y, std::int32_t sign) {
    if (decimal_detail::population_count(y) == 1) {
      return bigint(sign, shift_left_mag(x, mutable_bigint::number_of_trailing_zeros(y)));
    }

    const std::int32_t xlen = x.length();
    jarray<std::uint32_t> rmag(xlen + 1);
    std::uint64_t carry = 0;
    const std::uint64_t yl = y & 0xffffffffULL;
    std::int32_t rstart = rmag.length() - 1;
    for (std::int32_t i = xlen - 1; i >= 0; --i) {
      const std::uint64_t product = (x[i] & 0xffffffffULL) * yl + carry;
      rmag[rstart--] = static_cast<std::uint32_t>((product));
      carry = product >> 32;
    }
    if (carry == 0) {
      rmag = rmag.copy_of_range(1, rmag.length());
    } else {
      rmag[rstart] = static_cast<std::uint32_t>((carry));
    }
    return bigint(sign, std::move(rmag));
  }

  // 使用 O(xlen * ylen) 标准乘法计算两个大端 magnitude 的乘积
  // z 非空时作为可复用结果缓冲区
  static jarray<std::uint32_t> multiply_to_len(const jarray<std::uint32_t>& x, std::int32_t xlen,
                                               const jarray<std::uint32_t>& y, std::int32_t ylen,
                                               jarray<std::uint32_t>* z) {
    multiply_to_len_check(x, xlen);
    multiply_to_len_check(y, ylen);
    return impl_multiply_to_len(x, xlen, y, ylen, z);
  }

  // multiply_to_len() 的无重复参数检查实现
  static jarray<std::uint32_t> impl_multiply_to_len(const jarray<std::uint32_t>& x, std::int32_t xlen,
                                                    const jarray<std::uint32_t>& y, std::int32_t ylen,
                                                    jarray<std::uint32_t>* z) {
    jarray<std::uint32_t> local;
    if (z == nullptr || z->length() < xlen + ylen) {
      local = jarray<std::uint32_t>(xlen + ylen);
      z = &local;
    }
    impl_multiply_to_len_into(x, xlen, y, ylen, *z);
    return z == &local ? std::move(local) : z->clone();
  }

  // 使用标准多 limb 乘法将乘积直接写入 z
  // z 必须至少包含 xlen + ylen 个 limb
  static void impl_multiply_to_len_into(const jarray<std::uint32_t>& x, std::int32_t xlen,
                                        const jarray<std::uint32_t>& y, std::int32_t ylen, jarray<std::uint32_t>& z) {
    const std::int32_t required = xlen + ylen;
    if (z.length() < required) {
      z.alloc(required);
    }
    const std::int32_t xstart = xlen - 1;
    const std::int32_t ystart = ylen - 1;

    std::uint64_t carry = 0;
    for (std::int32_t j = ystart, k = ystart + 1 + xstart; j >= 0; --j, --k) {
      const std::uint64_t product = (y[j] & 0xffffffffULL) * (x[xstart] & 0xffffffffULL) + carry;
      z[k] = static_cast<std::uint32_t>((product));
      carry = product >> 32;
    }
    z[xstart] = static_cast<std::uint32_t>((carry));

    for (std::int32_t i = xstart - 1; i >= 0; --i) {
      if (x[i] == 0) {
        z[i] = 0;
        continue;
      }
      carry = 0;
      for (std::int32_t j = ystart, k = ystart + 1 + i; j >= 0; --j, --k) {
        const std::uint64_t product = (y[j] & 0xffffffffULL) * (x[i] & 0xffffffffULL) + (z[k] & 0xffffffffULL) + carry;
        z[k] = static_cast<std::uint32_t>((product));
        carry = product >> 32;
      }
      z[i] = static_cast<std::uint32_t>((carry));
    }
  }

  // 检查标准乘法使用的数组长度参数
  // length 超过 array.length() 时抛出 std::out_of_range,非正长度直接接受
  static void multiply_to_len_check(const jarray<std::uint32_t>& array, std::int32_t length) {
    if (length <= 0) {
      return;
    }
    if (length > array.length()) {
      throw std::out_of_range("multiply to len length out of range");
    }
  }

  // 返回大端非负 magnitude 左移 n bit 后的规范数组
  // n 按无符号移位距离处理
  static jarray<std::uint32_t> shift_left_mag(const jarray<std::uint32_t>& mag, std::int32_t n) {
    const std::int32_t n_ints = static_cast<std::int32_t>((static_cast<std::uint32_t>((n)) >> 5));
    const std::int32_t n_bits = n & 0x1f;
    const std::int32_t mag_len = mag.length();
    jarray<std::uint32_t> new_mag;

    if (n_bits == 0) {
      new_mag = jarray<std::uint32_t>(mag_len + n_ints);
      jarray_copy(mag, 0, new_mag, 0, mag_len);
    } else {
      std::int32_t i = 0;
      const std::int32_t n_bits2 = 32 - n_bits;
      const std::uint32_t high_bits = mag[0] >> n_bits2;
      if (high_bits != 0) {
        new_mag = jarray<std::uint32_t>(mag_len + n_ints + 1);
        new_mag[i++] = high_bits;
      } else {
        new_mag = jarray<std::uint32_t>(mag_len + n_ints);
      }
      const std::int32_t num_iter = mag_len - 1;
      shift_left_impl_worker(new_mag, mag, i, n_bits, num_iter);
      new_mag[num_iter + i] = mag[num_iter] << n_bits;
    }
    return new_mag;
  }

  // 将 old_arr 相邻 limb 按 shift_count 合并并写入 new_arr
  // 用于 shift_left_mag(),调用方负责保证数组区间和移位范围有效
  static void shift_left_impl_worker(jarray<std::uint32_t>& new_arr, const jarray<std::uint32_t>& old_arr,
                                     std::int32_t new_idx, std::int32_t shift_count, std::int32_t num_iter) {
    const std::int32_t shift_count_right = 32 - shift_count;
    std::int32_t old_idx = 0;
    while (old_idx < num_iter) {
      const std::uint32_t high = old_arr[old_idx++];
      const std::uint32_t low = old_arr[old_idx];
      new_arr[new_idx++] = (high << shift_count) | (low >> shift_count_right);
    }
  }

  // 将 old_arr 相邻 limb 按 shift_count 合并并写入 new_arr
  // 用于 shift_right_impl(),调用方负责保证数组区间和移位范围有效
  static void shift_right_impl_worker(jarray<std::uint32_t>& new_arr, const jarray<std::uint32_t>& old_arr,
                                      std::int32_t new_idx, std::int32_t shift_count, std::int32_t num_iter) {
    const std::int32_t shift_count_left = 32 - shift_count;
    std::int32_t idx = num_iter;
    std::int32_t nidx = new_idx == 0 ? num_iter - 1 : num_iter;
    while (nidx >= new_idx) {
      const std::uint32_t low = old_arr[idx--] >> shift_count;
      const std::uint32_t high = old_arr[idx] << shift_count_left;
      new_arr[nidx--] = low | high;
    }
  }

  // 将大端无符号 magnitude 加 1 并返回结果
  // 发生最高位进位时扩展一个 limb
  static jarray<std::uint32_t> java_increment(jarray<std::uint32_t> val) {
    std::uint32_t last_sum = 0;
    for (std::int32_t i = val.length() - 1; i >= 0 && last_sum == 0; --i) {
      val[i] += 1;
      last_sum = val[i];
    }
    if (last_sum == 0) {
      val = jarray<std::uint32_t>(val.length() + 1);
      val[0] = 1;
    }
    return val;
  }

  // 返回 val 前 len 个大端 limb 的 bit 长度
  // 调用方必须保证 len == 0 或 val[0] 非零
  static std::int32_t bit_length(const jarray<std::uint32_t>& val, std::int32_t len) {
    if (len == 0) {
      return 0;
    }
    return ((len - 1) << 5) + mutable_bigint::bit_length_for_limb(val[0]);
  }

  // 返回 this^2,根据 magnitude 规模选择标准、Karatsuba 或 Toom-Cook 平方
  bigint square() const {
    return square(false);
  }

  // square() 的递归实现入口
  // is_recursion 为 true 时跳过仅需在最外层执行的部分范围检查
  bigint square(bool is_recursion) const {
    if (signum_ == 0) {
      return ZERO;
    }
    const std::int32_t len = mag_.length();

    if (len < KARATSUBA_SQUARE_THRESHOLD) {
      jarray<std::uint32_t> z = square_to_len(mag_, len, nullptr);
      return bigint(1, trusted_strip_leading_zero_limbs(std::move(z)));
    }
    if (len < TOOM_COOK_SQUARE_THRESHOLD) {
      return square_karatsuba();
    }

    if (!is_recursion) {
      if (bit_length(mag_, mag_.length()) > 16LL * MAX_MAG_LENGTH) {
        report_overflow();
      }
    }
    return square_toom_cook3();
  }

  // 使用标准 O(len^2) 算法计算大端 magnitude x 的平方
  // z 非空时作为可复用结果缓冲区
  static jarray<std::uint32_t> square_to_len(const jarray<std::uint32_t>& x, std::int32_t len,
                                             jarray<std::uint32_t>* z) {
    const std::int32_t zlen = len << 1;
    jarray<std::uint32_t> local;
    if (z == nullptr || z->length() < zlen) {
      local = jarray<std::uint32_t>(zlen);
      z = &local;
    }
    impl_square_to_len_checks(x, len, *z, zlen);
    return impl_square_to_len(x, len, *z, zlen);
  }

  // 检查标准平方使用的输入长度和可选结果缓冲区
  // 参数越界或 z 容量不足时抛出 std::invalid_argument
  static void impl_square_to_len_checks(const jarray<std::uint32_t>& x, std::int32_t len,
                                        const jarray<std::uint32_t>& z, std::int32_t zlen) {
    if (len < 1) {
      throw std::invalid_argument("invalid input length");
    }
    if (len > x.length()) {
      throw std::invalid_argument("input length out of bound");
    }
    if (len * 2 > z.length()) {
      throw std::invalid_argument("input length out of bound");
    }
    if (zlen < 1) {
      throw std::invalid_argument("invalid input length");
    }
    if (zlen > z.length()) {
      throw std::invalid_argument("input length out of bound");
    }
  }

  // 使用 Colin Plumb 的多精度平方算法计算 x 前 len 个 limb 的平方
  static jarray<std::uint32_t> impl_square_to_len(const jarray<std::uint32_t>& x, std::int32_t len,
                                                  jarray<std::uint32_t>& z, std::int32_t zlen) {
    impl_square_to_len_into(x, len, z, zlen);
    return z.clone();
  }

  // 使用标准多 limb 平方将结果直接写入 z
  // z 必须至少包含 2 * len 个 limb
  static void impl_square_to_len_into(const jarray<std::uint32_t>& x, std::int32_t len, jarray<std::uint32_t>& z,
                                      std::int32_t zlen) {
    std::uint32_t last_product_low_word = 0;
    for (std::int32_t j = 0, i = 0; j < len; ++j) {
      const std::uint64_t piece = x[j] & 0xffffffffULL;
      const std::uint64_t product = piece * piece;
      z[i++] = (last_product_low_word << 31) | static_cast<std::uint32_t>((product >> 33));
      z[i++] = static_cast<std::uint32_t>((product >> 1));
      last_product_low_word = static_cast<std::uint32_t>((product));
    }

    for (std::int32_t i = len, offset = 1; i > 0; --i, offset += 2) {
      const std::uint32_t t0 = x[i - 1];
      if (t0 == 0) {
        continue;
      }
      const std::uint32_t t = mul_add(z, x, offset, i - 1, t0);
      add_one(z, offset - 1, i, t);
    }

    primitive_left_shift(z, zlen, 1);
    z[zlen - 1] |= x[len - 1] & 1U;
  }

  // 将大端数组 a 的前 len 个 limb 左移 n bit 并返回结果
  // n 必须非负,结果会按整 limb 位移和最高位进位扩展
  static jarray<std::uint32_t> left_shift(jarray<std::uint32_t> a, std::int32_t len, std::int32_t n) {
    const std::int32_t n_ints = static_cast<std::int32_t>((static_cast<std::uint32_t>((n)) >> 5));
    const std::int32_t n_bits = n & 0x1f;
    const std::int32_t bits_in_high_word = mutable_bigint::bit_length_for_limb(a[0]);

    if (n <= 32 - bits_in_high_word) {
      primitive_left_shift(a, len, n_bits);
      return a;
    }
    if (n_bits <= 32 - bits_in_high_word) {
      jarray<std::uint32_t> result(n_ints + len);
      jarray_copy(a, 0, result, 0, len);
      primitive_left_shift(result, result.length(), n_bits);
      return result;
    }

    jarray<std::uint32_t> result(n_ints + len + 1);
    jarray_copy(a, 0, result, 0, len);
    primitive_right_shift(result, result.length(), 32 - n_bits);
    return result;
  }

  // 将大端数组 a 的前 len 个 limb 原地右移 n bit
  // 调用方必须保证 0 < n < 32
  static void primitive_right_shift(jarray<std::uint32_t>& a, std::int32_t len, std::int32_t n) {
    shift_right_impl_worker(a, a, 1, n, len - 1);
    a[0] >>= n;
  }

  // 将大端数组 a 的前 len 个 limb 原地左移 n bit
  // 调用方必须保证 0 <= n < 32 且不需要额外最高 limb
  static void primitive_left_shift(jarray<std::uint32_t>& a, std::int32_t len, std::int32_t n) {
    if (len == 0 || n == 0) {
      return;
    }
    shift_left_impl_worker(a, a, 0, n, len - 1);
    a[len - 1] <<= n;
  }

  // 将 in 的最低 len 个 limb 乘以单 limb k,并累加到 out 的指定低位区间
  // 原地更新 out 并返回最高进位
  static std::uint32_t mul_add(jarray<std::uint32_t>& out, const jarray<std::uint32_t>& in, std::int32_t offset,
                               std::int32_t len, std::uint32_t k) {
    impl_mul_add_check(out, in, offset, len);
    return impl_mul_add(out, in, offset, len, k);
  }

  // 检查 mul_add() 的输入长度、输出偏移和数组容量
  // 任一范围无效时抛出 std::invalid_argument
  static void impl_mul_add_check(const jarray<std::uint32_t>& out, const jarray<std::uint32_t>& in, std::int32_t offset,
                                 std::int32_t len) {
    if (len > in.length()) {
      throw std::invalid_argument("input length is out of bound");
    }
    if (offset < 0) {
      throw std::invalid_argument("input offset is invalid");
    }
    if (offset > out.length() - 1) {
      throw std::invalid_argument("input offset is out of bound");
    }
    if (len > out.length() - offset) {
      throw std::invalid_argument("input len is out of bound");
    }
  }

  // mul_add() 的无重复参数检查实现,原地更新 out 并返回最高进位
  static std::uint32_t impl_mul_add(jarray<std::uint32_t>& out, const jarray<std::uint32_t>& in, std::int32_t offset,
                                    std::int32_t len, std::uint32_t k) {
    const std::uint64_t k_long = k & 0xffffffffULL;
    std::uint64_t carry = 0;

    offset = out.length() - offset - 1;
    for (std::int32_t j = len - 1; j >= 0; --j) {
      const std::uint64_t product = (in[j] & 0xffffffffULL) * k_long + (out[offset] & 0xffffffffULL) + carry;
      out[offset--] = static_cast<std::uint32_t>((product));
      carry = product >> 32;
    }
    return static_cast<std::uint32_t>((carry));
  }

  // 从 a 的指定低位片段开始加上 carry 并向高位传播
  // 返回传播出目标片段的最终进位
  static std::uint32_t add_one(jarray<std::uint32_t>& a, std::int32_t offset, std::int32_t mlen, std::uint32_t carry) {
    offset = a.length() - 1 - mlen - offset;
    std::uint64_t t = (a[offset] & 0xffffffffULL) + (carry & 0xffffffffULL);

    a[offset] = static_cast<std::uint32_t>((t));
    if ((t >> 32) == 0) {
      return 0;
    }
    while (--mlen >= 0) {
      if (--offset < 0) {
        return 1;
      }
      ++a[offset];
      if (a[offset] != 0) {
        return 0;
      }
    }
    return 1;
  }

  // 返回 a * b * R^(-1) mod n 的 Montgomery 乘积
  // a、b 和 n 使用等长大端 magnitude,product 可作为可复用工作数组
  static jarray<std::uint32_t> montgomery_multiply(const jarray<std::uint32_t>& a, const jarray<std::uint32_t>& b,
                                                   const jarray<std::uint32_t>& n, std::int32_t len, std::uint64_t inv,
                                                   jarray<std::uint32_t>* product) {
    impl_montgomery_multiply_checks(a, b, n, len, product);
    if (len > MONTGOMERY_INTRINSIC_THRESHOLD) {
      jarray<std::uint32_t> prod = multiply_to_len(a, len, b, len, product);
      return mont_reduce(prod, n, len, static_cast<std::uint32_t>((inv)));
    }
    jarray<std::uint32_t> materialized = materialize(product, len);
    return impl_montgomery_multiply(a, b, n, len, inv, materialized);
  }

  // 返回 a^2 * R^(-1) mod n 的 Montgomery 平方
  // a 和 n 使用等长大端 magnitude,product 可作为可复用工作数组
  static jarray<std::uint32_t> montgomery_square(const jarray<std::uint32_t>& a, const jarray<std::uint32_t>& n,
                                                 std::int32_t len, std::uint64_t inv, jarray<std::uint32_t>* product) {
    impl_montgomery_multiply_checks(a, a, n, len, product);
    if (len > MONTGOMERY_INTRINSIC_THRESHOLD) {
      jarray<std::uint32_t> prod = square_to_len(a, len, product);
      return mont_reduce(prod, n, len, static_cast<std::uint32_t>((inv)));
    }
    jarray<std::uint32_t> materialized = materialize(product, len);
    return impl_montgomery_square(a, n, len, inv, materialized);
  }

  // 计算 Montgomery 乘积并将约减结果写入 product 的前 len 个 limb
  // product 会调整为 2 * len 个 limb 以容纳乘法工作值
  static void montgomery_multiply_into(const jarray<std::uint32_t>& a, const jarray<std::uint32_t>& b,
                                       const jarray<std::uint32_t>& n, std::int32_t len, std::uint64_t inv,
                                       jarray<std::uint32_t>& product) {
    if (product.length() != (len << 1)) {
      product.alloc(len << 1);
    }
    impl_montgomery_multiply_checks(a, b, n, len, &product);
    impl_multiply_to_len_into(a, len, b, len, product);
    mont_reduce_in_place(product, n, len, static_cast<std::uint32_t>(inv));
  }

  // 计算 Montgomery 平方并将约减结果写入 product 的前 len 个 limb
  // product 会调整为 2 * len 个 limb 以容纳平方工作值
  static void montgomery_square_into(const jarray<std::uint32_t>& a, const jarray<std::uint32_t>& n, std::int32_t len,
                                     std::uint64_t inv, jarray<std::uint32_t>& product) {
    if (product.length() != (len << 1)) {
      product.alloc(len << 1);
    }
    impl_montgomery_multiply_checks(a, a, n, len, &product);
    const std::int32_t product_len = len << 1;
    impl_square_to_len_into(a, len, product, product_len);
    mont_reduce_in_place(product, n, len, static_cast<std::uint32_t>(inv));
  }

  // 检查 Montgomery 运算的数组长度、偶数 limb 数和缓冲区容量
  // 参数无效时抛出 std::invalid_argument
  static void impl_montgomery_multiply_checks(const jarray<std::uint32_t>& a, const jarray<std::uint32_t>& b,
                                              const jarray<std::uint32_t>& n, std::int32_t len,
                                              const jarray<std::uint32_t>* product) {
    if (len % 2 != 0) {
      throw std::invalid_argument("input array length must be even");
    }
    if (len < 1) {
      throw std::invalid_argument("invalid input length");
    }
    if (len > a.length() || len > b.length() || len > n.length() || (product != nullptr && len > product->length())) {
      throw std::invalid_argument("input array length out of bound");
    }
  }

  // 返回容量至少为 len 的 Montgomery 工作数组
  // z 为空或容量不足时分配新数组,否则复制 z 的现有内容
  static jarray<std::uint32_t> materialize(const jarray<std::uint32_t>* z, std::int32_t len) {
    if (z == nullptr || z->length() < len) {
      return jarray<std::uint32_t>(len);
    }
    return z->clone();
  }

  // 使用标准乘法和 Montgomery reduction 实现 a * b * R^(-1) mod n
  static jarray<std::uint32_t> impl_montgomery_multiply(const jarray<std::uint32_t>& a, const jarray<std::uint32_t>& b,
                                                        const jarray<std::uint32_t>& n, std::int32_t len,
                                                        std::uint64_t inv, jarray<std::uint32_t>& product) {
    product = multiply_to_len(a, len, b, len, &product);
    return mont_reduce(product, n, len, static_cast<std::uint32_t>((inv)));
  }

  // 使用标准平方和 Montgomery reduction 实现 a^2 * R^(-1) mod n
  static jarray<std::uint32_t> impl_montgomery_square(const jarray<std::uint32_t>& a, const jarray<std::uint32_t>& n,
                                                      std::int32_t len, std::uint64_t inv,
                                                      jarray<std::uint32_t>& product) {
    product = square_to_len(a, len, &product);
    return mont_reduce(product, n, len, static_cast<std::uint32_t>((inv)));
  }

  // 将 Montgomery 工作值 n 约减为 n * R^(-1) mod mod
  // 结果写在返回数组的前 mlen 个 limb,数组保留原工作区长度
  static jarray<std::uint32_t> mont_reduce(jarray<std::uint32_t> n, const jarray<std::uint32_t>& mod, std::int32_t mlen,
                                           std::uint32_t inv) {
    mont_reduce_in_place(n, mod, mlen, inv);
    return n;
  }

  // 对工作数组 n 原地执行 Montgomery reduction
  // n 必须有 2 * mlen 个 limb,mod 必须有 mlen 个 limb
  static void mont_reduce_in_place(jarray<std::uint32_t>& n, const jarray<std::uint32_t>& mod, std::int32_t mlen,
                                   std::uint32_t inv) {
    std::int32_t c = 0;
    std::int32_t len = mlen;
    std::int32_t offset = 0;

    do {
      const std::uint32_t n_end = n[n.length() - 1 - offset];
      const std::uint32_t carry = mul_add(n, mod, offset, mlen, inv * n_end);
      c += static_cast<std::int32_t>((add_one(n, offset, mlen, carry)));
      ++offset;
    } while (--len > 0);

    while (c > 0) {
      c += sub_n(n, mod, mlen);
    }

    while (int_array_cmp_to_len(n, mod, mlen) >= 0) {
      sub_n(n, mod, mlen);
    }
  }

  // 按无符号数值比较两个大端数组的前 len 个 limb
  // arg1 小于、等于或大于 arg2 时分别返回 -1、0 或 1
  static std::int32_t int_array_cmp_to_len(const jarray<std::uint32_t>& arg1, const jarray<std::uint32_t>& arg2,
                                           std::int32_t len) {
    for (std::int32_t i = 0; i < len; ++i) {
      const std::uint64_t b1 = arg1[i] & 0xffffffffULL;
      const std::uint64_t b2 = arg2[i] & 0xffffffffULL;
      if (b1 < b2) {
        return -1;
      }
      if (b1 > b2) {
        return 1;
      }
    }
    return 0;
  }

  // 原地计算 a[0, len) -= b[0, len)
  // 两个数组按大端无符号 magnitude 解释,返回最终借位
  static std::int32_t sub_n(jarray<std::uint32_t>& a, const jarray<std::uint32_t>& b, std::int32_t len) {
    std::int64_t sum = 0;
    while (--len >= 0) {
      sum = static_cast<std::int64_t>((a[len] & 0xffffffffULL)) - static_cast<std::int64_t>((b[len] & 0xffffffffULL)) +
            (sum >> 32);
      a[len] = static_cast<std::uint32_t>((sum));
    }
    return static_cast<std::int32_t>((sum >> 32));
  }

  // 使用 Karatsuba 分治算法返回当前值的平方
  // 调用方在 magnitude 达到 KARATSUBA_SQUARE_THRESHOLD 后选择此路径
  bigint square_karatsuba() const {
    const std::int32_t half = (mag_.length() + 1) / 2;
    const bigint xl = get_lower(half);
    const bigint xh = get_upper(half);

    const bigint xhs = xh.square();
    const bigint xls = xl.square();

    return xhs.shift_left(half * 32).add(xl.add(xh).square().subtract(xhs.add(xls))).shift_left(half * 32).add(xls);
  }

  // 使用 Toom-Cook 3 路分治与插值算法返回当前值的平方
  // 调用方在 magnitude 达到 TOOM_COOK_SQUARE_THRESHOLD 后选择此路径
  bigint square_toom_cook3() const {
    const std::int32_t len = mag_.length();
    const std::int32_t k = (len + 2) / 3;
    const std::int32_t r = len - 2 * k;

    const bigint a2 = get_toom_slice(k, r, 0, len);
    const bigint a1 = get_toom_slice(k, r, 1, len);
    const bigint a0 = get_toom_slice(k, r, 2, len);

    const bigint v0 = a0.square(true);
    bigint da1 = a2.add(a0);
    const bigint vm1 = da1.subtract(a1).square(true);
    da1 = da1.add(a1);
    const bigint v1 = da1.square(true);
    const bigint vinf = a2.square(true);
    const bigint v2 = da1.add(a2).shift_left(1).subtract(a0).square(true);

    bigint t2 = v2.subtract(vm1).exact_divide_by3();
    bigint tm1 = v1.subtract(vm1).shift_right(1);
    bigint t1 = v1.subtract(v0);
    t2 = t2.subtract(t1).shift_right(1);
    t1 = t1.subtract(tm1).subtract(vinf);
    t2 = t2.subtract(vinf.shift_left(1));
    tm1 = tm1.subtract(t2);

    const std::int32_t ss = k * 32;
    return vinf.shift_left(ss).add(t2).shift_left(ss).add(t1).shift_left(ss).add(tm1).shift_left(ss).add(v0);
  }

  // 使用 Karatsuba 分治算法返回 x * y
  // 输入可以带符号,结果符号由两个操作数共同确定
  static bigint multiply_karatsuba(const bigint& x, const bigint& y) {
    const std::int32_t xlen = x.mag_.length();
    const std::int32_t ylen = y.mag_.length();

    const std::int32_t half = (std::max(xlen, ylen) + 1) / 2;

    const bigint xl = x.get_lower(half);
    const bigint xh = x.get_upper(half);
    const bigint yl = y.get_lower(half);
    const bigint yh = y.get_upper(half);

    const bigint p1 = xh.multiply(yh);
    const bigint p2 = xl.multiply(yl);
    const bigint p3 = xh.add(xl).multiply(yh.add(yl));

    bigint result = p1.shift_left(32 * half).add(p3.subtract(p1).subtract(p2)).shift_left(32 * half).add(p2);

    if (x.signum_ != y.signum_) {
      return result.negate();
    }
    return result;
  }

  // 使用 Toom-Cook 3 路分治与插值算法返回 a * b
  // 输入可以带符号,结果符号由两个操作数共同确定
  static bigint multiply_toom_cook3(const bigint& a, const bigint& b) {
    const std::int32_t alen = a.mag_.length();
    const std::int32_t blen = b.mag_.length();
    const std::int32_t largest = std::max(alen, blen);

    const std::int32_t k = (largest + 2) / 3;
    const std::int32_t r = largest - 2 * k;

    const bigint a2 = a.get_toom_slice(k, r, 0, largest);
    const bigint a1 = a.get_toom_slice(k, r, 1, largest);
    const bigint a0 = a.get_toom_slice(k, r, 2, largest);
    const bigint b2 = b.get_toom_slice(k, r, 0, largest);
    const bigint b1 = b.get_toom_slice(k, r, 1, largest);
    const bigint b0 = b.get_toom_slice(k, r, 2, largest);

    const bigint v0 = a0.multiply(b0, true);
    bigint da1 = a2.add(a0);
    bigint db1 = b2.add(b0);
    const bigint vm1 = da1.subtract(a1).multiply(db1.subtract(b1), true);
    da1 = da1.add(a1);
    db1 = db1.add(b1);
    const bigint v1 = da1.multiply(db1, true);
    const bigint v2 = da1.add(a2).shift_left(1).subtract(a0).multiply(db1.add(b2).shift_left(1).subtract(b0), true);
    const bigint vinf = a2.multiply(b2, true);

    bigint t2 = v2.subtract(vm1).exact_divide_by3();
    bigint tm1 = v1.subtract(vm1).shift_right(1);
    bigint t1 = v1.subtract(v0);
    t2 = t2.subtract(t1).shift_right(1);
    t1 = t1.subtract(tm1).subtract(vinf);
    t2 = t2.subtract(vinf.shift_left(1));
    tm1 = tm1.subtract(t2);

    const std::int32_t ss = k * 32;
    bigint result = vinf.shift_left(ss).add(t2).shift_left(ss).add(t1).shift_left(ss).add(tm1).shift_left(ss).add(v0);

    if (a.signum_ != b.signum_) {
      return result.negate();
    }
    return result;
  }

  // 返回大端非负 magnitude x 与无符号 64 位 val 的和
  // 输入保持不变,最高位进位时结果增加一个 limb
  static jarray<std::uint32_t> add_magnitude(const jarray<std::uint32_t>& x, std::uint64_t val) {
    std::int32_t x_index = x.length();
    jarray<std::uint32_t> result;
    std::uint64_t sum = 0;
    const std::uint32_t high_word = static_cast<std::uint32_t>((val >> 32));
    if (high_word == 0) {
      result = jarray<std::uint32_t>(x_index);
      sum = (x[--x_index] & 0xffffffffULL) + val;
      result[x_index] = static_cast<std::uint32_t>((sum));
    } else {
      if (x_index == 1) {
        result = jarray<std::uint32_t>(2);
        sum = val + (x[0] & 0xffffffffULL);
        result[1] = static_cast<std::uint32_t>((sum));
        result[0] = static_cast<std::uint32_t>((sum >> 32));
        return result;
      }
      result = jarray<std::uint32_t>(x_index);
      sum = (x[--x_index] & 0xffffffffULL) + (val & 0xffffffffULL);
      result[x_index] = static_cast<std::uint32_t>((sum));
      sum = (x[--x_index] & 0xffffffffULL) + (high_word & 0xffffffffULL) + (sum >> 32);
      result[x_index] = static_cast<std::uint32_t>((sum));
    }

    bool carry = (sum >> 32) != 0;
    while (x_index > 0 && carry) {
      --x_index;
      result[x_index] = x[x_index] + 1;
      carry = result[x_index] == 0;
    }
    while (x_index > 0) {
      --x_index;
      result[x_index] = x[x_index];
    }
    if (carry) {
      jarray<std::uint32_t> bigger(result.length() + 1);
      jarray_copy(result, 0, bigger, 1, result.length());
      bigger[0] = 1;
      return bigger;
    }
    return result;
  }

  // 返回两个大端非负 magnitude 的和
  // 输入保持不变,最高位进位时结果增加一个 limb
  static jarray<std::uint32_t> add_magnitude(const jarray<std::uint32_t>& xin, const jarray<std::uint32_t>& yin) {
    const jarray<std::uint32_t>* x = &xin;
    const jarray<std::uint32_t>* y = &yin;
    if (x->length() < y->length()) {
      std::swap(x, y);
    }

    std::int32_t x_index = x->length();
    std::int32_t y_index = y->length();
    jarray<std::uint32_t> result(x_index);
    std::uint64_t sum = 0;
    if (y_index == 1) {
      sum = ((*x)[--x_index] & 0xffffffffULL) + ((*y)[0] & 0xffffffffULL);
      result[x_index] = static_cast<std::uint32_t>((sum));
    } else {
      while (y_index > 0) {
        sum = ((*x)[--x_index] & 0xffffffffULL) + ((*y)[--y_index] & 0xffffffffULL) + (sum >> 32);
        result[x_index] = static_cast<std::uint32_t>((sum));
      }
    }

    bool carry = (sum >> 32) != 0;
    while (x_index > 0 && carry) {
      --x_index;
      result[x_index] = (*x)[x_index] + 1;
      carry = result[x_index] == 0;
    }
    while (x_index > 0) {
      --x_index;
      result[x_index] = (*x)[x_index];
    }
    if (carry) {
      jarray<std::uint32_t> bigger(result.length() + 1);
      jarray_copy(result, 0, bigger, 1, result.length());
      bigger[0] = 1;
      return bigger;
    }
    return result;
  }

  // 返回无符号 64 位 val 减去大端非负 magnitude little 的结果
  // 调用方必须保证 val >= little 且 little 最多包含两个 limb
  static jarray<std::uint32_t> subtract_magnitude(std::uint64_t val, const jarray<std::uint32_t>& little) {
    const std::uint32_t high_word = static_cast<std::uint32_t>((val >> 32));
    if (high_word == 0) {
      jarray<std::uint32_t> result(1);
      result[0] = static_cast<std::uint32_t>((val - (little[0] & 0xffffffffULL)));
      return result;
    }

    jarray<std::uint32_t> result(2);
    if (little.length() == 1) {
      std::int64_t difference = std::int64_t(val & 0xffffffffULL) - std::int64_t(little[0] & 0xffffffffULL);
      result[1] = static_cast<std::uint32_t>((difference));
      result[0] = difference < 0 ? high_word - 1 : high_word;
      return result;
    }

    std::int64_t difference = std::int64_t(val & 0xffffffffULL) - std::int64_t(little[1] & 0xffffffffULL);
    result[1] = static_cast<std::uint32_t>((difference));
    difference = std::int64_t(high_word & 0xffffffffULL) - std::int64_t(little[0] & 0xffffffffULL) + (difference >> 32);
    result[0] = static_cast<std::uint32_t>((difference));
    return result;
  }

  // 返回大端非负 magnitude big 减去无符号 64 位 val 的结果
  // 调用方必须保证 big >= val
  static jarray<std::uint32_t> subtract_magnitude(const jarray<std::uint32_t>& big, std::uint64_t val) {
    const std::uint32_t high_word = static_cast<std::uint32_t>((val >> 32));
    std::int32_t big_index = big.length();
    jarray<std::uint32_t> result(big_index);
    std::int64_t difference = 0;

    if (high_word == 0) {
      difference = std::int64_t(big[--big_index] & 0xffffffffULL) - std::int64_t(val);
      result[big_index] = static_cast<std::uint32_t>((difference));
    } else {
      difference = std::int64_t(big[--big_index] & 0xffffffffULL) - std::int64_t(val & 0xffffffffULL);
      result[big_index] = static_cast<std::uint32_t>((difference));
      difference =
          std::int64_t(big[--big_index] & 0xffffffffULL) - std::int64_t(high_word & 0xffffffffULL) + (difference >> 32);
      result[big_index] = static_cast<std::uint32_t>((difference));
    }

    bool borrow = difference < 0;
    while (big_index > 0 && borrow) {
      --big_index;
      result[big_index] = big[big_index] - 1;
      borrow = result[big_index] == 0xffffffffU;
    }
    while (big_index > 0) {
      --big_index;
      result[big_index] = big[big_index];
    }
    return result;
  }

  // 返回两个大端非负 magnitude 的差 big - little
  // 调用方必须保证 big >= little
  static jarray<std::uint32_t> subtract_magnitude(const jarray<std::uint32_t>& big,
                                                  const jarray<std::uint32_t>& little) {
    std::int32_t big_index = big.length();
    jarray<std::uint32_t> result(big_index);
    std::int32_t little_index = little.length();
    std::int64_t difference = 0;

    while (little_index > 0) {
      difference = std::int64_t(big[--big_index] & 0xffffffffULL) -
                   std::int64_t(little[--little_index] & 0xffffffffULL) + (difference >> 32);
      result[big_index] = static_cast<std::uint32_t>((difference));
    }

    bool borrow = difference < 0;
    while (big_index > 0 && borrow) {
      --big_index;
      result[big_index] = big[big_index] - 1;
      borrow = result[big_index] == 0xffffffffU;
    }
    while (big_index > 0) {
      --big_index;
      result[big_index] = big[big_index];
    }
    return result;
  }
};

inline const bigint bigint::ZERO{};
inline const bigint bigint::ONE{1};
inline const bigint bigint::TWO{2};
inline const bigint bigint::TEN{10};
inline const bigint bigint::NEGATIVE_ONE{-1};

inline mutable_bigint::mutable_bigint(const bigint& val) : value_(val.mag_), int_len_(val.mag_.length()) {
}

inline bigint mutable_bigint::get_lower(std::int32_t n) {
  if (is_zero()) {
    return bigint::ZERO;
  }
  if (int_len_ < n) {
    return to_bigint(1);
  }

  std::int32_t len = n;
  while (len > 0 && value_[offset_ + int_len_ - len] == 0) {
    --len;
  }
  if (len == 0) {
    return bigint::ZERO;
  }
  return bigint(1, value_.copy_of_range(offset_ + int_len_ - len, offset_ + int_len_));
}

inline bigint mutable_bigint::to_bigint(std::int32_t sign) {
  if (int_len_ == 0 || sign == 0) {
    return bigint::ZERO;
  }
  return bigint(sign, get_magnitude_array());
}

inline bigint mutable_bigint::to_bigint() {
  normalize();
  return to_bigint(is_zero() ? 0 : 1);
}

inline std::string mutable_bigint::to_string() {
  return to_bigint(1).to_string();
}

// 返回缓存中的正小整数 n,调用方必须保证 n 位于 [1, MAX_CONSTANT]
inline bigint bigint::pos_const(std::int32_t n) {
  static const std::array<bigint, MAX_CONSTANT + 1> cache = [] {
    std::array<bigint, MAX_CONSTANT + 1> values{};
    for (std::int32_t i = 1; i <= MAX_CONSTANT; ++i) {
      jarray<std::uint32_t> magnitude(1);
      magnitude[0] = static_cast<std::uint32_t>((i));
      values[static_cast<size_t>((i))] = bigint(1, std::move(magnitude));
    }
    return values;
  }();
  return cache[static_cast<size_t>((n))];
}

// 返回缓存中的负小整数 -n,调用方必须保证 n 位于 [1, MAX_CONSTANT]
inline bigint bigint::neg_const(std::int32_t n) {
  static const std::array<bigint, MAX_CONSTANT + 1> cache = [] {
    std::array<bigint, MAX_CONSTANT + 1> values{};
    for (std::int32_t i = 1; i <= MAX_CONSTANT; ++i) {
      jarray<std::uint32_t> magnitude(1);
      magnitude[0] = static_cast<std::uint32_t>((i));
      values[static_cast<size_t>((i))] = bigint(-1, std::move(magnitude));
    }
    return values;
  }();
  return cache[static_cast<size_t>((n))];
}

// bit_sieve
// bigint 素数搜索使用的固定长度候选筛
//
// 筛中只表示奇数以减少存储和筛选工作量
// 对偶数基值 base,下标 index 对应的整数为 base + 2 * index + 1
// bit 为 0 表示尚未排除的候选值,bit 为 1 表示该位置已被筛除
//
// 默认构造的小筛以 0 为基值,用于枚举筛选所需的小素数
// 搜索筛利用这些小素数标记 base 之后的合数,但未标记的值仍只是 probable-prime 候选
// retrieve() 会对候选值继续执行概率素性测试,筛本身不构成素数证明
struct bit_sieve {
  // 存储筛 bit
  jarray<std::uint64_t> bits_;

  // 筛持有的 bit 数量
  std::int32_t length_{0};

  // 构造以 0 为基值的共享小筛
  // 筛长是性能折中值,用于生成后续搜索筛需要的小素数集合
  bit_sieve() {
    length_ = 150 * 64;
    bits_ = jarray<std::uint64_t>(unit_index(length_ - 1) + 1);

    // 标记 1 为合数
    set(0);
    std::int32_t next_index = 1;
    std::int32_t next_prime = 3;

    // 寻找素数并从筛中删除它们的倍数
    do {
      sieve_single(length_, next_index + next_prime, next_prime);
      next_index = sieve_search(length_, next_index + 1);
      next_prime = 2 * next_index + 1;
    } while ((next_index > 0) && (next_prime < length_));
  }

  // 构造从偶数 base 之后开始、包含 search_len 个奇数候选的搜索筛
  // 使用共享小筛中的素数预先标记候选区间内的合数
  bit_sieve(const bigint& base, std::int32_t search_len) {
    bits_ = jarray<std::uint64_t>(unit_index(search_len - 1) + 1);
    length_ = search_len;
    std::int32_t start = 0;

    const bit_sieve& primes = small_sieve();
    std::int32_t step = primes.sieve_search(primes.length_, start);
    std::int32_t converted_step = (step * 2) + 1;

    // 在偶数 base 指定的偏移处构造大筛
    do {
      // 计算 base mod convertedStep
      start = static_cast<std::int32_t>((base.mod_uint32(static_cast<std::uint32_t>((converted_step)))));

      // 从筛中删除 step 的每个倍数
      start = converted_step - start;
      if (start % 2 == 0) {
        start += converted_step;
      }
      sieve_single(search_len, (start - 1) / 2, converted_step);

      // 从小筛中寻找下一个素数
      step = primes.sieve_search(primes.length_, step + 1);
      converted_step = (step * 2) + 1;
    } while (step > 0);
  }

  // 返回 bit_index 所在的 64 位存储单元下标
  static std::int32_t unit_index(std::int32_t bit_index) {
    return static_cast<std::int32_t>((static_cast<std::uint32_t>((bit_index)) >> 6));
  }

  // 返回仅设置 bit_index 在其 64 位存储单元内对应位置的掩码
  static std::uint64_t bit(std::int32_t bit_index) {
    return std::uint64_t{1} << (bit_index & ((1 << 6) - 1));
  }

  // 返回 bit_index 是否已被设置,即对应候选是否已被筛除
  bool get(std::int32_t bit_index) const {
    const std::int32_t index = unit_index(bit_index);
    return (bits_[index] & bit(bit_index)) != 0;
  }

  // 设置 bit_index,将对应候选标记为已筛除
  void set(std::int32_t bit_index) {
    const std::int32_t index = unit_index(bit_index);
    bits_[index] |= bit(bit_index);
  }

  // 返回 [start, limit) 中第一个未设置 bit 的下标
  // 范围内不存在候选或 start >= limit 时返回 -1
  std::int32_t sieve_search(std::int32_t limit, std::int32_t start) const {
    if (start >= limit) {
      return -1;
    }

    std::int32_t index = start;
    do {
      if (!get(index)) {
        return index;
      }
      ++index;
    } while (index < limit - 1);
    return -1;
  }

  // 从 start 开始每隔 step 设置一个 bit,直到到达 limit
  // 用于标记某个小素数在候选区间内的所有倍数
  void sieve_single(std::int32_t limit, std::int32_t start, std::int32_t step) {
    while (start < limit) {
      set(start);
      start += step;
    }
  }

  // 按升序对筛中未标记的候选执行 certainty 强度的 probable-prime 测试
  // 找到候选时写入 out 并返回 true,遍历完整个筛仍未找到时返回 false
  bool retrieve(bigint& out, const bigint& init_value, std::int32_t certainty,
                std::mt19937_64* random = nullptr) const {
    std::int32_t offset = 1;
    for (std::int32_t i = 0; i < bits_.length(); ++i) {
      std::uint64_t next_long = ~bits_[i];
      for (std::int32_t j = 0; j < 64; ++j) {
        if ((next_long & 1U) == 1U) {
          bigint candidate = init_value.add(static_cast<std::int64_t>(offset));
          if (candidate.prime_to_certainty(certainty, random)) {
            out = std::move(candidate);
            return true;
          }
        }
        next_long >>= 1;
        offset += 2;
      }
    }
    return false;
  }

  // 返回进程内惰性构造的共享小筛
  static const bit_sieve& small_sieve() {
    static const bit_sieve sieve;
    return sieve;
  }
};

// 使用 rnd 返回 bit_length 恰好等于指定值的正 probable prime
// 使用 DEFAULT_PRIME_CERTAINTY 控制误判概率,bit_length < 2 时抛出 std::runtime_error
inline bigint bigint::probable_prime(std::int32_t bit_length, std::mt19937_64& rnd) {
  if (bit_length < 2) {
    throw std::runtime_error("bit length < 2");
  }
  return bit_length < SMALL_PRIME_THRESHOLD ? small_prime(bit_length, DEFAULT_PRIME_CERTAINTY, rnd)
                                            : large_prime(bit_length, DEFAULT_PRIME_CERTAINTY, rnd);
}

// 搜索 bit_length 小于 SMALL_PRIME_THRESHOLD 的随机 probable prime
// 先排除小素数因子,再按 certainty 执行概率素性测试
inline bigint bigint::small_prime(std::int32_t bit_length, std::int32_t certainty, std::mt19937_64& rnd) {
  const std::int32_t mag_len = static_cast<std::int32_t>((static_cast<std::uint32_t>((bit_length + 31)) >> 5));
  jarray<std::uint32_t> temp(mag_len);
  const std::uint32_t high_bit = std::uint32_t{1} << ((bit_length + 31) & 0x1f);
  const std::uint32_t high_mask = (high_bit << 1) - 1U;

  while (true) {
    for (std::int32_t i = 0; i < mag_len; ++i) {
      temp[i] = static_cast<std::uint32_t>((rnd()));
    }
    temp[0] = (temp[0] & high_mask) | high_bit;
    if (bit_length > 2) {
      temp[mag_len - 1] |= 1U;
    }

    bigint p(1, temp);
    if (bit_length > 6) {
      const std::uint64_t r = p.mod_small_prime_product();
      if ((r % 3 == 0) || (r % 5 == 0) || (r % 7 == 0) || (r % 11 == 0) || (r % 13 == 0) || (r % 17 == 0) ||
          (r % 19 == 0) || (r % 23 == 0) || (r % 29 == 0) || (r % 31 == 0) || (r % 37 == 0) || (r % 41 == 0)) {
        continue;
      }
    }

    if (bit_length < 4) {
      return p;
    }
    if (p.prime_to_certainty(certainty, &rnd)) {
      return p;
    }
  }
}

// 搜索 bit_length 较大的随机 probable prime
// 使用 bit_sieve 批量排除合数后对剩余候选执行概率素性测试
inline bigint bigint::large_prime(std::int32_t bit_length, std::int32_t certainty, std::mt19937_64& rnd) {
  bigint p = random_bigint(bit_length, rnd).set_bit(bit_length - 1);
  p.mag_[p.mag_.length() - 1] &= 0xfffffffeU;

  const std::int32_t search_len = get_prime_search_len(bit_length);
  bit_sieve search_sieve(p, search_len);
  bigint candidate;
  bool found = search_sieve.retrieve(candidate, p, certainty, &rnd);

  while (!found || candidate.bit_length() != bit_length) {
    p = p.add(2LL * search_len);
    if (p.bit_length() != bit_length) {
      p = random_bigint(bit_length, rnd).set_bit(bit_length - 1);
    }
    p.mag_[p.mag_.length() - 1] &= 0xfffffffeU;
    search_sieve = bit_sieve(p, search_len);
    found = search_sieve.retrieve(candidate, p, certainty, &rnd);
  }
  return candidate;
}

// 返回 next_probable_prime() 对指定 bit_length 使用的筛选区间长度
// bit_length 超过实现允许的搜索上限时抛出 std::runtime_error
inline std::int32_t bigint::get_prime_search_len(std::int32_t bit_length) {
  if (bit_length > PRIME_SEARCH_BIT_LENGTH_LIMIT + 1) {
    throw std::runtime_error("prime search implementation restriction on bit length");
  }
  return bit_length / 20 * 64;
}

// 返回 3、5、7 到 41 的连续小素数乘积
inline bigint bigint::small_prime_product() {
  return value_of(static_cast<std::int64_t>(SMALL_PRIME_PRODUCT_MAGNITUDE));
}

// 返回严格大于当前值的第一个 probable prime
// 当前值为负数时抛出 std::runtime_error
inline bigint bigint::next_probable_prime() const {
  if (signum_ < 0) {
    throw std::runtime_error("start < 0: " + to_string());
  }

  if (signum_ == 0 || compare_to(ONE) == 0) {
    return TWO;
  }

  bigint result = add(ONE);
  if (result.bit_length() < SMALL_PRIME_THRESHOLD) {
    if (!result.test_bit(0)) {
      result = result.add(ONE);
    }

    while (true) {
      if (result.bit_length() > 6) {
        const std::uint64_t r = result.mod_small_prime_product();
        if ((r % 3 == 0) || (r % 5 == 0) || (r % 7 == 0) || (r % 11 == 0) || (r % 13 == 0) || (r % 17 == 0) ||
            (r % 19 == 0) || (r % 23 == 0) || (r % 29 == 0) || (r % 31 == 0) || (r % 37 == 0) || (r % 41 == 0)) {
          result = result.add(TWO);
          continue;
        }
      }

      if (result.bit_length() < 4) {
        return result;
      }
      if (result.prime_to_certainty(DEFAULT_PRIME_CERTAINTY)) {
        return result;
      }
      result = result.add(TWO);
    }
  }

  if (result.test_bit(0)) {
    result = result.subtract(ONE);
  }

  const std::int32_t search_len = get_prime_search_len(result.bit_length());
  while (true) {
    bit_sieve search_sieve(result, search_len);
    bigint candidate;
    if (search_sieve.retrieve(candidate, result, DEFAULT_PRIME_CERTAINTY)) {
      return candidate;
    }
    result = result.add(2LL * search_len);
  }
}

// decimal
// 任意精度带符号十进制数,对外按不可变值对象使用
//
// decimal 由任意精度整数 unscaled value 和 32 位 scale 组成
// 数值等于 unscaled value * 10^(-scale)
// scale >= 0 时表示小数点右侧位数,scale < 0 时表示 unscaled value 还要乘以 10^(-scale)
//
// 同一数值可以具有不同 scale,例如 2.0 和 2.00 数值相等但表示不同
// compare_to() 只比较数值,equals() 同时要求数值和 scale 相同
// to_string() 返回能够保留 unscaled value 与 scale 的规范字符串表示
//
// 不带 math_context 的算术运算要求精确结果
// precision == 0 的 math_context 同样要求精确结果且忽略 rounding_mode
// precision > 0 时先计算逻辑上的精确结果,再按指定有效位数和 rounding_mode 舍入
// 精确除法遇到无限循环小数时抛出异常,round_mode::UNNECESSARY 在需要舍入时抛出异常
//
// 为减少常见值的分配和多精度运算,内部使用两种等价表示
// int_compact_ != INFLATED 时由 std::int64_t 保存 unscaled value
// int_compact_ == INFLATED 时由 int_val_ 保存完整 unscaled value
//
// 这个 struct 按项目约定保持全公有,调用者仍不得直接修改表示字段和缓存字段
struct decimal {
  // 未缩放值; int_compact 为 INFLATED 时为主存储
  // 始终与 int_compact_ 同步; int_compact_ == INFLATED 时以此为真值
  bigint int_val_{};

  // scale: 非负时表示小数点右侧位数
  std::int32_t scale_{0};

  // 精度缓存; 0 表示未知
  mutable std::int32_t precision_{0};

  // to_string 结果缓存
  mutable std::string string_cache_{};

  // compact 表示; INFLATED 表示已膨胀到 bigint
  std::int64_t int_compact_{0};

  // int_compact 的哨兵值, 表示有效数字信息仅可从 int_val 获取
  static constexpr std::int64_t INFLATED = INT64_MIN;

  // 所有 18 位十进制字符串可放入 long; 并非所有 19 位字符串都可以
  static constexpr std::int32_t MAX_COMPACT_DIGITS = 18;

  // Long.MAX_VALUE / 2,用于舍入判断
  static constexpr std::int64_t HALF_LONG_MAX_VALUE = INT64_MAX / 2;

  // Long.MIN_VALUE / 2,用于舍入判断
  static constexpr std::int64_t HALF_LONG_MIN_VALUE = INT64_MIN / 2;

  // 10 的幂 lookup 表 (10^0.. 10^18)
  static constexpr std::int64_t LONG_TEN_POWERS_TABLE[19] = {
      1LL,                   // 0 / 10^0
      10LL,                  // 1 / 10^1
      100LL,                 // 2 / 10^2
      1000LL,                // 3 / 10^3
      10000LL,               // 4 / 10^4
      100000LL,              // 5 / 10^5
      1000000LL,             // 6 / 10^6
      10000000LL,            // 7 / 10^7
      100000000LL,           // 8 / 10^8
      1000000000LL,          // 9 / 10^9
      10000000000LL,         // 10 / 10^10
      100000000000LL,        // 11 / 10^11
      1000000000000LL,       // 12 / 10^12
      10000000000000LL,      // 13 / 10^13
      100000000000000LL,     // 14 / 10^14
      1000000000000000LL,    // 15 / 10^15
      10000000000000000LL,   // 16 / 10^16
      100000000000000000LL,  // 17 / 10^17
      1000000000000000000LL  // 18 / 10^18
  };

  // Knuth 128/64 除法基数
  static constexpr std::int64_t DIV_NUM_BASE = (1LL << 32);

  // 128 位十进制幂表, 用于 precision(hi, lo)
  static constexpr std::int64_t LONGLONG_TEN_POWERS_TABLE[20][2] = {
      {0LL, static_cast<std::int64_t>(0x8AC7230489E80000ULL)},                   // 10^19
      {0x5LL, static_cast<std::int64_t>(0x6bc75e2d63100000ULL)},                 // 10^20
      {0x36LL, static_cast<std::int64_t>(0x35c9adc5dea00000ULL)},                // 10^21
      {0x21eLL, static_cast<std::int64_t>(0x19e0c9bab2400000ULL)},               // 10^22
      {0x152dLL, static_cast<std::int64_t>(0x02c7e14af6800000ULL)},              // 10^23
      {0xd3c2LL, static_cast<std::int64_t>(0x1bcecceda1000000ULL)},              // 10^24
      {0x84595LL, static_cast<std::int64_t>(0x161401484a000000ULL)},             // 10^25
      {0x52b7d2LL, static_cast<std::int64_t>(0xdcc80cd2e4000000ULL)},            // 10^26
      {0x33b2e3cLL, static_cast<std::int64_t>(0x9fd0803ce8000000ULL)},           // 10^27
      {0x204fce5eLL, static_cast<std::int64_t>(0x3e25026110000000ULL)},          // 10^28
      {0x1431e0faeLL, static_cast<std::int64_t>(0x6d7217caa0000000ULL)},         // 10^29
      {0xc9f2c9cd0LL, static_cast<std::int64_t>(0x4674edea40000000ULL)},         // 10^30
      {0x7e37be2022LL, static_cast<std::int64_t>(0xc0914b2680000000ULL)},        // 10^31
      {0x4ee2d6d415bLL, static_cast<std::int64_t>(0x85acef8100000000ULL)},       // 10^32
      {0x314dc6448d93LL, static_cast<std::int64_t>(0x38c15b0a00000000ULL)},      // 10^33
      {0x1ed09bead87c0LL, static_cast<std::int64_t>(0x378d8e6400000000ULL)},     // 10^34
      {0x13426172c74d82LL, static_cast<std::int64_t>(0x2b878fe800000000ULL)},    // 10^35
      {0xc097ce7bc90715LL, static_cast<std::int64_t>(0xb34b9f1000000000ULL)},    // 10^36
      {0x785ee10d5da46d9LL, static_cast<std::int64_t>(0x00f436a000000000ULL)},   // 10^37
      {0x4b3b4ca85a86c47aLL, static_cast<std::int64_t>(0x098a224000000000ULL)},  // 10^38
  };

  // long 与 10 的幂相乘时的溢出阈值表
  static constexpr std::int64_t THRESHOLDS_TABLE[19] = {
      INT64_MAX,                         // 0
      INT64_MAX / 10LL,                  // 1
      INT64_MAX / 100LL,                 // 2
      INT64_MAX / 1000LL,                // 3
      INT64_MAX / 10000LL,               // 4
      INT64_MAX / 100000LL,              // 5
      INT64_MAX / 1000000LL,             // 6
      INT64_MAX / 10000000LL,            // 7
      INT64_MAX / 100000000LL,           // 8
      INT64_MAX / 1000000000LL,          // 9
      INT64_MAX / 10000000000LL,         // 10
      INT64_MAX / 100000000000LL,        // 11
      INT64_MAX / 1000000000000LL,       // 12
      INT64_MAX / 10000000000000LL,      // 13
      INT64_MAX / 100000000000000LL,     // 14
      INT64_MAX / 1000000000000000LL,    // 15
      INT64_MAX / 10000000000000000LL,   // 16
      INT64_MAX / 100000000000000000LL,  // 17
      INT64_MAX / 1000000000000000000LL  // 18
  };

  // 10 的幂 bigint 表, 惰性扩展
  // 惰性初始化 (单线程, 非线程安全)
  static const bigint& big_ten_to_the(std::int32_t n) {
    static const bigint init_vals[] = {
        bigint(1LL),
        bigint(10LL),
        bigint(100LL),
        bigint(1000LL),
        bigint(10000LL),
        bigint(100000LL),
        bigint(1000000LL),
        bigint(10000000LL),
        bigint(100000000LL),
        bigint(1000000000LL),
        bigint(10000000000LL),
        bigint(100000000000LL),
        bigint(1000000000000LL),
        bigint(10000000000000LL),
        bigint(100000000000000LL),
        bigint(1000000000000000LL),
        bigint(10000000000000000LL),
        bigint(100000000000000000LL),
        bigint(1000000000000000000LL),
    };
    static constexpr std::int32_t init_len = 19;
    // n 较小时返回预计算值
    if (n < 0) {
      return bigint::ZERO;
    }
    // 惰性扩展表 (非线程安全)
    static std::vector<bigint>* ext_cache = nullptr;
    if (n < init_len) {
      return init_vals[n];
    }
    if (ext_cache == nullptr) {
      ext_cache = new std::vector<bigint>();
      ext_cache->assign(std::begin(init_vals), std::end(init_vals));
    }
    while (n >= static_cast<std::int32_t>(ext_cache->size())) {
      bigint next = ext_cache->back().multiply(bigint::TEN);
      ext_cache->push_back(std::move(next));
    }
    return (*ext_cache)[n];
  }

  // 预缓存 [0, 10] 的 decimal 常量
  // 在此声明, 类外定义静态常量
  static const decimal ZERO_THROUGH_TEN[11];

  // scale 0~15 的零值 decimal 常量
  static const decimal ZERO_SCALED_BY[16];

  // 常量 0, scale 为 0
  static const decimal ZERO;
  // 常量 1, scale 为 0
  static const decimal ONE;
  // 常量 2, scale 为 0
  static const decimal TWO;
  // 常量 10, scale 为 0
  static const decimal TEN;

  // 常量 0.1,scale 为 1
  static const decimal ONE_TENTH;
  // 常量 0.5,scale 为 1
  static const decimal ONE_HALF;

  // --- 构造函数 ---

  // 构造数值为 0、scale 为 0 的 decimal
  decimal() = default;

  // 使用已经一致的内部字段直接构造 decimal
  // val != INFLATED 时 val 保存 unscaled value,int_val 可为空
  // val == INFLATED 时 int_val 必须保存完整 unscaled value,prec 为 0 表示精度尚未计算
  decimal(bigint int_val, std::int64_t val, std::int32_t scale, std::int32_t prec)
      : int_val_(std::move(int_val)), scale_(scale), precision_(prec), int_compact_(val) {
  }

  // 将 in[offset, offset + len) 解析为 decimal,并按 mc 舍入
  // 接受可选前导正负号、十进制小数点以及 e 或 E 指数,不接受空白和其他字符
  // len <= 0、格式非法、指数或 scale 越界以及要求但无法完成的舍入都会抛出异常
  decimal(const char* in, std::int32_t offset, std::int32_t len, const math_context& mc = math_context::UNLIMITED)
      : decimal() {
    if (len <= 0) {
      throw std::runtime_error("zero length bigdecimal");
    }
    std::int32_t prec = 0, scl = 0;
    std::int64_t rs = 0;
    bigint rb{};
    bool is_compact = (len <= MAX_COMPACT_DIGITS);
    bool isneg = false;
    if (in[offset] == '-') {
      isneg = true;
      offset++;
      len--;
    } else if (in[offset] == '+') {
      offset++;
      len--;
    }
    bool dot = false;
    std::int64_t exp_val = 0;
    std::int32_t idx = 0;
    if (is_compact) {
      for (; len > 0; offset++, len--) {
        char c = in[offset];
        if (c == '0') {
          if (prec == 0)
            prec = 1;
          else if (rs != 0) {
            rs *= 10;
            ++prec;
          }
          if (dot) {
            ++scl;
          }
        } else if (c >= '1' && c <= '9') {
          std::int32_t d = c - '0';
          if (prec != 1 || rs != 0) {
            ++prec;
          }
          rs = rs * 10 + d;
          if (dot) {
            ++scl;
          }
        } else if (c == '.') {
          if (dot) {
            throw std::runtime_error("more than one decimal point");
          }
          dot = true;
        } else if (c == 'e' || c == 'E') {
          exp_val = parse_exp(in, offset, len);
          if (static_cast<std::int32_t>(exp_val) != exp_val) {
            throw std::runtime_error("exponent overflow");
          }
          break;
        } else
          throw std::runtime_error(std::string("bad char: ") + c);
      }
      if (prec == 0) {
        throw std::runtime_error("no digits found");
      }
      if (exp_val != 0) {
        scl = adjust_scale(scl, exp_val);
      }
      rs = isneg ? -rs : rs;
      std::int32_t mcp = mc.precision(), drop = prec - mcp;
      if (mcp > 0 && drop > 0)
        while (drop > 0) {
          scl = check_scale_non_zero(static_cast<std::int64_t>(scl) - drop);
          rs = divide_and_round_64(rs, LONG_TEN_POWERS_TABLE[drop], mc.get_rounding_mode());
          prec = long_digit_length(rs);
          drop = prec - mcp;
        }
    } else {
      std::string coeff_str;
      coeff_str.reserve(static_cast<size_t>(len));
      for (; len > 0; offset++, len--) {
        char c = in[offset];
        if ((c >= '0' && c <= '9')) {
          if (c == '0') {
            if (prec == 0) {
              coeff_str.push_back(c);
              prec = 1;
            } else if (idx != 0) {
              coeff_str.push_back(c);
              ++prec;
              ++idx;
            }
          } else {
            if (prec != 1 || idx != 0) {
              ++prec;
            }
            coeff_str.push_back(c);
            ++idx;
          }
          if (dot) {
            ++scl;
          }
          continue;
        }
        if (c == '.') {
          if (dot) {
            throw std::runtime_error("more than one decimal point");
          }
          dot = true;
          continue;
        }
        if ((c != 'e') && (c != 'E')) {
          throw std::runtime_error("missing exponential mark");
        }
        exp_val = parse_exp(in, offset, len);
        if (static_cast<std::int32_t>(exp_val) != exp_val) {
          throw std::runtime_error("exponent overflow");
        }
        break;
      }
      if (prec == 0) {
        throw std::runtime_error("no digits found");
      }
      if (exp_val != 0) {
        scl = adjust_scale(scl, exp_val);
      }
      rb = bigint(coeff_str, 10);
      if (isneg) {
        rb = rb.negate();
      }
      rs = compact_val_for(rb);
      std::int32_t mcp = mc.precision();
      if (mcp > 0 && (prec > mcp)) {
        if (rs == INFLATED) {
          std::int32_t drop = prec - mcp;
          while (drop > 0) {
            scl = check_scale_non_zero(static_cast<std::int64_t>(scl) - drop);
            rb = divide_and_round_by_10pow(rb, drop, mc.get_rounding_mode());
            rs = compact_val_for(rb);
            if (rs != INFLATED) {
              prec = long_digit_length(rs);
              break;
            }
            prec = big_digit_length(rb);
            drop = prec - mcp;
          }
        }
        if (rs != INFLATED) {
          std::int32_t drop = prec - mcp;
          while (drop > 0) {
            scl = check_scale_non_zero(static_cast<std::int64_t>(scl) - drop);
            rs = divide_and_round_64(rs, LONG_TEN_POWERS_TABLE[drop], mc.get_rounding_mode());
            prec = long_digit_length(rs);
            drop = prec - mcp;
          }
          rb = bigint{};
        }
      }
    }
    scale_ = scl;
    precision_ = prec;
    int_compact_ = rs;
    int_val_ = (rs == INFLATED) ? std::move(rb) : bigint{};
  }

  // 将以空字符结尾的完整 C 字符串解析为 decimal
  // 使用不限精度上下文,格式规则与字符子序列构造函数相同
  decimal(const char* in) : decimal(in, 0, static_cast<std::int32_t>(std::strlen(in))) {
  }

  // 将以空字符结尾的完整 C 字符串解析为 decimal,并按 mc 舍入
  decimal(const char* in, const math_context& mc) : decimal(in, 0, static_cast<std::int32_t>(std::strlen(in)), mc) {
  }

  // 将完整 std::string 解析为 decimal,使用不限精度上下文
  decimal(const std::string& val) : decimal(val.c_str(), 0, static_cast<std::int32_t>(val.size())) {
  }

  // 将完整 std::string 解析为 decimal,并按 mc 舍入
  decimal(const std::string& val, const math_context& mc)
      : decimal(val.c_str(), 0, static_cast<std::int32_t>(val.size()), mc) {
  }

  // 将完整 std::string_view 解析为 decimal,使用不限精度上下文
  // 输入不需要以空字符结尾
  decimal(std::string_view val) : decimal(val.data(), 0, static_cast<std::int32_t>(val.size())) {
  }

  // 将完整 std::string_view 解析为 decimal,并按 mc 舍入
  // 输入不需要以空字符结尾
  decimal(std::string_view val, const math_context& mc)
      : decimal(val.data(), 0, static_cast<std::int32_t>(val.size()), mc) {
  }

  // 构造与 double 二进制浮点值精确相等的 decimal
  // 结果通常不同于对最短可往返字符串调用 value_of(double),NaN 和 infinity 会抛出异常
  decimal(double val) : decimal(val, math_context::UNLIMITED) {
  }

  // 构造与 double 二进制浮点值精确相等的 decimal,随后按 mc 舍入
  // NaN 和正负 infinity 无 decimal 表示并会抛出 std::runtime_error
  decimal(double val, const math_context& mc) : decimal() {
    if (std::isinf(val) || std::isnan(val)) {
      throw std::runtime_error("infinite or nan");
    }
    std::uint64_t val_bits;
    std::memcpy(&val_bits, &val, sizeof(val_bits));
    std::int32_t sign = ((val_bits >> 63) == 0 ? 1 : -1);
    std::int32_t exponent = static_cast<std::int32_t>(((val_bits >> 52) & 0x7ffULL));
    std::int64_t significand =
        (exponent == 0 ? (val_bits & ((1ULL << 52) - 1)) << 1 : (val_bits & ((1ULL << 52) - 1)) | (1ULL << 52));
    exponent -= 1075;
    if (significand == 0) {
      int_val_ = bigint::ZERO;
      scale_ = 0;
      int_compact_ = 0;
      precision_ = 1;
      return;
    }
    while ((significand & 1) == 0) {
      significand >>= 1;
      exponent++;
    }
    std::int32_t scl = 0;
    bigint rb{};
    std::int64_t compact_val = sign * significand;
    if (exponent == 0) {
      rb = (compact_val == INFLATED) ? bigint::value_of(INFLATED) : bigint{};
    } else if (exponent < 0) {
      rb = bigint::value_of(5).pow(-exponent).multiply(compact_val);
      scl = -exponent;
    } else {
      rb = bigint::TWO.pow(exponent).multiply(compact_val);
    }
    if (exponent != 0) {
      compact_val = compact_val_for(rb);
    }
    std::int32_t prec = 0;
    std::int32_t mcp = mc.precision();
    if (mcp > 0) {
      round_mode mode = mc.get_rounding_mode();
      std::int32_t drop;
      if (compact_val == INFLATED) {
        prec = big_digit_length(rb);
        drop = prec - mcp;
        while (drop > 0) {
          scl = check_scale_non_zero(static_cast<std::int64_t>(scl) - drop);
          rb = divide_and_round_by_10pow(rb, drop, mode);
          compact_val = compact_val_for(rb);
          if (compact_val != INFLATED) {
            break;
          }
          prec = big_digit_length(rb);
          drop = prec - mcp;
        }
      }
      if (compact_val != INFLATED) {
        prec = long_digit_length(compact_val);
        drop = prec - mcp;
        while (drop > 0) {
          scl = check_scale_non_zero(static_cast<std::int64_t>(scl) - drop);
          compact_val = divide_and_round_64(compact_val, LONG_TEN_POWERS_TABLE[drop], mode);
          prec = long_digit_length(compact_val);
          drop = prec - mcp;
        }
        rb = bigint{};
      }
    }
    int_val_ = std::move(rb);
    int_compact_ = compact_val;
    scale_ = scl;
    precision_ = prec;
  }

  // 使用 bigint val 作为 unscaled value 构造 scale 为 0 的 decimal
  // val 可放入 compact 表示时自动使用 int_compact_
  decimal(const bigint& val) {
    scale_ = 0;
    int_val_ = to_strict_bigint(val);
    int_compact_ = compact_val_for(int_val_);
  }

  // 使用 bigint val 构造 scale 为 0 的 decimal,随后按 mc 舍入
  decimal(const bigint& val, const math_context& mc) : decimal(to_strict_bigint(val), 0, mc) {
  }

  // 使用 unscaled_val 和 scale 构造数值 unscaled_val * 10^(-scale)
  // unscaled_val 可放入 compact 表示时自动使用 int_compact_
  decimal(const bigint& unscaled_val, std::int32_t scale) {
    int_val_ = to_strict_bigint(unscaled_val);
    int_compact_ = compact_val_for(int_val_);
    scale_ = scale;
  }

  // 使用 unscaled_val 和 scale 构造 decimal,随后按 mc 舍入
  // 舍入会减少有效数字并相应调整 scale 以保持结果数量级
  decimal(const bigint& unscaled_val, std::int32_t scale, const math_context& mc) : decimal() {
    bigint uv = to_strict_bigint(unscaled_val);
    std::int64_t compact_val = compact_val_for(uv);
    std::int32_t mcp = mc.precision();
    std::int32_t prec = 0;
    if (mcp > 0) {
      round_mode mode = mc.get_rounding_mode();
      if (compact_val == INFLATED) {
        prec = big_digit_length(uv);
        std::int32_t drop = prec - mcp;
        while (drop > 0) {
          scale = check_scale_non_zero(static_cast<std::int64_t>(scale) - drop);
          uv = divide_and_round_by_10pow(uv, drop, mode);
          compact_val = compact_val_for(uv);
          if (compact_val != INFLATED) {
            break;
          }
          prec = big_digit_length(uv);
          drop = prec - mcp;
        }
      }
      if (compact_val != INFLATED) {
        prec = long_digit_length(compact_val);
        std::int32_t drop = prec - mcp;
        while (drop > 0) {
          scale = check_scale_non_zero(static_cast<std::int64_t>(scale) - drop);
          compact_val = divide_and_round_64(compact_val, LONG_TEN_POWERS_TABLE[drop], mode);
          prec = long_digit_length(compact_val);
          drop = prec - mcp;
        }
        uv = bigint{};
      }
    }
    int_val_ = std::move(uv);
    int_compact_ = compact_val;
    scale_ = scale;
    precision_ = prec;
  }

  // 构造与 32 位有符号整数 val 精确相等且 scale 为 0 的 compact decimal
  decimal(std::int32_t val) {
    int_compact_ = val;
    scale_ = 0;
    int_val_ = bigint{};
  }

  // 构造与 32 位有符号整数 val 相等的 decimal,随后按 mc 舍入
  decimal(std::int32_t val, const math_context& mc) : decimal() {
    std::int32_t mcp = mc.precision();
    std::int64_t compact_val = val;
    std::int32_t scl = 0;
    std::int32_t prec = 0;
    if (mcp > 0) {
      prec = long_digit_length(compact_val);
      std::int32_t drop = prec - mcp;
      round_mode mode = mc.get_rounding_mode();
      while (drop > 0) {
        scl = check_scale_non_zero(static_cast<std::int64_t>(scl) - drop);
        compact_val = divide_and_round_64(compact_val, LONG_TEN_POWERS_TABLE[drop], mode);
        prec = long_digit_length(compact_val);
        drop = prec - mcp;
      }
    }
    int_val_ = bigint{};
    int_compact_ = compact_val;
    scale_ = scl;
    precision_ = prec;
  }

  // 构造与 64 位有符号整数 val 精确相等且 scale 为 0 的 decimal
  // val == INFLATED 时使用 bigint 保存,避免与 compact 哨兵值混淆
  decimal(std::int64_t val) {
    int_compact_ = val;
    int_val_ = (val == INFLATED) ? bigint::value_of(INFLATED) : bigint{};
    scale_ = 0;
  }

  // 构造与 64 位有符号整数 val 相等的 decimal,随后按 mc 舍入
  // val == INFLATED 时先使用 bigint 路径再尝试压缩
  decimal(std::int64_t val, const math_context& mc) : decimal() {
    std::int32_t mcp = mc.precision();
    round_mode mode = mc.get_rounding_mode();
    std::int32_t prec = 0;
    std::int32_t scl = 0;
    bigint rb = (val == INFLATED) ? bigint::value_of(INFLATED) : bigint{};
    if (mcp > 0) {
      if (val == INFLATED) {
        prec = 19;
        std::int32_t drop = prec - mcp;
        while (drop > 0) {
          scl = check_scale_non_zero(static_cast<std::int64_t>(scl) - drop);
          rb = divide_and_round_by_10pow(rb, drop, mode);
          val = compact_val_for(rb);
          if (val != INFLATED) {
            break;
          }
          prec = big_digit_length(rb);
          drop = prec - mcp;
        }
      }
      if (val != INFLATED) {
        prec = long_digit_length(val);
        std::int32_t drop = prec - mcp;
        while (drop > 0) {
          scl = check_scale_non_zero(static_cast<std::int64_t>(scl) - drop);
          val = divide_and_round_64(val, LONG_TEN_POWERS_TABLE[drop], mode);
          prec = long_digit_length(val);
          drop = prec - mcp;
        }
        rb = bigint{};
      }
    }
    int_val_ = std::move(rb);
    int_compact_ = val;
    scale_ = scl;
    precision_ = prec;
  }

  // 返回非 INFLATED 的 std::int64_t x 绝对值的十进制位数,0 返回 1
  static std::int32_t long_digit_length(std::int64_t x) {
    // 前提: x != INFLATED
    if (x < 0) {
      x = -x;
    }
    if (x < 10) {
      return 1;
    }
    // r = ((64 - 前导零位数(x) + 1) * 1233) >> 12
    std::int32_t r = static_cast<std::int32_t>(
        (((64 - decimal_detail::count_leading_zeros(static_cast<std::uint64_t>(x)) + 1) * 1233) >> 12));
    const std::int64_t* tab = LONG_TEN_POWERS_TABLE;
    return (r >= 19 || x < tab[r]) ? r : r + 1;
  }

  // 将 64 位有符号整数 value 饱和转换为 std::int32_t
  // 小于下界时返回 INT32_MIN,大于上界时返回 INT32_MAX
  static std::int32_t saturate_long(std::int64_t s) {
    std::int32_t i = static_cast<std::int32_t>(s);
    return (s == static_cast<std::int64_t>(i)) ? i : (s < 0 ? INT32_MIN : INT32_MAX);
  }

  // 比较两个非 INFLATED 的 std::int64_t 绝对值,返回 -1、0 或 1
  static std::int32_t long_compare_magnitude(std::int64_t x, std::int64_t y) {
    if (x < 0) {
      x = -x;
    }
    if (y < 0) {
      y = -y;
    }
    return (x < y) ? -1 : ((x == y) ? 0 : 1);
  }

  // 按无符号 64 位数值比较 one 和 two,one > two 时返回 true
  static bool unsigned_long_compare(std::uint64_t one, std::uint64_t two) {
    return one > two;
  }

  // 按无符号 64 位数值返回 a >= b
  static bool unsigned_long_compare_eq(std::uint64_t one, std::uint64_t two) {
    return one >= two;
  }

  // 将 bigint val 精确压缩为 std::int64_t unscaled value
  // val 无法表示或数值恰好等于 INFLATED 哨兵时返回 INFLATED
  static std::int64_t compact_val_for(const bigint& b) {
    const jarray<std::uint32_t>& m = b.mag_;
    std::int32_t len = m.length();
    if (len == 0) {
      return 0;
    }
    std::int32_t d = static_cast<std::int32_t>(m[0]);
    // 超过 18 位十进制或 mag[0] 为负时返回 INFLATED
    if (len > 2 || (len == 2 && d < 0)) {
      return INFLATED;
    }
    std::int64_t u =
        (len == 2) ? (((static_cast<std::int64_t>(m[1]) & 0xffffffffULL) + ((static_cast<std::int64_t>(d)) << 32)))
                   : ((static_cast<std::int64_t>(d)) & 0xffffffffULL);
    return (b.signum_ < 0) ? -u : u;
  }

  // 返回 x + y 的 compact 结果,发生 std::int64_t 溢出时返回 INFLATED
  static std::int64_t add_64(std::int64_t xs, std::int64_t ys) {
    std::int64_t sum = 0;
    if (decimal_detail::add_overflow(xs, ys, &sum)) {
      return INFLATED;
    }
    return sum;
  }

  // 返回 x * y 的 compact 结果,发生 std::int64_t 溢出时返回 INFLATED
  static std::int64_t multiply_64(std::int64_t x, std::int64_t y) {
    std::int64_t product = 0;
    if (decimal_detail::multiply_overflow(x, y, &product)) {
      return INFLATED;
    }
    return product;
  }

  // 返回 val * 10^n 的 compact 结果
  // n 超过 compact 十次幂表或乘积溢出 std::int64_t 时返回 INFLATED
  static std::int64_t long_mul_pow10(std::int64_t val, std::int32_t n) {
    if (val == 0 || n <= 0) {
      return val;
    }
    const std::int64_t* tab = LONG_TEN_POWERS_TABLE;
    const std::int64_t* bounds = THRESHOLDS_TABLE;
    if (n < 19 && n < 19) {
      std::int64_t tenpower = tab[n];
      if (val == 1) {
        return tenpower;
      }
      if ((val < 0 ? -val : val) <= bounds[n]) {
        return val * tenpower;
      }
    }
    return INFLATED;
  }

  // 将无符号高 32 位 hi 和低 32 位 lo 组合为一个 64 位 bit 模式
  static std::int64_t make_64(std::int64_t hi, std::int64_t lo) {
    return (hi << 32) | (lo & 0xffffffffULL);
  }

  // 从无符号 128 位工作值 (hi, lo) 减去 x * y
  // 返回更新后的 128 位高低部分,用于 compact 128/64 Knuth 除法
  static std::int64_t mulsub(std::int64_t u1, std::int64_t u0, std::int64_t v1, std::int64_t v0, std::int64_t q0) {
    std::int64_t tmp = u0 - q0 * v0;
    return make_64(u1 + (tmp >> 32) - q0 * v1, tmp & 0xffffffffLL);
  }

  // 按无符号 128 位数值比较 (hi0, lo0) 与 (hi1, lo1)
  // 前者较小时返回 true
  static bool long_long_compare_magnitude(std::int64_t hi0, std::int64_t lo0, std::int64_t hi1, std::int64_t lo1) {
    if (hi0 != hi1) {
      return hi0 < hi1;
    }
    return static_cast<std::uint64_t>((static_cast<std::uint64_t>((lo0)) + static_cast<std::uint64_t>(INT64_MIN))) <
           static_cast<std::uint64_t>((static_cast<std::uint64_t>((lo1)) + static_cast<std::uint64_t>(INT64_MIN)));
  }

  // 返回无符号 128 位值 (hi, lo) 的十进制位数,零返回 1
  static std::int32_t precision_128(std::int64_t hi, std::int64_t lo) {
    if (hi == 0) {
      if (lo >= 0) {
        return long_digit_length(lo);
      }
      return unsigned_long_compare_eq(static_cast<std::uint64_t>((lo)),
                                      static_cast<std::uint64_t>((LONGLONG_TEN_POWERS_TABLE[0][1])))
                 ? 20
                 : 19;
    }
    const std::int32_t r = static_cast<std::int32_t>(
        (((128 - decimal_detail::count_leading_zeros(static_cast<std::uint64_t>((hi))) + 1) * 1233) >> 12));
    const std::int32_t idx = r - 19;
    if (idx >= 20 ||
        long_long_compare_magnitude(hi, lo, LONGLONG_TEN_POWERS_TABLE[idx][0], LONGLONG_TEN_POWERS_TABLE[idx][1])) {
      return r;
    }
    return r + 1;
  }

  // 返回 bigint b 绝对值的十进制位数,零返回 1
  static std::int32_t big_digit_length(const bigint& b) {
    if (b.signum_ == 0) {
      return 1;
    }
    std::int32_t r = static_cast<std::int32_t>((((static_cast<std::int64_t>(b.bit_length()) + 1) * 646456993LL) >> 31));
    return b.compare_magnitude(big_ten_to_the(r)) < 0 ? r : r + 1;
  }

  // 返回 val 的规范 bigint 值副本
  // 通过按数值重新构造避免保留外部可能破坏的不规范表示
  static bigint to_strict_bigint(const bigint& val) {
    return val;
  }

  // 返回当前 decimal 的完整 bigint unscaled value
  // compact 表示按需构造 bigint,inflated 表示直接返回 int_val_ 的值副本
  bigint inflated() const {
    if (int_compact_ != INFLATED) {
      return bigint::value_of(int_compact_);
    }
    return int_val_;
  }

  // 返回当前 unscaled value * 10^n
  // 根据当前表示选择 compact 或 bigint 路径
  bigint big_mul_pow10(std::int32_t n) const {
    if (n <= 0) {
      return inflated();
    }
    if (int_compact_ != INFLATED) {
      return big_mul_pow10(int_compact_, n);
    }
    return big_mul_pow10(int_val_, n);
  }

  // 返回 compact unscaled value val * 10^n 的 bigint 结果
  // compact 乘法溢出时自动切换到 bigint
  static bigint big_mul_pow10(std::int64_t value, std::int32_t n) {
    if (n <= 0) {
      return bigint(value);
    }
    if (n < 19) {
      return bigint(value).multiply(LONG_TEN_POWERS_TABLE[n]);
    }
    return bigint(value).multiply(big_ten_to_the(n));
  }

  // 返回 bigint val * 10^n,n 必须非负
  static bigint big_mul_pow10(const bigint& value, std::int32_t n) {
    if (n <= 0) {
      return value;
    }
    if (n < 19) {
      return value.multiply(LONG_TEN_POWERS_TABLE[n]);
    }
    return value.multiply(big_ten_to_the(n));
  }

  // 根据 rounding_mode、结果符号、被截断商的奇偶性和余数比较结果判断是否进位
  // cmp_frac_half 表示 abs(remainder) 与 abs(divisor) / 2 的比较结果
  static bool common_need_increment(round_mode rounding_mode, std::int32_t qsign, std::int32_t cmp_frac_half,
                                    bool odd_quot) {
    switch (rounding_mode) {
      case round_mode::UNNECESSARY:
        throw std::runtime_error("rounding necessary");

      case round_mode::UP:
        return true;

      case round_mode::DOWN:
        return false;

      case round_mode::CEILING:
        return qsign > 0;

      case round_mode::FLOOR:
        return qsign < 0;

      default: {  // HALF_UP, HALF_DOWN, HALF_EVEN
        if (cmp_frac_half < 0) {
          return false;
        } else if (cmp_frac_half > 0) {
          return true;
        } else {  // cmp_frac_half == 0, 正好一半
          switch (rounding_mode) {
            case round_mode::HALF_DOWN:
              return false;
            case round_mode::HALF_UP:
              return true;
            case round_mode::HALF_EVEN:
              return odd_quot;
            default:
              throw std::runtime_error("unexpected rounding mode");
          }
        }
      }
    }
  }

  // 判断 std::int64_t 除法的截断商是否需要按 rounding_mode 远离零增加一个单位
  // qsign 是精确结果符号,UNNECESSARY 在 remainder 非零时抛出异常
  static bool need_increment(std::int64_t ldivisor, round_mode rounding_mode, std::int32_t qsign, std::int64_t q,
                             std::int64_t r) {
    // assert r != 0L
    std::int32_t cmp_frac_half;
    if (r <= HALF_LONG_MIN_VALUE || r > HALF_LONG_MAX_VALUE) {
      cmp_frac_half = 1;  // 2 * r can't fit into long
    } else {
      cmp_frac_half = long_compare_magnitude(2 * r, ldivisor);
    }
    return common_need_increment(rounding_mode, qsign, cmp_frac_half, (q & 1LL) != 0LL);
  }

  // 判断 mutable_bigint 商是否需要按 rounding_mode 增加一个单位
  // remainder 与 divisor 为无符号 64 位值,UNNECESSARY 在余数非零时抛出异常
  static bool need_increment(std::int64_t ldivisor, round_mode rounding_mode, std::int32_t qsign,
                             const mutable_bigint& mq, std::int64_t r) {
    // assert r != 0L
    std::int32_t cmp_frac_half;
    if (r <= HALF_LONG_MIN_VALUE || r > HALF_LONG_MAX_VALUE) {
      cmp_frac_half = 1;
    } else {
      cmp_frac_half = long_compare_magnitude(2 * r, ldivisor);
    }
    return common_need_increment(rounding_mode, qsign, cmp_frac_half, mq.is_odd());
  }

  // 判断 mutable_bigint 商是否需要按 rounding_mode 增加一个单位
  // remainder 和 divisor 为非负多精度值,UNNECESSARY 在余数非零时抛出异常
  static bool need_increment(const mutable_bigint& mdivisor, round_mode rounding_mode, std::int32_t qsign,
                             const mutable_bigint& mq, const mutable_bigint& mr) {
    // assert !mr.isZero()
    std::int32_t cmp_frac_half = mr.compare_half(mdivisor);
    return common_need_increment(rounding_mode, qsign, cmp_frac_half, mq.is_odd());
  }

  // 将 scale 饱和检查为 std::int32_t,并假定所表示的数值非零
  // scale 超出范围时抛出 std::runtime_error
  static std::int32_t check_scale_non_zero(std::int64_t val) {
    std::int32_t as_int = static_cast<std::int32_t>(val);
    if (static_cast<std::int64_t>(as_int) != val) {
      throw std::runtime_error(as_int > 0 ? "underflow" : "overflow");
    }
    return as_int;
  }

  // 将 scale 检查并转换为 std::int32_t
  // 当前值非零且 scale 越界时抛出异常,零值越界时饱和到最近边界
  std::int32_t check_scale(std::int64_t val) const {
    std::int32_t as_int = static_cast<std::int32_t>(val);
    if (static_cast<std::int64_t>(as_int) != val) {
      as_int = val > INT32_MAX ? INT32_MAX : INT32_MIN;
      if (signum() != 0) {
        throw std::runtime_error(as_int > 0 ? "underflow" : "overflow");
      }
    }
    return as_int;
  }

  // 将 std::int64_t dividend 除以 divisor 并按 rounding_mode 返回 compact 商
  // DOWN 直接返回向零截断的商,其他模式根据余数决定是否远离零加一
  static std::int64_t divide_and_round_64(std::int64_t ldividend, std::int64_t ldivisor, round_mode rounding_mode) {
    std::int32_t qsign;
    std::int64_t q = ldividend / ldivisor;
    if (rounding_mode == round_mode::DOWN) {
      return q;
    }
    std::int64_t r = ldividend % ldivisor;
    qsign = ((ldividend < 0) == (ldivisor < 0)) ? 1 : -1;
    if (r != 0) {
      bool inc = need_increment(ldivisor, rounding_mode, qsign, q, r);
      return inc ? q + qsign : q;
    }
    return q;
  }

  // 将 compact dividend 除以 divisor,并使用 scale 构造 decimal 商
  // 有余数时按 rounding_mode 舍入,精确商可向 preferred_scale 剥离多余尾随零
  static decimal divide_and_round(std::int64_t ldividend, std::int64_t ldivisor, std::int32_t scale, round_mode rm,
                                  std::int32_t pref_sc) {
    std::int32_t qsign;
    std::int64_t q = ldividend / ldivisor;
    if (rm == round_mode::DOWN && scale == pref_sc) {
      return value_of(q, scale);
    }
    std::int64_t r = ldividend % ldivisor;
    qsign = ((ldividend < 0) == (ldivisor < 0)) ? 1 : -1;
    if (r != 0) {
      bool inc = need_increment(ldivisor, rm, qsign, q, r);
      return value_of((inc ? q + qsign : q), scale);
    }
    if (pref_sc != scale) {
      return create_and_strip_zeros_to_match_scale(q, scale, pref_sc);
    }
    return value_of(q, scale);
  }

  // 将 bigint dividend 除以非零 std::int64_t divisor,并按 rounding_mode 返回 bigint 商
  // 商向零截断后根据余数决定是否远离零增加一个单位
  static bigint divide_and_round(const bigint& bdividend, std::int64_t ldivisor, round_mode rm) {
    mutable_bigint mdividend(bdividend.mag_);
    mutable_bigint mq;
    // mutable_bigint::divide 期望除数的无符号幅度 (符号已由 qsign 单独处理)
    const std::uint64_t udivisor =
        (ldivisor < 0) ? -static_cast<std::uint64_t>((ldivisor)) : static_cast<std::uint64_t>((ldivisor));
    std::int64_t r = mdividend.divide(udivisor, mq);
    bool is_rem_zero = (r == 0);
    std::int32_t qsign = (ldivisor < 0) ? -bdividend.signum_ : bdividend.signum_;
    if (!is_rem_zero) {
      if (need_increment(ldivisor, rm, qsign, mq, r)) {
        mq.add(mutable_bigint::ONE);
      }
    }
    return bigint::from_mutable(std::move(mq), qsign);
  }

  // 将 bigint dividend 除以 std::int64_t divisor,并使用 scale 构造 decimal 商
  // 有余数时按 rounding_mode 舍入,精确商可向 preferred_scale 剥离尾随零
  static decimal divide_and_round(const bigint& bdividend, std::int64_t ldivisor, std::int32_t scale, round_mode rm,
                                  std::int32_t pref_sc) {
    mutable_bigint mdividend(bdividend.mag_);
    mutable_bigint mq;
    // mutable_bigint::divide 期望除数的无符号幅度 (符号已由 qsign 单独处理)
    const std::uint64_t udivisor =
        (ldivisor < 0) ? -static_cast<std::uint64_t>((ldivisor)) : static_cast<std::uint64_t>((ldivisor));
    std::int64_t r = mdividend.divide(udivisor, mq);
    bool is_rem_zero = (r == 0);
    std::int32_t qsign = (ldivisor < 0) ? -bdividend.signum_ : bdividend.signum_;
    if (!is_rem_zero) {
      if (need_increment(ldivisor, rm, qsign, mq, r)) {
        mq.add(mutable_bigint::ONE);
      }
      const std::int64_t cv = mq.to_compact_value(qsign);
      if (cv != INFLATED) {
        return value_of(cv, scale);
      }
      return value_of(bigint::from_mutable(std::move(mq), qsign), scale, 0);
    }
    if (pref_sc != scale) {
      const std::int64_t cv = mq.to_compact_value(qsign);
      if (cv != INFLATED) {
        return create_and_strip_zeros_to_match_scale(cv, scale, pref_sc);
      }
      bigint iv = mq.to_bigint(qsign);
      return create_and_strip_zeros_to_match_scale(iv, scale, pref_sc);
    }
    const std::int64_t cv = mq.to_compact_value(qsign);
    if (cv != INFLATED) {
      return value_of(cv, scale);
    }
    return value_of(bigint::from_mutable(std::move(mq), qsign), scale, 0);
  }

  // 将 bigint dividend 除以 bigint divisor,并按 rounding_mode 返回 bigint 商
  // quotient 工作区保存向零截断的 magnitude 商,remainder 用于决定进位
  static bigint divide_and_round(const bigint& bdividend, const bigint& bdivisor, round_mode rm) {
    bool is_rem_zero;
    std::int32_t qsign;
    mutable_bigint mdividend(bdividend.mag_);
    mutable_bigint mq;
    mutable_bigint mdivisor(bdivisor.mag_);
    mutable_bigint mr = mdividend.divide(mdivisor, mq);
    is_rem_zero = mr.is_zero();
    qsign = (bdividend.signum_ != bdivisor.signum_) ? -1 : 1;
    if (!is_rem_zero) {
      if (need_increment(mdivisor, rm, qsign, mq, mr)) {
        mq.add(mutable_bigint::ONE);
      }
    }
    return bigint::from_mutable(std::move(mq), qsign);
  }

  // 将 bigint dividend 除以 bigint divisor,并使用 scale 构造 decimal 商
  // 有余数时按 rm 舍入,精确商可向 pref_sc 剥离十进制尾随零
  static decimal divide_and_round(const bigint& bdividend, const bigint& bdivisor, std::int32_t scale, round_mode rm,
                                  std::int32_t pref_sc) {
    bool is_rem_zero;
    std::int32_t qsign;
    mutable_bigint mdividend(bdividend.mag_);
    mutable_bigint mq;
    mutable_bigint mdivisor(bdivisor.mag_);
    mutable_bigint mr = mdividend.divide(mdivisor, mq);
    is_rem_zero = mr.is_zero();
    qsign = (bdividend.signum_ != bdivisor.signum_) ? -1 : 1;
    if (!is_rem_zero) {
      if (need_increment(mdivisor, rm, qsign, mq, mr)) {
        mq.add(mutable_bigint::ONE);
      }
      const std::int64_t cv = mq.to_compact_value(qsign);
      if (cv != INFLATED) {
        return value_of(cv, scale);
      }
      return value_of(bigint::from_mutable(std::move(mq), qsign), scale, 0);
    }
    if (pref_sc != scale) {
      const std::int64_t cv = mq.to_compact_value(qsign);
      if (cv != INFLATED) {
        return create_and_strip_zeros_to_match_scale(cv, scale, pref_sc);
      }
      bigint iv = mq.to_bigint(qsign);
      return create_and_strip_zeros_to_match_scale(iv, scale, pref_sc);
    }
    const std::int64_t cv = mq.to_compact_value(qsign);
    if (cv != INFLATED) {
      return value_of(cv, scale);
    }
    return value_of(bigint::from_mutable(std::move(mq), qsign), scale, 0);
  }

  // 将 bigint value 除以 10^n 并按 rounding_mode 返回整数商
  // n 必须非负,较小 n 使用 compact 除数,较大 n 使用 bigint 十次幂
  static bigint divide_and_round_by_10pow(const bigint& int_val, std::int32_t ten_pow, round_mode rm) {
    if (ten_pow < 19) {
      return divide_and_round(int_val, LONG_TEN_POWERS_TABLE[ten_pow], rm);
    }
    return divide_and_round(int_val, big_ten_to_the(ten_pow), rm);
  }

  // 返回数值为零且具有指定 scale 的 decimal
  // scale 位于缓存范围时复用 ZERO_SCALED_BY 常量
  static decimal zero_value_of(std::int32_t scale) {
    if (scale >= 0 && scale < 16) {
      return ZERO_SCALED_BY[scale];
    }
    return decimal(bigint::ZERO, 0, scale, 1);
  }

  // 使用 compact unscaled value 和 scale 构造 decimal
  // 常用整数和带 scale 的零值优先复用缓存
  static decimal value_of(std::int64_t unscaled_val, std::int32_t scale) {
    if (scale == 0) {
      return value_of(unscaled_val);
    }
    if (unscaled_val == 0) {
      return zero_value_of(scale);
    }
    return decimal(unscaled_val == INFLATED ? bigint::value_of(INFLATED) : bigint{}, unscaled_val, scale, 0);
  }

  // 使用 compact unscaled value、scale 和已知 precision 构造 decimal
  // 常用整数和带 scale 的零值优先复用缓存
  static decimal value_of(std::int64_t unscaled_val, std::int32_t scale, std::int32_t prec) {
    if (scale == 0 && unscaled_val >= 0 && unscaled_val < 11) {
      return ZERO_THROUGH_TEN[static_cast<std::int32_t>(unscaled_val)];
    }
    if (unscaled_val == 0) {
      return zero_value_of(scale);
    }
    return decimal(unscaled_val == INFLATED ? bigint::value_of(INFLATED) : bigint{}, unscaled_val, scale, prec);
  }

  // 返回与 std::int64_t val 精确相等且 scale 为 0 的 decimal
  // [0, 10] 优先复用缓存,val == INFLATED 时使用 bigint 表示
  static decimal value_of(std::int64_t val) {
    if (val >= 0 && val < 11) {
      return ZERO_THROUGH_TEN[static_cast<std::int32_t>(val)];
    }
    if (val != INFLATED) {
      return decimal(bigint{}, val, 0, 0);
    }
    return decimal(bigint::value_of(INFLATED), val, 0, 0);
  }

  // 使用 bigint unscaled value 和 scale 构造 decimal
  // 数值可压缩时切换到 compact 表示,零值使用指定 scale 的缓存路径
  static decimal value_of(bigint int_val, std::int32_t scale, std::int32_t prec) {
    std::int64_t val = compact_val_for(int_val);
    if (val == 0) {
      return zero_value_of(scale);
    }
    if (scale == 0 && val >= 0 && val < 11) {
      return ZERO_THROUGH_TEN[static_cast<std::int32_t>(val)];
    }
    return decimal(std::move(int_val), val, scale, prec);
  }

  // 返回字符串 s 经 std::strtod 解析后是否与 val 具有完全相同的 double bit 模式
  // 用于验证候选十进制字符串能够往返
  static bool double_string_roundtrip(const char* s, double val) {
    char* end = nullptr;
    const double parsed = std::strtod(s, &end);
    if (parsed != val) {
      return false;
    }
    std::uint64_t bits_val = 0;
    std::uint64_t bits_parsed = 0;
    std::memcpy(&bits_val, &val, sizeof(val));
    std::memcpy(&bits_parsed, &parsed, sizeof(parsed));
    return bits_val == bits_parsed;
  }

  // 将最短可往返浮点字符串规范为 decimal 使用的 plain 或 scientific 形式
  // 科学计数法使用一个整数位、至少一个小数位和大写 E
  static std::string normalize_shortest_double(std::string_view raw) {
    const bool negative = !raw.empty() && raw.front() == '-';
    const std::size_t begin = negative ? 1 : 0;
    const std::size_t exponent_pos = raw.find_first_of("eE", begin);
    const std::size_t significand_end = exponent_pos == std::string_view::npos ? raw.size() : exponent_pos;
    const std::size_t dot_pos = raw.find('.', begin);
    const std::size_t integer_end = dot_pos < significand_end ? dot_pos : significand_end;

    std::int32_t exponent = 0;
    if (exponent_pos != std::string_view::npos) {
      exponent = static_cast<std::int32_t>(std::strtol(raw.data() + exponent_pos + 1, nullptr, 10));
    }

    std::string digits;
    digits.reserve(significand_end - begin);
    std::size_t first_nonzero = std::string_view::npos;
    for (std::size_t i = begin; i < significand_end; ++i) {
      if (raw[i] == '.') {
        continue;
      }
      if (first_nonzero == std::string_view::npos && raw[i] != '0') {
        first_nonzero = i;
      }
      if (first_nonzero != std::string_view::npos) {
        digits.push_back(raw[i]);
      }
    }
    if (digits.empty()) {
      return negative ? "-0.0" : "0.0";
    }
    while (digits.size() > 1 && digits.back() == '0') {
      digits.pop_back();
    }

    std::int32_t adjusted_exponent = exponent;
    if (exponent_pos == std::string_view::npos) {
      if (first_nonzero < integer_end) {
        adjusted_exponent = static_cast<std::int32_t>(integer_end - first_nonzero) - 1;
      } else {
        adjusted_exponent = -static_cast<std::int32_t>(first_nonzero - integer_end);
      }
    } else {
      adjusted_exponent += static_cast<std::int32_t>(integer_end - begin) - 1;
    }

    std::string result;
    result.reserve(digits.size() + 16);
    if (negative) {
      result.push_back('-');
    }
    if (adjusted_exponent >= -3 && adjusted_exponent < 7) {
      if (adjusted_exponent >= 0) {
        const std::size_t integer_digits = static_cast<std::size_t>(adjusted_exponent) + 1;
        if (digits.size() <= integer_digits) {
          result.append(digits);
          result.append(integer_digits - digits.size(), '0');
          result.append(".0");
        } else {
          result.append(digits, 0, integer_digits);
          result.push_back('.');
          result.append(digits, integer_digits, std::string::npos);
        }
      } else {
        result.append("0.");
        result.append(static_cast<std::size_t>(-adjusted_exponent - 1), '0');
        result.append(digits);
      }
      return result;
    }

    result.push_back(digits.front());
    result.push_back('.');
    if (digits.size() == 1) {
      result.push_back('0');
    } else {
      result.append(digits, 1, std::string::npos);
    }
    result.push_back('E');
    result.append(std::to_string(adjusted_exponent));
    return result;
  }

  // 返回能够经 std::strtod 无损还原 val 的最短规范十进制字符串
  // NaN 和 infinity 被拒绝,科学计数法使用大写 E
  static std::string double_to_format_string(double val) {
    if (std::isinf(val) || std::isnan(val)) {
      throw std::runtime_error("infinite or nan");
    }
    if (val == 0.0) {
      return std::signbit(val) ? "-0.0" : "0.0";
    }

    char buf[64];
    const std::to_chars_result chars = std::to_chars(buf, buf + sizeof(buf), val, std::chars_format::scientific);
    if (chars.ec != std::errc{}) {
      throw std::runtime_error("failed to format double");
    }
    *chars.ptr = '\0';
    const std::string_view shortest(buf, static_cast<std::size_t>(chars.ptr - buf));
    std::size_t significant_digits = 0;
    for (const char digit : shortest.substr(0, shortest.find_first_of("eE"))) {
      if (digit >= '1' && digit <= '9') {
        ++significant_digits;
      } else if (digit == '0' && significant_digits != 0) {
        ++significant_digits;
      }
    }

    // 最短结果只有一位有效数字时同时比较两位候选
    // 例如最小非零 double 使用 4.9E-324,而不是一般 shortest 模式给出的 5.0E-324
    if (significant_digits == 1) {
      char two_digit_buf[64];
      const std::to_chars_result two_digit =
          std::to_chars(two_digit_buf, two_digit_buf + sizeof(two_digit_buf), val, std::chars_format::scientific, 1);
      if (two_digit.ec == std::errc{}) {
        *two_digit.ptr = '\0';
      }
      if (two_digit.ec == std::errc{} && double_string_roundtrip(two_digit_buf, val)) {
        return normalize_shortest_double(
            std::string_view(two_digit_buf, static_cast<std::size_t>(two_digit.ptr - two_digit_buf)));
      }
    }
    return normalize_shortest_double(shortest);
  }

  // 使用 double 的最短可往返十进制表示构造 decimal
  // 与 decimal(double) 的精确二进制值转换不同,该结果通常更适合人类可读输出
  static decimal value_of(double val) {
    return decimal(double_to_format_string(val));
  }

  // 从 compact unscaled value 中剥离十进制尾随零,同时降低 scale
  // 不会把 scale 降到 preferred_scale 以下
  static decimal create_and_strip_zeros_to_match_scale(std::int64_t cv, std::int32_t sc, std::int64_t psc) {
    while ((cv < 0 ? -cv : cv) >= 10LL && sc > psc) {
      if ((cv & 1LL) != 0LL) {
        break;
      }
      std::int64_t r = cv % 10LL;
      if (r != 0LL) {
        break;
      }
      cv /= 10;
      std::int64_t ns = static_cast<std::int64_t>(sc) - 1;
      std::int32_t as = static_cast<std::int32_t>(ns);
      if (static_cast<std::int64_t>(as) != ns) {
        throw std::runtime_error(as > 0 ? "underflow" : "overflow");
      }
      sc = as;
    }
    return value_of(cv, sc);
  }

  // 从 bigint unscaled value 中剥离十进制尾随零,同时降低 scale
  // 不会把 scale 降到 preferred_scale 以下,结果可压缩时切换到 compact 表示
  static decimal create_and_strip_zeros_to_match_scale(const bigint& iv, std::int32_t sc, std::int64_t psc) {
    bigint cur = iv;
    std::int32_t cs = sc;
    while (cur.compare_magnitude(bigint::TEN) >= 0 && cs > psc) {
      if (cur.is_odd()) {
        break;
      }
      auto qr = cur.divide_and_remainder(bigint::TEN);
      if (qr.second.signum_ != 0) {
        break;
      }
      cur = std::move(qr.first);
      std::int64_t ns = static_cast<std::int64_t>(cs) - 1;
      std::int32_t as = static_cast<std::int32_t>(ns);
      if (static_cast<std::int64_t>(as) != ns) {
        throw std::runtime_error(as > 0 ? "underflow" : "overflow");
      }
      cs = as;
    }
    return value_of(std::move(cur), cs, 0);
  }

  // 根据 qsign 和 quotient 的实际表示构造 decimal 商
  // 精确商会剥离尾随零,但 scale 不低于 preferred_scale
  static decimal strip_zeros_to_match_scale(const bigint& iv, std::int64_t ic, std::int32_t sc, std::int32_t psc) {
    if (ic != INFLATED) {
      return create_and_strip_zeros_to_match_scale(ic, sc, psc);
    }
    return create_and_strip_zeros_to_match_scale(iv, sc, psc);
  }

  // 将 bigint unscaled value 按 mc.precision() 和 mc.rounding_mode 舍入
  // 舍入删除的十进制位数会从 scale 中扣除,结果可压缩时使用 compact 表示
  static decimal do_round(const bigint& int_val, std::int32_t scale, const math_context& mc) {
    const std::int32_t mcp = mc.precision();
    std::int32_t prec = 0;
    if (mcp > 0) {
      std::int64_t cv = compact_val_for(int_val);
      if (cv != INFLATED) {
        return do_round(cv, scale, mc);
      }

      const round_mode mode = mc.get_rounding_mode();
      bigint cur_val = int_val;
      std::int32_t cur_s = scale;
      prec = big_digit_length(cur_val);
      std::int32_t drop = prec - mcp;
      while (drop > 0) {
        cur_s = check_scale_non_zero(static_cast<std::int64_t>(cur_s) - drop);
        cur_val = divide_and_round_by_10pow(cur_val, drop, mode);
        cv = compact_val_for(cur_val);
        if (cv != INFLATED) {
          break;
        }
        prec = big_digit_length(cur_val);
        drop = prec - mcp;
      }
      if (cv != INFLATED) {
        prec = long_digit_length(cv);
        drop = prec - mcp;
        while (drop > 0) {
          cur_s = check_scale_non_zero(static_cast<std::int64_t>(cur_s) - drop);
          cv = divide_and_round_64(cv, LONG_TEN_POWERS_TABLE[drop], mode);
          prec = long_digit_length(cv);
          drop = prec - mcp;
        }
        return value_of(cv, cur_s, prec);
      }
      return decimal(std::move(cur_val), INFLATED, cur_s, prec);
    }
    return decimal(int_val, INFLATED, scale, prec);
  }

  // 返回 val 按 mc 舍入后的 decimal,不修改 val
  // mc.precision() == 0 时直接返回 val
  static decimal do_round(const decimal& input, const math_context& mc) {
    if (input.int_compact_ != INFLATED) {
      return do_round(input.int_compact_, input.scale_, mc);
    }
    return do_round(input.int_val_, input.scale_, mc);
  }

  // 返回应用十进制指数后的 scale,即 scl - exp
  // 结果超出 std::int32_t 范围时抛出 std::runtime_error
  static std::int32_t adjust_scale(std::int32_t scl, std::int64_t exp) {
    std::int64_t as = static_cast<std::int64_t>(scl) - exp;
    if (as > INT32_MAX || as < INT32_MIN) {
      throw std::runtime_error("scale out of range");
    }
    return static_cast<std::int32_t>(as);
  }

  // 解析 e 或 E 后带可选正负号的十进制指数
  // 格式非法、缺少指数数字或指数溢出时抛出 std::runtime_error
  static std::int64_t parse_exp(const char* in, std::int32_t offset, std::int32_t len) {
    std::int64_t exp = 0;
    offset++;
    char c = in[offset];
    len--;
    bool negexp = (c == '-');
    if (negexp || c == '+') {
      offset++;
      c = in[offset];
      len--;
    }
    if (len <= 0) {
      throw std::runtime_error("no exponent digits");
    }
    while (len > 10 && (c == '0')) {
      offset++;
      c = in[offset];
      len--;
    }
    if (len > 10) {
      throw std::runtime_error("too many nonzero exponent digits");
    }
    for (;; len--) {
      std::int32_t v;
      if (c >= '0' && c <= '9')
        v = c - '0';
      else
        throw std::runtime_error("not a digit");
      exp = exp * 10 + v;
      if (len == 1) {
        break;
      }
      offset++;
      c = in[offset];
    }
    return negexp ? -exp : exp;
  }

  // 返回当前数值的符号,负数返回 -1,零返回 0,正数返回 1
  std::int32_t signum() const {
    if (int_compact_ != INFLATED) {
      return (int_compact_ > 0) ? 1 : ((int_compact_ < 0) ? -1 : 0);
    }
    return int_val_.signum_;
  }

  // 返回数值与当前值相等且 scale 为 new_scale 的 decimal
  // 增大 scale 时向 unscaled value 追加零,减小 scale 时按 rounding_mode 丢弃低位数字
  // UNNECESSARY 在减小 scale 需要舍入时抛出异常
  decimal set_scale(std::int32_t new_scale, round_mode rounding_mode) const {
    std::int32_t old_scale = scale_;
    if (new_scale == old_scale) {
      return *this;
    }
    if (signum() == 0) {
      return zero_value_of(new_scale);
    }
    if (int_compact_ != INFLATED) {
      std::int64_t rs = int_compact_;
      if (new_scale > old_scale) {
        std::int32_t raise = check_scale(static_cast<std::int64_t>(new_scale) - old_scale);
        rs = long_mul_pow10(rs, raise);
        if (rs != INFLATED) {
          return value_of(rs, new_scale);
        }
        bigint rb = big_mul_pow10(raise);
        return decimal(rb, INFLATED, new_scale, (precision_ > 0) ? precision_ + raise : 0);
      } else {
        std::int32_t drop = check_scale(static_cast<std::int64_t>(old_scale) - new_scale);
        if (drop < 19) {
          return divide_and_round(rs, LONG_TEN_POWERS_TABLE[drop], new_scale, rounding_mode, new_scale);
        } else {
          return divide_and_round(inflated(), big_ten_to_the(drop), new_scale, rounding_mode, new_scale);
        }
      }
    } else {
      if (new_scale > old_scale) {
        std::int32_t raise = check_scale(static_cast<std::int64_t>(new_scale) - old_scale);
        bigint rb = big_mul_pow10(int_val_, raise);
        return decimal(rb, INFLATED, new_scale, (precision_ > 0) ? precision_ + raise : 0);
      } else {
        std::int32_t drop = check_scale(static_cast<std::int64_t>(old_scale) - new_scale);
        if (drop < 19) {
          return divide_and_round(int_val_, LONG_TEN_POWERS_TABLE[drop], new_scale, rounding_mode, new_scale);
        } else {
          return divide_and_round(int_val_, big_ten_to_the(drop), new_scale, rounding_mode, new_scale);
        }
      }
    }
  }

  // 返回数值与当前值相等且 scale 为 new_scale 的 decimal
  // rounding_mode 使用 round_mode 的底层整数值,超出合法范围时抛出 std::invalid_argument
  decimal set_scale(std::int32_t new_scale, std::int32_t rounding_mode) const {
    if (rounding_mode < static_cast<std::int32_t>((round_mode::UP)) ||
        rounding_mode > static_cast<std::int32_t>((round_mode::UNNECESSARY))) {
      throw std::invalid_argument("invalid rounding mode");
    }
    return set_scale(new_scale, static_cast<round_mode>((rounding_mode)));
  }

  // 精确返回 scale 为 new_scale 的 decimal
  // 如果必须丢弃非零小数位才能达到目标 scale 则抛出异常
  decimal set_scale(std::int32_t new_scale) const {
    return set_scale(new_scale, round_mode::UNNECESSARY);
  }

  // 返回当前值的副本,作为与 negate() 对称的一元正号操作
  decimal plus() const {
    return *this;
  }

  // 返回当前值按 mc 的 precision 和 rounding_mode 舍入后的结果
  // mc.precision() == 0 时不执行舍入
  decimal plus(const math_context& mc) const {
    if (mc.precision() == 0) {
      return *this;
    }
    return do_round(*this, mc);
  }

  // 返回当前值按 mc 舍入后的结果,语义与 plus(mc) 相同
  decimal round(const math_context& mc) const {
    return plus(mc);
  }

  // 返回小数点左移 n 位后的值,等价于 this * 10^(-n)
  // n 可以为负,此时等价于向右移动,结果 scale 不小于 0
  decimal move_point_left(std::int32_t n) const {
    if (n == 0) {
      return *this;
    }
    std::int32_t new_scale = check_scale(static_cast<std::int64_t>(scale_) + n);
    decimal num(int_val_, int_compact_, new_scale, 0);
    return num.scale_ < 0 ? num.set_scale(0, round_mode::UNNECESSARY) : num;
  }

  // 返回小数点右移 n 位后的值,等价于 this * 10^n
  // n 可以为负,此时等价于向左移动,结果 scale 不小于 0
  decimal move_point_right(std::int32_t n) const {
    if (n == 0) {
      return *this;
    }
    std::int32_t new_scale = check_scale(static_cast<std::int64_t>(scale_) - n);
    decimal num(int_val_, int_compact_, new_scale, 0);
    return num.scale_ < 0 ? num.set_scale(0, round_mode::UNNECESSARY) : num;
  }

  // 返回数值等于 this * 10^n 的 decimal,仅通过调整 scale 实现
  // 与 move_point_right() 不同,结果允许具有负 scale
  decimal scale_by_power_of_ten(std::int32_t n) const {
    return decimal(int_val_, int_compact_, check_scale(static_cast<std::int64_t>(scale_) - n), precision_);
  }

  // 返回数值相等且移除 unscaled value 十进制尾随零后的 decimal
  // 每移除一个零便将 scale 减一,零值规范化为 scale 为 0 的 ZERO
  decimal strip_trailing_zeros() const {
    // compact 模式下 int_val_ 可能为空,仅当 int_compact_ == INFLATED 时才读取 int_val_
    if (signum() == 0) {
      return ZERO;
    } else if (int_compact_ != INFLATED) {
      return create_and_strip_zeros_to_match_scale(int_compact_, scale_, INT64_MIN);
    } else {
      return create_and_strip_zeros_to_match_scale(int_val_, scale_, INT64_MIN);
    }
  }

  // 返回当前表示的 scale
  // 非负值表示小数点右侧位数,负值表示 unscaled value 还需乘以 10^(-scale)
  std::int32_t scale() const {
    return scale_;
  }

  // 返回 unscaled value 绝对值的十进制位数,零值返回 1
  // 首次计算后缓存结果,precision 不包含前导正负号
  std::int32_t precision() const {
    std::int32_t result = precision_;
    if (result == 0) {
      std::int64_t s = int_compact_;
      if (s != INFLATED) {
        result = long_digit_length(s);
      } else {
        result = big_digit_length(int_val_);
      }
      precision_ = result;
    }
    return result;
  }

  // 返回完整 bigint unscaled value,满足 this == unscaled_value() * 10^(-scale())
  bigint unscaled_value() const {
    return inflated();
  }

  // 将 unscaled value 转换为 std::int64_t
  // inflated 值无法完整表示时仅保留二进制补码低 64 bit,可能丢失 magnitude 和符号信息
  std::int64_t unscaled_long_value() const {
    if (int_compact_ != INFLATED) {
      return int_compact_;
    }
    return int_val_.long_value();
  }

  // 将 unscaled value 精确转换为 std::int64_t
  // 无法完整表示时抛出 std::runtime_error
  std::int64_t unscaled_long_value_exact() const {
    if (int_compact_ != INFLATED) {
      return int_compact_;
    }
    return int_val_.long_value_exact();
  }

  // 将当前值向零截断为 bigint
  // 任何小数部分都会被丢弃,结果可能与当前值不相等
  bigint to_big_integer() const {
    return set_scale(0, round_mode::DOWN).inflated();
  }

  // 将当前值精确转换为 bigint
  // 存在非零小数部分时抛出异常
  bigint to_big_integer_exact() const {
    return set_scale(0, round_mode::UNNECESSARY).inflated();
  }

  // 将 val 检查并转换为 std::int32_t scale
  // int_compact 非零且 val 越界时抛出异常,零值越界时饱和到最近边界
  static std::int32_t check_scale(std::int64_t int_compact, std::int64_t val) {
    std::int32_t as_int = static_cast<std::int32_t>((val));
    if (static_cast<std::int64_t>((as_int)) != val) {
      as_int = (val > INT32_MAX) ? INT32_MAX : INT32_MIN;
      if (int_compact != 0LL) {
        throw std::runtime_error(as_int > 0 ? "underflow" : "overflow");
      }
    }
    return as_int;
  }

  // 将 val 检查并转换为 std::int32_t scale
  // int_val 非零且 val 越界时抛出异常,零值越界时饱和到最近边界
  static std::int32_t check_scale(const bigint& int_val, std::int64_t val) {
    std::int32_t as_int = static_cast<std::int32_t>((val));
    if (static_cast<std::int64_t>((as_int)) != val) {
      as_int = (val > INT32_MAX) ? INT32_MAX : INT32_MIN;
      if (int_val.signum_ != 0) {
        throw std::runtime_error(as_int > 0 ? "underflow" : "overflow");
      }
    }
    return as_int;
  }

  // 将具有相同 scale 的两个 compact unscaled value 相加
  // compact 加法溢出时自动切换到 bigint,结果保留指定 scale
  static decimal add_compact(std::int64_t xs, std::int64_t ys, std::int32_t scale) {
    std::int64_t sum = add_64(xs, ys);
    if (sum != INFLATED) {
      return value_of(sum, scale);
    }
    return value_of(bigint::value_of(xs).add(ys), scale, 0);
  }

  // 对齐两个 compact unscaled value 的 scale 后相加
  // 结果采用 max(scale1, scale2),缩放或加法溢出时切换到 bigint
  static decimal add_compact_unaligned(std::int64_t xs, std::int32_t scale1, std::int64_t ys, std::int32_t scale2) {
    std::int64_t sdiff = static_cast<std::int64_t>((scale1)) - scale2;
    if (sdiff == 0) {
      return add_compact(xs, ys, scale1);
    } else if (sdiff < 0) {
      std::int32_t raise = check_scale(xs, -sdiff);
      std::int64_t scaled_x = long_mul_pow10(xs, raise);
      if (scaled_x != INFLATED) {
        return add_compact(scaled_x, ys, scale2);
      }
      bigint bigsum = big_mul_pow10(xs, raise).add(ys);
      return ((xs ^ ys) >= 0) ? decimal(std::move(bigsum), INFLATED, scale2, 0)
                              : value_of(std::move(bigsum), scale2, 0);
    } else {
      std::int32_t raise = check_scale(ys, sdiff);
      std::int64_t scaled_y = long_mul_pow10(ys, raise);
      if (scaled_y != INFLATED) {
        return add_compact(xs, scaled_y, scale1);
      }
      bigint bigsum = big_mul_pow10(ys, raise).add(xs);
      return ((xs ^ ys) >= 0) ? decimal(std::move(bigsum), INFLATED, scale1, 0)
                              : value_of(std::move(bigsum), scale1, 0);
    }
  }

  // 对齐 compact unscaled value xs 与 bigint snd_in 的 scale 后相加
  // 结果采用 max(scale1, scale2),必要时对较低 scale 的操作数乘以十次幂
  static decimal add_mixed(std::int64_t xs, std::int32_t scale1, const bigint& snd_in, std::int32_t scale2) {
    std::int32_t rscale = scale1;
    std::int64_t sdiff = static_cast<std::int64_t>((rscale)) - scale2;
    bool same_sign = ((xs > 0) == (snd_in.signum_ > 0)) || (xs == 0 && snd_in.signum_ == 0);
    bigint sum;
    if (sdiff < 0) {
      std::int32_t raise = check_scale(xs, -sdiff);
      rscale = scale2;
      std::int64_t scaled_x = long_mul_pow10(xs, raise);
      sum = (scaled_x == INFLATED) ? snd_in.add(big_mul_pow10(xs, raise)) : snd_in.add(scaled_x);
    } else if (sdiff == 0) {
      sum = snd_in.add(xs);
    } else {
      std::int32_t raise = check_scale(snd_in, sdiff);
      bigint snd = big_mul_pow10(snd_in, raise);
      sum = snd.add(xs);
    }
    return same_sign ? decimal(std::move(sum), INFLATED, rscale, 0) : value_of(std::move(sum), rscale, 0);
  }

  // 对齐两个 bigint unscaled value 的 scale 后相加
  // 结果采用 max(scale1, scale2),可压缩时自动切换到 compact 表示
  static decimal add_inflated(const bigint& fst_in, std::int32_t scale1, const bigint& snd_in, std::int32_t scale2) {
    std::int32_t rscale = scale1;
    std::int64_t sdiff = static_cast<std::int64_t>((rscale)) - scale2;
    bigint sum;
    if (sdiff < 0) {
      std::int32_t raise = check_scale(fst_in, -sdiff);
      rscale = scale2;
      sum = big_mul_pow10(fst_in, raise).add(snd_in);
    } else if (sdiff > 0) {
      std::int32_t raise = check_scale(snd_in, sdiff);
      sum = fst_in.add(big_mul_pow10(snd_in, raise));
    } else {
      sum = fst_in.add(snd_in);
    }
    return (fst_in.signum_ == snd_in.signum_) ? decimal(std::move(sum), INFLATED, rscale, 0)
                                              : value_of(std::move(sum), rscale, 0);
  }

  // 将两个 compact unscaled value 相乘并使用 check_scale 计算的 scale 构造结果
  // compact 乘法溢出时切换到 bigint
  static decimal multiply_compact(std::int64_t x, std::int64_t y, std::int32_t scale) {
    std::int64_t product = multiply_64(x, y);
    if (product != INFLATED) {
      return value_of(product, scale);
    }
    return value_of(bigint::value_of(x).multiply(y), scale, 0);
  }

  // 将 compact unscaled value x 与 bigint y 相乘并使用指定 scale 构造结果
  // 结果可压缩时自动使用 compact 表示
  static decimal multiply_mixed(std::int64_t x, const bigint& y, std::int32_t scale) {
    if (x == 0 || y.signum_ == 0) {
      return zero_value_of(scale);
    }
    return value_of(y.multiply(x), scale, 0);
  }

  // 将两个 bigint unscaled value 相乘并使用指定 scale 构造结果
  // 结果可压缩时自动使用 compact 表示
  static decimal multiply_inflated(const bigint& x, const bigint& y, std::int32_t scale) {
    if (x.signum_ == 0 || y.signum_ == 0) {
      return zero_value_of(scale);
    }
    return value_of(x.multiply(y), scale, 0);
  }

  // 将无符号 128 位 dividend 除以无符号 64 位 divisor 并按 rounding_mode 舍入
  // qsign 指定结果符号,商无法表示为 compact std::int64_t 时返回 std::nullopt
  static std::optional<decimal> try_divide_and_round_128(std::int64_t dividend_hi, std::int64_t dividend_lo,
                                                         std::int64_t divisor, std::int32_t sign, std::int32_t scale,
                                                         round_mode rounding_mode, std::int32_t preferred_scale) {
    if (dividend_hi >= divisor) {
      return std::nullopt;
    }

    const std::int32_t shift = decimal_detail::count_leading_zeros(static_cast<std::uint64_t>((divisor)));
    divisor <<= shift;

    const std::int64_t v1 = static_cast<std::uint64_t>((divisor)) >> 32;
    const std::int64_t v0 = divisor & 0xffffffffLL;

    std::int64_t tmp = dividend_lo << shift;
    std::int64_t u1 = static_cast<std::uint64_t>((tmp)) >> 32;
    std::int64_t u0 = tmp & 0xffffffffLL;

    tmp = (dividend_hi << shift) | (static_cast<std::uint64_t>((dividend_lo)) >> (64 - shift));
    std::int64_t u2 = tmp & 0xffffffffLL;
    std::int64_t q1;
    std::int64_t r_tmp;
    if (v1 == 1) {
      q1 = tmp;
      r_tmp = 0;
    } else if (tmp >= 0) {
      q1 = tmp / v1;
      r_tmp = tmp - q1 * v1;
    } else {
      auto rq = div_rem_negative_long(tmp, v1);
      q1 = rq.second;
      r_tmp = rq.first;
    }

    while (q1 >= DIV_NUM_BASE || unsigned_long_compare(static_cast<std::uint64_t>((q1 * v0)),
                                                       static_cast<std::uint64_t>((make_64(r_tmp, u1))))) {
      q1--;
      r_tmp += v1;
      if (r_tmp >= DIV_NUM_BASE) {
        break;
      }
    }

    tmp = mulsub(u2, u1, v1, v0, q1);
    u1 = tmp & 0xffffffffLL;
    std::int64_t q0;
    if (v1 == 1) {
      q0 = tmp;
      r_tmp = 0;
    } else if (tmp >= 0) {
      q0 = tmp / v1;
      r_tmp = tmp - q0 * v1;
    } else {
      auto rq = div_rem_negative_long(tmp, v1);
      q0 = rq.second;
      r_tmp = rq.first;
    }

    while (q0 >= DIV_NUM_BASE || unsigned_long_compare(static_cast<std::uint64_t>((q0 * v0)),
                                                       static_cast<std::uint64_t>((make_64(r_tmp, u0))))) {
      q0--;
      r_tmp += v1;
      if (r_tmp >= DIV_NUM_BASE) {
        break;
      }
    }

    if (static_cast<std::int32_t>((q1)) < 0) {
      jarray<std::uint32_t> mag(2);
      mag[0] = static_cast<std::uint32_t>((q1));
      mag[1] = static_cast<std::uint32_t>((q0));
      mutable_bigint mq(mag);
      if (rounding_mode == round_mode::DOWN && scale == preferred_scale) {
        return mq_to_decimal(mq, sign, scale);
      }
      std::int64_t r = static_cast<std::uint64_t>((mulsub(u1, u0, v1, v0, q0))) >> shift;
      if (r != 0) {
        // 使用无符号右移还原 divisor
        if (need_increment(static_cast<std::int64_t>((static_cast<std::uint64_t>((divisor)) >> shift)), rounding_mode,
                           sign, mq, r)) {
          mq.add(mutable_bigint::ONE);
        }
        return mq_to_decimal(mq, sign, scale);
      }
      if (preferred_scale != scale) {
        mq.normalize();
        bigint iv(sign, mq.to_int_array());
        return create_and_strip_zeros_to_match_scale(iv, scale, preferred_scale);
      }
      return mq_to_decimal(mq, sign, scale);
    }

    std::int64_t q = make_64(q1, q0);
    q *= sign;

    if (rounding_mode == round_mode::DOWN && scale == preferred_scale) {
      return value_of(q, scale);
    }

    std::int64_t r = static_cast<std::uint64_t>((mulsub(u1, u0, v1, v0, q0))) >> shift;
    if (r != 0) {
      // 使用无符号右移还原 divisor,因为左移后高位可能置位
      const bool increment = need_increment(static_cast<std::int64_t>((static_cast<std::uint64_t>((divisor)) >> shift)),
                                            rounding_mode, sign, q, r);
      return value_of(increment ? q + sign : q, scale);
    }
    if (preferred_scale != scale) {
      return create_and_strip_zeros_to_match_scale(q, scale, preferred_scale);
    }
    return value_of(q, scale);
  }

  // 将有符号 128 位 unscaled value 按 mc 舍入并使用 scale 构造 decimal
  // 结果无法保留在 compact 路径时返回 std::nullopt,由调用方回退到 bigint
  static std::optional<decimal> do_round_128(std::int64_t hi, std::int64_t lo, std::int32_t sign, std::int32_t scale,
                                             const math_context& mc) {
    const std::int32_t mcp = mc.precision();
    if (mcp <= 0) {
      return std::nullopt;
    }
    const std::int32_t drop = precision_128(hi, lo) - mcp;
    if (drop <= 0 || drop >= 19) {
      return std::nullopt;
    }
    scale = check_scale_non_zero(static_cast<std::int64_t>(scale) - drop);
    std::optional<decimal> res =
        try_divide_and_round_128(hi, lo, LONG_TEN_POWERS_TABLE[drop], sign, scale, mc.get_rounding_mode(), scale);
    if (!res.has_value()) {
      return std::nullopt;
    }
    return do_round(*res, mc);
  }

  // 计算 (dividend0 * dividend1) / divisor 并按 rounding_mode 返回 decimal
  // 乘积使用 128 位中间值,compact 商不可用时回退到 bigint
  static decimal multiply_divide_and_round(std::int64_t dividend0, std::int64_t dividend1, std::int64_t divisor,
                                           std::int32_t scale, round_mode rm, std::int32_t preferred_scale) {
    if (auto result = try_multiply_divide_and_round(dividend0, dividend1, divisor, scale, rm, preferred_scale)) {
      return std::move(*result);
    }
    return divide_and_round(bigint::value_of(dividend0).multiply(dividend1), divisor, scale, rm, preferred_scale);
  }

  // 尝试在 compact 128 位路径计算 (dividend0 * dividend1) / divisor
  // 商超出 std::int64_t 范围时返回 std::nullopt,由调用方回退到 bigint
  static std::optional<decimal> try_multiply_divide_and_round(std::int64_t dividend0, std::int64_t dividend1,
                                                              std::int64_t divisor, std::int32_t scale, round_mode rm,
                                                              std::int32_t preferred_scale) {
    if (divisor == 0) {
      throw std::runtime_error("division by zero");
    }

    const std::uint64_t divisor_magnitude = decimal_detail::unsigned_magnitude(divisor);
    if (divisor_magnitude > static_cast<std::uint64_t>(INT64_MAX)) {
      return std::nullopt;
    }

    const decimal_detail::uint128_words product = decimal_detail::multiply_64x64(
        decimal_detail::unsigned_magnitude(dividend0), decimal_detail::unsigned_magnitude(dividend1));
    const std::int32_t quotient_sign = ((dividend0 < 0) == (dividend1 < 0)) == (divisor > 0) ? 1 : -1;
    const decimal_detail::uint128_words maximum_dividend =
        decimal_detail::multiply_64x64(divisor_magnitude, static_cast<std::uint64_t>(INT64_MAX));
    if (product.high > maximum_dividend.high ||
        (product.high == maximum_dividend.high && product.low > maximum_dividend.low)) {
      return std::nullopt;
    }

#if DECIMAL_DETAIL_HAS_FAST_DIV128
    if (product.high >= divisor_magnitude) {
      return std::nullopt;
    }
    std::uint64_t remainder = 0;
    const std::uint64_t quotient =
        decimal_detail::divide_128_by_64(product.high, product.low, divisor_magnitude, &remainder);
    if (quotient > static_cast<std::uint64_t>(INT64_MAX)) {
      return std::nullopt;
    }
    std::int64_t qq = static_cast<std::int64_t>(quotient) * quotient_sign;
    if (rm == round_mode::DOWN && scale == preferred_scale) {
      return value_of(qq, scale);
    }
    if (remainder != 0) {
      if (need_increment(static_cast<std::int64_t>(divisor_magnitude), rm, quotient_sign, qq,
                         static_cast<std::int64_t>(remainder))) {
        qq += quotient_sign;
      }
      return value_of(qq, scale);
    }
    // 余数为 0 时才 strip 尾随零
    if (preferred_scale != scale) {
      return create_and_strip_zeros_to_match_scale(qq, scale, preferred_scale);
    }
    return value_of(qq, scale);
#else
    return try_divide_and_round_128(static_cast<std::int64_t>(product.high), static_cast<std::int64_t>(product.low),
                                    static_cast<std::int64_t>(divisor_magnitude), quotient_sign, scale, rm,
                                    preferred_scale);
#endif
  }

  // 对可能为负的 std::int64_t dividend 和 divisor 执行向零截断除法
  // 返回值 first 为余数,second 为商,支持最小负数而不触发 abs 溢出
  static std::pair<std::int64_t, std::int64_t> div_rem_negative_long(std::int64_t n, std::int64_t d) {
    std::int64_t q =
        static_cast<std::int64_t>(((static_cast<std::uint64_t>((n)) >> 1) / (static_cast<std::uint64_t>((d)) >> 1)));
    std::int64_t r = n - q * d;
    while (r < 0) {
      r += d;
      q--;
    }
    while (r >= d) {
      r -= d;
      q++;
    }
    return std::pair<std::int64_t, std::int64_t>(r, q);
  }

  // 返回 unscaled value 为 sign * 10^n 且具有指定 scale 的 decimal
  // n 较小时使用 compact 十次幂,较大时使用 bigint
  static decimal scaled_ten_pow(std::int32_t n, std::int32_t sign, std::int32_t scale) {
    if (n < 0) {
      throw std::invalid_argument("negative power");
    }
    if (n < 19) {
      std::int64_t v = LONG_TEN_POWERS_TABLE[n];
      if (sign < 0) {
        v = -v;
      }
      return value_of(v, scale);
    }
    bigint iv = big_ten_to_the(n);
    if (sign < 0) {
      iv = iv.negate();
    }
    return value_of(iv, scale, 0);
  }

  // 在被除数与除数绝对值相等时构造带舍入的十次幂商
  // raise 是 unscaled 商的十次幂指数,qsign 指定结果符号
  static decimal rounded_ten_power(std::int32_t qsign, std::int32_t raise, std::int32_t scale,
                                   std::int32_t preferred_scale) {
    if (scale > preferred_scale) {
      const std::int32_t diff = scale - preferred_scale;
      if (diff < raise) {
        return scaled_ten_pow(raise - diff, qsign, preferred_scale);
      }
      return value_of(static_cast<std::int64_t>((qsign)), scale - raise);
    }
    return scaled_ten_pow(raise, qsign, scale);
  }

  // 将两个 compact unscaled value 相乘并按 mc 舍入
  // 乘积无法保留在 compact 路径时切换到 bigint
  static decimal multiply_and_round(std::int64_t x, std::int64_t y, std::int32_t scale, const math_context& mc) {
    std::int64_t product = multiply_64(x, y);
    if (product != INFLATED) {
      return do_round(product, scale, mc);
    }
    const std::int32_t result_sign = (x < 0) == (y < 0) ? 1 : -1;
    const decimal_detail::uint128_words wide_product =
        decimal_detail::multiply_64x64(decimal_detail::unsigned_magnitude(x), decimal_detail::unsigned_magnitude(y));
    if (auto res = do_round_128(static_cast<std::int64_t>(wide_product.high),
                                static_cast<std::int64_t>(wide_product.low), result_sign, scale, mc)) {
      return std::move(*res);
    }
    decimal res = value_of(bigint::value_of(x).multiply(y), scale, 0);
    return do_round_value(res, mc);
  }

  // 将 compact x 与 bigint y 的 unscaled value 相乘并按 mc 舍入
  static decimal multiply_and_round(std::int64_t x, const bigint& y, std::int32_t scale, const math_context& mc) {
    if (x == 0) {
      return zero_value_of(scale);
    }
    return do_round(y.multiply(x), scale, mc);
  }

  // 将两个 bigint unscaled value 相乘并按 mc 舍入
  static decimal multiply_and_round(const bigint& x, const bigint& y, std::int32_t scale, const math_context& mc) {
    return do_round(x.multiply(y), scale, mc);
  }

  // 使用 compact unscaled value 和 scale 构造 decimal,随后按 mc 舍入
  // mc.precision() == 0 时不舍入
  static decimal do_round(std::int64_t compact, std::int32_t scale, const math_context& mc) {
    std::int32_t mcp = mc.precision();
    if (mcp == 0 || compact == 0) {
      return value_of(compact, scale);
    }
    std::int32_t prec = long_digit_length(compact);
    std::int32_t drop = prec - mcp;
    std::int64_t cur = compact;
    std::int32_t cur_scale = scale;
    while (drop > 0) {
      cur_scale = check_scale_non_zero(static_cast<std::int64_t>((cur_scale)) - drop);
      cur = divide_and_round_64(cur, LONG_TEN_POWERS_TABLE[drop], mc.get_rounding_mode());
      prec = long_digit_length(cur);
      drop = prec - mcp;
    }
    return value_of(cur, cur_scale, prec);
  }

  // 返回 d 按 mc 舍入后的 decimal
  // mc.precision() == 0 时直接返回 d
  static decimal do_round_value(const decimal& val, const math_context& mc) {
    if (mc.precision() == 0) {
      return val;
    }
    if (val.int_compact_ != INFLATED) {
      return do_round(val.int_compact_, val.scale_, mc);
    }
    return do_round(val.int_val_, val.scale_, mc);
  }

  // 将 snd 与 fst 对齐到两者较大的 scale 后相加
  // 用于无限精度加法,结果的 preferred scale 为 max(fst.scale(), snd.scale())
  static void match_scale(std::pair<decimal, decimal>& val) {
    if (val.first.scale_ < val.second.scale_) {
      val.first = val.first.set_scale(val.second.scale_, round_mode::UNNECESSARY);
    } else if (val.second.scale_ < val.first.scale_) {
      val.second = val.second.set_scale(val.first.scale_, round_mode::UNNECESSARY);
    }
  }

  // 在有限 precision 加法前调整较小操作数的 scale 和有效位位置
  // 保留会影响最终舍入的最高丢弃位,避免为巨大 scale 差分配十次幂
  std::pair<decimal, decimal> pre_align(const decimal& augend, std::int64_t padding, const math_context& mc) const {
    decimal big;
    decimal small;
    if (padding < 0) {
      big = *this;
      small = augend;
    } else {
      big = augend;
      small = *this;
    }
    std::int64_t est_result_ulp_scale = static_cast<std::int64_t>((big.scale_)) - big.precision() + mc.precision();
    std::int64_t small_high_digit_pos = static_cast<std::int64_t>((small.scale_)) - small.precision() + 1;
    if (small_high_digit_pos > big.scale_ + 2 && small_high_digit_pos > est_result_ulp_scale + 2) {
      std::int64_t new_scale = std::max(static_cast<std::int64_t>((big.scale_)), est_result_ulp_scale) + 3;
      small = value_of(small.signum(), check_scale(new_scale));
    }
    return std::pair<decimal, decimal>(big, small);
  }

  // 忽略符号并按数值比较 lhs 与 rhs 的绝对值
  // 返回 -1、0 或 1,必要时对齐 scale 后比较 unscaled magnitude
  std::int32_t compare_magnitude(const decimal& val) const {
    std::int64_t ys = val.int_compact_;
    std::int64_t xs = int_compact_;
    if (xs == 0) {
      return (ys == 0) ? 0 : -1;
    }
    if (ys == 0) {
      return 1;
    }
    std::int64_t sdiff = static_cast<std::int64_t>((scale_)) - val.scale_;
    if (sdiff != 0) {
      std::int64_t xae = static_cast<std::int64_t>((precision())) - scale_;
      std::int64_t yae = static_cast<std::int64_t>((val.precision())) - val.scale_;
      if (xae < yae) {
        return -1;
      }
      if (xae > yae) {
        return 1;
      }
      if (sdiff < 0) {
        if (sdiff > INT32_MIN &&
            (xs == INFLATED || (xs = long_mul_pow10(xs, static_cast<std::int32_t>((-sdiff)))) == INFLATED) &&
            ys == INFLATED) {
          bigint rb = big_mul_pow10(static_cast<std::int32_t>((-sdiff)));
          return rb.compare_magnitude(val.int_val_);
        }
      } else {
        if (sdiff <= INT32_MAX &&
            (ys == INFLATED || (ys = long_mul_pow10(ys, static_cast<std::int32_t>((sdiff)))) == INFLATED) &&
            xs == INFLATED) {
          bigint rb = val.big_mul_pow10(static_cast<std::int32_t>((sdiff)));
          return int_val_.compare_magnitude(rb);
        }
      }
    }
    if (xs != INFLATED) {
      return (ys != INFLATED) ? long_compare_magnitude(xs, ys) : -1;
    }
    if (ys != INFLATED) {
      return 1;
    }
    return int_val_.compare_magnitude(val.int_val_);
  }

  // 忽略符号并比较两个 compact unscaled value 在不同 scale 下的数值幅度
  // 先尝试 compact 十次幂缩放,溢出时切换到 bigint
  static std::int32_t compare_magnitude_normalized(std::int64_t xs, std::int32_t xscale, std::int64_t ys,
                                                   std::int32_t yscale) {
    std::int64_t sdiff = static_cast<std::int64_t>((xscale)) - yscale;
    if (sdiff == 0) {
      return long_compare_magnitude(xs, ys);
    }
    if (sdiff < 0) {
      std::int32_t raise = check_scale(xs, -sdiff);
      std::int64_t sx = long_mul_pow10(xs, raise);
      if (sx != INFLATED) {
        return long_compare_magnitude(sx, ys);
      }
      return big_mul_pow10(xs, raise).compare_magnitude(bigint::value_of(ys));
    }
    std::int32_t raise = check_scale(ys, sdiff);
    std::int64_t sy = long_mul_pow10(ys, raise);
    if (sy != INFLATED) {
      return long_compare_magnitude(xs, sy);
    }
    return bigint::value_of(xs).compare_magnitude(big_mul_pow10(ys, raise));
  }

  // 忽略符号并比较 compact lhs 与 bigint rhs 在不同 scale 下的数值幅度
  static std::int32_t compare_magnitude_normalized(std::int64_t xs, std::int32_t xscale, const bigint& ys,
                                                   std::int32_t yscale) {
    std::int64_t sdiff = static_cast<std::int64_t>((xscale)) - yscale;
    if (sdiff < 0) {
      std::int32_t raise = check_scale(xs, -sdiff);
      std::int64_t sx = long_mul_pow10(xs, raise);
      if (sx != INFLATED) {
        return bigint::value_of(sx).compare_magnitude(ys);
      }
      return big_mul_pow10(xs, raise).compare_magnitude(ys);
    }
    std::int32_t raise = check_scale(ys, sdiff);
    return bigint::value_of(xs).compare_magnitude(big_mul_pow10(ys, raise));
  }

  // 忽略符号并比较两个 bigint unscaled value 在不同 scale 下的数值幅度
  static std::int32_t compare_magnitude_normalized(const bigint& xs, std::int32_t xscale, const bigint& ys,
                                                   std::int32_t yscale) {
    std::int64_t sdiff = static_cast<std::int64_t>((xscale)) - yscale;
    if (sdiff == 0) {
      return xs.compare_magnitude(ys);
    }
    if (sdiff < 0) {
      std::int32_t raise = check_scale(xs, -sdiff);
      return big_mul_pow10(xs, raise).compare_magnitude(ys);
    }
    std::int32_t raise = check_scale(ys, sdiff);
    return xs.compare_magnitude(big_mul_pow10(ys, raise));
  }

  // 在操作数 precision 和 scale 差较小时尝试 compact 除法快速路径
  // 返回 true 表示 result 已写入,否则调用方继续使用通用除法路径
  static decimal divide_small_fast_path(std::int64_t xs, std::int32_t xscale, std::int64_t ys, std::int32_t yscale,
                                        std::int64_t preferred_scale, const math_context& mc) {
    const std::int32_t mcp = mc.precision();
    const round_mode rm = mc.get_rounding_mode();

    // assert (xscale <= yscale) && (yscale < 18) && (mcp < 18)
    const std::int32_t xraise = yscale - xscale;                                    // xraise >= 0
    const std::int64_t scaled_x = (xraise == 0) ? xs : long_mul_pow10(xs, xraise);  // 此处不会溢出
    decimal quotient;

    const std::int32_t cmp = long_compare_magnitude(scaled_x, ys);
    if (cmp > 0) {  // 满足约束 (b)
      yscale -= 1;  // 即 divisor *= 10
      const std::int32_t scl = check_scale_non_zero(preferred_scale + yscale - xscale + mcp);
      if (check_scale_non_zero(static_cast<std::int64_t>((mcp)) + yscale - xscale) > 0) {
        const std::int32_t raise = check_scale_non_zero(static_cast<std::int64_t>((mcp)) + yscale - xscale);
        const std::int64_t scaled_xs = long_mul_pow10(xs, raise);
        if (scaled_xs == INFLATED) {
          std::optional<decimal> q;
          if ((mcp - 1) >= 0 && (mcp - 1) < 19) {
            q = try_multiply_divide_and_round(LONG_TEN_POWERS_TABLE[mcp - 1], scaled_x, ys, scl, rm,
                                              check_scale_non_zero(preferred_scale));
          }
          if (!q.has_value()) {
            const bigint rb = big_mul_pow10(scaled_x, mcp - 1);
            quotient = divide_and_round(rb, ys, scl, rm, check_scale_non_zero(preferred_scale));
          } else {
            quotient = std::move(*q);
          }
        } else {
          quotient = divide_and_round(scaled_xs, ys, scl, rm, check_scale_non_zero(preferred_scale));
        }
      } else {
        const std::int32_t new_scale = check_scale_non_zero(static_cast<std::int64_t>((xscale)) - mcp);
        // assert new_scale >= yscale
        if (new_scale == yscale) {  // 简单情形
          quotient = divide_and_round(xs, ys, scl, rm, check_scale_non_zero(preferred_scale));
        } else {
          const std::int32_t raise = check_scale_non_zero(static_cast<std::int64_t>((new_scale)) - yscale);
          const std::int64_t scaled_ys = long_mul_pow10(ys, raise);
          if (scaled_ys == INFLATED) {
            const bigint rb = big_mul_pow10(ys, raise);
            quotient = divide_and_round(bigint::value_of(xs), rb, scl, rm, check_scale_non_zero(preferred_scale));
          } else {
            quotient = divide_and_round(xs, scaled_ys, scl, rm, check_scale_non_zero(preferred_scale));
          }
        }
      }
    } else {
      // abs(scaled_x) <= abs(ys), 结果为 "scaled_x * 10^mcp / ys"
      const std::int32_t scl = check_scale_non_zero(preferred_scale + yscale - xscale + mcp);
      if (cmp == 0) {
        // abs(scaled_x) == abs(ys), 结果为 10^mcp 量级并带正确符号
        quotient =
            rounded_ten_power(((scaled_x < 0) == (ys < 0)) ? 1 : -1, mcp, scl, check_scale_non_zero(preferred_scale));
      } else {
        // abs(scaled_x) < abs(ys)
        const std::int64_t scaled_xs = long_mul_pow10(scaled_x, mcp);
        if (scaled_xs == INFLATED) {
          std::optional<decimal> q;
          if (mcp < 19) {
            q = try_multiply_divide_and_round(LONG_TEN_POWERS_TABLE[mcp], scaled_x, ys, scl, rm,
                                              check_scale_non_zero(preferred_scale));
          }
          if (!q.has_value()) {
            const bigint rb = big_mul_pow10(scaled_x, mcp);
            quotient = divide_and_round(rb, ys, scl, rm, check_scale_non_zero(preferred_scale));
          } else {
            quotient = std::move(*q);
          }
        } else {
          quotient = divide_and_round(scaled_xs, ys, scl, rm, check_scale_non_zero(preferred_scale));
        }
      }
    }
    // 此处 doRound 仅影响 1000000000 这类情形
    return do_round_value(quotient, mc);
  }

  // 将两个 compact decimal 表示 xs * 10^-xscale 与 ys * 10^-yscale 相除
  // 按 mc 选择商 precision、scale 和舍入方式,除数为零时抛出异常
  static decimal divide(std::int64_t xs, std::int32_t xscale, std::int64_t ys, std::int32_t yscale,
                        std::int64_t preferred_scale, const math_context& mc) {
    std::int32_t mcp = mc.precision();
    if (xscale <= yscale && yscale < 18 && mcp < 18) {
      return divide_small_fast_path(xs, xscale, ys, yscale, preferred_scale, mc);
    }
    if (compare_magnitude_normalized(xs, xscale, ys, yscale) > 0) {
      yscale -= 1;
    }
    std::int32_t scl = check_scale_non_zero(preferred_scale + yscale - xscale + mcp);
    decimal quotient;
    if (check_scale_non_zero(static_cast<std::int64_t>((mcp)) + yscale - xscale) > 0) {
      std::int32_t raise = check_scale_non_zero(static_cast<std::int64_t>((mcp)) + yscale - xscale);
      std::int64_t scaled_xs = long_mul_pow10(xs, raise);
      quotient = (scaled_xs == INFLATED) ? divide_and_round(big_mul_pow10(xs, raise), ys, scl, mc.get_rounding_mode(),
                                                            check_scale_non_zero(preferred_scale))
                                         : divide_and_round(scaled_xs, ys, scl, mc.get_rounding_mode(),
                                                            check_scale_non_zero(preferred_scale));
    } else {
      std::int32_t new_scale = check_scale_non_zero(static_cast<std::int64_t>((xscale)) - mcp);
      if (new_scale == yscale) {
        quotient = divide_and_round(xs, ys, scl, mc.get_rounding_mode(), check_scale_non_zero(preferred_scale));
      } else {
        std::int32_t raise = check_scale_non_zero(static_cast<std::int64_t>((new_scale)) - yscale);
        std::int64_t scaled_ys = long_mul_pow10(ys, raise);
        quotient =
            (scaled_ys == INFLATED)
                ? divide_and_round(bigint::value_of(xs), big_mul_pow10(ys, raise), scl, mc.get_rounding_mode(),
                                   check_scale_non_zero(preferred_scale))
                : divide_and_round(xs, scaled_ys, scl, mc.get_rounding_mode(), check_scale_non_zero(preferred_scale));
      }
    }
    return do_round_value(quotient, mc);
  }

  // 将 bigint xval * 10^-xscale 除以 compact yval * 10^-yscale
  // 按 mc 选择商 precision、scale 和舍入方式
  static decimal divide(const bigint& xs, std::int32_t xscale, std::int64_t ys, std::int32_t yscale,
                        std::int64_t preferred_scale, const math_context& mc) {
    if ((-compare_magnitude_normalized(ys, yscale, xs, xscale)) > 0) {
      yscale -= 1;
    }
    std::int32_t mcp = mc.precision();
    std::int32_t scl = check_scale_non_zero(preferred_scale + yscale - xscale + mcp);
    decimal quotient;
    if (check_scale_non_zero(static_cast<std::int64_t>((mcp)) + yscale - xscale) > 0) {
      std::int32_t raise = check_scale_non_zero(static_cast<std::int64_t>((mcp)) + yscale - xscale);
      quotient = divide_and_round(big_mul_pow10(xs, raise), ys, scl, mc.get_rounding_mode(),
                                  check_scale_non_zero(preferred_scale));
    } else {
      std::int32_t new_scale = check_scale_non_zero(static_cast<std::int64_t>((xscale)) - mcp);
      if (new_scale == yscale) {
        quotient = divide_and_round(xs, ys, scl, mc.get_rounding_mode(), check_scale_non_zero(preferred_scale));
      } else {
        std::int32_t raise = check_scale_non_zero(static_cast<std::int64_t>((new_scale)) - yscale);
        std::int64_t scaled_ys = long_mul_pow10(ys, raise);
        quotient = (scaled_ys == INFLATED) ? divide_and_round(xs, big_mul_pow10(ys, raise), scl, mc.get_rounding_mode(),
                                                              check_scale_non_zero(preferred_scale))
                                           : divide_and_round(xs, scaled_ys, scl, mc.get_rounding_mode(),
                                                              check_scale_non_zero(preferred_scale));
      }
    }
    return do_round_value(quotient, mc);
  }

  // 将 compact xval * 10^-xscale 除以 bigint yval * 10^-yscale
  // 按 mc 选择商 precision、scale 和舍入方式
  static decimal divide(std::int64_t xs, std::int32_t xscale, const bigint& ys, std::int32_t yscale,
                        std::int64_t preferred_scale, const math_context& mc) {
    if (compare_magnitude_normalized(xs, xscale, ys, yscale) > 0) {
      yscale -= 1;
    }
    std::int32_t mcp = mc.precision();
    std::int32_t scl = check_scale_non_zero(preferred_scale + yscale - xscale + mcp);
    decimal quotient;
    if (check_scale_non_zero(static_cast<std::int64_t>((mcp)) + yscale - xscale) > 0) {
      std::int32_t raise = check_scale_non_zero(static_cast<std::int64_t>((mcp)) + yscale - xscale);
      quotient = divide_and_round(big_mul_pow10(xs, raise), ys, scl, mc.get_rounding_mode(),
                                  check_scale_non_zero(preferred_scale));
    } else {
      std::int32_t new_scale = check_scale_non_zero(static_cast<std::int64_t>((xscale)) - mcp);
      std::int32_t raise = check_scale_non_zero(static_cast<std::int64_t>((new_scale)) - yscale);
      quotient = divide_and_round(bigint::value_of(xs), big_mul_pow10(ys, raise), scl, mc.get_rounding_mode(),
                                  check_scale_non_zero(preferred_scale));
    }
    return do_round_value(quotient, mc);
  }

  // 将两个 bigint unscaled value 表示的 decimal 相除
  // 按 mc 选择商 precision、scale 和舍入方式
  static decimal divide(const bigint& xs, std::int32_t xscale, const bigint& ys, std::int32_t yscale,
                        std::int64_t preferred_scale, const math_context& mc) {
    if (compare_magnitude_normalized(xs, xscale, ys, yscale) > 0) {
      yscale -= 1;
    }
    std::int32_t mcp = mc.precision();
    std::int32_t scl = check_scale_non_zero(preferred_scale + yscale - xscale + mcp);
    decimal quotient;
    if (check_scale_non_zero(static_cast<std::int64_t>((mcp)) + yscale - xscale) > 0) {
      std::int32_t raise = check_scale_non_zero(static_cast<std::int64_t>((mcp)) + yscale - xscale);
      quotient = divide_and_round(big_mul_pow10(xs, raise), ys, scl, mc.get_rounding_mode(),
                                  check_scale_non_zero(preferred_scale));
    } else {
      std::int32_t new_scale = check_scale_non_zero(static_cast<std::int64_t>((xscale)) - mcp);
      std::int32_t raise = check_scale_non_zero(static_cast<std::int64_t>((new_scale)) - yscale);
      quotient = divide_and_round(xs, big_mul_pow10(ys, raise), scl, mc.get_rounding_mode(),
                                  check_scale_non_zero(preferred_scale));
    }
    return do_round_value(quotient, mc);
  }

  // 对齐两个 compact unscaled value,使商具有指定 scale,随后按 rounding_mode 相除
  // scale 差导致 compact 缩放溢出时切换到 bigint
  static decimal divide(std::int64_t xs, std::int32_t xscale, std::int64_t ys, std::int32_t yscale, std::int32_t scale,
                        round_mode rm) {
    std::int32_t raise = check_scale_non_zero(static_cast<std::int64_t>((scale)) + yscale - xscale);
    if (raise == 0) {
      return divide_and_round(xs, ys, scale, rm, scale);
    }
    if (raise > 0) {
      std::int64_t scaled_xs = long_mul_pow10(xs, raise);
      return (scaled_xs != INFLATED) ? divide_and_round(scaled_xs, ys, scale, rm, scale)
                                     : divide_and_round(big_mul_pow10(xs, raise), ys, scale, rm, scale);
    }
    std::int64_t scaled_ys = long_mul_pow10(ys, -raise);
    return (scaled_ys != INFLATED)
               ? divide_and_round(xs, scaled_ys, scale, rm, scale)
               : divide_and_round(bigint::value_of(xs), big_mul_pow10(ys, -raise), scale, rm, scale);
  }

  // 对齐 bigint 被除数与 compact 除数,使商具有指定 scale
  // 随后按 rounding_mode 舍入并尽量向 preferred_scale 剥离尾随零
  static decimal divide(const bigint& xs, std::int32_t xscale, std::int64_t ys, std::int32_t yscale, std::int32_t scale,
                        round_mode rm) {
    std::int32_t raise = check_scale_non_zero(static_cast<std::int64_t>((scale)) + yscale - xscale);
    if (raise >= 0) {
      return divide_and_round(big_mul_pow10(xs, raise), ys, scale, rm, scale);
    }
    std::int64_t scaled_ys = long_mul_pow10(ys, -raise);
    return (scaled_ys != INFLATED) ? divide_and_round(xs, scaled_ys, scale, rm, scale)
                                   : divide_and_round(xs, big_mul_pow10(ys, -raise), scale, rm, scale);
  }

  // 对齐 compact 被除数与 bigint 除数,使商具有指定 scale
  // 随后按 rounding_mode 舍入并尽量向 preferred_scale 剥离尾随零
  static decimal divide(std::int64_t xs, std::int32_t xscale, const bigint& ys, std::int32_t yscale, std::int32_t scale,
                        round_mode rm) {
    std::int32_t raise = check_scale_non_zero(static_cast<std::int64_t>((scale)) + yscale - xscale);
    if (raise >= 0) {
      return divide_and_round(big_mul_pow10(xs, raise), ys, scale, rm, scale);
    }
    return divide_and_round(bigint::value_of(xs), big_mul_pow10(ys, -raise), scale, rm, scale);
  }

  // 对齐两个 bigint unscaled value,使商具有指定 scale
  // 随后按 rounding_mode 舍入并尽量向 preferred_scale 剥离尾随零
  static decimal divide(const bigint& xs, std::int32_t xscale, const bigint& ys, std::int32_t yscale,
                        std::int32_t scale, round_mode rm) {
    std::int32_t raise = check_scale_non_zero(static_cast<std::int64_t>((scale)) + yscale - xscale);
    if (raise >= 0) {
      return divide_and_round(big_mul_pow10(xs, raise), ys, scale, rm, scale);
    }
    return divide_and_round(xs, big_mul_pow10(ys, -raise), scale, rm, scale);
  }

  // 使用 magnitude 商 mq、符号 qsign 和 scale 构造 decimal
  // 结果可压缩时使用 compact 表示
  static decimal mq_to_decimal(mutable_bigint& mq, std::int32_t sign, std::int32_t scale) {
    mq.normalize();
    bigint iv(sign, mq.to_int_array());
    return value_of(std::move(iv), scale, 0);
  }

  // 将 magnitude 商 mq 与 qsign 合成为 compact std::int64_t
  // 无法精确表示时返回 INFLATED
  static std::int64_t mq_to_compact_value(mutable_bigint& mq, std::int32_t sign) {
    mq.normalize();
    bigint iv(sign, mq.to_int_array());
    return compact_val_for(iv);
  }

  // 返回非零 decimal 的绝对值是否小于 1
  // 调用方负责保证当前值非零
  static bool fraction_only(const decimal& x) {
    return !x.is_zero() && x.precision() - x.scale_ <= 0;
  }

  // 返回当前值的绝对值是否为 10 的整数次幂
  // 同时检查 unscaled value 和 scale 表示
  static bool is_power_of_ten(std::int64_t v) {
    if (v < 0) {
      v = -v;
    }
    if (v <= 0) {
      return false;
    }
    while (v % 10 == 0) v /= 10;
    return v == 1;
  }

  // 返回正 bigint v 是否恰好为 10 的非负整数次幂
  static bool is_power_of_ten(const bigint& v) {
    if (v.signum_ <= 0) {
      return false;
    }
    if (v.compare_to(bigint::ONE) == 0) {
      return true;
    }
    if (v.mod_uint32(10) != 0) {
      return false;
    }
    mutable_bigint cur(v.mag_);
    mutable_bigint quotient;
    while (true) {
      if (cur.divide(10, quotient) != 0) {
        return false;
      }
      if (quotient.is_one()) {
        return true;
      }
      std::swap(cur, quotient);
    }
  }

  // 返回 decimal x 的精确平方,等价于 x.multiply(x)
  static decimal square(const decimal& x, const math_context& mc) {
    return x.multiply(x, mc);
  }

  // 返回当前 decimal 的 unscaled value 是否恰好为 1
  // 与 scale 结合可判断当前值是否为 10 的幂
  bool is_power_of_ten_unscaled() const {
    return int_compact_ != INFLATED ? int_compact_ == 1 : int_val_.compare_to(bigint::ONE) == 0;
  }

  // 返回保留当前 unscaled value 和 scale 的规范字符串
  // sci 为 true 时使用科学计数法规则,false 时将指数调整为 3 的倍数以生成工程计数法
  std::string layout_chars(bool sci) const {
    if (scale_ == 0) {
      return (int_compact_ != INFLATED) ? std::to_string(int_compact_) : int_val_.to_string();
    }
    if (scale_ == 2 && int_compact_ != INFLATED && int_compact_ >= 0 && int_compact_ < INT32_MAX) {
      std::int32_t low = static_cast<std::int32_t>((int_compact_ % 100));
      std::int32_t high = static_cast<std::int32_t>((int_compact_ / 100));
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%d.%02d", high, low);
      return buf;
    }

    std::string coeff;
    if (int_compact_ != INFLATED) {
      coeff = std::to_string(int_compact_ < 0 ? -int_compact_ : int_compact_);
    } else {
      coeff = int_val_.abs().to_string();
    }
    const std::int32_t coeff_len = static_cast<std::int32_t>((coeff.size()));
    const std::int64_t adjusted = -static_cast<std::int64_t>((scale_)) + (coeff_len - 1);
    const std::int32_t sign = signum();

    std::string out;
    const size_t sign_len = (sign < 0) ? 1u : 0u;
    if (sign < 0) {
      out.push_back('-');
    }

    if (scale_ >= 0 && adjusted >= -6) {
      const std::int32_t pad = scale_ - coeff_len;
      if (pad >= 0) {
        out.reserve(sign_len + 2u + static_cast<size_t>(pad) + coeff.size());
        out += "0.";
        out.append(static_cast<size_t>((pad)), '0');
        out += coeff;
      } else {
        out.reserve(sign_len + coeff.size() + 1u);
        out.append(coeff, 0, static_cast<size_t>((-pad)));
        out.push_back('.');
        out.append(coeff, static_cast<size_t>((-pad)), std::string::npos);
      }
    } else {
      if (sci) {
        out.reserve(sign_len + coeff.size() + 8u);
        out.push_back(coeff[0]);
        if (coeff_len > 1) {
          out.push_back('.');
          out.append(coeff, 1, std::string::npos);
        }
      } else {
        std::int32_t sig = static_cast<std::int32_t>((adjusted % 3));
        if (sig < 0) {
          sig += 3;
        }
        std::int64_t eng_adjusted = adjusted - sig;
        ++sig;
        out.reserve(sign_len + coeff.size() + static_cast<size_t>(std::max<std::int32_t>(0, sig - coeff_len)) + 8u);
        if (sign == 0) {
          switch (sig) {
            case 1:
              out.push_back('0');
              break;
            case 2:
              out += "0.00";
              eng_adjusted += 3;
              break;
            case 3:
              out += "0.0";
              eng_adjusted += 3;
              break;
            default:
              break;
          }
        } else if (sig >= coeff_len) {
          out += coeff;
          out.append(static_cast<size_t>((sig - coeff_len)), '0');
        } else {
          out.append(coeff, 0, static_cast<size_t>((sig)));
          out.push_back('.');
          out.append(coeff, static_cast<size_t>((sig)), std::string::npos);
        }
        if (eng_adjusted != 0) {
          out.push_back('E');
          if (eng_adjusted > 0) {
            out.push_back('+');
          }
          out += std::to_string(eng_adjusted);
        }
        return out;
      }
      if (adjusted != 0) {
        out.push_back('E');
        if (adjusted > 0) {
          out.push_back('+');
        }
        out += std::to_string(adjusted);
      }
    }
    return out;
  }

  // 按指定 scale 生成不含指数字段的字符串,正 scale 插入小数点,负 scale 在非零值末尾补零
  static std::string get_value_string(std::int64_t int_compact, const bigint& int_val, std::int32_t scale) {
    std::string int_string = (int_compact != INFLATED) ? std::to_string(int_compact) : int_val.to_string();
    if (scale == 0) {
      return int_string;
    }
    bool neg = !int_string.empty() && int_string[0] == '-';
    const size_t digit_offset = neg ? 1u : 0u;
    const size_t digit_len = int_string.size() - digit_offset;
    std::int32_t precision = static_cast<std::int32_t>((digit_len));
    if (scale > 0) {
      if (scale >= precision) {
        std::string out;
        out.reserve((neg ? 3u : 2u) + static_cast<size_t>((scale - precision)) + digit_len);
        if (neg) {
          out += "-0.";
        } else {
          out += "0.";
        }
        out.append(static_cast<size_t>((scale - precision)), '0');
        out.append(int_string, digit_offset, std::string::npos);
        return out;
      }
      std::string out;
      out.reserve((neg ? 1u : 0u) + digit_len + 1u);
      if (neg) {
        out.push_back('-');
      }
      out.append(int_string, digit_offset, static_cast<size_t>((precision - scale)));
      out.push_back('.');
      out.append(int_string, digit_offset + static_cast<size_t>((precision - scale)), std::string::npos);
      return out;
    }
    // scale < 0 时无小数点,零值忽略负 scale 并直接返回 "0"
    if (digit_len == 1 && int_string[digit_offset] == '0') {
      return "0";
    }
    std::string out;
    out.reserve((neg ? 1u : 0u) + digit_len + static_cast<size_t>((-static_cast<std::int64_t>(scale))));
    if (neg) {
      out.push_back('-');
    }
    out.append(int_string, digit_offset, std::string::npos);
    out.append(static_cast<size_t>((-static_cast<std::int64_t>(scale))), '0');
    return out;
  }

  // 丢弃小数部分后返回 64 位整数,整数部分超出 int64_t 范围时抛出 overflow
  static std::int64_t long_overflow_check(const decimal& num) {
    decimal integral = num.scale_ == 0 ? num : num.set_scale(0, round_mode::DOWN);
    if (integral.int_compact_ != INFLATED) {
      return integral.int_compact_;
    }
    const bigint& iv = integral.int_val_;
    if (iv.bit_length() > 63) {
      throw std::runtime_error("overflow");
    }
    return iv.long_value();
  }

  static constexpr float FLOAT_10_POW[11] = {1.0f,   10.0f,  100.0f, 1.0e3f, 1.0e4f, 1.0e5f,
                                             1.0e6f, 1.0e7f, 1.0e8f, 1.0e9f, 1.0e10f};
  static constexpr double DOUBLE_10_POW[23] = {1.0,    1.0e1,  1.0e2,  1.0e3,  1.0e4,  1.0e5,  1.0e6,  1.0e7,
                                               1.0e8,  1.0e9,  1.0e10, 1.0e11, 1.0e12, 1.0e13, 1.0e14, 1.0e15,
                                               1.0e16, 1.0e17, 1.0e18, 1.0e19, 1.0e20, 1.0e21, 1.0e22};

  // 返回精确的 this + augend,结果的首选 scale 为两个操作数 scale 的较大值
  decimal add(const decimal& augend) const {
    if (int_compact_ != INFLATED) {
      if (augend.int_compact_ != INFLATED)
        return add_compact_unaligned(int_compact_, scale_, augend.int_compact_, augend.scale_);
      return add_mixed(int_compact_, scale_, augend.int_val_, augend.scale_);
    }
    if (augend.int_compact_ != INFLATED) {
      return add_mixed(augend.int_compact_, augend.scale_, int_val_, scale_);
    }
    return add_inflated(int_val_, scale_, augend.int_val_, augend.scale_);
  }

  // 返回按 mc 精度和舍入模式计算的 this + augend,mc 精度为 0 时返回精确结果
  // 对零操作数尽量保留两个操作数中较大的首选 scale,但不会为此超过 mc 的有效位数
  decimal add(const decimal& augend, const math_context& mc) const {
    if (mc.precision() == 0) {
      return add(augend);
    }
    bool lhs_is_zero = signum() == 0;
    bool augend_is_zero = augend.signum() == 0;
    if (lhs_is_zero || augend_is_zero) {
      std::int32_t preferred_scale = std::max(scale_, augend.scale_);
      if (lhs_is_zero && augend_is_zero) {
        return zero_value_of(preferred_scale);
      }
      decimal result = lhs_is_zero ? do_round_value(augend, mc) : do_round_value(*this, mc);
      if (result.scale_ == preferred_scale) {
        return result;
      }
      if (result.scale_ > preferred_scale) {
        return strip_zeros_to_match_scale(result.int_val_, result.int_compact_, result.scale_, preferred_scale);
      }
      std::int32_t precision_diff = mc.precision() - result.precision();
      std::int32_t scale_diff = preferred_scale - result.scale_;
      if (precision_diff >= scale_diff) {
        return result.set_scale(preferred_scale);
      }
      return result.set_scale(result.scale_ + precision_diff);
    }
    std::int64_t padding = static_cast<std::int64_t>((scale_)) - augend.scale_;
    if (int_compact_ != INFLATED && augend.int_compact_ != INFLATED && padding > -19 && padding < 19) {
      return do_round_value(add_compact_unaligned(int_compact_, scale_, augend.int_compact_, augend.scale_), mc);
    }
    if (padding != 0) {
      std::pair<decimal, decimal> arg = pre_align(augend, padding, mc);
      match_scale(arg);
      return do_round(arg.first.inflated().add(arg.second.inflated()), arg.first.scale_, mc);
    }
    return do_round(inflated().add(augend.inflated()), scale_, mc);
  }

  // 返回精确的 this - subtrahend,结果的首选 scale 为两个操作数 scale 的较大值
  decimal subtract(const decimal& subtrahend) const {
    if (int_compact_ != INFLATED) {
      if (subtrahend.int_compact_ != INFLATED) {
        return add_compact_unaligned(int_compact_, scale_, -subtrahend.int_compact_, subtrahend.scale_);
      }
      return add_mixed(int_compact_, scale_, subtrahend.int_val_.negate(), subtrahend.scale_);
    }
    if (subtrahend.int_compact_ != INFLATED) {
      return add_mixed(-subtrahend.int_compact_, subtrahend.scale_, int_val_, scale_);
    }
    return add_inflated(int_val_, scale_, subtrahend.int_val_.negate(), subtrahend.scale_);
  }

  // 返回按 mc 精度和舍入模式计算的 this - subtrahend,mc 精度为 0 时返回精确结果
  decimal subtract(const decimal& subtrahend, const math_context& mc) const {
    if (mc.precision() == 0) {
      return subtract(subtrahend);
    }
    return add(subtrahend.negate(), mc);
  }

  // 返回精确的 this * multiplicand,结果的首选 scale 为两个操作数 scale 之和
  // scale 之和超出 int32_t 范围且 this 非零时抛出 overflow,零值路径将 scale 饱和到最近边界
  decimal multiply(const decimal& multiplicand) const {
    std::int32_t product_scale = check_scale(static_cast<std::int64_t>((scale_)) + multiplicand.scale_);
    if (int_compact_ != INFLATED) {
      if (multiplicand.int_compact_ != INFLATED)
        return multiply_compact(int_compact_, multiplicand.int_compact_, product_scale);
      return multiply_mixed(int_compact_, multiplicand.int_val_, product_scale);
    }
    if (multiplicand.int_compact_ != INFLATED)
      return multiply_mixed(multiplicand.int_compact_, int_val_, product_scale);
    return multiply_inflated(int_val_, multiplicand.int_val_, product_scale);
  }

  // 返回按 mc 精度和舍入模式计算的 this * multiplicand
  // 结果的首选 scale 为两个操作数 scale 之和,mc 精度为 0 时返回精确结果
  // scale 之和超出 int32_t 范围且 this 非零时抛出 overflow
  decimal multiply(const decimal& multiplicand, const math_context& mc) const {
    if (mc.precision() == 0) {
      return multiply(multiplicand);
    }
    std::int32_t product_scale = check_scale(static_cast<std::int64_t>((scale_)) + multiplicand.scale_);
    if (int_compact_ != INFLATED) {
      if (multiplicand.int_compact_ != INFLATED) {
        return multiply_and_round(int_compact_, multiplicand.int_compact_, product_scale, mc);
      }
      return multiply_and_round(int_compact_, multiplicand.int_val_, product_scale, mc);
    }
    if (multiplicand.int_compact_ != INFLATED) {
      return multiply_and_round(multiplicand.int_compact_, int_val_, product_scale, mc);
    }
    return multiply_and_round(int_val_, multiplicand.int_val_, product_scale, mc);
  }

  // 返回 this / divisor 的精确商,结果的首选 scale 为 this.scale() - divisor.scale()
  // 除数为零或商没有有限十进制展开时抛出异常,必要时增加 scale 以完整表示精确结果
  decimal divide(const decimal& divisor) const {
    if (divisor.signum() == 0) {
      if (signum() == 0) {
        throw std::runtime_error("division undefined");
      }
      throw std::runtime_error("division by zero");
    }
    std::int32_t preferred_scale = saturate_long(static_cast<std::int64_t>((scale_)) - divisor.scale_);
    if (signum() == 0) {
      return zero_value_of(preferred_scale);
    }
    std::int32_t max_prec = static_cast<std::int32_t>(
        (std::min<std::int64_t>(static_cast<std::int64_t>((precision())) +
                                    static_cast<std::int64_t>((std::ceil(10.0 * divisor.precision() / 3.0))),
                                static_cast<std::int64_t>((INT32_MAX)))));
    math_context mc(max_prec, round_mode::UNNECESSARY);
    decimal quotient;
    try {
      quotient = divide(divisor, mc);
    } catch (const std::exception&) {
      throw std::runtime_error("non-terminating decimal expansion; no exact representable decimal result");
    }
    if (preferred_scale > quotient.scale_) {
      return quotient.set_scale(preferred_scale, round_mode::UNNECESSARY);
    }
    return quotient;
  }

  // 返回 scale 与 this 相同的 this / divisor,不能精确表示的部分按 rounding_mode 舍入
  // 除数为零或 rounding_mode 为 UNNECESSARY 且需要舍入时抛出异常
  decimal divide(const decimal& divisor, round_mode rounding_mode) const {
    return divide(divisor, scale_, rounding_mode);
  }

  // 返回 scale 与 this 相同的 this / divisor,使用兼容旧接口的整数舍入模式
  // 舍入模式值无效、除数为零或要求精确但存在舍入时抛出异常
  decimal divide(const decimal& divisor, std::int32_t rounding_mode) const {
    return divide(divisor, ::value_of(static_cast<std::int32_t>(rounding_mode)));
  }

  // 返回具有指定 scale 的 this / divisor,不能精确表示的部分按 rounding_mode 舍入
  // 除数为零或 rounding_mode 为 UNNECESSARY 且需要舍入时抛出异常
  decimal divide(const decimal& divisor, std::int32_t scale, round_mode rounding_mode) const {
    if (divisor.signum() == 0) {
      throw std::runtime_error("division by zero");
    }
    if (int_compact_ != INFLATED) {
      if (divisor.int_compact_ != INFLATED)
        return divide(int_compact_, scale_, divisor.int_compact_, divisor.scale_, scale, rounding_mode);
      return divide(int_compact_, scale_, divisor.int_val_, divisor.scale_, scale, rounding_mode);
    }
    if (divisor.int_compact_ != INFLATED)
      return divide(int_val_, scale_, divisor.int_compact_, divisor.scale_, scale, rounding_mode);
    return divide(int_val_, scale_, divisor.int_val_, divisor.scale_, scale, rounding_mode);
  }

  // 返回具有指定 scale 的 this / divisor,使用兼容旧接口的整数舍入模式
  // 舍入模式值无效、除数为零或要求精确但存在舍入时抛出异常
  decimal divide(const decimal& divisor, std::int32_t scale, std::int32_t rounding_mode) const {
    return divide(divisor, scale, ::value_of(static_cast<std::int32_t>(rounding_mode)));
  }

  // 返回按 mc 精度和舍入模式计算的 this / divisor,结果的首选 scale 为两操作数 scale 之差
  // mc 精度为 0 时要求精确且具有有限十进制展开,除数为零或需要但不允许舍入时抛出异常
  decimal divide(const decimal& divisor, const math_context& mc) const {
    if (mc.precision() == 0) {
      return divide(divisor);
    }
    const std::int64_t preferred_scale = static_cast<std::int64_t>((scale_)) - divisor.scale_;
    if (divisor.signum() == 0) {  // x / 0
      if (signum() == 0) {        // 0 / 0
        throw std::runtime_error("division undefined");
      }
      throw std::runtime_error("division by zero");
    }
    if (signum() == 0) {  // 0 / y
      return zero_value_of(saturate_long(preferred_scale));
    }
    // 注意: 内部 divide 的 xscale/yscale 参数传入的是 precision(),而非 scale
    // 归一化时把 x,y 视作 unscaled*10^-precision,使其落入 [0.1, 1),
    // 从而 divideAndRound 到 mc.precision 即可得到恰好 mc.precision 位有效数字的结果
    std::int32_t xscale = precision();
    std::int32_t yscale = divisor.precision();
    if (int_compact_ != INFLATED) {
      if (divisor.int_compact_ != INFLATED)
        return divide(int_compact_, xscale, divisor.int_compact_, yscale, preferred_scale, mc);
      return divide(int_compact_, xscale, divisor.int_val_, yscale, preferred_scale, mc);
    }
    if (divisor.int_compact_ != INFLATED)
      return divide(int_val_, xscale, divisor.int_compact_, yscale, preferred_scale, mc);
    return divide(int_val_, xscale, divisor.int_val_, yscale, preferred_scale, mc);
  }

  // 返回 this / divisor 向零截断后的整数部分,结果的首选 scale 为 this.scale() - divisor.scale()
  // 商始终精确表示截断后的整数部分,除数为零时抛出异常
  decimal divide_to_integral_value(const decimal& divisor) const {
    std::int32_t preferred_scale = saturate_long(static_cast<std::int64_t>((scale_)) - divisor.scale_);
    if (compare_magnitude(divisor) < 0) {
      return zero_value_of(preferred_scale);
    }
    if (signum() == 0 && divisor.signum() != 0) {
      return set_scale(preferred_scale, round_mode::UNNECESSARY);
    }
    const std::int64_t scale_diff = static_cast<std::int64_t>((scale_)) - static_cast<std::int64_t>((divisor.scale_));
    std::int32_t max_digits = static_cast<std::int32_t>(
        (std::min<std::int64_t>(static_cast<std::int64_t>((precision())) +
                                    static_cast<std::int64_t>((std::ceil(10.0 * divisor.precision() / 3.0))) +
                                    (scale_diff < 0 ? -scale_diff : scale_diff) + static_cast<std::int64_t>(2),
                                static_cast<std::int64_t>((INT32_MAX)))));
    decimal quotient = divide(divisor, math_context(max_digits, round_mode::DOWN));
    if (quotient.scale_ > 0) {
      quotient = quotient.set_scale(0, round_mode::DOWN);
      quotient = strip_zeros_to_match_scale(quotient.int_val_, quotient.int_compact_, quotient.scale_, preferred_scale);
    }
    if (quotient.scale_ < preferred_scale) {
      quotient = quotient.set_scale(preferred_scale, round_mode::UNNECESSARY);
    }
    return quotient;
  }

  // 返回 this / divisor 向零截断后的整数部分,mc 的舍入模式不影响结果
  // mc 精度非零且整数商需要超过该有效位数,或除数为零时抛出异常
  decimal divide_to_integral_value(const decimal& divisor, const math_context& mc) const {
    if (mc.precision() == 0 || compare_magnitude(divisor) < 0) {
      return divide_to_integral_value(divisor);
    }
    std::int32_t preferred_scale = saturate_long(static_cast<std::int64_t>((scale_)) - divisor.scale_);
    decimal result = divide(divisor, math_context(mc.precision(), round_mode::DOWN));
    if (result.scale_ < 0) {
      decimal product = result.multiply(divisor);
      if (subtract(product).compare_magnitude(divisor) >= 0) {
        throw std::runtime_error("division impossible");
      }
    } else if (result.scale_ > 0) {
      result = result.set_scale(0, round_mode::DOWN);
    }
    std::int32_t precision_diff;
    if (preferred_scale > result.scale_ && (precision_diff = mc.precision() - result.precision()) > 0) {
      return result.set_scale(result.scale_ + std::min(precision_diff, preferred_scale - result.scale_));
    }
    return strip_zeros_to_match_scale(result.int_val_, result.int_compact_, result.scale_, preferred_scale);
  }

  // 返回 this - divide_to_integral_value(divisor) * divisor,因此是余数而不是非负模
  // 结果符号与 this 相同或为零且绝对值小于 divisor 的绝对值,除数为零时抛出异常
  decimal remainder(const decimal& divisor) const {
    return subtract(divide_to_integral_value(divisor).multiply(divisor));
  }

  // 使用受 mc 精度约束的整数商计算余数,余数本身不按 mc 舍入
  // 整数商超过 mc 精度或除数为零时抛出异常
  decimal remainder(const decimal& divisor, const math_context& mc) const {
    return subtract(divide_to_integral_value(divisor, mc).multiply(divisor));
  }

  // 同时返回整数商和余数,first 为向零截断的商,second 满足 this = first * divisor + second
  // 除数为零时抛出异常
  std::pair<decimal, decimal> divide_and_remainder(const decimal& divisor) const {
    decimal q = divide_to_integral_value(divisor);
    decimal r = subtract(q.multiply(divisor));
    return std::pair<decimal, decimal>(std::move(q), std::move(r));
  }

  // 同时返回受 mc 精度约束的整数商及由该商精确计算的余数,余数本身不按 mc 舍入
  // 整数商超过 mc 精度或除数为零时抛出异常
  std::pair<decimal, decimal> divide_and_remainder(const decimal& divisor, const math_context& mc) const {
    if (mc.precision() == 0) {
      return divide_and_remainder(divisor);
    }
    decimal q = divide_to_integral_value(divisor, mc);
    decimal r = subtract(q.multiply(divisor));
    return std::pair<decimal, decimal>(std::move(q), std::move(r));
  }

  // 按数值比较 this 与 val,忽略表示形式和 scale 差异,返回负数、零或正数
  // 因此 2.0 与 2.00 的比较结果为零,即使 equals 返回 false
  std::int32_t compare_to(const decimal& val) const {
    if (scale_ == val.scale_) {
      if (int_compact_ != INFLATED && val.int_compact_ != INFLATED) {
        return (int_compact_ < val.int_compact_) ? -1 : ((int_compact_ == val.int_compact_) ? 0 : 1);
      }
      bigint a = (int_compact_ != INFLATED) ? bigint::value_of(int_compact_) : int_val_;
      bigint b = (val.int_compact_ != INFLATED) ? bigint::value_of(val.int_compact_) : val.int_val_;
      return a.compare_to(b);
    }
    std::int32_t xsign = signum();
    std::int32_t ysign = val.signum();
    if (xsign != ysign) {
      return (xsign > ysign) ? 1 : -1;
    }
    if (xsign == 0) {
      return 0;
    }
    std::int32_t cmp = compare_magnitude(val);
    return (xsign > 0) ? cmp : -cmp;
  }

  // 判断 this 与 other 是否具有相同数值和相同 scale,表示同一数值但 scale 不同的值不相等
  bool equals(const decimal& other) const {
    if (scale_ != other.scale_) {
      return false;
    }
    if (int_compact_ != INFLATED && other.int_compact_ != INFLATED) {
      return int_compact_ == other.int_compact_;
    }
    bigint a = (int_compact_ != INFLATED) ? bigint::value_of(int_compact_) : int_val_;
    bigint b = (other.int_compact_ != INFLATED) ? bigint::value_of(other.int_compact_) : other.int_val_;
    return a.compare_to(b) == 0;
  }

  // 按 compare_to 返回 this 与 val 中数值较小者,数值相等时保留 this 的表示形式
  decimal min(const decimal& val) const {
    return compare_to(val) <= 0 ? *this : val;
  }

  // 按 compare_to 返回 this 与 val 中数值较大者,数值相等时保留 this 的表示形式
  decimal max(const decimal& val) const {
    return compare_to(val) >= 0 ? *this : val;
  }

  // 返回由未缩放值和 scale 共同计算的哈希值,保证 equals 相等的对象具有相同哈希值
  // 数值相等但 scale 不同的对象通常具有不同哈希值
  std::int32_t hash_code() const {
    if (int_compact_ != INFLATED) {
      std::int64_t val2 = (int_compact_ < 0) ? -int_compact_ : int_compact_;
      std::int32_t temp = static_cast<std::int32_t>(
          ((static_cast<std::int32_t>((static_cast<std::uint64_t>((val2)) >> 32))) * 31 + (val2 & 0xffffffffLL)));
      return 31 * ((int_compact_ < 0) ? -temp : temp) + scale_;
    }
    return 31 * int_val_.hash_code() + scale_;
  }

  // 返回 this 的绝对值并保留 scale,非负值直接返回等价副本
  decimal abs() const {
    return (signum() < 0) ? negate() : *this;
  }

  // 返回按 mc 精度和舍入模式舍入的绝对值,mc 精度为 0 时结果精确
  decimal abs(const math_context& mc) const {
    return (signum() < 0) ? negate(mc) : plus(mc);
  }

  // 返回与 this 数值相反且 scale 相同的精确结果,零值仍保持原 scale
  decimal negate() const {
    if (int_compact_ == INFLATED) {
      return value_of(int_val_.negate(), scale_, precision_);
    }
    return value_of(-int_compact_, scale_, precision_);
  }

  // 返回按 mc 精度和舍入模式舍入的相反数,mc 精度为 0 时结果精确
  decimal negate(const math_context& mc) const {
    if (mc.precision() == 0) {
      return negate();
    }
    return do_round_value(negate(), mc);
  }

  // 判断数值是否为零,不受 scale 和内部紧凑或大整数表示形式影响
  bool is_zero() const {
    return int_compact_ == 0LL || (int_compact_ == INFLATED && int_val_.signum_ == 0);
  }

  // 返回 this 的 n 次幂,结果精确且首选 scale 为 this.scale() * n
  // n 必须位于 [0, 999999999],n 为 0 时包括 0^0 均返回 1,scale 溢出时抛出异常
  decimal pow(std::int32_t n) const {
    if (n < 0 || n > 999999999) {
      throw std::invalid_argument("invalid operation");
    }
    if (n == 0) {
      return ONE;
    }
    if (is_zero()) {
      return zero_value_of(check_scale(static_cast<std::int64_t>((scale_)) * n));
    }
    std::int32_t new_scale = check_scale(static_cast<std::int64_t>((scale_)) * n);
    if (int_compact_ != INFLATED) {
      bigint r = bigint::value_of(int_compact_).pow(n);
      std::int64_t cv = compact_val_for(r);
      return (cv != INFLATED) ? value_of(cv, new_scale) : value_of(r, new_scale, 0);
    }
    bigint r = int_val_.pow(n);
    std::int64_t cv = compact_val_for(r);
    return (cv != INFLATED) ? value_of(cv, new_scale) : value_of(r, new_scale, 0);
  }

  // 按 ANSI X3.274-1996 算法返回 this 的 n 次幂,中间结果使用扩展精度并最终按 mc 舍入
  // n 必须位于 [-999999999, 999999999],负指数计算正指数幂的倒数,mc 精度为 0 时仅允许非负指数
  // n 的十进制位数超过 mc 精度、结果需要但不允许舍入或除零时抛出异常
  decimal pow(std::int32_t n, const math_context& mc) const {
    if (mc.precision() == 0) {
      return pow(n);
    }
    if (n < -999999999 || n > 999999999) {
      throw std::invalid_argument("invalid operation");
    }
    if (n == 0) {
      return ONE;  // X3.274 中 x**0 == 1
    }
    const decimal lhs = *this;
    math_context workmc = mc;  // 工作精度
    std::uint32_t mag = (n < 0) ? static_cast<std::uint32_t>((-static_cast<std::int64_t>((n))))
                                : static_cast<std::uint32_t>((n));  // |n|
    if (mc.precision() > 0) {
      const std::int32_t elength = long_digit_length(static_cast<std::int64_t>((mag)));  // n 的十进制位数
      if (elength > mc.precision()) {                                                    // X3.274 规则
        throw std::invalid_argument("invalid operation");
      }
      workmc = math_context(mc.precision() + elength + 1, mc.get_rounding_mode());
    }
    // 逐位平方-乘并忽略最高位,使用 std::uint32_t 保证 32 位回绕语义
    decimal acc = ONE;
    bool seenbit = false;
    for (std::int32_t i = 1;; ++i) {
      mag += mag;               // 左移一位
      if (mag & 0x80000000u) {  // 最高位为 1
        seenbit = true;
        acc = acc.multiply(lhs, workmc);
      }
      if (i == 31) {
        break;  // 最后一位
      }
      if (seenbit) {
        acc = acc.multiply(acc, workmc);  // 平方
      }
    }
    if (n < 0) {  // 负指数: 用工作精度求倒数
      acc = ONE.divide(acc, workmc);
    }
    return do_round_value(acc, mc);  // 舍入到目标精度
  }

  // 返回按 mc 精度和舍入模式计算的 this 平方根,结果的首选 scale 为 this.scale() / 2
  // this 为负数时抛出异常,mc 精度为 0 或模式为 UNNECESSARY 时要求平方根能够精确表示
  decimal sqrt(const math_context& mc) const {
    const std::int32_t sig = signum();
    if (sig < 0) {
      throw std::runtime_error("attempted square root of negative bigdecimal");
    }
    if (sig == 0) {
      return zero_value_of(scale_ / 2);
    }

    const std::int32_t preferred_scale = scale_ / 2;
    const decimal zero_with_final_preferred_scale = value_of(0LL, preferred_scale);

    decimal stripped = strip_trailing_zeros();
    const std::int32_t stripped_scale = stripped.scale();

    if (stripped.is_power_of_ten_unscaled() && stripped_scale % 2 == 0) {
      decimal result = value_of(1LL, stripped_scale / 2);
      if (result.scale() != preferred_scale) {
        result = result.add(zero_with_final_preferred_scale, mc);
      }
      return result;
    }

    std::int32_t scale_adjust = 0;
    const std::int32_t norm_scale = stripped.scale() - stripped.precision() + 1;
    if (norm_scale % 2 == 0) {
      scale_adjust = norm_scale;
    } else {
      scale_adjust = norm_scale - 1;
    }

    const decimal working = stripped.scale_by_power_of_ten(scale_adjust);
    decimal approx = decimal(std::sqrt(working.to_double()));

    std::int32_t guess_precision = 15;
    const std::int32_t original_precision = mc.precision();
    std::int32_t target_precision;
    if (original_precision == 0) {
      target_precision = stripped.precision() / 2 + 1;
    } else {
      switch (mc.get_rounding_mode()) {
        case round_mode::HALF_UP:
        case round_mode::HALF_DOWN:
        case round_mode::HALF_EVEN:
          target_precision = original_precision * 2;
          if (target_precision < 0) {
            target_precision = INT32_MAX - 2;
          }
          break;
        default:
          target_precision = original_precision;
          break;
      }
    }

    const std::int32_t working_precision = working.precision();
    do {
      const std::int32_t tmp_precision = std::max(guess_precision, std::max(target_precision + 2, working_precision));
      const math_context mc_tmp(tmp_precision, round_mode::HALF_EVEN);
      approx = ONE_HALF.multiply(approx.add(working.divide(approx, mc_tmp), mc_tmp), mc_tmp);
      guess_precision *= 2;
    } while (guess_precision < target_precision + 2);

    const round_mode target_rm = mc.get_rounding_mode();
    decimal result;
    if (target_rm == round_mode::UNNECESSARY || original_precision == 0) {
      const round_mode tmp_rm = (target_rm == round_mode::UNNECESSARY) ? round_mode::DOWN : target_rm;
      const math_context mc_tmp(target_precision, tmp_rm);
      result = approx.scale_by_power_of_ten(-scale_adjust / 2).round(mc_tmp);
      if (subtract(result.multiply(result)).compare_to(ZERO) != 0) {
        throw std::runtime_error("computed square root not exact");
      }
    } else {
      result = approx.scale_by_power_of_ten(-scale_adjust / 2).round(mc);
      switch (target_rm) {
        case round_mode::DOWN:
        case round_mode::FLOOR:
          if (result.multiply(result).compare_to(*this) > 0) {
            decimal ulp_val = result.ulp();
            if (approx.compare_to(ONE) == 0) {
              ulp_val = ulp_val.multiply(ONE_TENTH);
            }
            result = result.subtract(ulp_val);
          }
          break;
        case round_mode::UP:
        case round_mode::CEILING:
          if (result.multiply(result).compare_to(*this) < 0) {
            result = result.add(result.ulp());
          }
          break;
        default:
          break;
      }
    }

    if (result.scale() != preferred_scale) {
      result = result.strip_trailing_zeros().add(zero_with_final_preferred_scale,
                                                 math_context(original_precision, round_mode::UNNECESSARY));
    }
    return result;
  }

  // 向零丢弃小数部分并转换为 int32_t,超出范围时仅保留低 32 位,不会抛出 overflow
  std::int32_t to_int() const {
    return static_cast<std::int32_t>((to_long()));
  }

  // 向零丢弃小数部分并转换为 int64_t,超出范围时仅保留低 64 位,不会抛出 overflow
  std::int64_t to_long() const {
    if (int_compact_ != INFLATED && scale_ == 0) {
      return int_compact_;
    }
    if (signum() == 0 || fraction_only(*this) || scale_ <= -64) {
      return 0;
    }
    return to_big_integer().long_value();
  }

  // 转换为 float 近似值,可能损失精度或在绝对值过大时返回正负无穷
  float to_float() const {
    return static_cast<float>((to_double()));
  }

  // 转换为 double 近似值,可能损失精度或在绝对值过大时返回正负无穷
  double to_double() const {
    std::string s = to_string();
    char* end = nullptr;
    double d = std::strtod(s.c_str(), &end);
    return d;
  }

  // 精确转换为 int64_t,存在非零小数部分或整数值超出 int64_t 范围时抛出异常
  std::int64_t to_long_exact() const {
    if (int_compact_ != INFLATED && scale_ == 0) {
      return int_compact_;
    }
    if (signum() == 0) {
      return 0;
    }
    if (fraction_only(*this)) {
      throw std::runtime_error("rounding necessary");
    }
    if (precision() - scale_ > 19) {
      throw std::runtime_error("overflow");
    }
    decimal num = set_scale(0, round_mode::UNNECESSARY);
    if (num.precision() >= 19) {
      return long_overflow_check(num);
    }
    return num.int_compact_ != INFLATED ? num.int_compact_ : num.int_val_.long_value();
  }

  // 精确转换为 int32_t,存在非零小数部分或整数值超出 int32_t 范围时抛出异常
  std::int32_t to_int_exact() const {
    std::int64_t v = to_long_exact();
    if (v < INT32_MIN || v > INT32_MAX) {
      throw std::runtime_error("overflow");
    }
    return static_cast<std::int32_t>((v));
  }

  // 精确转换为 int16_t,存在非零小数部分或整数值超出 int16_t 范围时抛出异常
  std::int16_t to_short_exact() const {
    std::int64_t v = to_long_exact();
    if (v < INT16_MIN || v > INT16_MAX) {
      throw std::runtime_error("overflow");
    }
    return static_cast<std::int16_t>((v));
  }

  // 精确转换为 int8_t,存在非零小数部分或整数值超出 int8_t 范围时抛出异常
  std::int8_t to_byte_exact() const {
    std::int64_t v = to_long_exact();
    if (v < INT8_MIN || v > INT8_MAX) {
      throw std::runtime_error("overflow");
    }
    return static_cast<std::int8_t>((v));
  }

  // 返回 this 当前 scale 对应的末位单位 10^-scale,结果始终为正且与 this 具有相同 scale
  decimal ulp() const {
    return value_of(1, scale_);
  }

  // 返回规范字符串表示,必要时使用科学计数法并保留能够体现 scale 的尾随零
  // 相同对象的结果会缓存,该表示可被 decimal 字符串构造函数无损解析
  std::string to_string() const {
    if (!string_cache_.empty()) {
      return string_cache_;
    }
    if (scale_ == 0) {
      string_cache_ = (int_compact_ != INFLATED) ? std::to_string(int_compact_) : int_val_.to_string();
      return string_cache_;
    }
    string_cache_ = layout_chars(true);
    return string_cache_;
  }

  // 返回工程计数法字符串,需要指数字段时保证指数为 3 的倍数且整数部分为一至三位
  std::string to_engineering_string() const {
    return layout_chars(false);
  }

  // 返回不含指数字段的普通十进制字符串,负 scale 通过在非零未缩放值后补零展开
  std::string to_plain_string() const {
    return get_value_string(int_compact_, int_val_, scale_);
  }
};

// 预缓存 [0, 10] 的 decimal 常量
inline const decimal decimal::ZERO_THROUGH_TEN[11] = {
    decimal(bigint::ZERO, 0, 0, 1),         // 0
    decimal(bigint::ONE, 1, 0, 1),          // 1
    decimal(bigint::TWO, 2, 0, 1),          // 2
    decimal(bigint::value_of(3), 3, 0, 1),  // 3
    decimal(bigint::value_of(4), 4, 0, 1),  // 4
    decimal(bigint::value_of(5), 5, 0, 1),  // 5
    decimal(bigint::value_of(6), 6, 0, 1),  // 6
    decimal(bigint::value_of(7), 7, 0, 1),  // 7
    decimal(bigint::value_of(8), 8, 0, 1),  // 8
    decimal(bigint::value_of(9), 9, 0, 1),  // 9
    decimal(bigint::TEN, 10, 0, 2),         // 10
};

// scale 0~15 的零值 decimal 常量
inline const decimal decimal::ZERO_SCALED_BY[16] = {
    ZERO_THROUGH_TEN[0],              // scale 0
    decimal(bigint::ZERO, 0, 1, 1),   // scale 1
    decimal(bigint::ZERO, 0, 2, 1),   // scale 2
    decimal(bigint::ZERO, 0, 3, 1),   // scale 3
    decimal(bigint::ZERO, 0, 4, 1),   // scale 4
    decimal(bigint::ZERO, 0, 5, 1),   // scale 5
    decimal(bigint::ZERO, 0, 6, 1),   // scale 6
    decimal(bigint::ZERO, 0, 7, 1),   // scale 7
    decimal(bigint::ZERO, 0, 8, 1),   // scale 8
    decimal(bigint::ZERO, 0, 9, 1),   // scale 9
    decimal(bigint::ZERO, 0, 10, 1),  // scale 10
    decimal(bigint::ZERO, 0, 11, 1),  // scale 11
    decimal(bigint::ZERO, 0, 12, 1),  // scale 12
    decimal(bigint::ZERO, 0, 13, 1),  // scale 13
    decimal(bigint::ZERO, 0, 14, 1),  // scale 14
    decimal(bigint::ZERO, 0, 15, 1),  // scale 15
};

// 常量 ZERO
inline const decimal decimal::ZERO = ZERO_THROUGH_TEN[0];

// 常量 ONE
inline const decimal decimal::ONE = ZERO_THROUGH_TEN[1];

// 常量 TWO
inline const decimal decimal::TWO = ZERO_THROUGH_TEN[2];

// 常量 TEN
inline const decimal decimal::TEN = ZERO_THROUGH_TEN[10];

// 常量 0.1,scale 为 1
inline const decimal decimal::ONE_TENTH = decimal::value_of(1LL, 1);

// 常量 0.5,scale 为 1
inline const decimal decimal::ONE_HALF = decimal::value_of(5LL, 1);

inline std::int64_t mutable_bigint::to_compact_value(std::int32_t sign) {
  if (int_len_ == 0 || sign == 0) {
    return 0;
  }

  if (int_len_ > 2 || (int_len_ == 2 && (value_[offset_] & UINT32_C(0x80000000)) != 0)) {
    return decimal::INFLATED;
  }

  const std::uint64_t magnitude =
      int_len_ == 2 ? (static_cast<std::uint64_t>(value_[offset_]) << 32) | value_[offset_ + 1] : value_[offset_];
  const std::int64_t compact = static_cast<std::int64_t>(magnitude);
  return sign == -1 ? -compact : compact;
}

inline decimal mutable_bigint::to_decimal(std::int32_t sign, std::int32_t scale) {
  if (int_len_ == 0 || sign == 0) {
    return decimal::zero_value_of(scale);
  }

  const std::int64_t compact = to_compact_value(sign);
  if (compact != decimal::INFLATED) {
    return decimal::value_of(compact, scale);
  }
  return decimal(bigint(sign, to_int_array()), decimal::INFLATED, scale, 0);
}

#undef DECIMAL_DETAIL_HAS_FAST_DIV128
#undef DECIMAL_DETAIL_HAS_MSVC_DIV128
#undef DECIMAL_DETAIL_HAS_MSVC_INTRINSICS
#undef DECIMAL_DETAIL_HAS_NATIVE_INT128
#undef DECIMAL_DETAIL_HAS_OVERFLOW_BUILTINS
#undef DECIMAL_DETAIL_HAS_BIT_BUILTINS
#undef DECIMAL_DETAIL_HAS_BUILTIN
#undef DECIMAL_DETAIL_INTRINSICS_DISABLED

#endif  // DECIMAL_H_
