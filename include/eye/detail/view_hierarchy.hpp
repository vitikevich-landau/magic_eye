// ОКО МАГА / eye/detail/view_hierarchy.hpp — секция «иерархия»: под-объекты баз.
#pragma once
#include "abi.hpp"          // EYE_ITANIUM_ABI — заметка про virtual-базу
#include "frame.hpp"
#include "model_types.hpp"

namespace eye::detail {

// ════════════════════════════════════════════════════════════════════════════
//  Секция «иерархия» — под-объекты баз (только при наследовании через EYE_BASES)
// ════════════════════════════════════════════════════════════════════════════
inline void render_hierarchy(const std::vector<BaseInfo>& bases,
                             bool has_vbase = false) {
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
#if EYE_ITANIUM_ABI
        // На Itanium (эта сборка) на offset 0 лежит vptr; смещение virtual-базы
        // достаётся через vtable — ОТДЕЛЬНОГО указателя на неё нет.
        put_text("virtual-база (ромб): её смещение зашито в vtable —");
        put_text("отдельного указателя нет (на offset 0 — vptr)");
        put_text("на MSVC иначе: отдельный vbptr; схема vtable ниже — по Itanium");
#else
        // На MSVC virtual-база адресуется через отдельный vbptr.
        put_text("virtual-база (ромб): на MSVC — отдельный указатель vbptr");
        put_text("(на Itanium его нет: смещение зашито в vtable)");
#endif
    }
}

} // namespace eye::detail
