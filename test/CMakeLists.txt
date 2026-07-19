add_executable(decimal_quick_test quick.cc)
target_link_libraries(decimal_quick_test PRIVATE decimal::decimal)

add_test(NAME decimal.quick COMMAND decimal_quick_test)

add_executable(decimal_intrinsics_test portable.cc)
target_link_libraries(decimal_intrinsics_test PRIVATE decimal::decimal)

add_test(NAME decimal.intrinsics COMMAND decimal_intrinsics_test)

add_executable(decimal_portable_test portable.cc)
target_link_libraries(decimal_portable_test PRIVATE decimal::decimal)
target_compile_definitions(decimal_portable_test PRIVATE DECIMAL_DISABLE_INTRINSICS=1)

add_test(NAME decimal.portable COMMAND decimal_portable_test)
