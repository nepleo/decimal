cmake_minimum_required(VERSION 3.16)

project(decimal_install_test LANGUAGES CXX)

find_package(decimal 0.1 CONFIG REQUIRED)

add_executable(decimal_install_test main.cpp)
target_link_libraries(decimal_install_test PRIVATE decimal::decimal)

enable_testing()
add_test(NAME decimal.install COMMAND decimal_install_test)
