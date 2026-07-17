// ============================================================================
//  Пример 11: странствие Ока — интерактивный обозреватель (eye::explore)
//  Сборка: g++ -std=c++20 -Wall -Wextra -Wpedantic -I../include 11_*.cpp -o ex11
//
//  Запусти в терминале — откроется полноэкранный обозреватель: слева дерево
//  (базы, поля, указатели, куча), справа «Гримуар» выбранного узла, внизу
//  гид по клавишам. Стрелки — ходить, Enter — раскрыть или ПЕРЕЙТИ по
//  указателю (⌫ — назад), / — поиск, ? — помощь, q — выход.
//
//  Тот же бинарь без терминала (redirect, CI) честно печатает статический
//  осмотр всех корней. EYE_SCRIPT="down enter q" — прогнать клавиши и
//  напечатать кадры (так странствие тестируется).
// ============================================================================
#include <eye/magic_eye.hpp>

#include <memory>
#include <vector>

// Рыцарь: наследование + private-поля + строка с кучным буфером.
class Unit {
    int hp = 100, speed = 5;
public:
    virtual ~Unit() = default;
    EYE_DESCRIBE(Unit, hp, speed)
};

class Knight : public Unit {
    int armor = 30;
    std::string banner = "Griffin banner, long enough to live on the heap";
public:
    EYE_BASES(Knight, Unit)
    EYE_DESCRIBE(Knight, armor, banner)
};

// Свиток: указатели трёх судеб — по ним МОЖНО ХОДИТЬ (Enter/g, назад — ⌫):
// power ведёт наружу к мане, self замыкает цикл (Око прыгнет, а не задвоит),
// nothing и rune — честные отказы (nullptr и C-строка без длины).
struct Scroll {
    int         charges = 3;
    int*        power = nullptr;
    void*       nothing = nullptr;
    const char* rune = "ANSUZ-RAIDO";
    Scroll*     self = nullptr;
    EYE_DESCRIBE(Scroll, charges, power, nothing, rune, self)
};

// Казна: умный указатель — переход через .get().
struct Vault {
    std::unique_ptr<int> gold = std::make_unique<int>(777);
    EYE_DESCRIBE(Vault, gold)
};

int main() {
    Knight knight;

    int mana = 350;
    Scroll scroll{};
    scroll.power = &mana;
    scroll.self = &scroll;

    Vault vault;

    std::vector<int> runes;
    for (int i = 0; i < 250; ++i) runes.push_back(i * i);   // страницы по 100

    // Галерея: несколько корней в одной сессии; клавиши 1..5 — прыжки.
    // Объекты обязаны жить, пока идёт run() — Око смотрит на живую память.
    eye::Gallery g;
    g.add(knight, "рыцарь");
    g.add(scroll, "свиток (переходы по указателям)");
    g.add(vault, "казна (умный указатель)");
    g.add(runes, "руны (vector на 250)");
    g.add(mana, "мана");
    g.run();
}
