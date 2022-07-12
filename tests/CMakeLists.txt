cmake_minimum_required(VERSION 3.10)

include(CTest)

add_executable(test_timeline_helpers test_timeline_helpers.c)

add_test(NAME timeline_helpers COMMAND test_timeline_helpers)
