// ОКО МАГА / eye/detail/frame.hpp — РАМКИ: put, две зоны, картуш, разделители.
//   Каждая рамочная строка собирается в СТРОКУ и уходит через emit_line
//   (surface.hpp): без активного Surface — в std::cout, байт-в-байт как
//   раньше; с ним — в буфер, откуда её заберёт TUI-панель или тест.
#pragma once
#include <string>
#include "geometry.hpp"
#include "surface.hpp"
#include "text.hpp"

namespace eye::detail {


// Рамочная строка контента: ║ <content, дополнено до frame_width()> ║
// Внешняя рамка объекта — ДВОЙНАЯ (║ ═ ╔ ╗ ╚ ╝), как диалог DOS-игры; секции
// внутри — одинарные (╟─ ─╢), поэтому «двойное = окно, одинарное = строки».
inline void put(const Line& ln) {
    const std::size_t fw = frame_width();
    const std::size_t body_w = std::min(ln.w, fw);
    const std::size_t pad = fw - body_w;
    std::string out;
    out += margin_str();
    out += clr::grey(); out += "║"; out += clr::reset(); out += ' ';
    out += ln.w > fw ? clip_ansi(ln.s, fw) : ln.s;
    out.append(pad, ' ');
    out += ' ';
    out += clr::grey(); out += "║"; out += clr::reset();
    emit_line(std::move(out));
}

// --- Двухзонная строка: │ <карта map_w> ║ <кодекс codex_w> │ -----------------
// Левую и правую ячейки жёстко дополняем/обрезаем до их ширины, поэтому спайн ║
// и правая рамка стоят в фиксированных колонках на всех строках.
inline void put_two_zone(const Line& left, const Line& right) {
    const std::size_t mw = geo().map_w, cw = geo().codex_w;
    const std::size_t rw = cw > 1 ? cw - 1 : cw;   // 1 колонка — отступ после ║
    Line ln;
    if (left.w <= mw) { ln.s += left.s; ln.s.append(mw - left.w, ' '); }
    else              { ln.s += clip_ansi(left.s, mw); }
    ln.col(clr::grey(), "║");
    ln.s += ' ';                                   // воздух в кодекс-зоне
    if (right.w <= rw) { ln.s += right.s; ln.s.append(rw - right.w, ' '); }
    else               { ln.s += clip_ansi(right.s, rw); }
    ln.w = mw + 1 + 1 + rw;                         // = mw + 1 + cw = frame_w
    put(ln);
}

// Кромка/разделитель двухзонной полосы: ├─…─<j>─…─┤ (j над спайном ║).
//   j = "╥" открыть полосу · "╫" карточка-разделитель · "╨" закрыть.
inline void frame_span2(const char* j) {
    const std::size_t mw = geo().map_w, cw = geo().codex_w;
    std::string out;
    out += margin_str();
    out += clr::grey();
    out += "╟"; out += dashes(mw + 1); out += j; out += dashes(cw + 1); out += "╢";
    out += clr::reset();
    emit_line(std::move(out));
}
// Рамочная строка с простым серым текстом (для сообщений).
inline void put_text(const std::string& t) {
    Line l; l.col(clr::grey(), clip(t, frame_width()));
    put(l);
}
inline void put_blank() { put(Line{}); }

// Верх рамки — двойная с картушем:   ╔═◈╡ Заголовок ╞════════◈═╗
inline void frame_top(const std::string& title) {
    const std::size_t fw = frame_width();
    const std::string t = clip(title, fw > 8 ? fw - 8 : 1);
    // Полная ширина между углами = fw+4. Фикс-части: ╔═(2)+◈╡ (3)+t+ ╞(2)+═◈═╗(4).
    const std::size_t used = 2 + 3 + vwidth(t) + 2 + 4;
    const std::size_t fill = fw + 4 > used ? fw + 4 - used : 1;
    std::string out;
    out += margin_str();
    out += clr::grey(); out += "╔═"; out += clr::violet(); out += "◈╡ "; out += clr::reset();
    out += clr::gold(); out += t; out += clr::reset();
    out += clr::violet(); out += " ╞"; out += clr::grey(); out += dashes(fill);
    out += clr::violet(); out += "═◈"; out += clr::grey(); out += "═╗";
    out += clr::reset();
    emit_line(std::move(out));
}
// Разделитель секции — одинарный в двойную рамку:   ╟─ секция ─────────╢
inline void frame_sep(const std::string& label) {
    const std::size_t fw = frame_width();
    const std::string l = clip(label, fw - 2);
    const std::size_t fill = fw - vwidth(l) - 1;
    std::string out;
    out += margin_str();
    out += clr::grey(); out += "╟─ "; out += clr::reset();
    out += clr::gold(); out += l; out += clr::reset(); out += ' ';
    out += clr::grey(); out += dashes(fill); out += "╢"; out += clr::reset();
    emit_line(std::move(out));
}
// Низ рамки — двойная:   ╚══════════════════╝
inline void frame_bottom() {
    std::string out;
    out += margin_str();
    out += clr::grey(); out += "╚"; out += dashes(frame_width() + 2); out += "╝";
    out += clr::reset();
    emit_line(std::move(out));
}

} // namespace eye::detail
