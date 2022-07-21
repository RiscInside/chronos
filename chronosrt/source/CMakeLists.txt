cmake_minimum_required(VERSION 3.13)

target_include_directories(${CHRONOSRT_TARGET} PRIVATE .)
target_sources(${CHRONOSRT_TARGET} PRIVATE chronosrt.c)
target_sources(${CHRONOSRT_TARGET} PRIVATE hijack.c)
target_sources(${CHRONOSRT_TARGET} PRIVATE fail.c)
