// ОКО МАГА / eye/detail/tui/term.hpp — СЕССИЯ ТЕРМИНАЛА: raw, alt-screen, ввод.
//   RAII на всё странствие: вход переводит терминал в raw-режим и альтернативный
//   экран, выход ВСЕГДА возвращает как было — через деструктор, atexit-страховку
//   и обработчики SIGTERM/SIGHUP (восстановить и умереть как раньше). Ctrl-C
//   не сигналит (ISIG снят) — приходит байтом 0x03, и приложение выходит само,
//   гарантированно пройдя восстановление.
//
//   POSIX: termios + poll + ioctl. Windows: ReadConsoleInputW переводится в те
//   же VT-байты, что шлёт POSIX-терминал, — декодер клавиш один на обе ОС.
#pragma once
#include <cerrno>    // EINTR — повтор записи, прерванной сигналом (POSIX)
#include <cstddef>
#include <cstdlib>
#include <string>
#include "../geometry.hpp"   // maximize_console_once (Windows), env_value
#include "../platform.hpp"
#include "canvas.hpp"        // TermSize

namespace eye::detail::tui {

// ─── глобальное состояние восстановления (сигналы/atexit) ───────────────────
struct TermRestore {
    bool active = false;
#if defined(_WIN32)
    DWORD in_mode = 0, out_mode = 0;
    UINT  out_cp = 0;
#else
    termios saved{};
#endif
};
inline TermRestore& term_restore() { static TermRestore r; return r; }

// Вернуть терминал в исходное состояние. Идемпотентно: второй вызов — no-op.
// Зовётся из деструктора, atexit и сигналов, поэтому без iostream.
inline void restore_terminal() {
    TermRestore& r = term_restore();
    if (!r.active) return;
    r.active = false;
    static const char leave[] = "\x1b[0m\x1b[?25h\x1b[?1049l";
#if defined(_WIN32)
    const HANDLE ho = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written = 0;
    WriteConsoleA(ho, leave, static_cast<DWORD>(sizeof(leave) - 1), &written,
                  nullptr);
    SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), r.in_mode);
    SetConsoleMode(ho, r.out_mode);
    if (r.out_cp != 0) SetConsoleOutputCP(r.out_cp);
#else
    // write из <unistd.h> — async-signal-safe, в отличие от std::cout.
    (void)!::write(STDOUT_FILENO, leave, sizeof(leave) - 1);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &r.saved);
#endif
}

#if !defined(_WIN32)
inline volatile std::sig_atomic_t& winch_flag() {
    static volatile std::sig_atomic_t flag = 0;
    return flag;
}
#endif

extern "C" inline void eye_term_on_deadly_signal(int sig) {
    restore_terminal();
    std::signal(sig, SIG_DFL);
    std::raise(sig);            // умереть с тем же кодом, но чистым терминалом
}
#if !defined(_WIN32)
extern "C" inline void eye_term_on_winch(int) { winch_flag() = 1; }
#endif

// ─── сессия ──────────────────────────────────────────────────────────────────
class TermSession {
public:
    // Возможен ли TUI вообще: оба конца — терминал, TERM не «dumb»,
    // EYE_INTERACTIVE не 0. Не-TTY → зовущий печатает статикой (inspect).
    static bool interactive_capable() {
        const std::string force = env_value("EYE_INTERACTIVE");
        if (!force.empty() && force.front() == '0') return false;
#if defined(_WIN32)
        if (!_isatty(_fileno(stdin)) || !_isatty(_fileno(stdout))) return false;
        DWORD mode = 0;
        const HANDLE ho = GetStdHandle(STD_OUTPUT_HANDLE);
        if (ho == INVALID_HANDLE_VALUE || !GetConsoleMode(ho, &mode))
            return false;
        // Без VT-вывода TUI не позиционирует курсор — честный отказ. ПРОБУЕМ
        // включить VT и СРАЗУ возвращаем режим как был: этот вызов лишь
        // ПРОВЕРЯЕТ способность. Иначе TermSession ниже захватил бы уже
        // изменённый режим и на выходе не восстановил бы VT-бит — терминал
        // остался бы «подкрашенным» (ревью Codex, PR #5). Настоящее включение
        // VT сделает TermSession, захватив честный исходный режим.
        const bool vt_ok =
            SetConsoleMode(ho, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
        SetConsoleMode(ho, mode);   // откатить пробу
        return vt_ok;
#else
        if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) return false;
        const std::string term = env_value("TERM");
        return term != "dumb";
#endif
    }

