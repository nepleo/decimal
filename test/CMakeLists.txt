function(decimal_add_test target test_name source)
  add_executable(${target} ${source})
  target_link_libraries(${target} PRIVATE decimal::decimal)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/support)
  add_test(NAME ${test_name} COMMAND ${target})
endfunction()

decimal_add_test(decimal_jarray_test decimal.jarray container/jarray_test.cc)

decimal_add_test(decimal_constructor_test decimal.constructor decimal/constructor_test.cc)
decimal_add_test(decimal_arithmetic_test decimal.arithmetic decimal/arithmetic_test.cc)
decimal_add_test(decimal_division_test decimal.division decimal/division_test.cc)
decimal_add_test(decimal_rounding_scale_test decimal.rounding_scale decimal/rounding_scale_test.cc)
decimal_add_test(decimal_internal_test decimal.internal decimal/internal_test.cc)
decimal_add_test(decimal_conversion_test decimal.conversion decimal/conversion_test.cc)

decimal_add_test(bigint_constructor_test bigint.constructor bigint/constructor_test.cc)
decimal_add_test(bigint_arithmetic_test bigint.arithmetic bigint/arithmetic_test.cc)
decimal_add_test(bigint_division_test bigint.division bigint/division_test.cc)
decimal_add_test(bigint_bit_test bigint.bit bigint/bit_test.cc)
decimal_add_test(bigint_modular_prime_test bigint.modular_prime bigint/modular_prime_test.cc)
decimal_add_test(bigint_conversion_test bigint.conversion bigint/conversion_test.cc)
decimal_add_test(bigint_internal_test bigint.internal bigint/internal_test.cc)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_test(
  NAME decimal.function_coverage
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/function_coverage_test.py
          --type decimal ${PROJECT_SOURCE_DIR}/include/decimal/decimal.h ${CMAKE_CURRENT_SOURCE_DIR})

add_test(
  NAME bigint.function_coverage
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/function_coverage_test.py
          --type bigint ${PROJECT_SOURCE_DIR}/include/decimal/decimal.h ${CMAKE_CURRENT_SOURCE_DIR})

decimal_add_test(decimal_intrinsics_test decimal.intrinsics compiler/intrinsics_test.cc)
decimal_add_test(decimal_portable_test decimal.portable compiler/intrinsics_test.cc)
target_compile_definitions(decimal_portable_test PRIVATE DECIMAL_DISABLE_INTRINSICS=1)
