// ============================================================================
//  Пример 06: полиморфизм — vptr, vtable и динамический тип
//  Сборка: g++ -std=c++20 -Wall -Wextra -Wpedantic -I../include 06_*.cpp -o ex06
//
//  У полиморфного класса компилятор вставляет скрытый vptr в НАЧАЛО объекта —
//  поэтому первое поле лежит НЕ с нулевого offset. Око показывает регион vptr,
//  блок-диаграмму «объект → vptr → vtable → слот → код» и динамический тип из
//  RTTI. Сырые ячейки vtable (offset-to-top, typeinfo) читаются под Itanium
//  ABI (GCC/Clang); на MSVC — портируемая часть (тип через typeid).
// ============================================================================
#include <eye/magic_eye.hpp>

class Hero {
    int level = 1;

public:
    virtual void attack() const { std::cout << "тычок\n"; }
    virtual ~Hero() = default;
    EYE_DESCRIBE(Hero, level)
};

// rage наследника садится в ХВОСТОВОЙ padding базы: у Hero после level (offset
// 8..11) до sizeof=16 оставались 4 байта, и rage занимает именно их (offset
// 0x0c). Полиморфная база — не standard-layout, поэтому переиспользование
// хвоста разрешено. Смотри: sizeof(CragHack) == sizeof(Hero).
class CragHack : public Hero {
    int rage = 99;

public:
    void attack() const override { std::cout << "УДАР\n"; }
    EYE_BASES(CragHack, Hero)
    EYE_DESCRIBE(CragHack, rage)
};

int main() {
    Hero hero;
    eye::inspect(hero, "герой-новичок");

    CragHack crag;
    eye::inspect(crag, "Crag Hack (rage в хвосте базы)");

    // Осмотр через ссылку на базу: статический тип у Ока — Hero&, а
    // динамический в секции vtable — правда из RTTI: CragHack.
    const Hero& base = crag;
    eye::inspect(base, "Crag Hack через Hero&");
}
