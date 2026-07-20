#include <decimal/decimal.h>

#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

#define CHECK(condition)                                                           \
  do {                                                                             \
    if (!(condition)) {                                                            \
      std::cerr << "check failed at line " << __LINE__ << ": " #condition << '\n'; \
      return false;                                                                \
    }                                                                              \
  } while (false)

bool equals(const jarray<std::int32_t>& actual, std::initializer_list<std::int32_t> expected) {
  if (actual.length() != static_cast<std::int32_t>(expected.size())) {
    return false;
  }

  std::int32_t index = 0;
  for (std::int32_t value : expected) {
    if (actual[index++] != value) {
      return false;
    }
  }
  return true;
}

template <typename Function>
bool throws_out_of_range(Function&& function) {
  try {
    std::forward<Function>(function)();
  } catch (const std::out_of_range&) {
    return true;
  } catch (...) {
    return false;
  }
  return false;
}

bool test_construction_and_access() {
  jarray<std::int32_t> empty;
  CHECK(empty.length() == 0);
  CHECK(empty.empty());
  CHECK(empty.data() == nullptr);
  CHECK(empty.begin() == nullptr);
  CHECK(empty.end() == nullptr);

  jarray<std::int32_t> zero_length(0);
  CHECK(zero_length.empty());
  CHECK(zero_length.data() == nullptr);

  jarray<std::int32_t> sized(3);
  CHECK(sized.length() == 3);
  CHECK(!sized.empty());
  CHECK(equals(sized, {0, 0, 0}));
  CHECK(sized.begin() == sized.data());
  CHECK(sized.end() == sized.data() + sized.length());

  sized[0] = 10;
  sized[1] = 20;
  sized[2] = 30;
  const jarray<std::int32_t>& const_sized = sized;
  CHECK(const_sized[1] == 20);
  CHECK(const_sized.data() == sized.data());
  CHECK(const_sized.begin() == const_sized.data());
  CHECK(const_sized.end() == const_sized.data() + const_sized.length());

  std::int32_t sum = 0;
  for (std::int32_t value : const_sized) {
    sum += value;
  }
  CHECK(sum == 60);

  const jarray<std::int32_t> initialized{4, 5, 6};
  CHECK(equals(initialized, {4, 5, 6}));
  return true;
}

bool test_copy_and_move() {
  jarray<std::int32_t> source{1, 2, 3};
  jarray<std::int32_t> copy(source);
  CHECK(equals(copy, {1, 2, 3}));
  CHECK(copy.data() != source.data());
  source[0] = 9;
  CHECK(equals(copy, {1, 2, 3}));

  jarray<std::int32_t> assigned{7};
  assigned = source;
  CHECK(equals(assigned, {9, 2, 3}));
  CHECK(assigned.data() != source.data());
  source[1] = 8;
  CHECK(equals(assigned, {9, 2, 3}));

  std::int32_t* assigned_data = assigned.data();
  assigned = assigned;
  CHECK(equals(assigned, {9, 2, 3}));
  CHECK(assigned.data() == assigned_data);

  std::int32_t* source_data = source.data();
  jarray<std::int32_t> moved(std::move(source));
  CHECK(equals(moved, {9, 8, 3}));
  CHECK(moved.data() == source_data);
  CHECK(source.empty());
  CHECK(source.data() == nullptr);

  jarray<std::int32_t> move_assigned{4, 5};
  std::int32_t* moved_data = moved.data();
  move_assigned = std::move(moved);
  CHECK(equals(move_assigned, {9, 8, 3}));
  CHECK(move_assigned.data() == moved_data);
  CHECK(moved.empty());
  CHECK(moved.data() == nullptr);

  std::int32_t* move_assigned_data = move_assigned.data();
  move_assigned = std::move(move_assigned);
  CHECK(equals(move_assigned, {9, 8, 3}));
  CHECK(move_assigned.data() == move_assigned_data);
  return true;
}

