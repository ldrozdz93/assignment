cmake_minimum_required (VERSION 2.8) 
project (CODILIME_SHARED_PTR)

add_executable (tests  tests.cpp test-main.cpp)

set_target_properties(tests PROPERTIES
        CXX_STANDARD 20
        CXX_EXTENSIONS OFF)

enable_testing()
add_test(MyTest tests)