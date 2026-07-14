// ============================================================================
//   ОКО МАГА / eye/render.hpp — ВИД: тема «Гримуар v2» (схема памяти + выноски)
// ============================================================================
//   Берёт факты из eye/reflect.hpp и рисует их в консоли. Здесь вся эстетика:
//   ANSI-цвета, box-drawing, раскладка, обрезка длинного. Хочешь другой
//   внешний вид — меняешь только этот файл, модель не трогаешь.
//
//   Что нового против v1:
//     * панель центрируется по ширине терминала (EYE_CENTER=0 — прижать влево);
//     * СХЕМА ПАМЯТИ: секции «поля» + «карта» + «байты» слиты в одну
//       вертикальную схему — регион (vptr / поле / padding) = блок строк:
//       offset, цветной «кирпич», hex-байты, ascii; байт со смещением N
//       стоит в колонке N%8 — выравнивание видно глазами;
//     * боковые ВЫНОСКИ справа от рамки, как на блок-схемах (◄── и ◄┬─/├─/╰─);
//       в узком терминале переезжают внутрь рамки (компактный режим);
//     * std::string: SSO против кучи; на libstdc++ поле разбирается на
//       .ptr / .len / .buf; кучный буфер — отдельная панель-«спутник»;
//     * vtable — блок-диаграмма: объект → vptr → vtable → слот → код.
//
//   Как рамка сходится справа, несмотря на цвета и кириллицу: каждую строку
//   собираем через Line, отдельно считая ВИДИМУЮ ширину (ANSI-коды = 0 колонок,
//   один символ UTF-8 = 1 колонка). Переменные куски заранее обрезаем clip().
// ============================================================================
#pragma once
#include "reflect.hpp"

// --- Платформа: isatty + ANSI-консоль + ширина терминала ---------------------
#if defined(_WIN32)
#  define NOMINMAX             // чтобы windows.h не переопределил std::min/max
#  define WIN32_LEAN_AND_MEAN
#  include <io.h>             // _isatty, _fileno
#  include <windows.h>        // кодовая страница, ANSI (VT), ширина консоли
#else
#  include <sys/ioctl.h>      // TIOCGWINSZ — ширина терминала
#  include <unistd.h>         // isatty, fileno — POSIX
#endif

#include <algorithm>  // std::sort, std::min, std::max
#include <cctype>     // std::isprint
#include <cstddef>
#include <cstdio>     // std::snprintf
#include <cstdlib>    // std::getenv, std::atoi
#include <iostream>
#include <sstream>    // std::ostringstream (hexptr)
#include <string>
#include <vector>