    TermSession() {
        TermRestore& r = term_restore();
#if defined(_WIN32)
        hin_ = GetStdHandle(STD_INPUT_HANDLE);
        hout_ = GetStdHandle(STD_OUTPUT_HANDLE);
        GetConsoleMode(hin_, &r.in_mode);
        GetConsoleMode(hout_, &r.out_mode);
        r.out_cp = GetConsoleOutputCP();
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleMode(hout_, r.out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        // Ввод: сырые события клавиш + resize; echo/line/Ctrl-C — выкл.
        SetConsoleMode(hin_, ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT);
        maximize_console_once();     // «во весь экран» (уважает EYE_RESIZE=0)
#else
        tcgetattr(STDIN_FILENO, &r.saved);
        termios raw = r.saved;
        raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO | ISIG | IEXTEN);
        raw.c_iflag &= ~static_cast<tcflag_t>(IXON | ICRNL | BRKINT | INPCK);
        raw.c_cc[VMIN] = 0;          // read не блокирует — ждём через poll
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
        prev_winch_ = std::signal(SIGWINCH, eye_term_on_winch);
#endif
        r.active = true;
        // Смертельные сигналы ИЗВНЕ (kill -INT/-QUIT/-TERM/-HUP) убивают процесс
        // действием по умолчанию, минуя и деструктор, и atexit, — терминал
        // остался бы в raw-режиме на alt-экране. Ctrl-C, набранный в САМОМ
        // терминале, сюда не приходит (ISIG снят) и обрабатывается байтом 0x03,
        // но внешний SIGINT обязан восстановить терминал так же, как SIGTERM
        // (ревью Codex, PR #5).
        prev_term_ = std::signal(SIGTERM, eye_term_on_deadly_signal);
        prev_int_ = std::signal(SIGINT, eye_term_on_deadly_signal);
#if !defined(_WIN32)
        prev_hup_ = std::signal(SIGHUP, eye_term_on_deadly_signal);
        prev_quit_ = std::signal(SIGQUIT, eye_term_on_deadly_signal);
#endif
        static const bool at_exit_armed = [] {
            std::atexit(restore_terminal);
            return true;
        }();
        (void)at_exit_armed;
        write("\x1b[?1049h\x1b[?25l\x1b[2J");   // alt-screen, скрыть курсор
    }

    TermSession(const TermSession&) = delete;
    TermSession& operator=(const TermSession&) = delete;

    ~TermSession() {
        restore_terminal();
        std::signal(SIGTERM, prev_term_ ? prev_term_ : SIG_DFL);
        std::signal(SIGINT, prev_int_ ? prev_int_ : SIG_DFL);
#if !defined(_WIN32)
        std::signal(SIGHUP, prev_hup_ ? prev_hup_ : SIG_DFL);
        std::signal(SIGQUIT, prev_quit_ ? prev_quit_ : SIG_DFL);
        std::signal(SIGWINCH, prev_winch_ ? prev_winch_ : SIG_DFL);
#endif
    }

    TermSize size() const {
#if defined(_WIN32)
        CONSOLE_SCREEN_BUFFER_INFO bi{};
        if (GetConsoleScreenBufferInfo(hout_, &bi))
            return {static_cast<std::size_t>(bi.srWindow.Right -
                                             bi.srWindow.Left + 1),
                    static_cast<std::size_t>(bi.srWindow.Bottom -
                                             bi.srWindow.Top + 1)};
#else
        winsize ws{};
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
            return {ws.ws_col, ws.ws_row};
#endif
        return {80, 24};
    }

    void write(const std::string& s) {
        if (s.empty()) return;
#if defined(_WIN32)
        DWORD written = 0;
        WriteConsoleA(hout_, s.data(), static_cast<DWORD>(s.size()), &written,
                      nullptr);
#else
        std::size_t done = 0;
        while (done < s.size()) {
            const ssize_t n = ::write(STDOUT_FILENO, s.data() + done,
                                      s.size() - done);
            if (n < 0) {
                // EINTR — запись прервал сигнал (наш же SIGWINCH при resize).
                // Бросить хвост кадра нельзя: канва уже продвинула дифф, и
                // недописанные байты никто не перерисует — экран остался бы
                // порванным. Повторяем. Рассчитывать на SA_RESTART нельзя:
                // std::signal ставит его не на всех платформах (Codex, PR #5).
                if (errno == EINTR) continue;
                return;             // настоящая ошибка (EIO/EPIPE) — сдаёмся
            }
            if (n == 0) return;
            done += static_cast<std::size_t>(n);
        }
#endif
    }

