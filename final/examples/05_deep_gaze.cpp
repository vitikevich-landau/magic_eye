// ============================================================================
//  Пример 5: глубокий взгляд — указатели, куча, SSO и массивы
//  Сборка: g++ -std=c++20 -Wall -Wextra -Wpedantic -I.. 05_deep_gaze.cpp -o ex05
// ============================================================================
#include "magic_eye.hpp"

// Свиток: три указателя разной судьбы. Око подпишет каждому, куда он смотрит:
// наружу (и что там лежит), в никуда (nullptr) и внутрь СВОЕГО же объекта.
struct Scroll {
    int     charges;
    int*    power;     // → наружу, на локальную переменную (Око заглянет)
    void*   nothing;   // → nullptr
    Scroll* self;      // → на самого себя: «база+0x0000»
};

// Гримуар: две строки — короткая живёт в объекте (SSO), длинная только
// ДЕРЖИТ УКАЗАТЕЛЬ на кучу (буфер появится панелью-спутником ниже).
// Массив рун длинный — Око свернёт середину в «⋯ ещё N Б ⋯»
// (EYE_FULL=1 разворачивает).
class Grimoire {
    std::string owner = "Solmyr";                                   // SSO
    std::string spell = "Chain Lightning strikes every foe thrice"; // куча
    char        runes[40] = "ANSUZ-RAIDO-KENAZ-GEBO-WUNJO";

public:
    EYE_DESCRIBE(Grimoire, owner, spell, runes)
};

int main() {
    int mana = 350;
    Scroll scroll{3, &mana, nullptr, nullptr};
    scroll.self = &scroll;
    eye::inspect(mana, "mana");
    eye::inspect(scroll, "свиток (три указателя)");

    Grimoire book;
    eye::inspect(book, "гримуар (SSO против кучи)");
}
