#SPSC_EventQueue
# cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_SPSC_EventQueueLib_TESTS=ON -DBUILD_SPSC_EventQueueLib_BENCHMARKS=ON
cmake_minimum_required(VERSION 3.20)
project(SPSC_EventQueue VERSION 1.0.0 LANGUAGES CXX)

# ==============================================================================
# Настройки C++20 Standard
# ==============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)


# *.cpp — Стандартный файл исходного кода C++ (C Plus Plus).
# *.cxx — Альтернативное расширение для исходного кода C++.
# *.c — Файл исходного кода на языке C.
# *.hpp — Стандартный заголовочный файл C++, содержащий объявления и шаблоны.
# *.hxx — Альтернативное расширение для заголовочных файлов C++.
# *.inl — Файл с реализацией inline-функций и шаблонов для выноса из заголовков (только для подключения в заголовках не в ресурсных файлах).
# *.h — Классический заголовочный файл C/C++ для объявлений функций и структур.
# Собираем все .cpp и .h файлы из папки src и include в переменную
file(GLOB SPSC_EventQueue_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"

    "${CMAKE_CURRENT_SOURCE_DIR}/include/SPSC_EventQueue/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/SPSC_EventQueue/*.hxx"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/SPSC_EventQueue/*.inl"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/SPSC_EventQueue/*.h"
)


add_library(SPSC_EventQueueLib STATIC ${SPSC_EventQueue_SOURCES})

set_target_properties(SPSC_EventQueueLib PROPERTIES LINKER_LANGUAGE CXX)

target_include_directories(SPSC_EventQueueLib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# ==============================================================================
# Модульные тесты (doctest)
# ==============================================================================
option(BUILD_SPSC_EventQueueLib_TESTS "Собирать тесты для SPSC_EventQueue" OFF)

if(BUILD_SPSC_EventQueueLib_TESTS)
    enable_testing()

    # Файлы тестов
    file(GLOB_RECURSE SPSC_EventQueueLib_TEST_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/Tests/src/*.cpp"
    )

    add_executable(SPSC_EventQueueLibTests ${SPSC_EventQueueLib_TEST_SOURCES})

    # Подключаем doctest
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Tests/doctest/CMakeLists.txt")
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/Tests/doctest")
    else()
        find_package(doctest REQUIRED)
    endif()

    target_link_libraries(SPSC_EventQueueLibTests PRIVATE
        SPSC_EventQueueLib
        doctest::doctest
    )

    # Интеграция с CTest / doctest discovery
    include(CTest)
    if(COMMAND doctest_discover_tests)
        doctest_discover_tests(SPSC_EventQueueLibTests)
    endif()

    message(STATUS "SPSC_EventQueueLib: Сборка тестов ВКЛЮЧЕНА")
else()
    message(STATUS "SPSC_EventQueueLib: Сборка тестов ВЫКЛЮЧЕНА")
endif()



# ==============================================================================
# Бенчмарки (Google Benchmark)
# ==============================================================================
option(BUILD_SPSC_EventQueueLib_BENCHMARKS "Собирать бенчмарки для SPSC_EventQueue" OFF)

if(BUILD_SPSC_EventQueueLib_BENCHMARKS)
    message(STATUS "SPSC_EventQueueLib: Сборка бенчмарков ВКЛЮЧЕНА")

    # 1. Отключаем внутренние тесты и зависимости Google Benchmark
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

    file(GLOB_RECURSE SPSC_EventQueueLib_BENCHMARK_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/Benchmarks/src/*.cpp"
    )

    add_executable(SPSC_EventQueueLibBenchmarks ${SPSC_EventQueueLib_BENCHMARK_SOURCES})

    # 2. Подключаем google/benchmark
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Benchmarks/benchmark/CMakeLists.txt")
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/Benchmarks/benchmark")
    else()
        find_package(benchmark REQUIRED)
    endif()

    target_link_libraries(SPSC_EventQueueLibBenchmarks PRIVATE
        SPSC_EventQueueLib
        benchmark::benchmark
    )
endif()