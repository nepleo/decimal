add_executable(decimal_quick_test quick.cpp)
target_link_libraries(decimal_quick_test PRIVATE decimal::decimal)

add_test(NAME decimal.quick COMMAND decimal_quick_test)
