// ============================================================================
//   ОКО МАГА / eye/render.hpp — ВИД: тема «Гримуар v3» (лист героя + две зоны)
// ============================================================================
//   Берёт факты из eye/reflect.hpp и рисует их в консоли как «лист героя» в духе
//   компьютерной игры конца 80-х. Здесь вся эстетика: ANSI-цвета, box-drawing,
//   раскладка, обрезка длинного. Другой вид — меняешь только этот файл.
//
//   Ключевое в v3:
//     * ДВОЙНАЯ рамка ╔═╗ с картушем-заголовком (диалог DOS-игры); секции
//       внутри — одинарные ╟─╢ («двойное = окно, одинарное = строки»);
//     * ПАСПОРТ: каждая черта сама говорит вердикт словом (да/нет) + микро-урок;
//     * ШКАЛА-DEFRAG: весь объект одной полосой █▒░ — доли данных/vptr/прах;
//     * СХЕМА ПАМЯТИ в ДВЕ ЗОНЫ (широкий режим): слева карта байт ║ справа
//       кодекс-выноски; регион = карточка, разделённая ╫; байт со смещением N
//       стоит в колонке N%8 — выравнивание видно глазами. Узко → одна колонка,
//       выноски стопкой под байтами (compact, см. geo_refresh);
//     * КЛЮЧ КАРТЫ — отдельная секция, по знаку на строку (не обрезается);
//     * игровые образы рядом с ТЕРМИНОМ (vptr→портал, padding→прах, куча→
//       сокровищница) — вывод остаётся греппаемым;
//     * std::string: SSO против кучи; кучный буфер — панель-«спутник».
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

namespace detail {
// getenv помечен MSVC как deprecated. Возвращаем владеющую строку: на Windows
// используем _dupenv_s, на POSIX сразу копируем значение из окружения.
inline std::string env_value(const char* name) {
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) return {};
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string(value);
#endif
}
} // namespace detail

