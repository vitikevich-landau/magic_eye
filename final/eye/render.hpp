// ============================================================================
//   ОКО МАГА / eye/render.hpp — ВИД: тема «Гримуар» (рамки + цвет)
// ============================================================================
//   Берёт факты из eye/reflect.hpp и рисует их рамочными панелями в консоли.
//   Здесь вся эстетика: ANSI-цвета, box-drawing, раскладка, обрезка длинного.
//   Хочешь другой внешний вид — меняешь только этот файл, модель не трогаешь.
//
//   Как рамка сходится справа, несмотря на цвета и кириллицу: каждую строку
//   собираем через Line, отдельно считая ВИДИМУЮ ширину (ANSI-коды = 0 колонок,
//   один символ UTF-8 = 1 колонка). Переменные куски (имя/тип/значение/заголовок)
//   заранее обрезаем до ширины колонок функцией clip(). Тогда правый борт всегда
//   на одном месте.
// ============================================================================
#pragma once
#include "reflect.hpp"

// --- Платформа: isatty + включение ANSI-консоли ------------------------------
#if defined(_WIN32)
#  define NOMINMAX             // чтобы windows.h не переопределил std::min/max
#  define WIN32_LEAN_AND_MEAN
#  include <io.h>             // _isatty, _fileno
#  include <windows.h>        // кодовая страница + ANSI (VT) в консоли
#else
#  include <unistd.h>         // isatty, fileno — POSIX
#endif

#include <algorithm>  // std::sort, std::min
#include <cctype>     // std::isprint
#include <cstddef>
#include <cstdio>     // std::snprintf
#include <iostream>
#include <sstream>    // std::ostringstream (hexptr)
#include <string>
#include <vector>

