// ОКО МАГА / eye/detail/surface.hpp — ШОВ ВИДА: строки в буфер или в поток.
//   Вид собирает готовые ANSI-строки. Куда они уходят — решает активный
//   Surface: он есть → строки копятся (их заберёт TUI-панель или тест),
//   его нет → печать в std::cout, как всегда печатал inspect. Благодаря
//   этому статический вывод не меняется ни на байт, а интерактивный режим
//   получает те же секции без переписывания view_*.
#pragma once
#include <iostream>
#include <string>
#include <utility>
#include <vector>
#include "text.hpp"   // vwidth_ansi — видимая ширина готовой ANSI-строки

namespace eye::detail {

// Готовая строка вида: текст С ANSI-кодами + его видимая ширина (для клипа
// и горизонтального скролла в TUI; ANSI-коды не занимают колонок).
struct StyledLine {
    std::string s;
    std::size_t w = 0;
};

struct Surface {
    std::vector<StyledLine> lines;
    void clear() { lines.clear(); }
};

inline Surface*& active_surface() {
    static Surface* current = nullptr;
    return current;
}

// Единая точка выхода строк вида. Ширину считаем только для Surface —
// печатному пути она не нужна, а inspect не должен платить за TUI.
inline void emit_line(std::string s) {
    if (Surface* sf = active_surface()) {
        const std::size_t w = vwidth_ansi(s);
        sf->lines.push_back({std::move(s), w});
    } else {
        std::cout << s << '\n';
    }
}

// RAII: перенаправить строки вида в данный Surface (вложение допустимо —
// прежний адресат восстанавливается).
class SurfaceScope {
public:
    explicit SurfaceScope(Surface& s) : prev_(active_surface()) {
        active_surface() = &s;
    }
    SurfaceScope(const SurfaceScope&) = delete;
    SurfaceScope& operator=(const SurfaceScope&) = delete;
    ~SurfaceScope() { active_surface() = prev_; }

private:
    Surface* prev_;
};

} // namespace eye::detail