bool test_allocation_fill_and_swap() {
  jarray<std::int32_t> values{1, 2, 3};
  values.alloc(4);
  CHECK(equals(values, {0, 0, 0, 0}));

  values.fill(7);
  CHECK(equals(values, {7, 7, 7, 7}));
  values.fill(1, 3, 9);
  CHECK(equals(values, {7, 9, 9, 7}));
  values.fill(2, 2, 5);
  CHECK(equals(values, {7, 9, 9, 7}));

  jarray<std::int32_t> other{10, 11};
  std::int32_t* values_data = values.data();
  std::int32_t* other_data = other.data();
  values.swap(other);
  CHECK(equals(values, {10, 11}));
  CHECK(equals(other, {7, 9, 9, 7}));
  CHECK(values.data() == other_data);
  CHECK(other.data() == values_data);

  values.alloc(0);
  CHECK(values.empty());
  CHECK(values.data() == nullptr);
  values.fill(3);
  return true;
}

bool test_copy_operations() {
  const jarray<std::int32_t> original{1, 2, 3};

  jarray<std::int32_t> shortened = original.copy_of(2);
  CHECK(equals(shortened, {1, 2}));
  jarray<std::int32_t> extended = original.copy_of(5);
  CHECK(equals(extended, {1, 2, 3, 0, 0}));
  CHECK(original.copy_of(0).empty());

  jarray<std::int32_t> range = original.copy_of_range(1, 3);
  CHECK(equals(range, {2, 3}));
  jarray<std::int32_t> extended_range = original.copy_of_range(1, 5);
  CHECK(equals(extended_range, {2, 3, 0, 0}));
  CHECK(original.copy_of_range(3, 3).empty());
  CHECK(equals(original.copy_of_range(3, 5), {0, 0}));

  jarray<std::int32_t> cloned = original.clone();
  CHECK(equals(cloned, {1, 2, 3}));
  CHECK(cloned.data() != original.data());
  cloned[0] = 8;
  CHECK(equals(original, {1, 2, 3}));
  return true;
}

bool test_jarray_copy() {
  const jarray<std::int32_t> source{1, 2, 3, 4};
  jarray<std::int32_t> destination{9, 9, 9, 9, 9};
  jarray_copy(source, 1, destination, 2, 3);
  CHECK(equals(destination, {9, 9, 2, 3, 4}));

  jarray<std::int32_t> overlap{1, 2, 3, 4, 5};
  jarray_copy(overlap, 0, overlap, 1, 4);
  CHECK(equals(overlap, {1, 1, 2, 3, 4}));
  overlap = jarray<std::int32_t>{1, 2, 3, 4, 5};
  jarray_copy(overlap, 1, overlap, 0, 4);
  CHECK(equals(overlap, {2, 3, 4, 5, 5}));

  jarray_copy(source, source.length(), destination, destination.length(), 0);
  CHECK(equals(destination, {9, 9, 2, 3, 4}));

  CHECK(throws_out_of_range([&] { jarray_copy(source, -1, destination, 0, 1); }));
  CHECK(throws_out_of_range([&] { jarray_copy(source, 0, destination, -1, 1); }));
  CHECK(throws_out_of_range([&] { jarray_copy(source, 0, destination, 0, -1); }));
  CHECK(throws_out_of_range([&] { jarray_copy(source, 2, destination, 0, 3); }));
  CHECK(throws_out_of_range([&] { jarray_copy(source, 0, destination, 4, 2); }));
  CHECK(throws_out_of_range([&] {
    jarray_copy(source, (std::numeric_limits<std::int32_t>::max)(), destination, 0,
                (std::numeric_limits<std::int32_t>::max)());
  }));
  return true;
}

int main() {
  if (!test_construction_and_access()) {
    return 1;
  }
  if (!test_copy_and_move()) {
    return 2;
  }
  if (!test_allocation_fill_and_swap()) {
    return 3;
  }
  if (!test_copy_operations()) {
    return 4;
  }
  return test_jarray_copy() ? 0 : 5;
}
