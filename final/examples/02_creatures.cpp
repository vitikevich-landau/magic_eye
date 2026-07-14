// ============================================================================
//  Пример 2: агрегаты — авторазбор полей и охота на padding
//  Сборка: g++ -std=c++20 -Wall -Wextra -Wpedantic -I.. 02_creatures.cpp -o ex02
// ============================================================================
#include "magic_eye.hpp"

// Агрегат: Око разберёт по полям само, без регистрации.
struct Creature {
    std::string name;
    int hp;
    int attack;
    bool flying;
};

// Классика: одинаковые поля, разный порядок объявления.
struct SloppyStack {
    char grade;
    double damage;
    char is_upgraded;
    int count;
};

struct TidyStack {
    double damage;
    int count;
    char grade;
    char is_upgraded;
};

int main() {
    eye::inspect(Creature{"Griffin", 25, 8, true}, "грифон");

    // Смотри на карту памяти и на процент padding:
    eye::inspect(SloppyStack{'A', 6.5, 1, 42}, "отряд (поля вразнобой)");
    eye::inspect(TidyStack{6.5, 42, 'A', 1},   "отряд (поля по уму)");
}
