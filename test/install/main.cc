#include <decimal/decimal.h>

#include <cstdint>

int main() {
  return decimal::ONE.add(decimal::TWO).compare_to(decimal::value_of(std::int64_t{3})) == 0 ? 0 : 1;
}