namespace eye {

// ════════════════════════════════════════════════════════════════════════════
//  Палитра (M0). ANSI-коды; при выводе не в терминал отключаются сами.
// ════════════════════════════════════════════════════════════════════════════
namespace clr {
inline bool enabled() {
    static const bool on = [] {
#if defined(_WIN32)
        if (!_isatty(_fileno(stdout))) return false;
        // Кодовая страница консоли Windows по умолчанию НЕ UTF-8 — без этого
        // кириллица и рамки (│ ═ █) выводятся кракозябрами.
        SetConsoleOutputCP(CP_UTF8);
        // Включаем ANSI-escape (VT). Не поддерживается (SetConsoleMode вернёт 0)
        // — цвета ВЫКЛЮЧАЕМ, чтобы не сыпать escape-коды как текст.
        const HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        if (h == INVALID_HANDLE_VALUE || !GetConsoleMode(h, &mode)) return false;
        return SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
        return isatty(fileno(stdout)) != 0;
#endif
    }();
    return on;
}
inline const char* code(const char* c) { return enabled() ? c : ""; }
inline const char* reset()  { return code("\033[0m");        }
inline const char* gold()   { return code("\033[38;5;178m"); }  // заголовки, рамка-акцент
inline const char* cyan()   { return code("\033[36m");       }  // типы
inline const char* green()  { return code("\033[32m");       }  // значения
inline const char* grey()   { return code("\033[38;5;245m"); }  // рамка, служебное
inline const char* violet() { return code("\033[35m");       }  // vptr / магия
inline const char* red()    { return code("\033[38;5;131m"); }  // padding
} // namespace clr

namespace detail {

// ════════════════════════════════════════════════════════════════════════════
//  Геометрия рамки и низкоуровневые примитивы
// ════════════════════════════════════════════════════════════════════════════
inline constexpr std::size_t FRAME_W = 60;  // ширина внутренней области (в символах)

// Видимая ширина простой (без ANSI) строки = число кодовых точек UTF-8.
// Считаем все байты, кроме continuation-байтов 10xxxxxx. Для нашего набора
// (ASCII, кириллица, рамки, блоки) 1 кодовая точка = 1 колонка.
inline std::size_t vwidth(const std::string& s) {
    std::size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;
    return n;
}

// Обрезать ПРОСТУЮ строку (без ANSI) до maxcp колонок; при обрезке ставим «…».
inline std::string clip(const std::string& s, std::size_t maxcp) {
    if (maxcp == 0) return "";
    if (vwidth(s) <= maxcp) return s;
    std::string out;
    std::size_t cps = 0;
    for (std::size_t i = 0; i < s.size() && cps + 1 < maxcp; ) {
        const unsigned char c = s[i];
        const std::size_t len =
            c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : 4;
        out.append(s, i, len);
        i += len;
        ++cps;
    }
    return out + "…";
}

// Дополнить простую строку пробелами до ширины w (слева/справа).
inline std::string ljust(std::string s, std::size_t w) {
    const std::size_t v = vwidth(s);
    if (v < w) s.append(w - v, ' ');
    return s;
}
inline std::string rjust(std::string s, std::size_t w) {
    const std::size_t v = vwidth(s);
    if (v < w) s.insert(0, std::string(w - v, ' '));
    return s;
}
inline std::string hex4(std::size_t x) {          // 0x001c
    char b[16];
    std::snprintf(b, sizeof(b), "0x%04zx", static_cast<std::size_t>(x));
    return b;
}
inline std::string hexptr(const void* p) {         // 0x55f3...
    std::ostringstream o; o << p; return o.str();
}
inline std::string hex2(unsigned char b) {         // 1b
    char s[4];
    std::snprintf(s, sizeof(s), "%02x", static_cast<unsigned>(b));
    return s;
}

// Конструктор строки контента: копит текст и ОТДЕЛЬНО видимую ширину, чтобы
// цветовые коды не сбивали расчёт паддинга.
struct Line {
    std::string s;
    std::size_t w = 0;
    Line& plain(const std::string& t) { s += t; w += vwidth(t); return *this; }
    Line& col(const char* c, const std::string& t) {
        s += c; s += t; s += clr::reset(); w += vwidth(t); return *this;
    }
    Line& sp(std::size_t n = 1) { s.append(n, ' '); w += n; return *this; }
};

inline std::string dashes(std::size_t n) {
    std::string r; r.reserve(n * 3);
    for (std::size_t i = 0; i < n; ++i) r += "─";
    return r;
}

// Рамочная строка контента: │ <content, дополнено до FRAME_W> │
inline void put(const Line& ln) {
    const std::size_t pad = ln.w < FRAME_W ? FRAME_W - ln.w : 0;
    std::cout << clr::grey() << "│" << clr::reset() << ' ' << ln.s
              << std::string(pad, ' ') << ' '
              << clr::grey() << "│" << clr::reset() << '\n';
}
// Рамочная строка с простым серым текстом (для сообщений).
inline void put_text(const std::string& t) {
    Line l; l.col(clr::grey(), clip(t, FRAME_W));
    put(l);
}
// Верх рамки:      ╭─◈ Заголовок ─────╮
inline void frame_top(const std::string& title) {
    const std::string t = clip(title, FRAME_W - 3);
    const std::size_t fill = FRAME_W - vwidth(t) - 2;   // t обрезан → fill >= 1
    std::cout << clr::grey() << "╭─" << clr::violet() << "◈ " << clr::reset()
              << clr::gold() << t << clr::reset() << ' '
              << clr::grey() << dashes(fill) << "╮" << clr::reset() << '\n';
}
// Разделитель:     ├─ секция ─────────┤
inline void frame_sep(const std::string& label) {
    const std::string l = clip(label, FRAME_W - 2);
    const std::size_t fill = FRAME_W - vwidth(l) - 1;
    std::cout << clr::grey() << "├─ " << clr::reset()
              << clr::gold() << l << clr::reset() << ' '
              << clr::grey() << dashes(fill) << "┤" << clr::reset() << '\n';
}
// Низ рамки:       ╰──────────────────╯
inline void frame_bottom() {
    std::cout << clr::grey() << "╰" << dashes(FRAME_W + 2) << "╯"
              << clr::reset() << '\n';
}

// ════════════════════════════════════════════════════════════════════════════
//  Паспорт (M0): размер/выравнивание + трейты-чипы
// ════════════════════════════════════════════════════════════════════════════
inline void render_passport(const Passport& p) {
    Line l1;
    l1.col(clr::grey(), "размер ")
      .col(clr::cyan(), std::to_string(p.size)).plain(" Б")
      .col(clr::grey(), "  ·  выравнивание ")
      .col(clr::cyan(), std::to_string(p.align));
    put(l1);

    auto chip = [](Line& ln, bool v, const char* name) {
        ln.col(v ? clr::green() : clr::grey(), v ? "●" : "○").sp()
          .col(clr::grey(), name).sp(2);
    };
    Line l2;
    chip(l2, p.polymorphic, "polymorphic");
    chip(l2, p.aggregate, "aggregate");
    chip(l2, p.trivially_copyable, "trivially-copyable");
    put(l2);
}

// ════════════════════════════════════════════════════════════════════════════
//  Поля + карта памяти + сводка по padding (M2/M4)
//  Ширины колонок (сумма ≤ FRAME_W): off 6 · sz 4 · имя 12 · тип 16 · значение 16
// ════════════════════════════════════════════════════════════════════════════
inline void render_fields(std::vector<FieldInfo> fields, std::size_t total, bool poly) {
    // Реестр мог перечислить поля не по порядку — сортируем по offset, иначе
    // карта памяти и строки padding'а посчитаются неверно.
    std::sort(fields.begin(), fields.end(),
              [](const FieldInfo& a, const FieldInfo& b) { return a.offset < b.offset; });

    // Шапка таблицы.
    Line h;
    h.col(clr::grey(), ljust("off", 6)).sp(2)
     .col(clr::grey(), rjust("sz", 4)).sp(2)
     .col(clr::grey(), ljust("поле", 12)).sp()
     .col(clr::grey(), ljust("тип", 16)).sp()
     .col(clr::grey(), "значение");
    put(h);

    auto pad_row = [](std::size_t from, std::size_t len) {
        Line ln;
        ln.col(clr::red(), hex4(from)).sp(2)
          .col(clr::red(), rjust(std::to_string(len), 4)).sp(2)
          .col(clr::red(), "░ padding ░");
        put(ln);
    };

    // У полиморфного класса первые 8 байт — не «дыра», а vptr. Честно подписываем.
    const bool vptr_band = poly && (fields.empty() || fields.front().offset >= 8);
    std::size_t cursor = 0;
    if (vptr_band) {
        Line ln;
        ln.col(clr::violet(), hex4(0)).sp(2)
          .col(clr::violet(), rjust("8", 4)).sp(2)
          .col(clr::violet(), "▒ vptr ▒").sp()
          .col(clr::grey(), "скрытое поле, см. M3");
        put(ln);
        cursor = 8;
    }

    for (const auto& f : fields) {
        if (f.offset > cursor && f.offset <= total)  // дыра перед полем
            pad_row(cursor, f.offset - cursor);
        Line ln;
        ln.col(clr::grey(), hex4(f.offset)).sp(2)
          .col(clr::grey(), rjust(std::to_string(f.size), 4)).sp(2)
          .col(clr::green(), ljust(clip(f.name.empty() ? "?" : f.name, 12), 12)).sp()
          .col(clr::cyan(),  ljust(clip(f.type, 16), 16)).sp()
          .col(clr::green(), clip(f.value, 16));
        put(ln);
        if (f.offset + f.size > cursor) cursor = f.offset + f.size;
    }
    if (cursor < total) pad_row(cursor, total - cursor);  // хвостовой padding

    // --- карта памяти: 1 символ = 1 байт, до 32 в ряд, с линейкой offset'ов ---
    std::string strip(total, 'p');
    if (vptr_band)
        for (std::size_t b = 0; b < 8 && b < total; ++b) strip[b] = 'V';
    for (std::size_t i = 0; i < fields.size(); ++i)
        for (std::size_t b = 0; b < fields[i].size; ++b) {
            const std::size_t at = fields[i].offset + b;
            if (at < strip.size()) strip[at] = (i % 2 == 0) ? 'A' : 'B';  // без OOB
        }

    put_text("· карта памяти (1 символ = 1 байт)");
    for (std::size_t base = 0; base < total; base += 32) {
        const std::size_t len = std::min<std::size_t>(32, total - base);
        // линейка: число через каждые 8 байт, выровнено под глифы карты
        Line ruler;
        for (std::size_t i = 0; i < len; i += 8)
            ruler.col(clr::grey(), ljust(std::to_string(base + i),
                                         std::min<std::size_t>(8, len - i)));
        put(ruler);
        // сами байты
        Line m;
        for (std::size_t i = 0; i < len; ++i) {
            switch (strip[base + i]) {
                case 'A': m.col(clr::cyan(),   "█"); break;
                case 'B': m.col(clr::cyan(),   "▓"); break;
                case 'V': m.col(clr::violet(), "▒"); break;
                default:  m.col(clr::red(),    "░"); break;
            }
        }
        put(m);
    }
    // легенда
    Line leg;
    leg.col(clr::cyan(), "█").col(clr::grey(), " поле").sp(3)
       .col(clr::red(), "░").col(clr::grey(), " padding").sp(3)
       .col(clr::violet(), "▒").col(clr::grey(), " vptr");
    put(leg);

    // --- сводка: считаем ПО КАРТЕ (дубли/перекрытия полей не переполнят size_t) -
    std::size_t field_bytes = 0, vptr_bytes = 0;
    for (char c : strip) {
        if (c == 'A' || c == 'B') ++field_bytes;
        else if (c == 'V')        ++vptr_bytes;
    }
    const std::size_t padding = strip.size() - field_bytes - vptr_bytes;
    Line sum;
    sum.col(clr::grey(), "занято ").col(clr::green(), std::to_string(field_bytes));
    if (vptr_band) sum.col(clr::grey(), "  ·  vptr ").col(clr::violet(), std::to_string(vptr_bytes));
    sum.col(clr::grey(), "  ·  дыры ").col(clr::red(), std::to_string(padding))
       .col(clr::grey(), " (" + std::to_string(total ? padding * 100 / total : 0) + "%)");
    put(sum);
}

// ════════════════════════════════════════════════════════════════════════════
//  Секция vtable (M3)
// ════════════════════════════════════════════════════════════════════════════
inline void render_vtable(const VtableInfo& vi) {
    Line l1;
    l1.col(clr::violet(), ljust("vptr", 14)).plain(hexptr(vi.vptr))
      .sp(2).col(clr::grey(), "первые 8 байт объекта");
    put(l1);
    Line l2;
    l2.col(clr::violet(), ljust("динамич. тип", 14))
      .col(clr::cyan(), clip(vi.dyn_type, FRAME_W - 16)).sp()
      .col(clr::grey(), "(RTTI)");
    put(l2);
    if (vi.itanium) {
        Line l3;
        l3.col(clr::violet(), ljust("offset-to-top", 14))
          .plain(std::to_string(vi.offset_to_top));
        put(l3);
        Line l4;
        l4.col(clr::violet(), ljust("слот [0]", 14))
          .col(clr::green(), hexptr(vi.slot0)).sp()
          .col(clr::grey(), "1-я вирт. функция");
        put(l4);
    } else {
        put_text("offset-to-top и слоты vtable — деталь Itanium ABI (GCC/Clang);");
        put_text("у MSVC раскладка другая, сырые ячейки не читаем.");
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  Hex-дамп сырых байтов (M1)
// ════════════════════════════════════════════════════════════════════════════
inline void render_bytes(const void* addr, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(addr);
    const std::size_t shown = std::min<std::size_t>(size, 128);

    for (std::size_t row = 0; row < shown; row += 8) {
        Line ln;
        ln.col(clr::grey(), hex4(row)).sp(2);
        for (std::size_t i = row; i < row + 8; ++i) {
            if (i < shown) ln.col(clr::cyan(), hex2(bytes[i])).sp();
            else           ln.sp(3);
        }
        ln.sp().col(clr::green(), "│");
        std::string ascii;
        for (std::size_t i = row; i < row + 8 && i < shown; ++i)
            ascii += std::isprint(bytes[i]) ? static_cast<char>(bytes[i]) : '.';
        ln.col(clr::green(), ascii).col(clr::green(), "│");
        put(ln);
    }
    if (shown < size)
        put_text("... ещё " + std::to_string(size - shown) + " байт скрыто");
}

} // namespace detail
} // namespace eye
