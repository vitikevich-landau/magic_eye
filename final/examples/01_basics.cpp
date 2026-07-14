// ============================================================================
//  Пример 1: основы — скаляры, указатели, std::string
//  Сборка: g++ -std=c++20 -Wall -Wextra -Wpedantic -I.. 01_basics.cpp -o ex01
// ============================================================================
#include "magic_eye.hpp"

int main() {
    // Скаляры: паспорт + байты. На int'e видно little-endian.
    int gold = 0x11223344;
    eye::inspect(gold, "казна (int)");

    double luck = 1.0;
    eye::inspect(luck, "удача (double)");

    // Указатель — тоже просто 8 байт со значением-адресом.
    int* ptr = &gold;
    eye::inspect(ptr, "указатель на казну");

    // std::string — непрозрачный класс (у него конструкторы),
    // но байты не врут: короткая строка лежит внутри объекта (SSO)...
    std::string short_name = "Sir Mullich";
    eye::inspect(short_name, "короткая строка (SSO)");

    // ...а у длинной внутри объекта только указатель на кучу и длина.
    std::string long_name = "Sir Mullich, Champion of the Absolute Speed";
    eye::inspect(long_name, "длинная строка (куча)");

    // Осмотр типа без объекта — всё, что известно до запуска:
    eye::inspect<std::string>();
}
