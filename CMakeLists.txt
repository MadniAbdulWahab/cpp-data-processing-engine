cmake_minimum_required(VERSION 3.20)
project(datapipe VERSION 0.1.0 LANGUAGES CXX)

# CMake's default compiler check does not include a standard header. Catch a
# standalone Windows Clang setup with missing MSVC/SDK paths during configure.
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND
   CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
    include(CheckCXXSourceCompiles)
    check_cxx_source_compiles(
        "#include <cstdint>\nint main() { std::int64_t value = 0; return static_cast<int>(value); }"
        DATAPIPE_WINDOWS_CLANG_HAS_STDLIB)
    if(NOT DATAPIPE_WINDOWS_CLANG_HAS_STDLIB)
        message(FATAL_ERROR
            "Clang cannot find the Windows C++ standard library. Use "
            "'cmake --preset windows-msvc' from regular PowerShell, or run "
            "the Ninja configuration inside a Visual Studio Developer Prompt.")
    endif()
endif()

option(DATAPIPE_BUILD_TESTS "Build tests" ON)
option(DATAPIPE_BUILD_BENCHMARKS "Build benchmark executable" ON)
option(DATAPIPE_BUILD_PYTHON "Build optional pybind11 module" OFF)
option(DATAPIPE_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

add_library(datapipe_core
    src/types.cpp
    src/csv.cpp
    src/pipeline.cpp)
add_library(datapipe::core ALIAS datapipe_core)
set_target_properties(datapipe_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(datapipe_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(datapipe_core PUBLIC cxx_std_20)

function(datapipe_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    endif()
endfunction()

datapipe_set_warnings(datapipe_core)

if(DATAPIPE_ENABLE_SANITIZERS)
    if(MSVC)
        target_compile_options(datapipe_core PUBLIC /fsanitize=address)
    else()
        target_compile_options(datapipe_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(datapipe_core PUBLIC -fsanitize=address,undefined)
    endif()
endif()

add_executable(datapipe app/main.cpp)
target_link_libraries(datapipe PRIVATE datapipe::core)
datapipe_set_warnings(datapipe)

include(CTest)
if(BUILD_TESTING AND DATAPIPE_BUILD_TESTS)
    add_executable(datapipe_tests tests/test_main.cpp)
    target_link_libraries(datapipe_tests PRIVATE datapipe::core)
    target_compile_definitions(datapipe_tests PRIVATE
        DATAPIPE_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    datapipe_set_warnings(datapipe_tests)
    add_test(NAME datapipe_tests COMMAND datapipe_tests)
    add_test(NAME datapipe_cli_help COMMAND datapipe --help)
    add_test(NAME datapipe_cli_success COMMAND datapipe
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/basic.csv"
        --filter "temperature >= 20"
        --select region,temperature
        --chunk-size 2
        --threads 3
        --output "${CMAKE_CURRENT_BINARY_DIR}/cli-output.csv")
    set_tests_properties(datapipe_cli_success PROPERTIES
        PASS_REGULAR_EXPRESSION "Processed 5 rows in 3 chunk\\(s\\); wrote 3 rows")
    add_test(NAME datapipe_cli_invalid COMMAND datapipe
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/basic.csv"
        --threads 0
        --output "${CMAKE_CURRENT_BINARY_DIR}/invalid-output.csv")
    set_tests_properties(datapipe_cli_invalid PROPERTIES WILL_FAIL TRUE)
    add_test(NAME datapipe_cli_overwrite COMMAND datapipe
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/basic.csv"
        --output "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/basic.csv")
    set_tests_properties(datapipe_cli_overwrite PROPERTIES WILL_FAIL TRUE)
endif()

if(DATAPIPE_BUILD_BENCHMARKS)
    add_executable(datapipe_benchmark benchmarks/benchmark.cpp)
    target_link_libraries(datapipe_benchmark PRIVATE datapipe::core)
    datapipe_set_warnings(datapipe_benchmark)
endif()

if(DATAPIPE_BUILD_PYTHON)
    find_package(Python COMPONENTS Interpreter Development.Module REQUIRED)
    execute_process(
        COMMAND "${Python_EXECUTABLE}" -m pybind11 --cmakedir
        OUTPUT_VARIABLE DATAPIPE_PYBIND11_CMAKE_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE DATAPIPE_PYBIND11_QUERY_RESULT)
    string(REGEX REPLACE "^\"(.*)\"$" "\\1"
        DATAPIPE_PYBIND11_CMAKE_DIR "${DATAPIPE_PYBIND11_CMAKE_DIR}")
    if(NOT DATAPIPE_PYBIND11_QUERY_RESULT EQUAL 0)
        message(FATAL_ERROR
            "pybind11 is not available to ${Python_EXECUTABLE}; install it with that interpreter")
    endif()
    find_package(pybind11 CONFIG REQUIRED PATHS "${DATAPIPE_PYBIND11_CMAKE_DIR}" NO_DEFAULT_PATH)
    pybind11_add_module(datapipe_cpp python/bindings.cpp)
    target_link_libraries(datapipe_cpp PRIVATE datapipe::core)
    datapipe_set_warnings(datapipe_cpp)
    if(BUILD_TESTING AND DATAPIPE_BUILD_TESTS)
        add_test(NAME datapipe_python COMMAND "${Python_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_python.py"
            "$<TARGET_FILE_DIR:datapipe_cpp>"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/basic.csv"
            "${CMAKE_CURRENT_BINARY_DIR}/python-output.csv")
    endif()
endif()
