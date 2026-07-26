cmake_minimum_required(VERSION 3.21)
project(cube LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
)
FetchContent_MakeAvailable(glfw)

find_package(Threads REQUIRED)

add_executable(cube src/main.cpp src/renderer.cpp)
target_include_directories(cube PRIVATE ${CMAKE_SOURCE_DIR}/extern ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(cube PRIVATE glfw Threads::Threads)

enable_testing()
add_executable(mat4_tests tests/mat4_test.cpp)
target_include_directories(mat4_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME mat4_tests COMMAND mat4_tests)

add_executable(input_tests tests/input_test.cpp)
target_include_directories(input_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME input_tests COMMAND input_tests)
