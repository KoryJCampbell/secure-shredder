cmake_minimum_required(VERSION 3.20)
project(shredder VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion)

add_library(shredder_core STATIC
  src/secure_buffer.cpp
  src/safety.cpp
  src/progress.cpp
  src/sha256.cpp
  src/shredder.cpp
)
target_include_directories(shredder_core PUBLIC src)

add_executable(shredder src/main.cpp)
target_link_libraries(shredder PRIVATE shredder_core)

enable_testing()
add_subdirectory(tests)