    // Подождать ввод ≤ timeout_ms и скормить прочитанное декодеру.
    // true — декодеру что-то досталось (событие или хвост посл-ности);
    // false — таймаут (пора проверить resize и незакрытый ESC).
    // feed — вызываемое (const char*, std::size_t), обычно KeyDecoder::feed.
    template <class F>
    bool pump(F&& feed, int timeout_ms) {
#if defined(_WIN32)
        if (WaitForSingleObject(hin_, static_cast<DWORD>(timeout_ms)) !=
            WAIT_OBJECT_0)
            return false;
        INPUT_RECORD recs[16];
        DWORD n = 0;
        if (!ReadConsoleInputW(hin_, recs, 16, &n) || n == 0) return false;
        std::string bytes;
        for (DWORD i = 0; i < n; ++i) {
            if (recs[i].EventType == WINDOW_BUFFER_SIZE_EVENT) {
                resized_ = true;
                continue;
            }
            if (recs[i].EventType != KEY_EVENT ||
                !recs[i].Event.KeyEvent.bKeyDown)
                continue;
            append_vt_for_key(bytes, recs[i].Event.KeyEvent);
        }
        if (bytes.empty()) return false;
        feed(bytes.data(), bytes.size());
        return true;
#else
        pollfd pfd{STDIN_FILENO, POLLIN, 0};
        const int rc = ::poll(&pfd, 1, timeout_ms);
        if (rc <= 0 || !(pfd.revents & POLLIN)) return false;
        char buf[256];
        const ssize_t n = ::read(STDIN_FILENO, buf, sizeof(buf));
        if (n <= 0) return false;
        feed(buf, static_cast<std::size_t>(n));
        return true;
#endif
    }

    // Изменился ли размер с прошлой проверки (сигнал/событие или сравнение).
    bool take_resize(TermSize& now) {
        now = size();
#if !defined(_WIN32)
        if (winch_flag() != 0) {
            winch_flag() = 0;
            last_ = now;
            return true;
        }
#else
        if (resized_) {
            resized_ = false;
            last_ = now;
            return true;
        }
#endif
        if (now != last_) {          // страховка: событие могло потеряться
            last_ = now;
            return true;
        }
        return false;
    }

private:
    TermSize last_{};
    void (*prev_term_)(int) = nullptr;
    void (*prev_int_)(int) = nullptr;    // SIGINT есть и на Windows (CRT)
#if defined(_WIN32)
    HANDLE hin_ = nullptr, hout_ = nullptr;
    bool resized_ = false;

    // Виртуальную клавишу Windows → те же байты, что шлёт POSIX-терминал.
    static void append_vt_for_key(std::string& out, const KEY_EVENT_RECORD& k) {
        switch (k.wVirtualKeyCode) {
            case VK_UP:    out += "\x1b[A"; return;
            case VK_DOWN:  out += "\x1b[B"; return;
            case VK_RIGHT: out += "\x1b[C"; return;
            case VK_LEFT:  out += "\x1b[D"; return;
            case VK_HOME:  out += "\x1b[H"; return;
            case VK_END:   out += "\x1b[F"; return;
            case VK_PRIOR: out += "\x1b[5~"; return;
            case VK_NEXT:  out += "\x1b[6~"; return;
            case VK_DELETE:out += "\x1b[3~"; return;
            case VK_F1:    out += "\x1b[11~"; return;
            case VK_ESCAPE:out += "\x1b";   return;
            case VK_BACK:  out += "\x7f";   return;
            case VK_TAB:   out += "\t";     return;
            case VK_RETURN:out += "\r";     return;
            default: break;
        }
        const wchar_t wc = k.uChar.UnicodeChar;
        if (wc == 0) return;
        // UTF-16 → UTF-8 (суррогатные пары клавиатура не шлёт — BMP хватает).
        const char32_t cp = wc;
        if (cp < 0x80) { out += static_cast<char>(cp); }
        else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3f));
        } else {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (cp & 0x3f));
        }
    }
#else
    void (*prev_hup_)(int) = nullptr;
    void (*prev_quit_)(int) = nullptr;   // SIGQUIT — только POSIX
    void (*prev_winch_)(int) = nullptr;
#endif
};

} // namespace eye::detail::tui
