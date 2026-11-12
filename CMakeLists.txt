cmake_minimum_required(VERSION 3.16)
project(WahCapture CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")

# Round 34 (D71): -DWAH_SANITIZE=ON builds the library and the seven test programs with AddressSanitizer and UndefinedBehaviorSanitizer
# (gcc or clang; not MSVC). Use a build directory of its own; the programs are slower (the closed-loop ones several times).
option(WAH_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(WAH_SANITIZE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address,undefined")
endif()

# libwah core library
add_library(libwah
    libwah.cpp
    wah_analyze.cpp
    wah_fit.cpp
    wah_toml.cpp
    wah_capture.cpp
    wah_measure.cpp
    wah_guided.cpp
)

target_include_directories(libwah PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Test executables
add_executable(wah_test_analyze
    test_analyze.cpp
)

add_executable(wah_test_fit
    test_fit.cpp
)

add_executable(wah_test_toml
    test_toml.cpp
)

add_executable(wah_test_capture
    test_capture.cpp
)

add_executable(wah_test_estimator
    test_estimator.cpp
)

add_executable(wah_test_measure
    test_measure.cpp
)

add_executable(wah_test_guided
    test_guided.cpp
)

target_link_libraries(wah_test_analyze PRIVATE libwah)
target_link_libraries(wah_test_fit PRIVATE libwah)
target_link_libraries(wah_test_toml PRIVATE libwah)
target_link_libraries(wah_test_capture PRIVATE libwah)
target_link_libraries(wah_test_estimator PRIVATE libwah)
target_link_libraries(wah_test_measure PRIVATE libwah)
target_link_libraries(wah_test_guided PRIVATE libwah)
target_compile_definitions(wah_test_guided PRIVATE WAH_EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/examples")
