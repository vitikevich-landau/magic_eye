# ============================================================================
#  magic_eye (Око мага) — header-only визуальный инспектор объектов C++
#
#     cmake -B build
#     cmake --build build
#     ctest --test-dir build --output-on-failure
#
#  Собираются: библиотека (INTERFACE), все примеры из examples/ и регрессии.
#  Учебные этапы M0–M5 переехали в docs/legacy/ и в сборку не входят.
# ============================================================================
cmake_minimum_required(VERSION 3.20)
project(magic_eye CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Предупреждения и (для MSVC) /utf-8 — у исходников кириллица в UTF-8; без флага
# MSVC может прочитать её неверно. -Wall/-Wextra/-Wpedantic — диалект GCC/Clang.
if(MSVC)
    # /utf-8            — исходники в UTF-8 (кириллица);
    # /Zc:preprocessor  — конформный препроцессор: без него legacy-препроцессор
    #                     MSVC неверно разворачивает __VA_ARGS__ и ломает
    #                     EYE_DESCRIBE (трюк FOR_EACH);
    # /permissive-      — строгое соответствие стандарту.
    add_compile_options(/W4 /permissive- /utf-8 /Zc:preprocessor)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# --- библиотека: header-only, один include-каталог ---------------------------
add_library(magic_eye INTERFACE)
target_include_directories(magic_eye INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(magic_eye INTERFACE cxx_std_20)

# --- примеры -----------------------------------------------------------------
# Каждый .cpp в examples/ становится отдельной целью ex<имя-файла>.
file(GLOB EYE_EXAMPLES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/examples/*.cpp)
foreach(src ${EYE_EXAMPLES})
    get_filename_component(name ${src} NAME_WE)
    add_executable(ex_${name} ${src})
    target_link_libraries(ex_${name} PRIVATE magic_eye)
endforeach()

# --- регрессии инспектора ----------------------------------------------------
include(CTest)
if(BUILD_TESTING)
    add_executable(magic_eye_tests tests/magic_eye_tests.cpp)
    target_link_libraries(magic_eye_tests PRIVATE magic_eye)
    add_test(NAME magic_eye_tests COMMAND magic_eye_tests)
    # Тесты сравнивают текст без ANSI и не трогают консоль Windows.
    set_tests_properties(magic_eye_tests PROPERTIES
        ENVIRONMENT "EYE_COLOR=0;EYE_CENTER=0;EYE_RESIZE=0")

    # Ядро TUI: декодер клавиш и канва — чистые юниты, терминал не нужен.
    add_executable(tui_core_tests tests/tui_core_tests.cpp)
    target_link_libraries(tui_core_tests PRIVATE magic_eye)
    add_test(NAME tui_core_tests COMMAND tui_core_tests)
    set_tests_properties(tui_core_tests PROPERTIES
        ENVIRONMENT "EYE_COLOR=0;EYE_CENTER=0;EYE_RESIZE=0")

    # Странствие (explore/Gallery): снапшоты кадров через EYE_SCRIPT.
    add_executable(explore_script_tests tests/explore_script_tests.cpp)
    target_link_libraries(explore_script_tests PRIVATE magic_eye)
    add_test(NAME explore_script_tests COMMAND explore_script_tests)
    set_tests_properties(explore_script_tests PROPERTIES
        ENVIRONMENT "EYE_COLOR=0;EYE_CENTER=0;EYE_RESIZE=0")

    # Ручная проба TUI (стрелки/resize/выход) — собирается, в ctest не входит.
    add_executable(tui_demo tests/tui_demo.cpp)
    target_link_libraries(tui_demo PRIVATE magic_eye)
endif()
