// ============================================================================
//  Пример 3: полиморфные типы — vptr и vtable
//  Сборка: g++ -std=c++20 -Wall -Wextra -Wpedantic -I.. 03_tavern_vtable.cpp -o ex03
// ============================================================================
#include "magic_eye.hpp"

struct Hero {
    virtual void attack() const { std::cout << "тычок\n"; }
    virtual ~Hero() = default;
    int level = 1;
};

struct CragHack : Hero {
    void attack() const override { std::cout << "УДАР\n"; }
    int rage = 99;
};

int main() {
    Hero     hero;
    CragHack crag;

    // У полиморфных появляется секция vtable: сравни vptr и динамический
    // тип. В байтах первые 8 — тот самый vptr.
    eye::inspect(hero, "герой-новичок");
    eye::inspect(crag, "Crag Hack");

    // Осмотр через ссылку на базу: статический тип у Ока — Hero&,
    // но динамический тип в секции vtable — правда из RTTI: CragHack.
    const Hero& base = crag;
    eye::inspect(base, "Crag Hack через Hero&");
}
