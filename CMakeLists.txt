cmake_minimum_required(VERSION 3.10)

project(Chronos C)

include(CTest)

include_directories(include)

add_library(chronos STATIC sources/chronos.c)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(chronos PRIVATE Threads::Threads)

add_subdirectory(tests)
