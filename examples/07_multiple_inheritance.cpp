// ============================================================================
//  Пример 07: множественное наследование — НЕСКОЛЬКО vptr
//  Сборка: g++ -std=c++20 -Wall -Wextra -Wpedantic -I../include 07_*.cpp -o ex07
//
//  Когда у класса две полиморфные базы, у него ДВА vptr: первый (primary) он
//  делит с первой базой на offset 0, а вторая база получает свой vptr в начале
//  своего под-объекта. Око показывает оба региона vptr в карте памяти и в
//  секции vtable выводит для вторичной базы её offset-to-top — отрицательное
//  число, «шаг назад» от под-объекта к началу самого производного объекта
//  (так вызов virtual через вторую базу находит настоящий this). Под Itanium
//  ABI (GCC/Clang) offset-to-top читается из vtable[-2]; на MSVC — заметка.
// ============================================================================
#include <eye/magic_eye.hpp>

class Swimmer {
    int fins = 2;

public:
    virtual void swim() const { std::cout << "плывёт\n"; }
    virtual ~Swimmer() = default;
    EYE_DESCRIBE(Swimmer, fins)
};

class Flyer {
    int wings = 2;

public:
    virtual void fly() const { std::cout << "летит\n"; }
    virtual ~Flyer() = default;
    EYE_DESCRIBE(Flyer, wings)
};

// Утка — и пловец, и летун. Порядок баз в EYE_BASES = порядок в объявлении.
class Duck : public Swimmer, public Flyer {
    int quack = 1;

public:
    void swim() const override { std::cout << "гребёт лапами\n"; }
    void fly() const override  { std::cout << "машет крыльями\n"; }
    EYE_BASES(Duck, Swimmer, Flyer)
    EYE_DESCRIBE(Duck, quack)
};

int main() {
    Duck duck;
    // Ищи в карте памяти ДВА региона ▒ vptr и поля, подписанные из какой базы.
    eye::inspect(duck, "утка (Swimmer + Flyer)");

    // Осмотр через вторую базу: указатель Flyer* смещён внутрь объекта, но
    // динамический тип из RTTI — по-прежнему Duck.
    const Flyer& as_flyer = duck;
    eye::inspect(as_flyer, "утка через Flyer&");
}