// ════════════════════════════════════════════════════════════════════════════
//  Палитра (M0). ANSI-коды; при выводе не в терминал отключаются сами.
//  EYE_COLOR=1 — форсировать цвета (redirect в файл, CI), EYE_COLOR=0 — убрать.
// ════════════════════════════════════════════════════════════════════════════
namespace clr {
inline bool enabled() {
    static const bool on = [] {
        const std::string env = detail::env_value("EYE_COLOR");
        if (!env.empty()) {
            if (env.front() == '0') return false;
            if (env.front() == '1') {
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
//  Геометрия. Панель = «│ » + frame_w + « │». Широкий режим «две зоны»: рамка
//  вмещает карту байт (map_w) ║ кодекс-выноски (codex_w) — см. geo_refresh.
//  В узком окне — одна колонка, выноски стопкой под байтами (compact).
// ════════════════════════════════════════════════════════════════════════════
inline constexpr std::size_t MIN_FRAME_W  = 60;
inline constexpr std::size_t PREF_FRAME_W = 68;

// Колонки схемы памяти (внутри минимальной рамки):
//   off(6) ␣ кирпич(2) ␣ hex-сетка(23) ␣␣ ascii-сетка(8)
inline constexpr std::size_t MEM_HEX_COL   = 10;  // старт hex-сетки
inline constexpr std::size_t MEM_ASCII_COL = 35;  // старт ascii-сетки

// Двухзонная раскладка (широкий режим): в ОДНОЙ рамке слева карта байт
// (map_w), затем спайн ║, затем кодекс-выноски (codex_w). MAP_W охватывает
// всю байтовую сетку (ascii кончается к 43-й колонке).
inline constexpr std::size_t MAP_W      = 44;   // левая зона: карта байт
inline constexpr std::size_t CODEX_MIN  = 38;   // правая зона: минимум
inline constexpr std::size_t CODEX_PREF = 54;   // правая зона: желаемое
// Цель авто-расширения Windows-консоли и разумная ширина для redirect: столько,
// чтобы уместились обе зоны (2+MAP_W+1+CODEX_PREF+2 = 103) с запасом на поля.
inline constexpr std::size_t DEFAULT_TERM_W = 126;

struct Geo {
    std::size_t margin  = 0;              // отступ слева (центрирование)
    std::size_t frame_w = PREF_FRAME_W;   // внутренняя ширина рамки
    std::size_t map_w   = MAP_W;          // ширина левой зоны (two_zone)
    std::size_t codex_w = CODEX_MIN;      // ширина правой зоны (two_zone)
    bool two_zone = false;                // широкий режим: карта ║ кодекс
    bool full     = false;                // EYE_FULL=1 — не сворачивать регионы
};
inline Geo& geo() { static Geo g; return g; }
inline std::size_t frame_width() { return geo().frame_w; }

#if defined(_WIN32)
// Обычный conhost умеет менять размер программно; Windows Terminal/ConPTY
// может отказать — это нормально, тогда раскладка просто подстроится под него.
inline void widen_console_once() {
    static const bool done = [] {
        const std::string resize = env_value("EYE_RESIZE");
        if (!resize.empty() && resize.front() == '0') return true;
        if (!_isatty(_fileno(stdout))) return true;

        const HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO bi{};
        if (h == INVALID_HANDLE_VALUE || !GetConsoleScreenBufferInfo(h, &bi))
            return true;

        const SHORT current =
            static_cast<SHORT>(bi.srWindow.Right - bi.srWindow.Left + 1);
        const COORD largest = GetLargestConsoleWindowSize(h);
        const SHORT desired = static_cast<SHORT>(
            std::min<std::size_t>(DEFAULT_TERM_W,
                                  largest.X > 0 ? largest.X : current));
        if (desired <= current) return true;

        const SHORT required = static_cast<SHORT>(bi.srWindow.Left + desired);
        if (bi.dwSize.X < required) {
            COORD size = bi.dwSize;
            size.X = required;
            if (!SetConsoleScreenBufferSize(h, size)) return true;
        }
        SMALL_RECT window = bi.srWindow;
        window.Right = static_cast<SHORT>(window.Left + desired - 1);
        SetConsoleWindowInfo(h, TRUE, &window); // отказ не критичен
        return true;
    }();
    (void)done;
}
#endif

// Ширина терминала: EYE_WIDTH → WinAPI/ioctl → разумный размер для redirect.
inline std::size_t term_width() {
    const std::string width = env_value("EYE_WIDTH");
    if (!width.empty())
        if (const int v = std::atoi(width.c_str()); v > 0)
            return static_cast<std::size_t>(v);
#if defined(_WIN32)
    widen_console_once();
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
    return DEFAULT_TERM_W;
}

// Пересчитать раскладку перед каждой панелью (окно могли растянуть).
inline void geo_refresh() {
    Geo g;
    const std::size_t w = term_width();

    // Широкий режим «две зоны»: карта байт ║ кодекс в ОДНОЙ рамке. Включается,
    // когда влезают обе зоны с рамкой (2 + MAP_W + 1 + CODEX_MIN + 2).
    const std::size_t two_zone_min = 2 + MAP_W + 1 + CODEX_MIN + 2;
    if (w >= two_zone_min) {
        g.two_zone = true;
        g.map_w = MAP_W;
        const std::size_t for_codex = w - 4 - MAP_W - 1;  // всё, что осталось
        g.codex_w = std::clamp(for_codex, CODEX_MIN, CODEX_PREF);
        g.frame_w = g.map_w + 1 + g.codex_w;
    } else {
        // Узкий режим: одна колонка, выноски стопкой под байтами (compact).
        g.two_zone = false;
        const std::size_t available = w > 4 ? w - 4 : MIN_FRAME_W;
        g.frame_w = std::clamp(available, MIN_FRAME_W, PREF_FRAME_W);
    }
    g.full = [] {
        const std::string value = env_value("EYE_FULL");
        return !value.empty() && value.front() == '1';
    }();
    const bool center = [] {
        const std::string value = env_value("EYE_CENTER");
        return value.empty() || value.front() != '0';
    }();
    if (center) {
        const std::size_t need = g.frame_w + 4;
        // По-настоящему по центру — БЕЗ потолка: развернул окно на весь экран →
        // панель уезжает в середину (пересчёт при каждом inspect по ширине окна).
        // EYE_CENTER=0 отключает центрирование (прижать влево, для отчётов).
        g.margin = w > need ? (w - need) / 2 : 0;
    }
    geo() = g;
}
inline std::string margin_str() { return std::string(geo().margin, ' '); }

// ════════════════════════════════════════════════════════════════════════════
//  Низкоуровневые примитивы текста
// ════════════════════════════════════════════════════════════════════════════
// Управляющим байтам не место внутри рамки: '\n' ломает геометрию, ESC может
// внедрить ANSI-код из пользовательской подписи. UTF-8-байты >= 0x80 сохраняем.
inline std::string clean_text(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (const unsigned char c : s)
        out += (c < 0x20 || c == 0x7f) ? ' ' : static_cast<char>(c);
    return out;
}

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
    const std::string safe = clean_text(s);
    if (vwidth(safe) <= maxcp) return safe;
    std::string out;
    std::size_t cps = 0;
    for (std::size_t i = 0; i < safe.size() && cps + 1 < maxcp; ) {
        const unsigned char c = safe[i];
        const std::size_t len =
            c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : 4;
        out.append(safe, i, len);
        i += len;
        ++cps;
    }
    return out + "…";
}

// То же для уже собранной строки с ANSI-кодами. Коды CSI не занимают колонок;
// если контент неожиданно длиннее бюджета, правая рамка всё равно не съедет.
inline std::string clip_ansi(const std::string& s, std::size_t maxcp) {
    if (maxcp == 0) return "";
    std::string out;
    std::size_t cps = 0;
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] == '\033' && i + 1 < s.size() && s[i + 1] == '[') {
            const std::size_t end = s.find('m', i + 2);
            if (end != std::string::npos) {
                out.append(s, i, end - i + 1);
                i = end + 1;
                continue;
            }
        }
        if (cps + 1 >= maxcp) {
            out += clr::reset();
            out += "…";
            return out;
        }
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const std::size_t wanted =
            c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : 4;
        const std::size_t len = std::min(wanted, s.size() - i);
        out.append(s, i, len);
        i += len;
        ++cps;
    }
    return out;
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
    Line& plain(const std::string& t) {
        const std::string safe = clean_text(t);
        s += safe; w += vwidth(safe); return *this;
    }
    Line& col(const char* c, const std::string& t) {
        const std::string safe = clean_text(t);
        s += c; s += safe; s += clr::reset(); w += vwidth(safe); return *this;
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

// Рамочная строка контента: ║ <content, дополнено до frame_width()> ║
// Внешняя рамка объекта — ДВОЙНАЯ (║ ═ ╔ ╗ ╚ ╝), как диалог DOS-игры; секции
// внутри — одинарные (╟─ ─╢), поэтому «двойное = окно, одинарное = строки».
inline void put(const Line& ln) {
    const std::size_t fw = frame_width();
    const std::size_t body_w = std::min(ln.w, fw);
    const std::size_t pad = fw - body_w;
    std::cout << margin_str()
              << clr::grey() << "║" << clr::reset() << ' '
              << (ln.w > fw ? clip_ansi(ln.s, fw) : ln.s)
              << std::string(pad, ' ') << ' '
              << clr::grey() << "║" << clr::reset() << '\n';
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
    std::cout << margin_str() << clr::grey()
              << "╟" << dashes(mw + 1) << j << dashes(cw + 1) << "╢"
              << clr::reset() << '\n';
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
    std::cout << margin_str()
              << clr::grey() << "╔═" << clr::violet() << "◈╡ " << clr::reset()
              << clr::gold() << t << clr::reset()
              << clr::violet() << " ╞" << clr::grey() << dashes(fill)
              << clr::violet() << "═◈" << clr::grey() << "═╗"
              << clr::reset() << '\n';
}
// Разделитель секции — одинарный в двойную рамку:   ╟─ секция ─────────╢
inline void frame_sep(const std::string& label) {
    const std::size_t fw = frame_width();
    const std::string l = clip(label, fw - 2);
    const std::size_t fill = fw - vwidth(l) - 1;
    std::cout << margin_str()
              << clr::grey() << "╟─ " << clr::reset()
              << clr::gold() << l << clr::reset() << ' '
              << clr::grey() << dashes(fill) << "╢" << clr::reset() << '\n';
}
// Низ рамки — двойная:   ╚══════════════════╝
inline void frame_bottom() {
    std::cout << margin_str()
              << clr::grey() << "╚" << dashes(frame_width() + 2) << "╝"
              << clr::reset() << '\n';
}

// ════════════════════════════════════════════════════════════════════════════
//  Паспорт (M0): размер/выравнивание + трейты-чипы
// ════════════════════════════════════════════════════════════════════════════
inline void render_passport(const Passport& p) {
    Line l1;
    l1.col(clr::grey(), "размер ")
      .col(clr::cyan(), std::to_string(p.size)).col(clr::grey(), " Б")
      .col(clr::grey(), "  ·  выравнивание ")
      .col(clr::cyan(), std::to_string(p.align)).col(clr::grey(), " Б")
      .col(clr::dim(), "   (Б — байт)");
    put(l1);

    // Черты типа: КАЖДАЯ строка сама говорит ответ словом (да/нет) и короткий
    // урок — зачем черта нужна. Отдельная легенда-«расшифровка» не требуется
    // (прежнее «●да ○нет» читалось как бессмысленный четвёртый чип).
    auto trait = [](bool v, const char* name, const char* why_yes,
                    const char* why_no) {
        Line l;
        l.col(v ? clr::green() : clr::dim(), v ? "◆ " : "◇ ")
         .col(v ? clr::green() : clr::grey(), name);
        const std::size_t lead = 25;                 // выровнять вердикт столбиком
        if (l.w < lead) l.col(clr::dim(), std::string(lead - l.w, '.'));
        l.col(clr::grey(), " ")
         .col(v ? clr::green() : clr::grey(), v ? "да" : "нет")
         .col(clr::grey(), " · ")
         .col(clr::dim(), clip(v ? why_yes : why_no,
                               frame_width() > l.w ? frame_width() - l.w : 0));
        put(l);
    };
    put_text("черты типа:");
    trait(p.polymorphic, "полиморфный", "есть vptr → таблица virtual (портал ▼)",
          "vtable не нужна");
    trait(p.aggregate, "агрегат", "поля разбираются автоматикой",
          "есть конструктор / private-поля");
    trait(p.trivially_copyable, "тривиально-копируемый", "можно копировать memcpy",
          "копировать memcpy нельзя");
}

// ════════════════════════════════════════════════════════════════════════════
//  СХЕМА ПАМЯТИ — сердце Гримуара v2.
//  Регион (vptr / поле / под-часть строки / padding / скрытое) = блок строк.
// ════════════════════════════════════════════════════════════════════════════
struct Region {
    enum class R { field, padding, vptr, opaque, vbase };
    R what;
    std::size_t off, size;
    const FieldInfo* f;             // для field
    bool shade;                     // чередование █/▓ между соседними полями
    int strpart;                    // 1 = .ptr, 2 = .len, 3 = .buf (std::string)
    std::size_t field_no;           // стабильный номер поля в порядке памяти
    std::string why;                // причина padding

    Region(R w, std::size_t o, std::size_t s, const FieldInfo* fi = nullptr,
           bool sh = false, int sp = 0, std::size_t no = 0)
        : what(w), off(o), size(s), f(fi), shade(sh), strpart(sp),
          field_no(no) {}
};

inline std::string field_mark(std::size_t no) {
    return no == 0 ? "" : "#" + std::to_string(no);
}

inline bool has_automatic_name(const FieldInfo& f) {
    return f.name.size() > 1 && f.name.front() == '#' &&
           std::all_of(f.name.begin() + 1, f.name.end(),
                       [](unsigned char c) { return std::isdigit(c) != 0; });
}

// В авторазборе имя уже выглядит как #0, #1, ... — второй номер только мешал
// бы. Настоящие имена получают #1, #2, ... и совпадают с панелью-спутником.
inline std::string field_mark(const FieldInfo& f, std::size_t no) {
    return has_automatic_name(f) || f.inferred ? "" : field_mark(no);
}

inline void add_field_mark(Line& l, const FieldInfo& f, std::size_t no) {
    const std::string mark = field_mark(f, no);
    if (!mark.empty()) l.col(clr::gold(), mark).sp();
}

// Диапазон включительный: так без мысленного вычитания видно, какой именно
// последний байт охватывает скобка справа.
inline std::string byte_range(std::size_t off, std::size_t size) {
    const std::size_t last = size == 0 ? off : off + size - 1;
    return "+" + hex4(off) + "…+" + hex4(last);
}

// Роль → глиф кирпича и цвет.
inline const char* region_glyph(const Region& r) {
    switch (r.what) {
        case Region::R::padding: return "░";
        case Region::R::vptr:    return "▒";
        case Region::R::vbase:   return "▒";
        case Region::R::opaque:  return "▓";
        // Поле — ВСЕГДА █. Соседние поля различаются только ОТТЕНКОМ цвета
        // (см. region_color), а не глифом: так ▓ остаётся однозначным «туманом»
        // (opaque), и глаз не ищет несуществующий смысл в █ против ▓.
        default:                 return "█";
    }
}
inline const char* region_color(const Region& r) {
    switch (r.what) {
        case Region::R::padding: return clr::red();
        case Region::R::vptr:    return clr::violet();
        case Region::R::vbase:   return clr::violet();
        case Region::R::opaque:  return clr::grey();
        default:                 return r.shade ? clr::cyan2() : clr::cyan();
    }
}

// Разбить поля объекта на регионы. Каждый vptr-сайт (у множественного
// наследования их несколько) занимает свои 8 байт. std::string на libstdc++
// раскрываем в .ptr / .len / .buf: каждая часть получает СВОИ строки и СВОЮ
// выноску (иначе подписи съезжают с байтов). fields ожидаются отсортированными
// по offset — тогда чередование тонов █/▓ идёт в порядке памяти.
inline std::vector<Region> build_regions(const std::vector<FieldInfo>& fields,
                                         std::size_t total,
                                         const std::vector<std::size_t>& vptr_offsets,
                                         const std::vector<std::size_t>& vbase_offsets,
                                         bool opaque,
                                         const std::string& opaque_why = "",
                                         const std::vector<OpaqueSpan>& opaque_spans = {}) {
    // Дыра [start, start+size): весь объект непрозрачен (vector) → opaque;
    // иначе если старт попал в под-объект неразобранной базы → opaque с её
    // именем (закрашиваем только НЕпокрытые байты — сами регионы уже размещены);
    // иначе честный padding с причиной от следующего региона.
    auto classify_gap = [&](std::size_t start, std::size_t size,
                            const Region* next) -> Region {
        if (opaque) { Region p{Region::R::opaque, start, size}; p.why = opaque_why; return p; }
        // Самый ТЕСНЫЙ диапазон: больший offset (глубже), при равном — меньший
        // размер (под-объект базы точнее, чем «своё хранилище» всего объекта).
        const OpaqueSpan* owner = nullptr;
        for (const OpaqueSpan& s : opaque_spans)
            if (start >= s.offset && start < s.offset + s.size)
                if (owner == nullptr || s.offset > owner->offset ||
                    (s.offset == owner->offset && s.size < owner->size))
                    owner = &s;
        if (owner != nullptr) {
            Region p{Region::R::opaque, start, size};
            p.why = owner->self
                ? "свои поля не описаны «" + clip(owner->name, 16) +
                      "» — нужен EYE_DESCRIBE"
                : "непрозрачная база «" + clip(owner->name, 20) +
                      "» — нужен EYE_DESCRIBE";
            return p;
        }
        Region p{Region::R::padding, start, size};
        if (next == nullptr)
            p.why = "добивка sizeof до кратного выравниванию";
        else if (next->what == Region::R::vptr)
            p.why = "выравнивание под vptr под-объекта";
        else if (next->what == Region::R::vbase)
            p.why = "выравнивание под указатель virtual-базы";
        else if (next->f != nullptr)
            p.why = clip(next->f->name, 12) + " требует адрес, кратный " +
                    std::to_string(next->f->align);
        return p;
    };
    // 1) Все «занятые» регионы: vptr-сайты + указатели vbase + поля (сплит строк).
    std::vector<Region> placed;
    for (std::size_t off : vptr_offsets)
        placed.push_back({Region::R::vptr, off, sizeof(void*)});
    for (std::size_t off : vbase_offsets)
        placed.push_back({Region::R::vbase, off, sizeof(void*)});
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const FieldInfo& f = fields[i];
        const bool shade = i % 2 != 0;
        if (f.kind == FieldInfo::Kind::str && f.str_layout &&
            f.size == 32 && f.offset % 8 == 0) {
            placed.push_back({Region::R::field, f.offset,      8, &f, shade, 1, i + 1});
            placed.push_back({Region::R::field, f.offset + 8,  8, &f, shade, 2, i + 1});
            placed.push_back({Region::R::field, f.offset + 16, 16, &f, shade, 3, i + 1});
        } else {
            placed.push_back({Region::R::field, f.offset, f.size, &f, shade, 0, i + 1});
        }
    }
    // 2) По возрастанию offset; при равных — vptr раньше поля (stable хранит
    //    порядок частей строки и уже отсортированных полей).
    std::stable_sort(placed.begin(), placed.end(),
                     [](const Region& a, const Region& b) { return a.off < b.off; });

    // 3) Прогон: между занятыми регионами вставляем padding / скрытое.
    std::vector<Region> rs;
    std::size_t cursor = 0;
    for (const Region& r : placed) {
        if (r.off > cursor && r.off <= total)        // дыра перед регионом
            rs.push_back(classify_gap(cursor, r.off - cursor, &r));
        rs.push_back(r);
        if (r.off + r.size > cursor) cursor = r.off + r.size;
    }
    if (cursor < total)                              // хвостовая дыра
        rs.push_back(classify_gap(cursor, total - cursor, nullptr));
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

// «имя · тип · N Б = значение» в жёсткий бюджет. Длинное имя можно вынести
// отдельной строкой (см. region_notes), поэтому здесь имя занимает до половины.
inline Line field_headline(const FieldInfo& f, std::size_t budget,
                           bool with_alt = true, bool with_name = true,
                           std::size_t field_no = 0) {
    const std::size_t name_cap = std::max<std::size_t>(1, budget / 2);
    const std::string name = with_name ? clip(f.name, name_cap) : "";
    const std::string sz = std::to_string(f.size) + " Б";
    // «= [массив N байт]» дублирует размер — у массивов значение опускаем.
    const bool no_val = f.value.rfind("[массив", 0) == 0;

    Line l;
    if (with_name) {
        if (field_no != 0) add_field_mark(l, f, field_no);
        l.col(clr::green(), name).col(clr::grey(), " · ");
    }

    const std::size_t val_keep =
        no_val ? 0 : std::min<std::size_t>(vwidth(f.value), 6);
    const std::size_t suffix = 3 + vwidth(sz) + (no_val ? 0 : 3 + val_keep);
    const std::size_t type_room =
        budget > l.w + suffix ? budget - l.w - suffix : 1;
    l.col(clr::cyan(), clip(f.type, type_room)).col(clr::grey(), " · ")
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
        Line l; l.col(clr::grey(), "× nullptr — связь обрывается");
        out.push_back(l);
        return;
    }
    const auto b = reinterpret_cast<std::uintptr_t>(base);
    const auto t = reinterpret_cast<std::uintptr_t>(f.target);
    const bool inside = t >= b && t < b + total;
    if (inside) {
        Line l;
        l.col(clr::gold(), "↩ внутрь объекта: начало +" +
                               hex4(static_cast<std::size_t>(t - b)));
        out.push_back(l);
    } else {
        Line l;
        l.col(clr::gold(), "► внешняя память @ ").plain(hexptr(f.target));
        out.push_back(l);
    }
    if (!f.pointee.empty()) {
        Line l;
        l.col(clr::grey(), "по адресу лежит: ")
         .col(clr::green(), clip(f.pointee, budget > 17 ? budget - 17 : 0));
        out.push_back(l);
    } else if (inside) {
        // Цель — внутри осматриваемого объекта: её байты уже видны в дампе
        // выше, читать нечего. Про валидность речи нет (это тот же объект).
        Line l;
        l.col(clr::grey(), clip("цель в этом объекте — байты видны выше", budget));
        out.push_back(l);
    } else {
        // Внешний адрес: инспектор намеренно НЕ разыменовывает чужую память
        // (она может быть висячей), поэтому показывает только адрес.
        Line l;
        l.col(clr::grey(), clip("цель не читаем: адрес может быть невалиден", budget));
        out.push_back(l);
    }
}

inline Line range_note(const Region& r) {
    Line l;
    l.col(clr::grey(), "в объекте: ")
     .col(r.what == Region::R::padding ? clr::red() : region_color(r),
          byte_range(r.off, r.size));
    return l;
}

// Выноски региона (каждая строка ≤ budget колонок).
inline std::vector<Line> region_notes(const Region& r, const unsigned char* base,
                                      std::size_t total, bool standalone,
                                      std::size_t budget, bool with_range = true) {
    std::vector<Line> out;
    // Диапазон «в объекте: +..+» лишний для однострочного региона (offset уже
    // слева от байтов) — двухзонный режим гасит его через with_range=false.
    auto push_range = [&] { if (with_range) out.push_back(range_note(r)); };
    switch (r.what) {
        case Region::R::vptr: {
            Line l1; l1.col(clr::violet(), "vptr (портал) → vtable класса ▼");
            out.push_back(l1); push_range();
            Line l3; l3.col(clr::grey(), "скрытое поле: его вставил virtual");
            out.push_back(l3);
            break;
        }
        case Region::R::vbase: {
            Line l1; l1.col(clr::violet(), "vbase-ptr → указатель на virtual-базу");
            out.push_back(l1); push_range();
            Line l3; l3.col(clr::grey(), "вставил компилятор ради общей vbase");
            out.push_back(l3);
            break;
        }
        case Region::R::padding: {
            Line l1;
            l1.col(clr::red(), "padding (прах) " + std::to_string(r.size) +
                                   " Б — дыра, внутри мусор");
            out.push_back(l1); push_range();
            Line l3; l3.col(clr::grey(), clip(r.why, budget));
            out.push_back(l3);
            break;
        }
        case Region::R::opaque: {
            if (r.why.empty()) {   // непрозрачный класс целиком
                Line l1; l1.col(clr::grey(), "поля скрыты: private/конструкторы");
                out.push_back(l1); push_range();
                Line l3; l3.col(clr::grey(), "добавь EYE_DESCRIBE — Око увидит");
                out.push_back(l3);
            } else {               // база без реестра / служебная часть std-типа
                Line l1; l1.col(clr::grey(), "скрытые байты (туман)");
                out.push_back(l1); push_range();
                Line l3; l3.col(clr::grey(), clip(r.why, budget));
                out.push_back(l3);
            }
            break;
        }
        case Region::R::field: {
            const FieldInfo& f = *r.f;
            if (r.strpart == 1) {          // .ptr — куда смотрит строка
                Line l;
                add_field_mark(l, f, r.field_no);
                l.col(clr::green(), clip(f.name, std::max<std::size_t>(10, budget / 2)) + ".ptr")
                 .col(clr::grey(), " = ").plain(hexptr(f.target));
                out.push_back(l);
                push_range();
                Line l2;
                if (f.sso) {
                    const auto b = reinterpret_cast<std::uintptr_t>(base);
                    const auto t = reinterpret_cast<std::uintptr_t>(f.target);
                    l2.col(clr::gold(), "↩ буфер в объекте: начало +" +
                        hex4(static_cast<std::size_t>(t - b)) +
                        " (ниже ▼)");
                } else {
                    l2.col(clr::gold(), "► КУЧА @ ").plain(hexptr(f.target))
                      .col(clr::gold(), " (панель ниже ▼)");
                }
                out.push_back(l2);
            } else if (r.strpart == 2) {   // .len
                Line l;
                add_field_mark(l, f, r.field_no);
                l.col(clr::green(), clip(f.name, std::max<std::size_t>(10, budget / 2)) + ".len")
                 .col(clr::grey(), " = ")
                 .col(clr::green(), std::to_string(f.str_len))
                 .col(clr::grey(), " — длина строки");
                out.push_back(l);
                push_range();
            } else if (r.strpart == 3) {   // .buf / .cap
                if (f.sso) {
                    Line l;
                    add_field_mark(l, f, r.field_no);
                    l.col(clr::green(), clip(f.name, std::max<std::size_t>(10, budget / 2)) + ".buf")
                     .col(clr::grey(), " = ")
                     .col(clr::green(), clip(f.value, budget > 20 ? budget - 20
                                                                  : 6));
                    out.push_back(l);
                    push_range();
                    Line l2;
                    l2.col(clr::grey(),
                        "строка внутри объекта (SSO, ≤15 символов)");
                    out.push_back(l2);
                } else {
                    Line l;
                    add_field_mark(l, f, r.field_no);
                    l.col(clr::green(), clip(f.name, std::max<std::size_t>(10, budget / 2)) + ".cap")
                     .col(clr::grey(), " = ")
                     .col(clr::green(), std::to_string(f.str_cap))
                     .col(clr::grey(), " — вместимость");
                    out.push_back(l);
                    push_range();
                    Line l2;
                    l2.col(clr::grey(), "SSO-буфер пуст: символы в куче");
                    out.push_back(l2);
                }
            } else {                        // обычное поле
                // Для одиночного значения hex уйдёт отдельной строкой-уроком.
                const bool wrap_name = vwidth(f.name) > budget / 2;
                if (wrap_name) {
                    Line name;
                    add_field_mark(name, f, r.field_no);
                    name.col(clr::green(), clip(f.name,
                        budget > name.w ? budget - name.w : 1));
                    out.push_back(name);
                }
                out.push_back(field_headline(f, budget, !standalone, !wrap_name,
                                             wrap_name ? 0 : r.field_no));
                push_range();
                if (f.base_depth > 0 && !f.owner.empty()) {
                    Line ob;
                    ob.col(clr::grey(), "из базы ")
                      .col(clr::cyan(), clip(f.owner, budget > 8 ? budget - 8 : 6));
                    out.push_back(ob);
                }
                if (f.kind == FieldInfo::Kind::pointer)
                    pointer_notes(out, f, base, total, budget);
                if (f.kind == FieldInfo::Kind::str && !f.str_layout) {
                    Line l2;
                    if (f.sso)
                        l2.col(clr::gold(),
                            "↩ буфер прямо в объекте (SSO — короткая строка)");
                    else
                        l2.col(clr::gold(), "► КУЧА @ ").plain(hexptr(f.target))
                          .col(clr::gold(), " (панель ниже ▼)");
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

// ---- Сама секция «память» ---------------------------------------------------
inline void render_memory(std::vector<FieldInfo> fields, std::size_t total,
                          std::size_t talign,
                          const std::vector<std::size_t>& vptr_offsets,
                          const std::vector<std::size_t>& vbase_offsets,
                          const void* addr, bool opaque, bool standalone,
                          const VectorInfo* vector = nullptr,
                          const std::vector<OpaqueSpan>& opaque_spans = {}) {
    const bool poly = !vptr_offsets.empty();
    const auto* base = static_cast<const unsigned char*>(addr);
    // Реестр мог перечислить поля не по порядку — сортируем по offset, иначе
    // регионы и padding посчитаются неверно.
    std::sort(fields.begin(), fields.end(),
              [](const FieldInfo& a, const FieldInfo& b) {
                  return a.offset < b.offset;
              });
    // Наследование (поля из баз или скрытые базы): совет по перестановке полей
    // неприменим — между базами поля не переставить.
    const bool inherited = !opaque_spans.empty() ||
        std::any_of(fields.begin(), fields.end(),
                    [](const FieldInfo& f) { return f.base_depth > 0; });
    const std::string opaque_why = vector == nullptr
        ? ""
        : "роль зависит от STL и режима Debug/Release";
    const auto regions = build_regions(fields, total, vptr_offsets, vbase_offsets,
                                       opaque, opaque_why, opaque_spans);

    bool has_pad = false, has_vptr = false, has_field = false, has_op = false,
         has_vbase_ptr = false;
    std::string strip(total, '.');
    for (const Region& r : regions)
        for (std::size_t b = r.off; b < r.off + r.size && b < total; ++b)
            switch (r.what) {
                case Region::R::field:   strip[b] = 'f'; has_field = true; break;
                case Region::R::padding: strip[b] = 'p'; has_pad = true;   break;
                case Region::R::vptr:    strip[b] = 'v'; has_vptr = true;  break;
                case Region::R::vbase:   strip[b] = 'b'; has_vbase_ptr = true; break;
                case Region::R::opaque:  strip[b] = 'o'; has_op = true;    break;
            }
    std::size_t nf = 0, np = 0, nv = 0, nb = 0;
    for (char c : strip) {
        nf += c == 'f' || c == 'o';
        np += c == 'p';
        nv += c == 'v';
        nb += c == 'b';
    }
    np += static_cast<std::size_t>(
        std::count(strip.begin(), strip.end(), '.'));   // не покрыто = дыра

    // Однострочная карта объекта перед подробностями: сначала общий масштаб,
    // затем уже байты. Это не отдельная таблица и ничего не дублирует.
    Line overview;
    overview.col(clr::grey(), "итог: ")
            .col(clr::cyan(), std::to_string(total) + " Б")
            .col(clr::grey(), " · полей " +
                               (opaque ? std::string("?")
                                       : std::to_string(fields.size())) +
                               " · данные ")
            .col(clr::green(), std::to_string(nf) + " Б");
    if (nv != 0)
        overview.col(clr::grey(), " · vptr ")
                .col(clr::violet(), std::to_string(nv) + " Б");
    if (nb != 0)
        overview.col(clr::grey(), " · vbase-ptr ")
                .col(clr::violet(), std::to_string(nb) + " Б");
    overview.col(clr::grey(), " · padding ")
            .col(np == 0 ? clr::dim() : clr::red(), std::to_string(np) + " Б");
    if (np != 0) {
        const std::size_t pct = total == 0 ? 0 : np * 100 / total;
        overview.col(clr::grey(), " (" + std::to_string(pct) + "%)");
    }
    put(overview);

    // Шкала-DEFRAG: весь объект одной полосой — доли данных/vptr/padding сразу
    // видны глазами (тот самый урок pahole «сколько тебя ушло в дыры»). Доли
    // считаются кумулятивно, поэтому сумма сегментов точно равна ширине полосы.
    if (!opaque && total > 0 && (np != 0 || nv != 0 || nb != 0 ||
                                 fields.size() > 1)) {
        const std::size_t bw = frame_width() > 4 ? frame_width() - 2 : frame_width();
        Line bar;
        bar.col(clr::grey(), "▐");
        std::size_t acc = 0, drawn = 0;
        auto seg = [&](std::size_t bytes, const char* color, const char* glyph) {
            acc += bytes;
            const std::size_t upto = total ? acc * bw / total : 0;
            while (drawn < upto) { bar.col(color, glyph); ++drawn; }
        };
        seg(nf, clr::cyan(), "█");                 // данные (+ opaque как данные)
        seg(nv + nb, clr::violet(), "▒");          // vptr / vbase — «портал»
        seg(np, clr::red(), "░");                  // padding — «прах»
        while (drawn < bw) { bar.col(clr::dim(), "░"); ++drawn; }
        bar.col(clr::grey(), "▌");
        put(bar);
    }

    if (vector != nullptr) {
        Line facts;
        facts.col(clr::grey(), "vector: size ")
             .col(clr::green(), std::to_string(vector->size))
             .col(clr::grey(), " · capacity ")
             .col(clr::green(), std::to_string(vector->capacity))
             .col(clr::grey(), " · элемент ")
             .col(clr::cyan(), vector->element_type);
        if (vector->bit_packed)
            facts.col(clr::grey(), " (1 бит логически)");
        else
            facts.col(clr::grey(), " (" +
                      std::to_string(vector->element_size) + " Б)");
        put(facts);

        Line storage;
        if (vector->bit_packed) {
            storage.col(clr::gold(), "vector<bool>: биты упакованы; data() недоступен");
        } else if (vector->data == nullptr) {
            storage.col(clr::grey(), "внешнего массива нет: vector пуст");
        } else {
            storage.col(clr::grey(), "куча: живые элементы ")
                   .col(clr::green(), std::to_string(vector->heap_used) + " Б")
                   .col(clr::grey(), " · резерв ")
                   .col(clr::green(),
                        std::to_string(vector->heap_reserved) + " Б")
                   .col(clr::grey(), " · data @ ")
                   .plain(hexptr(vector->data));
        }
        put(storage);

        if (vector->bit_packed && !vector->elements.empty()) {
            Line values;
            values.col(clr::grey(), "элементы через operator[]: [");
            for (std::size_t i = 0; i < vector->elements.size(); ++i) {
                if (i != 0) values.col(clr::grey(), ", ");
                values.col(clr::green(), vector->elements[i].value);
            }
            if (vector->size > vector->elements.size())
                values.col(clr::grey(), ", …");
            values.col(clr::grey(), "]");
            put(values);
        }

        if (!vector->bit_packed && vector->data != nullptr &&
            !vector->slots_matched)
            put_text("≈ адресные слоты не распознаны — не называем их наугад");
    }

    // Ключевой урок раскладки: столбец байта = offset mod 8, поэтому
    // выравнивание видно глазами (сдвинутое поле начинается не с +0).
    if (!standalone)
        put_text("колонка = offset mod 8 — выравнивание видно столбиком");

    // Шапка-линейка: подписи колонок стоят РОВНО над своими данными.
    Line h;
    h.col(clr::grey(), "off").to(MEM_HEX_COL);
    for (int i = 0; i < 8; ++i)
        h.col(clr::dim(), "+" + std::to_string(i)).sp(i == 7 ? 0 : 1);
    h.to(MEM_ASCII_COL).col(clr::dim(), "ascii");

    const bool two_zone = geo().two_zone;
    if (two_zone) {
        // Открываем двухзонную полосу: слева карта, справа кодекс-выноски.
        frame_span2("╥");
        Line codex_hd; codex_hd.col(clr::gold(), "КОДЕКС — выноски");
        put_two_zone(h, codex_hd);
    } else {
        put(h);
    }

    bool first_card = true;
    for (const Region& r : regions) {
        const auto rows = region_rows(r.off, r.size);
        if (two_zone) {
            const bool single = rows.size() <= 1;   // не дублировать диапазон
            const auto notes = region_notes(r, base, total, standalone,
                                            geo().codex_w, !single);
            if (!first_card) frame_span2("╫");       // разделитель карточек
            first_card = false;
            const std::size_t k = std::max(rows.size(), notes.size());
            for (std::size_t i = 0; i < k; ++i) {
                const Line lft = i < rows.size() ? mem_row(rows[i], r, base)
                                                 : Line{};
                const Line rgt = i < notes.size() ? notes[i] : Line{};
                put_two_zone(lft, rgt);
            }
        } else {
            // Компактный режим: сперва байты, потом выноски внутри рамки.
            const auto notes = region_notes(r, base, total, standalone,
                                            frame_width() - 8);
            for (const MRow& mr : rows) put(mem_row(mr, r, base));
            for (std::size_t i = 0; i < notes.size(); ++i) {
                Line l;
                l.to(3).col(clr::grey(), i == 0 ? "└► " : "   ");
                l.s += notes[i].s; l.w += notes[i].w;
                put(l);
            }
        }
    }
    if (two_zone) frame_span2("╨");                  // закрываем полосу

    // Роли для «ключа карты» (рисуется в конце секции — см. ниже).
    // Одиночное значение без дыр — ключ не скажет ничего нового.
    const bool trivial = standalone && np == 0 && !has_vptr;
    const bool has_links = std::any_of(
        fields.begin(), fields.end(), [](const FieldInfo& f) {
            return f.kind == FieldInfo::Kind::pointer ||
                   f.kind == FieldInfo::Kind::str;
        });

    // --- урок little-endian на первом же целом поле объекта -------------------
    if (!standalone) {
        for (const FieldInfo& f : fields)
            if (f.integral && !f.alt.empty() && f.size <= 4 &&
                f.offset + f.size <= total) {
                std::string bytes;
                for (std::size_t i = 0; i < f.size; ++i)
                    bytes += hex2(base[f.offset + i]) + (i + 1 < f.size ? " " : "");
                Line le;
                le.col(clr::grey(), "руна наоборот (LE): ")
                  .col(clr::cyan(), bytes)
                  .col(clr::grey(), " → ")
                  .col(clr::cyan(), f.alt);
                if (le.w + vwidth(f.value) + 3 <= frame_width())
                    le.col(clr::grey(), " = ").col(clr::green(), f.value);
                if (le.w <= frame_width()) put(le);
                break;
            }
    }

    // --- совет по перестановке (привет, pahole) --------------------------------
    // При virtual-базе указатель vbase — обязательная служебная ячейка, её
    // перестановкой не убрать; при наследовании поля разных баз не переставить.
    if (np > 0 && !poly && !opaque && fields.size() > 1 &&
        vbase_offsets.empty() && !inherited) {
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

    // --- ключ карты: по ЗНАКУ на строку, целыми словами (не обрезается) --------
    // Термин C++ первичен, игровой образ рядом. Один глиф — одна роль.
    if (!trivial) {
        frame_sep("ключ карты — условные знаки");
        auto key = [](const char* gl_color, const char* gl,
                      const std::string& text) {
            Line l;
            l.col(gl_color, gl).sp(2)
             .col(clr::grey(), clip(text, frame_width() > 3 ? frame_width() - 3 : 0));
            put(l);
        };
        if (has_field) {
            if (vector != nullptr)
                key(clr::cyan(), "█", "адресное слово (≈ сопоставлено по адресам)");
            else
                key(clr::cyan(), "█",
                    "поле · у соседа иной оттенок — так видно границы");
        }
        if (has_pad)
            key(clr::red(), "░", "padding (прах) · дыра выравнивания, внутри мусор");
        if (has_vptr)
            key(clr::violet(), "▒",
                "vptr (портал) · скрытый указатель на таблицу virtual ▼");
        if (has_vbase_ptr)
            key(clr::violet(), "▒",
                "vbase-ptr · служебный указатель на virtual-базу");
        if (has_op)
            key(clr::grey(), "▓",
                "скрытое (туман) · private / чужая ABI → EYE_DESCRIBE");
        if (has_links) {
            Line links;
            links.col(clr::gold(), "►").sp().col(clr::grey(), "наружу   ")
                 .col(clr::gold(), "↩").sp().col(clr::grey(), "внутрь объекта   ")
                 .col(clr::gold(), "×").sp().col(clr::grey(), "в никуда (nullptr)");
            put(links);
        }
    } else if (has_links) {
        put_blank();
        Line links;
        links.col(clr::gold(), "►").sp().col(clr::grey(), "наружу   ")
             .col(clr::gold(), "↩").sp().col(clr::grey(), "внутрь   ")
             .col(clr::gold(), "×").sp().col(clr::grey(), "nullptr");
        put(links);
    }
}

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

// ════════════════════════════════════════════════════════════════════════════
//  Панели-спутники: буферы std::string и массивы std::vector, живущие в куче.
//  Печатаются ПОСЛЕ главной панели — внешняя память правда «где-то ещё».
// ════════════════════════════════════════════════════════════════════════════
inline void render_satellites(const std::vector<FieldInfo>& fields,
                              const void* addr) {
    constexpr std::size_t SAT_W = 44;   // внутренняя ширина мини-рамки
    const std::string ind = margin_str() + "   ";

    for (std::size_t field_i = 0; field_i < fields.size(); ++field_i) {
        const FieldInfo& f = fields[field_i];
        if (f.kind != FieldInfo::Kind::str || f.sso || f.heap_bytes.empty())
            continue;
        std::size_t field_no = 1;
        for (std::size_t i = 0; i < fields.size(); ++i)
            if (fields[i].offset < f.offset ||
                (fields[i].offset == f.offset && i < field_i))
                ++field_no;
        // Стрелка-связка от главной панели к спутнику.
        std::cout << ind << clr::grey() << "│" << clr::reset() << '\n';
        Line link;
        link.col(clr::gold(), "╰──► ");
        add_field_mark(link, f, field_no);
        link.col(clr::green(), f.name + ".ptr")
            .col(clr::grey(), " ведёт во внешний блок; объект остался @ ")
            .plain(hexptr(addr)).col(clr::grey(), ":");
        const std::size_t link_budget =
            term_width() > vwidth(ind) ? term_width() - vwidth(ind) : 1;
        std::cout << ind
                  << (link.w > link_budget ? clip_ansi(link.s, link_budget)
                                           : link.s)
                  << '\n';

        const std::string pre = ind + "    ";
        auto sat_line = [&](const Line& ln) {
            const std::size_t body_w = std::min(ln.w, SAT_W);
            const std::size_t pad = SAT_W - body_w;
            std::cout << pre << clr::grey() << "│" << clr::reset() << ' '
                      << (ln.w > SAT_W ? clip_ansi(ln.s, SAT_W) : ln.s)
                      << std::string(pad, ' ') << ' '
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

// Внешний непрерывный массив std::vector. Показываем только ЖИВЫЕ элементы;
// зарезервированный, но не сконструированный хвост не читаем.
inline void render_vector_satellite(const VectorInfo& vector,
                                    const void* addr) {
    if (vector.bit_packed || vector.data == nullptr || vector.capacity == 0)
        return;

    constexpr std::size_t SAT_W = 52;
    const std::string ind = margin_str() + "   ";
    std::cout << ind << clr::grey() << "│" << clr::reset() << '\n';

    Line link;
    link.col(clr::gold(), "╰──► ")
        .col(clr::green(), "vector.data()")
        .col(clr::grey(), " ведёт во внешний массив; объект остался @ ")
        .plain(hexptr(addr)).col(clr::grey(), ":");
    const std::size_t link_budget =
        term_width() > vwidth(ind) ? term_width() - vwidth(ind) : 1;
    std::cout << ind
              << (link.w > link_budget ? clip_ansi(link.s, link_budget)
                                       : link.s)
              << '\n';

    const std::string pre = ind + "    ";
    auto sat_line = [&](const Line& ln) {
        const std::size_t body_w = std::min(ln.w, SAT_W);
        const std::size_t pad = SAT_W - body_w;
        std::cout << pre << clr::grey() << "│" << clr::reset() << ' '
                  << (ln.w > SAT_W ? clip_ansi(ln.s, SAT_W) : ln.s)
                  << std::string(pad, ' ') << ' '
                  << clr::grey() << "│" << clr::reset() << '\n';
    };

    {
        const std::string title = clip(
            "внешний массив @ " + hexptr(vector.data) + " · " +
                vector.element_type + "[]",
            SAT_W - 3);
        std::cout << pre << clr::grey() << "╭─" << clr::violet() << "◈ "
                  << clr::reset() << clr::gold() << title << clr::reset()
                  << ' ' << clr::grey()
                  << dashes(SAT_W - vwidth(title) - 2) << "╮"
                  << clr::reset() << '\n';
    }
    {
        Line info;
        info.col(clr::grey(), "size ")
            .col(clr::green(), std::to_string(vector.size))
            .col(clr::grey(), " · capacity ")
            .col(clr::green(), std::to_string(vector.capacity))
            .col(clr::grey(), " · живые ")
            .col(clr::green(), std::to_string(vector.heap_used) + " Б")
            .col(clr::grey(), " / резерв ")
            .col(clr::green(), std::to_string(vector.heap_reserved) + " Б");
        sat_line(info);
    }

    if (vector.elements.empty()) {
        Line empty;
        empty.col(clr::grey(), "элементов нет; память только зарезервирована");
        sat_line(empty);
    }
    for (const VectorElementInfo& element : vector.elements) {
        std::string bytes;
        for (unsigned char byte : element.bytes)
            bytes += hex2(byte) + " ";
        if (element.bytes.size() < vector.element_size) bytes += "… ";

        Line row;
        row.col(clr::gold(), "#" + std::to_string(element.index)).sp()
           .col(clr::grey(), "+" + hex4(element.index * vector.element_size))
           .sp().col(clr::cyan(), ljust(bytes, 25))
           .col(clr::grey(), "= ")
           .col(clr::green(), element.value);
        sat_line(row);
    }
    if (vector.size > vector.elements.size()) {
        Line more;
        more.col(clr::grey(), "⋯ ещё " +
                 std::to_string(vector.size - vector.elements.size()) +
                 " элементов ⋯");
        sat_line(more);
    }
    std::cout << pre << clr::grey() << "╰" << dashes(SAT_W + 2) << "╯"
              << clr::reset() << '\n';
}

} // namespace detail
} // namespace eye
