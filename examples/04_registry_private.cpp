// ============================================================================
//  Пример 04: реестр EYE_DESCRIBE — имена полей, private и порядок
//  Сборка: g++ -std=c++20 -Wall -Wextra -Wpedantic -I../include 04_*.cpp -o ex04
//
//  У класса с конструктором/private-полями авторазбор бессилен. EYE_DESCRIBE
//  даёт Оку зрение и ИМЕНА полей: макрос кладёт в класс метод, возвращающий
//  пары «имя + указатель-на-член», поэтому видит даже private. Список ручной:
//  добавил поле — впиши в EYE_DESCRIBE. Порядок при этом не важен — перед
//  отрисовкой поля сортируются по offset.
// ============================================================================
#include <eye/magic_eye.hpp>

class Mage {
    int level;
    int mana;
    std::string name;

public:
    Mage(std::string n, int lvl) : level(lvl), mana(lvl * 10), name(std::move(n)) {}
    EYE_DESCRIBE(Mage, level, mana, name)
};

// Поля перечислены В ОБРАТНОМ порядке объявления — карта памяти всё равно
// корректна: Око сортирует по offset. (Заодно демонстрация «порядок не важен».)
struct Point {
    int x;
    int y;

    EYE_DESCRIBE(Point, y, x)
};

int main() {
    Mage solmyr{"Solmyr", 13};
    eye::inspect(solmyr, "Солмир (private через реестр)");

    eye::inspect(Point{3, 7}, "точка (реестр вразнобой)");
}
