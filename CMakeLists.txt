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

add_executable(cube src/main.cpp src/renderer.cpp src/pacer.cpp)
target_include_directories(cube PRIVATE ${CMAKE_SOURCE_DIR}/extern ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(cube PRIVATE glfw Threads::Threads)
if(WIN32)
  target_link_libraries(cube PRIVATE winmm)
endif()

enable_testing()
add_executable(mat4_tests tests/mat4_test.cpp)
target_include_directories(mat4_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME mat4_tests COMMAND mat4_tests)

add_executable(input_tests tests/input_test.cpp)
target_include_directories(input_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(input_tests PRIVATE Threads::Threads)
add_test(NAME input_tests COMMAND input_tests)

add_executable(pacer_tests tests/pacer_test.cpp)
target_include_directories(pacer_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME pacer_tests COMMAND pacer_tests)

add_executable(pacer_smoke tests/pacer_smoke.cpp src/pacer.cpp)
target_include_directories(pacer_smoke PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(WIN32)
  target_link_libraries(pacer_smoke PRIVATE winmm)  # timeBeginPeriod fallback
endif()
add_test(NAME pacer_smoke COMMAND pacer_smoke)

add_executable(frame_log_tests tests/frame_log_test.cpp)
target_include_directories(frame_log_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME frame_log_tests COMMAND frame_log_tests)

add_executable(handoff_bench bench/handoff_bench.cpp src/pacer.cpp)
target_include_directories(handoff_bench PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(handoff_bench PRIVATE Threads::Threads)
if(WIN32)
  target_link_libraries(handoff_bench PRIVATE winmm)
endif()
add_test(NAME handoff_bench_smoke COMMAND handoff_bench --quick)

# --poll-hz rejection happens before glfwInit, so these run headless (CI-safe).
add_test(NAME cube_rejects_poll_hz_zero COMMAND cube --poll-hz=0)
add_test(NAME cube_rejects_poll_hz_high COMMAND cube --poll-hz=10001)
set_tests_properties(cube_rejects_poll_hz_zero cube_rejects_poll_hz_high PROPERTIES
    PASS_REGULAR_EXPRESSION "bad --poll-hz value"
    TIMEOUT 15)

# --resched validation happens before glfwInit, so these run headless (CI-safe).
add_test(NAME cube_rejects_resched_bogus COMMAND cube --fps=144 --resched=sometimes)
add_test(NAME cube_rejects_resched_uncapped COMMAND cube --resched=relative)
set_tests_properties(cube_rejects_resched_bogus PROPERTIES
    PASS_REGULAR_EXPRESSION "bad --resched value" TIMEOUT 15)
set_tests_properties(cube_rejects_resched_uncapped PROPERTIES
    PASS_REGULAR_EXPRESSION "--resched requires --fps" TIMEOUT 15)

add_executable(capture_tests tests/capture_test.cpp)
target_include_directories(capture_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME capture_tests COMMAND capture_tests)

# --capture validation happens before glfwInit, so these run headless (CI-safe).
add_test(NAME cube_rejects_capture_frames_zero
         COMMAND cube --fps=50 --capture=x.raw --capture-frames=0)
add_test(NAME cube_rejects_capture_frames_high
         COMMAND cube --fps=50 --capture=x.raw --capture-frames=501)
set_tests_properties(cube_rejects_capture_frames_zero cube_rejects_capture_frames_high
    PROPERTIES PASS_REGULAR_EXPRESSION "bad --capture-frames value" TIMEOUT 15)
add_test(NAME cube_rejects_capture_uncapped
         COMMAND cube --capture=x.raw --capture-frames=100)
set_tests_properties(cube_rejects_capture_uncapped PROPERTIES
    PASS_REGULAR_EXPRESSION "--capture requires --fps" TIMEOUT 15)
add_test(NAME cube_rejects_capture_orphan_frames
         COMMAND cube --fps=50 --capture-frames=100)
set_tests_properties(cube_rejects_capture_orphan_frames PROPERTIES
    PASS_REGULAR_EXPRESSION "--capture-frames requires --capture" TIMEOUT 15)
