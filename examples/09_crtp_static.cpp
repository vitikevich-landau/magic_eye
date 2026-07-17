// ============================================================================
//  Пример 09: CRTP — статический полиморфизм без vptr
//  Сборка: g++ -std=c++20 -Wall -Wextra -Wpedantic -I../include 09_*.cpp -o ex09
//
//  CRTP (Curiously Recurring Template Pattern): база — шаблон, параметр которого
//  сам наследник (Shape<Circle>). Диспетчеризация решается на этапе КОМПИЛЯЦИИ,
//  virtual не нужен — значит нет ни vptr, ни vtable. Сравни с примером 06:
//  в паспорте polymorphic ○ нет, первое поле лежит с offset 0, секции vtable
//  нет. При этом наследование настоящее — под-объект базы Shape<...> Око
//  показывает как обычно, с его собственными полями.
// ============================================================================
#include <eye/magic_eye.hpp>

#include <string>

template <class Derived>
class Shape {
    int id = 0;

public:
    // Статический вызов метода наследника — без virtual.
    double area() const {
        return static_cast<const Derived*>(this)->area_impl();
    }
    EYE_DESCRIBE(Shape, id)
};

class Circle : public Shape<Circle> {
    double radius = 2.0;

public:
    double area_impl() const { return 3.14159 * radius * radius; }
    EYE_BASES(Circle, Shape<Circle>)
    EYE_DESCRIBE(Circle, radius)
};

class Square : public Shape<Square> {
    double side = 3.0;

public:
    double area_impl() const { return side * side; }
    EYE_BASES(Square, Shape<Square>)
    EYE_DESCRIBE(Square, side)
};

int main() {
    // ТЕРМИНАЛ → интерактивный обозреватель. Сравни с примером 06: в паспорте
    // «полиморфный ◇ нет», узла vptr в дереве НЕТ, первое поле лежит с offset 0.
    // area() — статический вызов наследника (без virtual), кладём в подпись.
    // Стрелки/Enter/q, ? — помощь. Пайп/файл → те же панели статикой.
    Circle circle;
    Square square;

    eye::Gallery{}
        .add(circle, "круг · area()=" + std::to_string(circle.area()))
        .add(square, "квадрат · area()=" + std::to_string(square.area()))
        .run();
}
