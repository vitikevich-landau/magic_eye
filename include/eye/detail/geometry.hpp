// ОКО МАГА / eye/detail/geometry.hpp — ГЕОМЕТРИЯ: зоны, ширина, разворот окна.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <string>
#include "platform.hpp"   // env_value, <windows.h>/ioctl

namespace eye::detail {

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

// RAII: временно подменить геометрию (TUI рисует секции в зону произвольной
// ширины и без центрирования — margin у него свой, экранный).
class GeoScope {
public:
    explicit GeoScope(const Geo& g) : saved_(geo()) { geo() = g; }
    GeoScope(const GeoScope&) = delete;
    GeoScope& operator=(const GeoScope&) = delete;
    ~GeoScope() { geo() = saved_; }

private:
    Geo saved_;
};

#if defined(_WIN32)
// «Во весь экран, как в игре»: при первом выводе разворачиваем окно консоли на
// максимально возможный для текущего шрифта размер. Обычный conhost это умеет;
// Windows Terminal/ConPTY может проигнорировать — тогда раскладка просто
// подстроится под текущий размер. EYE_RESIZE=0 отключает авто-разворот.
inline void maximize_console_once() {
    static const bool done = [] {
        const std::string resize = env_value("EYE_RESIZE");
        if (!resize.empty() && resize.front() == '0') return true;
        if (!_isatty(_fileno(stdout))) return true;

        const HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO bi{};
        if (h == INVALID_HANDLE_VALUE || !GetConsoleScreenBufferInfo(h, &bi))
            return true;

        // Ширина БУФЕРА должна вмещать будущее окно, иначе развернуть на полную
        // не выйдет (окно упрётся в буфер). Только РАСТИМ ширину — высоту-
        // scrollback не трогаем, чтобы не потерять историю и не прокрутить вид.
        const COORD largest = GetLargestConsoleWindowSize(h);
        if (largest.X > 0 && bi.dwSize.X < largest.X) {
            COORD buf = bi.dwSize;
            buf.X = largest.X;
            SetConsoleScreenBufferSize(h, buf);   // отказ не критичен
        }

        // Развернуть само окно консоли «во весь экран» (как кнопка «развернуть»):
        // и по ширине, и по высоте. Позицию и размер под монитор выберет система.
        if (const HWND hwnd = GetConsoleWindow())
            ShowWindow(hwnd, SW_MAXIMIZE);
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
    maximize_console_once();
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

} // namespace eye::detail
