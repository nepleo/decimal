#include <decimal/decimal.h>

int main() {
  return decimal::ONE.compare_to(decimal::ONE) == 0 ? 0 : 1;
}