namespace eye {

// ════════════════════════════════════════════════════════════════════════════
//  Палитра (M0). ANSI-коды; при выводе не в терминал отключаются сами.
//  EYE_COLOR=1 — форсировать цвета (redirect в файл, CI), EYE_COLOR=0 — убрать.
// ════════════════════════════════════════════════════════════════════════════
namespace clr {
inline bool enabled() {
    static const bool on = [] {
        if (const char* e = std::getenv("EYE_COLOR")) {
            if (*e == '0') return false;
            if (*e == '1') {
#if defined(_WIN32)
                if (_isatty(_fileno(stdout))) {
                    SetConsoleOutputCP(CP_UTF8);
                    const HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
                    DWORD mode = 0;
                    if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode))
                        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
                }
#endif
                return true;
            }
        }
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
inline const char* gold()   { return code("\033[38;5;178m"); }  // заголовки
inline const char* cyan()   { return code("\033[36m");       }  // поля (чёт)
inline const char* cyan2()  { return code("\033[96m");       }  // поля (нечет)
inline const char* green()  { return code("\033[32m");       }  // значения
inline const char* grey()   { return code("\033[38;5;245m"); }  // рамка, служебное
inline const char* dim()    { return code("\033[38;5;240m"); }  // совсем тускло
inline const char* violet() { return code("\033[35m");       }  // vptr / магия
inline const char* red()    { return code("\033[38;5;131m"); }  // padding
} // namespace clr

namespace detail {

// ════════════════════════════════════════════════════════════════════════════
//  Геометрия. Панель = «│ » + FRAME_W + « │» = PANEL_W колонок; правее — gutter
//  с выносками. Всё центрируется отступом margin.
// ════════════════════════════════════════════════════════════════════════════
inline constexpr std::size_t FRAME_W = 60;             // внутренняя ширина рамки
inline constexpr std::size_t PANEL_W = FRAME_W + 4;    // рамка целиком
inline constexpr std::size_t GUT_PRE = 4;              // «◄── » / « ├─ »
inline constexpr std::size_t GUT_TXT = 40;             // бюджет текста выноски
inline constexpr std::size_t FULL_W  = PANEL_W + GUT_PRE + GUT_TXT;   // 108

// Колонки схемы памяти (внутри FRAME_W):
//   off(6) ␣ кирпич(2) ␣ hex-сетка(23) ␣␣ ascii-сетка(8)
inline constexpr std::size_t MEM_HEX_COL   = 10;  // старт hex-сетки
inline constexpr std::size_t MEM_ASCII_COL = 35;  // старт ascii-сетки

struct Geo {
    std::size_t margin = 0;   // отступ слева (центрирование)
    bool gutter = true;       // выноски сбоку (false → внутрь рамки)
    bool full   = false;      // EYE_FULL=1 — не сворачивать длинные регионы
};
inline Geo& geo() { static Geo g; return g; }

// Ширина терминала: EYE_WIDTH → WinAPI/ioctl → 110 (redirect в файл).
inline std::size_t term_width() {
    if (const char* e = std::getenv("EYE_WIDTH"))
        if (const int v = std::atoi(e); v > 0) return static_cast<std::size_t>(v);
#if defined(_WIN32)
    CONSOLE_SCREEN_BUFFER_INFO bi;
    const HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(h, &bi))
        // Именно окно (srWindow), а не буфер: буфер бывает 9999 строк на 120.
        return static_cast<std::size_t>(bi.srWindow.Right - bi.srWindow.Left + 1);
#else
    winsize ws{};
    if (ioctl(fileno(stdout), TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return ws.ws_col;
#endif
    return 110;
}

// Пересчитать раскладку перед каждой панелью (окно могли растянуть).
inline void geo_refresh() {
    Geo g;
    const std::size_t w = term_width();
    g.gutter = w >= FULL_W + 2;
    g.full   = [] { const char* e = std::getenv("EYE_FULL");
                    return e && *e == '1'; }();
    const bool center = [] { const char* e = std::getenv("EYE_CENTER");
                             return !(e && *e == '0'); }();
    if (center) {
        const std::size_t need = g.gutter ? FULL_W : PANEL_W;
        // Потолок 24: в сверхшироком терминале панель у центра уже не ищут.
        g.margin = w > need ? std::min<std::size_t>((w - need) / 2, 24) : 0;
    }
    geo() = g;
}
inline std::string margin_str() { return std::string(geo().margin, ' '); }

// ════════════════════════════════════════════════════════════════════════════
//  Низкоуровневые примитивы текста
// ════════════════════════════════════════════════════════════════════════════
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
    // Дополнить пробелами до колонки x (внутри рамки).
    Line& to(std::size_t x) { if (w < x) sp(x - w); return *this; }
};

inline std::string dashes(std::size_t n) {
    std::string r; r.reserve(n * 3);
    for (std::size_t i = 0; i < n; ++i) r += "─";
    return r;
}

// Рамочная строка контента: │ <content, дополнено до FRAME_W> │ [выноска]
inline void put(const Line& ln, const Line* gut = nullptr) {
    const std::size_t pad = ln.w < FRAME_W ? FRAME_W - ln.w : 0;
    std::cout << margin_str()
              << clr::grey() << "│" << clr::reset() << ' ' << ln.s
              << std::string(pad, ' ') << ' '
              << clr::grey() << "│" << clr::reset();
    if (gut != nullptr) std::cout << gut->s;
    std::cout << '\n';
}
// Рамочная строка с простым серым текстом (для сообщений).
inline void put_text(const std::string& t) {
    Line l; l.col(clr::grey(), clip(t, FRAME_W));
    put(l);
}
inline void put_blank() { put(Line{}); }

// Верх рамки:      ╭─◈ Заголовок ─────╮
inline void frame_top(const std::string& title) {
    const std::string t = clip(title, FRAME_W - 3);
    const std::size_t fill = FRAME_W - vwidth(t) - 2;   // t обрезан → fill >= 1
    std::cout << margin_str()
              << clr::grey() << "╭─" << clr::violet() << "◈ " << clr::reset()
              << clr::gold() << t << clr::reset() << ' '
              << clr::grey() << dashes(fill) << "╮" << clr::reset() << '\n';
}
// Разделитель:     ├─ секция ─────────┤
inline void frame_sep(const std::string& label) {
    const std::string l = clip(label, FRAME_W - 2);
    const std::size_t fill = FRAME_W - vwidth(l) - 1;
    std::cout << margin_str()
              << clr::grey() << "├─ " << clr::reset()
              << clr::gold() << l << clr::reset() << ' '
              << clr::grey() << dashes(fill) << "┤" << clr::reset() << '\n';
}
// Низ рамки:       ╰──────────────────╯
inline void frame_bottom() {
    std::cout << margin_str()
              << clr::grey() << "╰" << dashes(FRAME_W + 2) << "╯"
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
    l2.col(clr::dim(), "●да ○нет");
    put(l2);
}

// ════════════════════════════════════════════════════════════════════════════
//  СХЕМА ПАМЯТИ — сердце Гримуара v2.
//  Регион (vptr / поле / под-часть строки / padding / скрытое) = блок строк.
// ════════════════════════════════════════════════════════════════════════════
struct Region {
    enum class R { field, padding, vptr, opaque };
    R what;
    std::size_t off, size;
    const FieldInfo* f;             // для field
    bool shade;                     // чередование █/▓ между соседними полями
    int strpart;                    // 1 = .ptr, 2 = .len, 3 = .buf (std::string)
    std::string why;                // причина padding

