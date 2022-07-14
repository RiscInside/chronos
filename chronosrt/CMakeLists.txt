cmake_minimum_required(VERSION 3.13)

add_library(chronosrt STATIC chronosrt.c)
target_include_directories(chronosrt PUBLIC include PRIVATE .)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(chronosrt PRIVATE Threads::Threads)
