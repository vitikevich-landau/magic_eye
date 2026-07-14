// ============================================================================
//  Пример 4: реестр EYE_DESCRIBE — имена полей и private
//  Сборка: g++ -std=c++20 -Wall -Wextra -Wpedantic -I.. 04_guild_registry.cpp -o ex04
// ============================================================================
#include "magic_eye.hpp"

// Обычный класс: private-поля, конструктор — авторазбор M2 тут бессилен.
// EYE_DESCRIBE возвращает Оку зрение, а заодно дарит ИМЕНА полей.
class Mage {
    int level;
    int mana;
    std::string name;

public:
    Mage(std::string n, int lvl) : level(lvl), mana(lvl * 10), name(std::move(n)) {}
    EYE_DESCRIBE(Mage, level, mana, name)
};

// Полиморфный + описанный: реестр покажет, что первое поле лежит
// НЕ на нулевом смещении. Угадай, кто занял первые 8 байт.
class Dragon {
    int hp = 300;
    std::string breath = "fire";

public:
    virtual ~Dragon() = default;
    EYE_DESCRIBE(Dragon, hp, breath)
};

int main() {
    Mage solmyr{"Solmyr", 13};
    eye::inspect(solmyr, "Солмир (private через реестр)");

    Dragon dragon;
    eye::inspect(dragon, "дракон: vptr выталкивает поля");
}
