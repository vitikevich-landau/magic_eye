// ============================================================================
//  Пример 08: ромбовидное наследование — общая virtual-база
//  Сборка: g++ -std=c++20 -Wall -Wextra -Wpedantic -I../include 08_*.cpp -o ex08
//
//  Ромб: Paladin наследует Mage и Warr, а обе — virtual от Being. Без virtual
//  Being лёг бы в объект ДВАЖДЫ; с virtual он ОДИН, общий. Око замечает это:
//  в секции «иерархия» общий под-объект помечается «virtual» и «общий (показан
//  выше)», а в памяти его поля выводятся один раз. Виртуальная база в модели
//  Itanium лежит в ХВОСТЕ объекта (после невиртуальных данных) — видно по её
//  большому offset. Виртуальность база определяется приёмом с приводом типа,
//  он работает и на GCC/Clang, и на MSVC.
// ============================================================================
#include <eye/magic_eye.hpp>

class Being {
    int soul = 1;

public:
    virtual ~Being() = default;
    EYE_DESCRIBE(Being, soul)
};

class Mage : public virtual Being {
    int mana = 10;

public:
    EYE_BASES(Being)
    EYE_DESCRIBE(Mage, mana)
};

class Warrior : public virtual Being {
    int strength = 20;

public:
    EYE_BASES(Being)
    EYE_DESCRIBE(Warrior, strength)
};

// Паладин — и маг, и воин, но душа (Being) у него ОДНА на двоих.
class Paladin : public Mage, public Warrior {
    int faith = 5;

public:
    EYE_BASES(Mage, Warrior)
    EYE_DESCRIBE(Paladin, faith)
};

int main() {
    // Отдельная ветвь ромба: у Mage своя Being (offset смотри в «иерархии»).
    Mage mage;
    eye::inspect(mage, "маг (Mage : virtual Being)");

    // Полный ромб: Being общий — Око покажет его один раз и пометит.
    Paladin paladin;
    eye::inspect(paladin, "паладин (ромб Mage+Warrior→Being)");
}
