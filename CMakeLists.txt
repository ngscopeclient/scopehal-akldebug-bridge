cmake_minimum_required(VERSION 3.24)
project(scopehal-akldebug-bridge)

set(WARNINGS "-Wall -Wextra -Wuninitialized ")
set(WARNINGS "${WARNINGS} -Wshadow -Wunsafe-loop-optimizations -Wpedantic -Wcast-align -Wwrite-strings")
set(WARNINGS "${WARNINGS} -Wmissing-declarations -Wvla")
set(CMAKE_CXX_FLAGS "-g ${WARNINGS} --std=c++20 -mtune=native")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -fsanitize=address")

if(WIN32)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_USE_MATH_DEFINES -D_POSIX_THREAD_SAFE_FUNCTIONS")
endif()

find_package(PkgConfig)

add_subdirectory("${PROJECT_SOURCE_DIR}/lib/log")
add_subdirectory("${PROJECT_SOURCE_DIR}/lib/scpi-server-tools")
add_subdirectory("${PROJECT_SOURCE_DIR}/lib/xptools")
add_subdirectory("${PROJECT_SOURCE_DIR}/src/aklbridge")
