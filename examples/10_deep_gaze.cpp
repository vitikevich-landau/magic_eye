// ============================================================================
//  Пример 10: глубокий взгляд — указатели, куча, SSO и массивы
//  Сборка: g++ -std=c++20 -Wall -Wextra -Wpedantic -I../include 10_*.cpp -o ex10
//
//  Связи между памятью: указатель наружу (только адрес — чужое не читаем),
//  nullptr (связь обрывается) и указатель внутрь своего же объекта. Плюс
//  std::string SSO против кучи бок о бок и длинный C-массив со свёрткой
//  «⋯ ещё N Б ⋯» (EYE_FULL=1 разворачивает).
// ============================================================================
#include <eye/magic_eye.hpp>

#include <vector>

// Свиток: три указателя разной судьбы. Око подпишет каждому, КУДА он смотрит,
// но чужую память не разыменовывает (она может быть висячей): наружу (только
// адрес), в никуда (nullptr) и внутрь СВОЕГО же объекта (там байты видны в дампе).
struct Scroll {
    int     charges;
    int*    power;     // → наружу: Око покажет адрес, но значение читать не станет
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
    // Сам объект vector хранит служебные адреса, а три int живут отдельно
    // в куче. Адаптер сопоставит обе части и соединит их стрелкой.
    int x = 3333;
    std::vector nums{ 1, 2, x };

    eye::inspect(nums, "vector<int> — объект и внешний массив");
    int mana = 350;
    Scroll scroll{3, &mana, nullptr, nullptr};
    scroll.self = &scroll;
    eye::inspect(mana, "mana");
    eye::inspect(scroll, "свиток (три указателя)");

    Grimoire book;
    eye::inspect(book, "гримуар (SSO против кучи)");
}
