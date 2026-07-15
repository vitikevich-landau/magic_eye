// Ручная проба ядра TUI (в ctest НЕ входит): рамка, стрелки, resize, выход.
//   cmake --build build && ./build/tui_demo
// Проверить: стрелки двигают маркер, resize перестраивает рамку, q/Esc/Ctrl-C
// выходят, а терминал после выхода как был (история прокрутки цела).
#include <eye/detail/tui/canvas.hpp>
#include <eye/detail/tui/keys.hpp>
#include <eye/detail/tui/term.hpp>

#include <iostream>
#include <string>

namespace tui = eye::detail::tui;
using eye::detail::StyledLine;

int main() {
    if (!tui::TermSession::interactive_capable()) {
        std::cout << "не терминал (или EYE_INTERACTIVE=0) — TUI недоступен\n";
        return 0;
    }
    tui::TermSession term;
    tui::KeyDecoder keys;
    tui::Canvas canvas;

    std::size_t x = 2, y = 2;
    for (bool alive = true; alive;) {
        const tui::TermSize size = term.size();
        canvas.begin_frame(size);
        canvas.blit(0, 0,
                    std::string("tui_demo — стрелки двигают ◈, q выходит "
                                "(окно ") +
                        std::to_string(size.cols) + "×" +
                        std::to_string(size.rows) + ")",
                    size.cols);
        if (y >= size.rows) y = size.rows ? size.rows - 1 : 0;
        if (x >= size.cols) x = size.cols ? size.cols - 1 : 0;
        canvas.blit(y, x, std::string("◈"), 2);
        term.write(canvas.end_frame());

        const bool got = term.pump(
            [&](const char* d, std::size_t n) { keys.feed(d, n); }, 100);
        if (!got) {
            keys.flush_timeout();
            tui::TermSize now;
            if (term.take_resize(now)) continue;   // просто перерисуемся
        }
        tui::KeyEvent e;
        while (keys.next(e)) {
            switch (e.key) {
                case tui::Key::up:    if (y > 1) --y; break;
                case tui::Key::down:  ++y; break;
                case tui::Key::left:  if (x > 0) --x; break;
                case tui::Key::right: ++x; break;
                case tui::Key::esc:   alive = false; break;
                case tui::Key::chr:
                    if (e.ch == U'q' || e.ch == 3) alive = false;
                    break;
                default: break;
            }
        }
    }
    return 0;
}
