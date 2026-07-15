// ОКО МАГА / eye/detail/nav/nav_node.hpp — УЗЕЛ навигационного графа.
//   Вершина странствия: объект, под-объект базы, поле, vptr-сайт, спутник
//   кучи, элементы vector, «ещё…» (пагинация). Дети СТРОЯТСЯ ЛЕНИВО замыканием
//   expand — ничего не разбирается и не разыменовывается, пока пользователь не
//   раскрыл узел. Замыкания захватывают типизированные ссылки на живые
//   объекты, поэтому std::function живёт здесь, в nav-слое, а НЕ в
//   model_types.hpp — шов «модель ↔ вид» остаётся plain-структурами.
#pragma once
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace eye::detail::nav {

enum class NodeKind {
    root,        // корень галереи (объект или статика типа)
    object,      // вложенный объект (pointee после перехода — M-D)
    base,        // под-объект базового класса
    field,       // поле (или элемент массива/вектора)
    vptr,        // vptr-сайт
    satellite,   // кучный буфер строки
    elems,       // внешний массив vector
    opaque,      // скрытый диапазон («туман»)
    more,        // «⋯ ещё N — Enter» (пагинация больших коллекций)
    note,        // строка-пояснение (лист без данных)
};

// Режим правой панели (что рисовать про выбранный узел).
enum class DetailMode { memory, passport, vtable, hex };

struct NavNode {
    NodeKind kind = NodeKind::note;

    // --- строка дерева -------------------------------------------------------
    std::string title;     // имя: "hp", "база Unit", "vptr «BaseB»"
    std::string type;      // тип для дерева и поиска ("int", "std::string")
    std::string preview;   // короткое значение ("= 100", "\"Griffin\" (7/15)")
    std::string suffix;    // правый край: offset/адрес ("+0x0008", "@0x55e…")

    // --- данные узла (для hex-режима и меток) ---------------------------------
    const void* addr = nullptr;
    std::size_t size = 0;

    // --- ленивые дети ----------------------------------------------------------
    bool can_expand = false;
    std::function<std::vector<NavNode>()> expand;

    // --- панель деталей: рисует секции узла в АКТИВНЫЙ Surface ----------------
    std::function<void(DetailMode)> detail;
    bool has_vtable = false;   // режим [v] осмыслен (полиморфный узел)

    // --- переход по указателю (M-D) --------------------------------------------
    bool can_follow = false;
    std::function<NavNode()> follow;   // построить узел pointee
    std::string follow_block;          // почему перейти нельзя (nullptr/char*/…)

    // --- деградация не-TTY: как напечатать этот корень статикой ---------------
    std::function<void()> print_static;   // только у корней галереи
};

} // namespace eye::detail::nav
