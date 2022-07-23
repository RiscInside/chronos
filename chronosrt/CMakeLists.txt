cmake_minimum_required(VERSION 3.13)

set(CHRONOSRT_TARGET chronosrt)
add_library(${CHRONOSRT_TARGET} STATIC)
target_include_directories(${CHRONOSRT_TARGET} PUBLIC include)
target_link_libraries(${CHRONOSRT_TARGET} PRIVATE pthread rt atomic)
target_compile_options(${CHRONOSRT_TARGET} PRIVATE -Wall -Wextra -rdynamic)
target_link_options(${CHRONOSRT_TARGET} PRIVATE -rdynamic)

option(CHRONOSRT_MASK_SIGNALS "Mask signals in helper threads" ON)
if(CHRONOSRT_MASK_SIGNALS)
    target_compile_options(${CHRONOSRT_TARGET} PRIVATE -DCHRONOSRT_MASK_SIGNALS)
endif()

option(CHRONOSRT_LOG_TO_STDERR "Print some debug messages to stderr" OFF)
if(CHRONOSRT_LOG_TO_STDERR)
    target_compile_options(${CHRONOSRT_TARGET} PRIVATE -DCHRONOSRT_LOG_TO_STDERR)
endif()

add_subdirectory(source)
add_subdirectory(tests)