    Region(R w, std::size_t o, std::size_t s, const FieldInfo* fi = nullptr,
           bool sh = false, int sp = 0)
        : what(w), off(o), size(s), f(fi), shade(sh), strpart(sp) {}
};

// Роль → глиф кирпича и цвет.
inline const char* region_glyph(const Region& r) {
    switch (r.what) {
        case Region::R::padding: return "░";
        case Region::R::vptr:    return "▒";
        case Region::R::opaque:  return "▓";
        default:                 return r.shade ? "▓" : "█";
    }
}
inline const char* region_color(const Region& r) {
    switch (r.what) {
        case Region::R::padding: return clr::red();
        case Region::R::vptr:    return clr::violet();
        case Region::R::opaque:  return clr::grey();
        default:                 return r.shade ? clr::cyan2() : clr::cyan();
    }
}

// Разбить поля объекта на регионы. У полиморфных первые 8 байт — vptr.
// std::string на libstdc++ раскрываем в .ptr / .len / .buf: каждая часть
// получает СВОИ строки и СВОЮ выноску (иначе подписи съезжают с байтов).
inline std::vector<Region> build_regions(const std::vector<FieldInfo>& fields,
                                         std::size_t total, bool poly,
                                         bool opaque) {
    std::vector<Region> rs;
    std::size_t cursor = 0;

    if (poly && (fields.empty() || fields.front().offset >= sizeof(void*))) {
        rs.push_back({Region::R::vptr, 0, sizeof(void*)});
        cursor = sizeof(void*);
    }
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const FieldInfo& f = fields[i];
        if (f.offset > cursor && f.offset <= total) {   // дыра перед полем
            Region p{Region::R::padding, cursor, f.offset - cursor};
            p.why = clip(f.name, 12) + " требует адрес, кратный " +
                    std::to_string(f.align);
            rs.push_back(p);
        }
        const bool shade = i % 2 != 0;
        // Раскладка {ptr, len, buf} знакома, поле легло по сетке → разбираем.
        if (f.kind == FieldInfo::Kind::str && f.str_layout &&
            f.size == 32 && f.offset % 8 == 0) {
            rs.push_back({Region::R::field, f.offset,      8, &f, shade, 1});
            rs.push_back({Region::R::field, f.offset + 8,  8, &f, shade, 2});
            rs.push_back({Region::R::field, f.offset + 16, 16, &f, shade, 3});
        } else {
            rs.push_back({Region::R::field, f.offset, f.size, &f, shade});
        }
        if (f.offset + f.size > cursor) cursor = f.offset + f.size;
    }
    if (opaque && cursor < total)
        rs.push_back({Region::R::opaque, cursor, total - cursor});
    else if (cursor < total) {
        Region p{Region::R::padding, cursor, total - cursor};
        p.why = "добивка sizeof до кратного выравниванию";
        rs.push_back(p);
    }
    return rs;
}

// Строки региона: режем по АБСОЛЮТНОЙ 8-байтовой сетке (байт со смещением N
// печатается в колонке N%8 — выравнивание видно колонками). Длинные регионы
// сворачиваем: 2 строки + «⋯ ещё …» + последняя (EYE_FULL=1 отключает).
struct MRow {
    std::size_t at = 0;    // offset первого байта строки
    std::size_t n = 0;     // байт в строке (0 → строка-фолд)
    std::size_t skip = 0;  // сколько байт спрятано (для фолда)
};
inline std::vector<MRow> region_rows(std::size_t off, std::size_t size) {
    std::vector<MRow> rows;
    const std::size_t end = off + size;
    for (std::size_t line = off / 8 * 8; line < end; line += 8)
        rows.push_back({std::max(off, line), std::min(end, line + 8) -
                                             std::max(off, line), 0});
    if (!geo().full && rows.size() > 4) {
        std::size_t hidden = 0;
        for (std::size_t i = 2; i + 1 < rows.size(); ++i) hidden += rows[i].n;
        std::vector<MRow> cut(rows.begin(), rows.begin() + 2);
        cut.push_back({rows[2].at, 0, hidden});
        cut.push_back(rows.back());
        return cut;
    }
    return rows;
}

// Одна hex-строка схемы: off ␣ кирпич ␣ [сетка hex] ␣␣ [сетка ascii].
inline Line mem_row(const MRow& r, const Region& reg, const unsigned char* base) {
    const char* c = region_color(reg);
    Line ln;
    ln.col(clr::grey(), hex4(r.at)).sp();
    ln.col(c, std::string(region_glyph(reg)) + region_glyph(reg));
    if (r.n == 0) {   // строка-фолд
        ln.to(MEM_HEX_COL);
        ln.col(c, "⋯ ещё " + std::to_string(r.skip) + " Б ⋯");
        return ln;
    }
    // hex: байт со смещением o — в колонке MEM_HEX_COL + (o%8)*3.
    ln.to(MEM_HEX_COL + (r.at % 8) * 3);
    std::string hex;
    bool has_print = false;
    std::string ascii;
    for (std::size_t i = 0; i < r.n; ++i) {
        const unsigned char b = base[r.at + i];
        hex += hex2(b);
        if (i + 1 < r.n) hex += ' ';
        const bool pr = std::isprint(b) != 0;
        has_print = has_print || pr;
        ascii += pr ? static_cast<char>(b) : '.';
    }
    ln.col(c, hex);
    // ascii: той же сеткой; сплошные точки тушим — глаз ищет буквы (Solmyr!).
    ln.to(MEM_ASCII_COL + r.at % 8);
    ln.col(has_print ? c : clr::dim(), ascii);
    return ln;
}

// ---- Выноски: текст сбоку от рамки (или внутри — компактный режим) ---------

// «имя · тип · N Б = значение» в жёсткий бюджет: имя ≤ 16, размер всегда,
// первым режем тип, значение — сколько останется (минимум 6).
inline Line field_headline(const FieldInfo& f, std::size_t budget,
                           bool with_alt = true) {
    const std::string name = clip(f.name, 16);
    const std::string sz = std::to_string(f.size) + " Б";
    // «= [массив N байт]» дублирует размер — у массивов значение опускаем.
    const bool no_val = f.value.rfind("[массив", 0) == 0;
    const std::size_t fixed = vwidth(name) + 3 + 3 + vwidth(sz) + 3;  // « · »×2 + « = »
    const std::size_t val_keep =
        no_val ? 0 : std::min<std::size_t>(vwidth(f.value), 6);
    std::size_t type_w = budget > fixed + val_keep ? budget - fixed - val_keep : 6;
    type_w = std::min(type_w, vwidth(f.type));

    Line l;
    l.col(clr::green(), name).col(clr::grey(), " · ")
     .col(clr::cyan(), clip(f.type, type_w)).col(clr::grey(), " · ")
     .col(clr::grey(), sz);
    if (no_val) return l;
    l.col(clr::grey(), " = ");
    l.col(clr::green(), clip(f.value, budget > l.w ? budget - l.w : 0));
    if (with_alt && f.integral && !f.alt.empty() &&
        l.w + vwidth(f.alt) + 3 <= budget)
        l.col(clr::grey(), " (" + f.alt + ")");
    return l;
}

// Куда смотрит указатель: в никуда / внутрь этого объекта / наружу.
inline void pointer_notes(std::vector<Line>& out, const FieldInfo& f,
                          const unsigned char* base, std::size_t total,
                          std::size_t budget) {
    if (f.target == nullptr) {
        Line l; l.col(clr::grey(), "► nullptr — не указывает никуда");
        out.push_back(l);
        return;
    }
    const auto b = reinterpret_cast<std::uintptr_t>(base);
    const auto t = reinterpret_cast<std::uintptr_t>(f.target);
    if (t >= b && t < b + total) {
        Line l;
        l.col(clr::gold(), "► внутрь ЭТОГО объекта: база+" +
                               hex4(static_cast<std::size_t>(t - b)));
        out.push_back(l);
    } else {
        Line l; l.col(clr::grey(), "► наружу, за пределы объекта");
        out.push_back(l);
    }
    if (!f.pointee.empty()) {
        Line l;
        l.col(clr::grey(), "по адресу лежит: ")
         .col(clr::green(), clip(f.pointee, budget > 17 ? budget - 17 : 0));
        out.push_back(l);
    }
}

// Выноски региона (каждая строка ≤ budget колонок).
inline std::vector<Line> region_notes(const Region& r, const unsigned char* base,
                                      std::size_t total, bool standalone,
                                      std::size_t budget) {
    std::vector<Line> out;
    switch (r.what) {
        case Region::R::vptr: {
            Line l1; l1.col(clr::violet(), "vptr → vtable класса (секция ▼)");
            Line l2; l2.col(clr::grey(), "скрытое поле: его вставил virtual");
            out = {l1, l2};
            break;
        }
        case Region::R::padding: {
            Line l1;
            l1.col(clr::red(), "padding " + std::to_string(r.size) +
                                   " Б — дыра, внутри мусор");
            Line l2; l2.col(clr::grey(), clip(r.why, budget));
            out = {l1, l2};
            break;
        }
        case Region::R::opaque: {
            Line l1; l1.col(clr::grey(), "поля скрыты: private/конструкторы");
            Line l2; l2.col(clr::grey(), "добавь EYE_DESCRIBE — Око увидит");
            out = {l1, l2};
            break;
        }
        case Region::R::field: {
            const FieldInfo& f = *r.f;
            if (r.strpart == 1) {          // .ptr — куда смотрит строка
                Line l;
                l.col(clr::green(), clip(f.name, 10) + ".ptr")
                 .col(clr::grey(), " = ").plain(hexptr(f.target));
                out.push_back(l);
                Line l2;
                if (f.sso) {
                    const auto b = reinterpret_cast<std::uintptr_t>(base);
                    const auto t = reinterpret_cast<std::uintptr_t>(f.target);
                    l2.col(clr::gold(), "► это база+" +
                        hex4(static_cast<std::size_t>(t - b)) +
                        " — свой же буфер ▼");
                } else {
                    l2.col(clr::gold(), "► далеко от объекта: КУЧА (ниже ▼)");
                }
                out.push_back(l2);
            } else if (r.strpart == 2) {   // .len
                Line l;
                l.col(clr::green(), clip(f.name, 10) + ".len")
                 .col(clr::grey(), " = ")
                 .col(clr::green(), std::to_string(f.str_len))
                 .col(clr::grey(), " — длина строки");
                out.push_back(l);
            } else if (r.strpart == 3) {   // .buf / .cap
                if (f.sso) {
                    Line l;
                    l.col(clr::green(), clip(f.name, 10) + ".buf")
                     .col(clr::grey(), " = ")
                     .col(clr::green(), clip(f.value, budget > 20 ? budget - 20
                                                                  : 6));
                    out.push_back(l);
                    Line l2;
                    l2.col(clr::grey(), "символы ЗДЕСЬ (SSO, до 15 симв.)");
                    out.push_back(l2);
                } else {
                    Line l;
                    l.col(clr::green(), clip(f.name, 10) + ".cap")
                     .col(clr::grey(), " = ")
                     .col(clr::green(), std::to_string(f.str_cap))
                     .col(clr::grey(), " — вместимость");
                    out.push_back(l);
                    Line l2;
                    l2.col(clr::grey(), "SSO-буфер пуст: символы в куче");
                    out.push_back(l2);
                }
            } else {                        // обычное поле
                // Для одиночного значения hex уйдёт отдельной строкой-уроком.
                out.push_back(field_headline(f, budget, !standalone));
                if (f.kind == FieldInfo::Kind::pointer)
                    pointer_notes(out, f, base, total, budget);
                if (f.kind == FieldInfo::Kind::str && !f.str_layout) {
                    Line l2;
                    if (f.sso)
                        l2.col(clr::gold(), "SSO: буфер прямо в объекте");
                    else
                        l2.col(clr::gold(), "буфер в КУЧЕ — панель ниже ▼");
                    out.push_back(l2);
                    Line l3;
                    l3.col(clr::grey(), "длина " + std::to_string(f.str_len) +
                                            " · вместимость " +
                                            std::to_string(f.str_cap));
                    out.push_back(l3);
                }
                if (standalone && f.integral && !f.alt.empty()) {
                    Line l2;
                    l2.col(clr::grey(), "hex: ").col(clr::cyan(), f.alt);
                    out.push_back(l2);
                    Line l3;
                    l3.col(clr::grey(), "в дампе — задом наперёд: little-endian");
                    out.push_back(l3);
                }
            }
            break;
        }
    }
    return out;
}

// Соединитель выноски: одна строка — «◄── », блок — скобка ◄┬─ / ├─ / ╰─.
inline Line gut_connector(std::size_t i, std::size_t k, bool has_text) {
    Line g;
    const char* pre = k == 1        ? "◄── "
                      : i == 0      ? "◄┬─ "
                      : i + 1 == k  ? (has_text ? " ╰─ " : " ╰──")
                                    : (has_text ? " ├─ " : " │");
    g.col(clr::grey(), pre);
    return g;
}

// ---- Сама секция «память» ---------------------------------------------------
inline void render_memory(std::vector<FieldInfo> fields, std::size_t total,
                          std::size_t talign, bool poly, const void* addr,
                          bool opaque, bool standalone) {
    const auto* base = static_cast<const unsigned char*>(addr);
    // Реестр мог перечислить поля не по порядку — сортируем по offset, иначе
    // регионы и padding посчитаются неверно.
    std::sort(fields.begin(), fields.end(),
              [](const FieldInfo& a, const FieldInfo& b) {
                  return a.offset < b.offset;
              });
    const auto regions = build_regions(fields, total, poly, opaque);
    const bool gutter = geo().gutter;

    // Шапка-линейка: подписи колонок стоят РОВНО над своими данными.
    Line h;
    h.col(clr::grey(), "off").to(MEM_HEX_COL);
    for (int i = 0; i < 8; ++i)
        h.col(clr::dim(), "+" + std::to_string(i)).sp(i == 7 ? 0 : 1);
    h.to(MEM_ASCII_COL).col(clr::dim(), "ascii");
    put(h);

    for (const Region& r : regions) {
        const auto rows  = region_rows(r.off, r.size);
        const auto notes = region_notes(r, base, total, standalone,
                                        gutter ? GUT_TXT : FRAME_W - 8);
        if (gutter) {
            // Строк — сколько нужно и байтам, и выноскам; недостающие рамочные
            // строки — пустые. Выноска i стоит СТРОГО напротив своей строки.
            const std::size_t k = std::max(rows.size(), notes.size());
            for (std::size_t i = 0; i < k; ++i) {
                const Line frame =
                    i < rows.size() ? mem_row(rows[i], r, base) : Line{};
                if (i < notes.size()) {
                    Line g = gut_connector(i, k, true);
                    g.s += notes[i].s;
                    g.w += notes[i].w;
                    put(frame, &g);
                } else if (k > 1) {           // скобка тянется на весь регион
                    const Line g = gut_connector(i, k, false);
                    put(frame, &g);
                } else {
                    put(frame);
                }
            }
        } else {
            // Компактный режим: сперва байты, потом выноски внутри рамки.
            for (const MRow& mr : rows) put(mem_row(mr, r, base));
            for (std::size_t i = 0; i < notes.size(); ++i) {
                Line l;
                l.to(3).col(clr::grey(), i == 0 ? "└► " : "   ");
                l.s += notes[i].s; l.w += notes[i].w;
                put(l);
            }
        }
    }

    // --- легенда (только реально встреченные роли) + сводка -------------------
    bool has_pad = false, has_vptr = false, has_field = false, has_op = false;
    std::string strip(total, '.');
    for (const Region& r : regions)
        for (std::size_t b = r.off; b < r.off + r.size && b < total; ++b)
            switch (r.what) {
                case Region::R::field:   strip[b] = 'f'; has_field = true; break;
                case Region::R::padding: strip[b] = 'p'; has_pad = true;   break;
                case Region::R::vptr:    strip[b] = 'v'; has_vptr = true;  break;
                case Region::R::opaque:  strip[b] = 'o'; has_op = true;    break;
            }
    std::size_t nf = 0, np = 0, nv = 0;
    for (char c : strip) { nf += c == 'f' || c == 'o'; np += c == 'p'; nv += c == 'v'; }
    np += static_cast<std::size_t>(
        std::count(strip.begin(), strip.end(), '.'));   // не покрыто = дыра

    // Одиночное значение без дыр — легенда и сводка не скажут ничего нового.
    const bool trivial = standalone && np == 0 && !has_vptr;
    if (!trivial) {
        put_blank();
        Line leg;
        if (has_field) leg.col(clr::cyan(), "█▓").col(clr::grey(), " поля (сосед — другой тон)  ");
        if (has_pad)   leg.col(clr::red(), "░").col(clr::grey(), " padding  ");
        if (has_vptr)  leg.col(clr::violet(), "▒").col(clr::grey(), " vptr  ");
        if (has_op)    leg.col(clr::grey(), "▓ скрытое");
        put(leg);

        Line sum;
        sum.col(clr::grey(), "занято ").col(clr::green(), std::to_string(nf + nv))
           .col(clr::grey(), " из " + std::to_string(total) + " Б");
        if (np > 0) {
            const std::size_t pct = total ? np * 100 / total : 0;
            sum.col(clr::grey(), "  ·  padding ")
               .col(clr::red(), std::to_string(np) + " Б (" + std::to_string(pct) + "%)");
        }
        put(sum);
    }

    // --- урок little-endian на первом же целом поле объекта -------------------
    if (!standalone) {
        for (const FieldInfo& f : fields)
            if (f.integral && !f.alt.empty() && f.size <= 4 &&
                f.offset + f.size <= total) {
                std::string bytes;
                for (std::size_t i = 0; i < f.size; ++i)
                    bytes += hex2(base[f.offset + i]) + (i + 1 < f.size ? " " : "");
                Line le;
                le.col(clr::grey(), "байты наоборот (LE): ")
                  .col(clr::cyan(), bytes)
                  .col(clr::grey(), " → ")
                  .col(clr::cyan(), f.alt);
                if (le.w + vwidth(f.value) + 3 <= FRAME_W)
                    le.col(clr::grey(), " = ").col(clr::green(), f.value);
                if (le.w <= FRAME_W) put(le);
                break;
            }
    }

    // --- совет по перестановке (привет, pahole) --------------------------------
    if (np > 0 && !poly && !opaque && fields.size() > 1) {
        auto sorted = fields;
        std::sort(sorted.begin(), sorted.end(),
                  [](const FieldInfo& a, const FieldInfo& b) {
                      return a.align != b.align ? a.align > b.align
                                                : a.size > b.size;
                  });
        std::size_t cur = 0, maxal = talign;
        for (const FieldInfo& f : sorted) {
            if (f.align) cur = (cur + f.align - 1) / f.align * f.align;
            cur += f.size;
            maxal = std::max(maxal, f.align);
        }
        const std::size_t news = (cur + maxal - 1) / maxal * maxal;
        if (news < total) {
            Line a;
            a.col(clr::gold(), "◇ переставь поля по убыванию align: ")
             .col(clr::green(), std::to_string(news) + " Б")
             .col(clr::grey(), " вместо " + std::to_string(total));
            put(a);
            std::string order;
            for (std::size_t i = 0; i < sorted.size(); ++i)
                order += (i ? " · " : "") + sorted[i].name;
            put_text("  порядок: " + order);
        }
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  Секция vtable (M3) — блок-диаграмма: объект → vptr → vtable → слот → код
// ════════════════════════════════════════════════════════════════════════════
inline void render_vtable(const VtableInfo& vi, std::size_t obj_size) {
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

// ════════════════════════════════════════════════════════════════════════════
//  Панели-спутники: буферы std::string, живущие в куче. Печатаются ПОСЛЕ
//  главной панели — куча и правда живёт «где-то ещё», не внутри объекта.
// ════════════════════════════════════════════════════════════════════════════
inline void render_satellites(const std::vector<FieldInfo>& fields,
                              const void* addr) {
    constexpr std::size_t SAT_W = 44;   // внутренняя ширина мини-рамки
    const std::string ind = margin_str() + "   ";

    for (const FieldInfo& f : fields) {
        if (f.kind != FieldInfo::Kind::str || f.sso || f.heap_bytes.empty())
            continue;
        // Стрелка-связка от главной панели к спутнику.
        std::cout << ind << clr::grey() << "│" << clr::reset() << '\n';
        std::cout << ind << clr::grey() << "╰─► " << clr::reset()
                  << clr::green() << f.name << ".ptr" << clr::reset()
                  << clr::grey() << " ведёт сюда — в КУЧУ (далеко от объекта @ "
                  << clr::reset() << hexptr(addr) << clr::grey() << "):"
                  << clr::reset() << '\n';

        const std::string pre = ind + "    ";
        auto sat_line = [&](const Line& ln) {
            const std::size_t pad = ln.w < SAT_W ? SAT_W - ln.w : 0;
            std::cout << pre << clr::grey() << "│" << clr::reset() << ' '
                      << ln.s << std::string(pad, ' ') << ' '
                      << clr::grey() << "│" << clr::reset() << '\n';
        };
        // Верх: ╭─◈ куча @ 0x… ─╮
        {
            const std::string t =
                clip("куча @ " + hexptr(f.target) + " · буфер «" + f.name + "»",
                     SAT_W - 3);
            std::cout << pre << clr::grey() << "╭─" << clr::violet() << "◈ "
                      << clr::reset() << clr::gold() << t << clr::reset() << ' '
                      << clr::grey() << dashes(SAT_W - vwidth(t) - 2) << "╮"
                      << clr::reset() << '\n';
        }
        {
            Line info;
            info.col(clr::grey(), "длина ")
                .col(clr::green(), std::to_string(f.str_len))
                .col(clr::grey(), " · вместимость ")
                .col(clr::green(), std::to_string(f.str_cap))
                .col(clr::grey(), " · хвост '\\0'");
            sat_line(info);
        }
        for (std::size_t rowb = 0; rowb < f.heap_bytes.size(); rowb += 8) {
            Line ln;
            std::string hex, ascii;
            bool has_print = false;
            for (std::size_t i = rowb;
                 i < rowb + 8 && i < f.heap_bytes.size(); ++i) {
                const unsigned char b = f.heap_bytes[i];
                hex += hex2(b);
                hex += ' ';
                const bool pr = std::isprint(b) != 0;
                has_print = has_print || pr;
                ascii += pr ? static_cast<char>(b) : '.';
            }
            ln.col(clr::green(), ljust(hex, 24)).sp()
              .col(has_print ? clr::green() : clr::dim(), ascii);
            sat_line(ln);
        }
        if (f.str_len + 1 > f.heap_bytes.size()) {
            Line more;
            more.col(clr::grey(), "⋯ ещё " +
                     std::to_string(f.str_len + 1 - f.heap_bytes.size()) +
                     " Б ⋯");
            sat_line(more);
        }
        std::cout << pre << clr::grey() << "╰" << dashes(SAT_W + 2) << "╯"
                  << clr::reset() << '\n';
    }
}

} // namespace detail
} // namespace eye
