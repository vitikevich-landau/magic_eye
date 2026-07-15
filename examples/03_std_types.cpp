// ============================================================================
//  Пример 03: типы из стандартной библиотеки
//  Сборка: g++ -std=c++20 -Wall -Wextra -Wpedantic -I../include 03_*.cpp -o ex03
//
//  У STL-типов private-поля напрямую не читаются, но Око знает адаптеры для
//  std::string и std::vector: семантику берёт из публичного API, а сырые
//  адресные слова осторожно сопоставляет с объектом (помечает знаком ≈).
//  Прочие агрегатные обёртки (std::array, std::pair) разбираются автоматикой.
// ============================================================================
#include <eye/magic_eye.hpp>

#include <array>
#include <vector>

int main() {
    // --- std::string: короткая живёт ВНУТРИ объекта (SSO) ... -----------------
    std::string short_name = "Sir Mullich";
    eye::inspect(short_name, "строка (SSO — внутри объекта)");

    // ... а у длинной внутри только указатель на кучу, длина и вместимость;
    // сам буфер Око покажет отдельной панелью-спутником.
    std::string long_name = "Sir Mullich, Champion of the Absolute Speed";
    eye::inspect(long_name, "строка (буфер в куче)");

    // --- std::vector<int>: служебный объект (3 слова) + внешний массив --------
    std::vector<int> army{ 25, 8, 99 };
    army.reserve(6);   // end и capacity_end разъедутся — видно оба слота
    eye::inspect(army, "vector<int> — объект и массив в куче");

    // --- std::vector<bool>: биты упакованы, data() намеренно нет --------------
    std::vector<bool> flags{ true, false, true, true };
    eye::inspect(flags, "vector<bool> — упакованные биты");

    // --- std::array: фиксированный размер, все элементы ВНУТРИ объекта --------
    std::array<int, 4> resources{ 10, 20, 30, 40 };
    eye::inspect(resources, "array<int,4> (всё в объекте, без кучи)");
}
