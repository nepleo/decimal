# Decimal

[![CI](https://github.com/nepleo/decimal/actions/workflows/ci.yml/badge.svg)](https://github.com/nepleo/decimal/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

Decimal is a header-only C++17 library for decimal arithmetic. It provides
arbitrary-precision decimal values, configurable precision and rounding modes,
scale-aware operations, and string and primitive conversions.

## Features

- Header-only and dependency-free
- Arbitrary-precision decimal arithmetic
- Configurable precision through `math_context`
- Eight rounding modes, including half-even and unnecessary rounding
- Arithmetic, comparison, scale manipulation, powers, and square roots
- Plain, engineering, and scientific string representations

## Requirements

- A C++17-compatible compiler
- CMake 3.16 or later when using the CMake package

Decimal detects compiler capabilities instead of requiring a particular
compiler. GCC and Clang use their built-in bit, overflow, and 128-bit integer
operations. MSVC uses the corresponding bit-scan and 128-bit arithmetic
intrinsics where the target architecture provides them. Other C++17 compilers
and architectures use dependency-free portable implementations.

Define `DECIMAL_DISABLE_INTRINSICS=1` before including the header to force the
portable implementation, for example when validating a new compiler or target.

## Quick start

```cpp
#include <decimal/decimal.h>

#include <cstdint>
#include <iostream>

int main() {
  const decimal price = decimal::value_of(1999, 2);
  const decimal quantity = decimal::value_of(std::int64_t{3});
  const decimal total = price.multiply(quantity);

  std::cout << total.to_plain_string() << '\n';  // 59.97
}
```

## CMake integration

### With `add_subdirectory`

```cmake
add_subdirectory(path/to/decimal)

add_executable(my_app main.cc)
target_link_libraries(my_app PRIVATE decimal::decimal)
```

Including Decimal as a subproject does not build its tests or add installation
rules unless `DECIMAL_BUILD_TESTS` or `DECIMAL_INSTALL` is explicitly enabled.

### As an installed package

Configure and install Decimal:

```sh
cmake -S . -B build \
  -DDECIMAL_BUILD_TESTS=OFF \
  -DCMAKE_INSTALL_PREFIX=/path/to/decimal-install
cmake --build build
cmake --install build
```

Use the installed package from another project:

```cmake
find_package(decimal 0.1 CONFIG REQUIRED)

add_executable(my_app main.cc)
target_link_libraries(my_app PRIVATE decimal::decimal)
```

If Decimal is installed to a non-standard prefix, pass it when configuring the
consumer:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/decimal-install
```

## Building and testing

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

On a multi-configuration generator, add `--config Release` when building and
`-C Release` when running CTest.

## CMake options

| Option | Default as top-level | Default as subproject | Description |
| --- | --- | --- | --- |
| `DECIMAL_BUILD_TESTS` | `ON` | `OFF` | Build the test suite |
| `DECIMAL_INSTALL` | `ON` | `OFF` | Generate installation rules |

## Project status

The current version is 0.1.0. The public API may evolve before the first stable
release.

## License

Decimal is available under the [MIT License](LICENSE).
