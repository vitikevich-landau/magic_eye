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
    // ТЕРМИНАЛ → интерактивный обозреватель. Раскрой строку в куче — под ней
    // узел «буфер кучи»; раскрой vector — увидишь узел «элементы» (страницами
    // по 100). Стрелки/Enter/q, ? — помощь. Пайп/файл → те же панели статикой.
    std::string short_name = "Sir Mullich";                          // SSO
    std::string long_name  = "Sir Mullich, Champion of the Absolute Speed"; // куча
    std::vector<int> army{ 25, 8, 99 };
    army.reserve(6);   // end и capacity_end разъедутся — видно оба слота
    std::vector<bool> flags{ true, false, true, true };   // упакованные биты
    std::array<int, 4> resources{ 10, 20, 30, 40 };       // всё внутри объекта

    eye::Gallery{}
        .add(short_name, "строка (SSO — внутри объекта)")
        .add(long_name,  "строка (буфер в куче)")
        .add(army,       "vector<int> — объект и массив в куче")
        .add(flags,      "vector<bool> — упакованные биты")
        .add(resources,  "array<int,4> (всё в объекте, без кучи)")
        .run();
}
