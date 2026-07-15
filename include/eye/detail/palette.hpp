// ОКО МАГА / eye/detail/palette.hpp — ПАЛИТРА: ANSI-256, авто-выкл вне терминала.
#pragma once
#include "platform.hpp"   // env_value + <windows.h> (VT/UTF-8 на Windows)

namespace eye {

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

} // namespace eye
