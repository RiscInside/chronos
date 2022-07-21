cmake_minimum_required(VERSION 3.13)

set(CHRONOSRT_TARGET chronosrt)
add_library(${CHRONOSRT_TARGET} STATIC)
target_include_directories(${CHRONOSRT_TARGET} PUBLIC include)
target_link_libraries(${CHRONOSRT_TARGET} PRIVATE pthread rt)
target_compile_options(${CHRONOSRT_TARGET} PRIVATE -Wall -Wextra -rdynamic)
target_link_options(${CHRONOSRT_TARGET} PRIVATE -rdynamic)

add_subdirectory(source)
add_subdirectory(tests)
