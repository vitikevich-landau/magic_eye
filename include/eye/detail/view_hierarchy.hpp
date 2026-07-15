// ОКО МАГА / eye/detail/view_hierarchy.hpp — секция «иерархия»: под-объекты баз.
#pragma once
#include "frame.hpp"
#include "model_types.hpp"

namespace eye::detail {

// ════════════════════════════════════════════════════════════════════════════
//  Секция «иерархия» — под-объекты баз (только при наследовании через EYE_BASES)
// ════════════════════════════════════════════════════════════════════════════
// has_vbase_ptr — записал ли МОДЕЛЬ отдельный служебный указатель на virtual-
// базу (регион ▒ vbase-ptr на карте). Это и есть верный признак, а не ABI-#if:
//   • Itanium + ПОЛИморфный тип — указателя нет, смещение зашито в vtable;
//   • Itanium + НЕполиморфный (struct D : virtual B без virtual-функций) — есть
//     отдельный указатель на offset 0;
//   • MSVC — vbptr есть всегда (offset 0 у неполиморфного, +8 у полиморфного).
inline void render_hierarchy(const std::vector<BaseInfo>& bases,
                             bool has_vbase = false, bool has_vbase_ptr = false) {
    for (const BaseInfo& b : bases) {
        Line l;
        l.sp(static_cast<std::size_t>(b.depth) * 2);
        if (b.depth) l.col(clr::grey(), "└ ");
        l.col(clr::cyan(), clip(b.type, std::max<std::size_t>(8, frame_width() / 2)))
         .col(clr::grey(), " @ ")
         .col(clr::green(), "+" + hex4(b.offset));
        if (b.polymorphic)  l.col(clr::grey(), " · ").col(clr::violet(), "vptr");
        if (b.virtual_base) l.col(clr::grey(), " · ").col(clr::gold(), "virtual");
        if (b.shared)       l.col(clr::grey(), " · общий (показан выше)");
        put(l);
    }
    put_text("offset — где под-объект базы лежит внутри наследника");
    if (has_vbase) {
        if (has_vbase_ptr) {
            // Есть отдельная служебная ячейка — она и видна на карте как ▒.
            put_text("virtual-база (ромб): к ней ведёт служебный указатель");
            put_text("vbase-ptr (регион ▒ на карте) · на MSVC это vbptr");
        } else {
            // Itanium + полиморфный: смещение vbase лежит внутри vtable.
            put_text("virtual-база (ромб): её смещение зашито в vtable —");
            put_text("отдельного указателя нет (на offset 0 — vptr)");
        }
    }
}

} // namespace eye::detail
