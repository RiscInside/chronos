cmake_minimum_required(VERSION 3.13)

include(CTest)

function(add_smoke_test test_name src)
    add_executable(${test_name} ${src})
    target_link_libraries(${test_name} PRIVATE chronosrt)
    add_test(NAME ${test_name} COMMAND ${test_name})
    target_compile_options(${test_name} PRIVATE -rdynamic)
    target_link_options(${test_name} PRIVATE -rdynamic)
endfunction()

add_smoke_test(timeline_helpers test_timeline_helpers.c)
add_smoke_test(pairing_heap test_pairing_heap.c)
add_smoke_test(runner test_runner.c)
