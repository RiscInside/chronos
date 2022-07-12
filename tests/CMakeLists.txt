cmake_minimum_required(VERSION 3.10)

include(CTest)

add_executable(test_timeline_helpers test_timeline_helpers.c)
add_executable(test_pairing_heap test_pairing_heap.c)

add_test(NAME timeline_helpers COMMAND test_timeline_helpers)
add_test(NAME pairing_heap COMMAND test_pairing_heap)
