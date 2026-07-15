// ============================================================================
//  Пример 01: основы — скаляры, enum, указатели, осмотр типа без объекта
//  Сборка: g++ -std=c++20 -Wall -Wextra -Wpedantic -I../include 01_*.cpp -o ex01
// ============================================================================
#include <eye/magic_eye.hpp>

#include <cstdint>

enum class School : std::uint16_t { Air = 1, Fire = 2, Earth = 4, Water = 8 };

int main() {
    // Скаляр: паспорт + байты. На int'e видно little-endian (младший байт первым).
    int gold = 0x11223344;
    eye::inspect(gold, "казна (int)");

    // Узкий целочисленный тип — Око подпишет hex рядом с десятичным значением.
    std::uint8_t level = 200;
    eye::inspect(level, "уровень (uint8_t)");

    double luck = 1.0;
    eye::inspect(luck, "удача (double)");

    // enum class поверх uint16_t — тоже целое, но со своим типом.
    School spell = School::Fire;
    eye::inspect(spell, "школа магии (enum class)");

    // Указатель — просто 8 байт со значением-адресом. Око покажет, КУДА он
    // смотрит (наружу), но чужую память не разыменовывает.
    int* ptr = &gold;
    eye::inspect(ptr, "указатель на казну");

    // Осмотр ТИПА без объекта: всё, что известно на этапе компиляции.
    eye::inspect<double>();
}
