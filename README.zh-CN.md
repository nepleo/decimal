# decimal

[English](README.md) | 简体中文

[![CI](https://github.com/nepleo/decimal/actions/workflows/ci.yml/badge.svg)](https://github.com/nepleo/decimal/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

decimal 是一个用于十进制运算的 C++17 纯头文件库. 它提供任意精度十进制数值,
支持配置精度和舍入模式,并提供与 scale 相关的运算以及字符串和基本类型转换.

## 功能

- 纯头文件且无外部依赖
- 任意精度十进制运算
- 通过 `math_context` 配置精度
- 8 种舍入模式,包括 HALF_EVEN 和 UNNECESSARY
- 支持算术运算,比较,scale 调整,幂运算和平方根
- 支持普通计数法,工程计数法和科学计数法字符串

## 要求

- 支持 C++17 的编译器
- 使用 CMake 包时需要 CMake 3.16 或更高版本

decimal 检测编译器能力,而不是依赖特定编译器. GCC 和 Clang 使用内置的位运算,
溢出检测和 128 位整数操作. MSVC 在目标架构支持时使用对应的位扫描和 128 位
算术 intrinsic. 其他 C++17 编译器和架构使用无外部依赖的可移植实现.

在包含头文件前定义 `DECIMAL_DISABLE_INTRINSICS=1`,可以强制使用可移植实现.
这可用于验证新的编译器或目标平台.

## 快速开始

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

## CMake 集成

### 使用 `add_subdirectory`

```cmake
add_subdirectory(path/to/decimal)

add_executable(my_app main.cc)
target_link_libraries(my_app PRIVATE decimal::decimal)
```

将 decimal 作为子项目导入时,默认不会构建测试或添加安装规则. 如有需要,可以显式启用
`DECIMAL_BUILD_TESTS` 或 `DECIMAL_INSTALL`.

### 使用已安装的包

配置并安装 decimal:

```sh
cmake -S . -B build \
  -DDECIMAL_BUILD_TESTS=OFF \
  -DCMAKE_INSTALL_PREFIX=/path/to/decimal-install
cmake --build build
cmake --install build
```

在其他项目中使用已安装的包:

```cmake
find_package(decimal 0.1 CONFIG REQUIRED)

add_executable(my_app main.cc)
target_link_libraries(my_app PRIVATE decimal::decimal)
```

如果 decimal 安装在非标准路径,配置使用方项目时需要传入该路径:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/decimal-install
```

## 构建和测试

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

使用多配置生成器时,构建时添加 `--config Release`,运行 CTest 时添加
`-C Release`.

## CMake 选项

| 选项 | 作为顶层项目时的默认值 | 作为子项目时的默认值 | 说明 |
| --- | --- | --- | --- |
| `DECIMAL_BUILD_TESTS` | `ON` | `OFF` | 构建测试套件 |
| `DECIMAL_INSTALL` | `ON` | `OFF` | 生成安装规则 |

## 许可证

decimal 使用 [MIT License](LICENSE).
