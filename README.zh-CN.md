# decimal

[English](README.md) | 简体中文 | [在线文档](https://nepleo.github.io/decimal/)

[![CI](https://github.com/nepleo/decimal/actions/workflows/ci.yml/badge.svg)](https://github.com/nepleo/decimal/actions/workflows/ci.yml)
[![Documentation](https://img.shields.io/badge/docs-GitHub%20Pages-346ddb)](https://nepleo.github.io/decimal/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Repository views](https://hits.sh/github.com/nepleo/decimal.svg?label=Repository%20views&color=346ddb&logo=github)](https://hits.sh/github.com/nepleo/decimal/)


decimal 是一个用于精确任意精度十进制运算的 C++17 纯头文件库. 它适用于金额,利率,测量值以及其它不能接受
二进制浮点误差的计算场景.

```cpp
#include <decimal/decimal.h>

#include <iostream>

int main() {
  const decimal unit_price("19.99");
  const decimal quantity(3);
  const decimal total = unit_price.multiply(quantity).set_scale(2, round_mode::HALF_EVEN);

  std::cout << total.to_plain_string() << '\n';  // 59.97
}
```

## 为什么使用 decimal?

许多十进制小数无法使用二进制浮点数精确表示. 当数值经过解析,计算,存储和显示后仍需要保持可复现时,
这个差异非常重要.

```cpp
const decimal exact("0.1");
const decimal from_binary(0.1);

exact.to_plain_string();        // 0.1
from_binary.to_plain_string();  // 该二进制 double 的精确十进制值.
```

当十进制输入必须保持精确,舍入规则属于业务规则,数值可能超过固定整数范围,或者 scale 本身具有意义时,
可以使用 `decimal`. 当计算允许近似二进制浮点结果,并且硬件浮点性能比十进制表示更重要时,可以使用 `double`.

## 核心能力

- 纯头文件且无外部依赖.
- 使用任意精度整数支持任意精度十进制数值.
- 精确加法,减法,乘法,比较和有限小数除法.
- 显式 scale 和 8 种舍入模式,包括 `HALF_EVEN` 和 `UNNECESSARY`.
- 通过 `math_context` 控制运算有效位数.
- 支持普通计数法,工程计数法和科学计数法字符串.
- 提供可移植的 C++17 实现,并在编译器支持时使用优化 intrinsic.
- CI 覆盖 GCC,Clang,AppleClang,MSVC 和 clang-cl.

## 选择正确的输入方式

| 输入 | 推荐 API | 适用场景 |
| --- | --- | --- |
| 精确十进制文本 | `decimal("123.45")` | 金额,利率,序列化数值和用户输入 |
| 整数 | `decimal(std::int64_t{42})` | 计数以及不带小数位的整数值 |
| 未缩放整数和 scale | `decimal::value_of(12345, 2)` | 数据库数值和定点协议 |
| `double` 的规范十进制形式 | `decimal::value_of(0.1)` | 与已有浮点接口互操作 |
| `double` 表示的精确数值 | `decimal(0.1)` | 检查或保留二进制浮点值 |

`scale` 表示小数点右侧的位数. `math_context::precision()` 限制一次运算的有效数字位数. 例如,
`set_scale(2, mode)` 表示"保留两位小数",而 `math_context(6, mode)` 表示"保留六位有效数字".

## 安装

### 作为子目录导入

```cmake
add_subdirectory(path/to/decimal)

add_executable(my_app main.cc)
target_link_libraries(my_app PRIVATE decimal::decimal)
```

将 decimal 作为子项目导入时,默认不会构建测试或生成安装规则. 如有需要,可以显式启用
`DECIMAL_BUILD_TESTS` 或 `DECIMAL_INSTALL`.

### 使用已安装的 CMake 包

```sh
cmake -S . -B build \
  -DDECIMAL_BUILD_TESTS=OFF \
  -DCMAKE_INSTALL_PREFIX=/path/to/decimal-install
cmake --build build
cmake --install build
```

```cmake
find_package(decimal 0.1 CONFIG REQUIRED)

add_executable(my_app main.cc)
target_link_libraries(my_app PRIVATE decimal::decimal)
```

如果安装路径不是标准前缀,配置使用方项目时传入 `-DCMAKE_PREFIX_PATH=/path/to/decimal-install`.

## 要求和可移植性

- 支持 C++17 的编译器.
- 使用 CMake 包或构建测试时需要 CMake 3.16 或更高版本.

decimal 检测编译器能力,而不是假设编译器类型. GCC 和 Clang 可以使用内置位运算,溢出检测和原生 128 位
整数操作. MSVC 和 clang-cl 使用其支持的 `<intrin.h>` 操作. 其它编译器和架构使用无外部依赖的可移植实现.

在包含头文件前定义 `DECIMAL_DISABLE_INTRINSICS=1`,可以强制使用可移植实现,用于验证新的编译器或目标平台.

## 文档

[在线手册](https://nepleo.github.io/decimal/) 包含:

- 以任务为导向的 value,scale,precision 和舍入入门.
- 每个应用层 `decimal` 函数组的详细说明.
- 除法,转换和格式化重载的选择建议.
- 金额,汇率,CSV 输入,贷款和精确性检查的可运行示例.
- `round_mode`,`math_context` 和 `bigint` 的高级参考.

## 构建和测试

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

使用多配置生成器时,构建时添加 `--config Release`,运行 CTest 时添加 `-C Release`.

| CMake 选项 | 作为顶层项目的默认值 | 作为子项目的默认值 | 说明 |
| --- | --- | --- | --- |
| `DECIMAL_BUILD_TESTS` | `ON` | `OFF` | 构建测试套件 |
| `DECIMAL_INSTALL` | `ON` | `OFF` | 生成安装规则 |

## 许可证

decimal 使用 [MIT License](LICENSE).
