# ============================================================================
#  ОКО МАГА — сборка всех этапов одной командой
#
#     cmake -B build
#     cmake --build build
#
#  Бинарники лягут в build/. M5 собирается не отсюда — у него свой ритуал
#  (см. M5_oracle/oracle.sh): там важен не бинарник, а дамп компилятора.
# ============================================================================
cmake_minimum_required(VERSION 3.20)
project(magic_eye CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Флаги предупреждений и (для MSVC) /utf-8 — у исходников кириллица в UTF-8,
# без этого флага MSVC может прочитать её неверно. -Wall/-Wextra/-Wpedantic —
# диалект GCC/Clang, MSVC их не понимает, поэтому ветвимся.
if(MSVC)
    # /utf-8            — исходники в UTF-8 (кириллица);
    # /Zc:preprocessor  — конформный препроцессор: без него legacy-препроцессор
    #                     MSVC неверно разворачивает __VA_ARGS__ и ломает
    #                     EYE_DESCRIBE (трюк FOR_EACH из M4/финала);
    # /permissive-      — строгое соответствие стандарту.
    add_compile_options(/W4 /permissive- /utf-8 /Zc:preprocessor)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# --- этапы лабы --------------------------------------------------------------
add_executable(m0_passport       M0_passport/main.cpp)
add_executable(m1_xray           M1_xray/main.cpp)
add_executable(m2_anatomy        M2_anatomy/main.cpp)
add_executable(m3_vptr_lair      M3_vptr_lair/main.cpp)
add_executable(m4_guild_registry M4_guild_registry/main.cpp)

# --- финал: header-only библиотека + примеры ----------------------------------
add_library(magic_eye INTERFACE)
target_include_directories(magic_eye INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/final)

foreach(ex 01_basics 02_creatures 03_tavern_vtable 04_guild_registry 05_deep_gaze)
    add_executable(ex${ex} final/examples/${ex}.cpp)
    target_link_libraries(ex${ex} PRIVATE magic_eye)
endforeach()

# --- регрессии финального инспектора -----------------------------------------
include(CTest)
if(BUILD_TESTING)
    add_executable(magic_eye_tests tests/magic_eye_tests.cpp)
    target_link_libraries(magic_eye_tests PRIVATE magic_eye)
    add_test(NAME magic_eye_tests COMMAND magic_eye_tests)
endif()
