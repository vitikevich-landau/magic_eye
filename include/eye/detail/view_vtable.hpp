// ОКО МАГА / eye/detail/view_vtable.hpp — секция vtable: гримуар рода.
#pragma once
#include <string>
#include "frame.hpp"
#include "model_types.hpp"

namespace eye::detail {

// ════════════════════════════════════════════════════════════════════════════
//  Секция vtable (M3) — блок-диаграмма: объект → vptr → vtable → слот → код
// ════════════════════════════════════════════════════════════════════════════
inline void render_primary_vtable(const VtableSite& vi, std::size_t obj_size) {
    // Текстовая строка-факт: те же 8 байт, что лежали в начале дампа.
    Line l1;
    l1.col(clr::violet(), "vptr = ").plain(hexptr(vi.vptr))
      .col(clr::grey(), " · динамич. тип: ")
      .col(clr::cyan(), clip(vi.dyn_type, 18));
    put(l1);
    put_blank();

    const std::string dyn = clip(vi.dyn_type, 12);
    // Левый бокс (объект, 13 внутри) + стрелка + правый бокс (vtable, 29).
    // Адрес в шапке бокса = те же 8 байт, что видны в дампе как регион ▒ vptr.
    Line cap;
    cap.sp(3).col(clr::grey(), "этот объект").to(27)
       .col(clr::gold(), "vtable «" + dyn + "» @ ").plain(hexptr(vi.vptr));
    put(cap);
    {
        Line ln;
        ln.sp(2).col(clr::grey(), "┌─────────────┐        "
                                  "┌─────────────────────────────┐");
        put(ln);
    }
    { // vptr ●────────► [-2] ...
        Line ln;
        ln.sp(2).col(clr::grey(), "│ ")
          .col(clr::violet(), "0x00 vptr ")
          .col(clr::violet(), "●").col(clr::grey(), "─┼───────►│ ")
          .col(vi.itanium ? clr::dim() : clr::grey(),
               vi.itanium ? ljust("[-2] offset-to-top = " +
                                      std::to_string(vi.offset_to_top), 28)
                          : ljust("typeinfo «" + dyn + "» (RTTI)", 28))
          .col(clr::grey(), "│");
        put(ln);
    }
    { // поля… │ [-1] typeinfo
        Line ln;
        ln.sp(2).col(clr::grey(), "│ ")
          .col(clr::cyan(), obj_size > sizeof(void*) ? "0x08 поля…" : "          ")
          .col(clr::grey(), "  │        │ ")
          .col(clr::dim(), ljust(vi.itanium ? "[-1] typeinfo «" + dyn + "»"
                                            : "(служебные ячейки)", 28))
          .col(clr::grey(), "│");
        put(ln);
    }
    { // └──┘ │ [ 0] слот
        Line ln;
        ln.sp(2).col(clr::grey(), "└─────────────┘        │ ");
        if (vi.itanium)
            ln.col(clr::green(), ljust("[ 0] 1-я virtual-функция:", 28));
        else
            ln.col(clr::grey(), ljust("слоты: у MSVC своя раскладка", 28));
        ln.col(clr::grey(), "│");
        put(ln);
    }
    { // адрес кода / примечание
        Line ln;
        ln.sp(2).col(clr::grey(), "               ").sp(8).col(clr::grey(), "│ ");
        if (vi.itanium)
            ln.col(clr::green(), ljust("     код @ " + hexptr(vi.slot0), 28));
        else
            ln.col(clr::grey(), ljust("     сырые ячейки не читаем", 28));
        ln.col(clr::grey(), "│");
        put(ln);
    }
    {
        Line ln;
        ln.sp(2).plain("                       ")
          .col(clr::grey(), "└─────────────────────────────┘");
        put(ln);
    }
    put_blank();
    put_text("vptr — у КАЖДОГО объекта · vtable — ОДНА на класс");
    Line tr;
    tr.col(clr::grey(), "вызов virtual: объект → vptr → слот [0] → прыжок на код");
    put(tr);
}

// Все vptr-сайты. Для primary (offset 0) — подробная блок-диаграмма; для
// вторичных баз (множественное наследование) — компактные строки с их
// offset-to-top (тот самый «шаг назад» к началу самого производного объекта).
inline void render_vtables(const std::vector<VtableSite>& sites,
                           std::size_t obj_size) {
    if (sites.empty()) return;
    const VtableSite* primary = &sites.front();
    for (const VtableSite& s : sites)
        if (s.offset == 0) { primary = &s; break; }

    render_primary_vtable(*primary, obj_size);

    bool any_secondary = false;
    for (const VtableSite& s : sites)
        if (&s != primary) { any_secondary = true; break; }
    if (!any_secondary) return;

    put_blank();
    put_text("множественное наследование: у каждой полиморфной базы — свой vptr");
    for (const VtableSite& s : sites) {
        if (&s == primary) continue;
        Line l;
        l.col(clr::violet(), "vptr «" + clip(s.owner, 18) + "»")
         .col(clr::grey(), " @ +").col(clr::green(), hex4(s.offset));
        put(l);
        if (s.itanium) {
            Line l2;
            l2.sp(2).col(clr::grey(), "offset-to-top = ")
              .col(clr::cyan(), std::to_string(s.offset_to_top))
              .col(clr::grey(), " — шаг назад к началу объекта");
            put(l2);
            Line l3;
            l3.sp(2).col(clr::grey(), "код [0]: ").plain(hexptr(s.slot0));
            put(l3);
        } else {
            Line l2;
            l2.sp(2).col(clr::grey(), "динамич. тип: ")
              .col(clr::cyan(), clip(s.dyn_type, 22));
            put(l2);
        }
    }
}

} // namespace eye::detail
