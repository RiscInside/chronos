cmake_minimum_required(VERSION 3.13)

include(CTest)

function(add_smoke_test test_name src)
    add_executable(${test_name} ${src})
    target_link_libraries(${test_name} PRIVATE chronosrt)
    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

add_smoke_test(timeline_helpers test_timeline_helpers.c)
add_smoke_test(pairing_heap test_pairing_heap.c)
add_smoke_test(runtime_basic test_runtime_basic.c)
add_smoke_test(two_threads_basic test_two_threads_basic.c)
add_smoke_test(basic_scaling test_basic_scaling.c)
add_smoke_test(basic_jumps test_basic_jumps.c)
add_smoke_test(basic_critical test_basic_critical.c)
