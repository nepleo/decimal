#include <decimal/decimal.h>

int main() {
  return decimal::ONE.add(decimal::TWO).compare_to(decimal::value_of(3LL)) == 0 ? 0 : 1;
}
