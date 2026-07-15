// ============================================================================
//  Пример 05: одиночное наследование — под-объект базы и её поля
//  Сборка: g++ -std=c++20 -Wall -Wextra -Wpedantic -I../include 05_*.cpp -o ex05
//
//  EYE_BASES(...) объявляет базы наследника. Поля КАЖДОЙ базы Око берёт из ЕЁ
//  собственного EYE_DESCRIBE — поэтому видны даже private-поля базы, к которым
//  наследник доступа не имеет. Смещение под-объекта базы Око вычисляет по
//  живому объекту (через static_cast), поэтому базы должны быть публичными.
//
//  В EYE_DESCRIBE наследника перечисляем ТОЛЬКО его собственные поля —
//  унаследованные придут из реестров баз (иначе задвоятся).
// ============================================================================
#include <eye/magic_eye.hpp>

// База с private-полями и своим реестром.
class Unit {
    int hp = 100;
    int speed = 5;

public:
    EYE_DESCRIBE(Unit, hp, speed)
};

// Наследник: под-объект Unit лежит с offset 0, дальше — собственные поля.
// Каждое поле в карте памяти подписано, ИЗ КАКОЙ базы оно пришло.
class Knight : public Unit {
    int armor = 30;
    std::string banner = "Griffin";

public:
    EYE_BASES(Knight, Unit)
    EYE_DESCRIBE(Knight, armor, banner)
};

// Три уровня: Knight → Paladin. Реестр рекурсивен на любую глубину.
class Paladin : public Knight {
    int faith = 42;

public:
    EYE_BASES(Paladin, Knight)
    EYE_DESCRIBE(Paladin, faith)
};

int main() {
    Knight knight;
    eye::inspect(knight, "рыцарь (Knight : Unit)");

    Paladin paladin;
    eye::inspect(paladin, "паладин (Paladin : Knight : Unit)");
}
