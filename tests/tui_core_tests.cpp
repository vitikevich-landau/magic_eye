// Юнит-тесты ядра TUI: декодер клавиш (байты → события) и канва (blit/дифф).
// Ни одного системного вызова терминала — всё проверяется как чистые функции.
#include <eye/detail/tui/canvas.hpp>
#include <eye/detail/tui/keys.hpp>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace tui = eye::detail::tui;

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

std::vector<tui::KeyEvent> decode(const std::string& bytes,
                                  bool timeout_after = false) {
    tui::KeyDecoder d;
    d.feed(bytes.data(), bytes.size());
    if (timeout_after) d.flush_timeout();
    std::vector<tui::KeyEvent> events;
    tui::KeyEvent e;
    while (d.next(e)) events.push_back(e);
    return events;
}

bool keys_equal(const std::vector<tui::KeyEvent>& got,
                const std::vector<tui::Key>& want) {
    if (got.size() != want.size()) return false;
    for (std::size_t i = 0; i < got.size(); ++i)
        if (got[i].key != want[i]) return false;
    return true;
}

} // namespace

int main() {
    // TUI живёт с включёнными цветами (клип обязан закрывать их reset'ом) —
    // форсируем, чтобы тест не зависел от isatty среды запуска.
    setenv("EYE_COLOR", "1", 1);
    bool ok = true;

    // ── декодер: базовые клавиши ────────────────────────────────────────────
    ok &= expect(keys_equal(decode("\x1b[A\x1b[B\x1b[C\x1b[D"),
                            {tui::Key::up, tui::Key::down, tui::Key::right,
                             tui::Key::left}),
                 "CSI arrows are not decoded");
    ok &= expect(keys_equal(decode("\x1bOA\x1bOB\x1bOP"),
                            {tui::Key::up, tui::Key::down, tui::Key::f1}),
                 "SS3 sequences are not decoded");
    ok &= expect(keys_equal(decode("\x1b[5~\x1b[6~\x1b[H\x1b[F\x1b[1~\x1b[4~"),
                            {tui::Key::pgup, tui::Key::pgdn, tui::Key::home,
                             tui::Key::end, tui::Key::home, tui::Key::end}),
                 "PgUp/PgDn/Home/End are not decoded");
    ok &= expect(keys_equal(decode("\r\n\t\x7f\x08"),
                            {tui::Key::enter, tui::Key::enter, tui::Key::tab,
                             tui::Key::backspace, tui::Key::backspace}),
                 "enter/tab/backspace are not decoded");

    // ── ASCII и Ctrl-байты приходят как chr с кодовой точкой ────────────────
    {
        const auto events = decode("q\x03");
        ok &= expect(events.size() == 2 && events[0].key == tui::Key::chr &&
                         events[0].ch == U'q' &&
                         events[1].key == tui::Key::chr && events[1].ch == 3,
                     "ASCII/Ctrl-C bytes are not chr events");
    }

    // ── UTF-8: кириллица целиком и по байту ─────────────────────────────────
    {
        const auto events = decode("ф");
        ok &= expect(events.size() == 1 && events[0].key == tui::Key::chr &&
                         events[0].ch == U'ф',
                     "whole UTF-8 codepoint is not decoded");
    }
    {
        tui::KeyDecoder d;
        const std::string s = "ю";
        for (char c : s) d.feed(&c, 1);          // по одному байту, как read(2)
        tui::KeyEvent e;
        ok &= expect(d.next(e) && e.key == tui::Key::chr && e.ch == U'ю',
                     "split UTF-8 codepoint is not reassembled");
        ok &= expect(!d.next(e), "split UTF-8 produced extra events");
    }

    // ── ESC-последовательность, разрезанная между read'ами ──────────────────
    {
        tui::KeyDecoder d;
        d.feed("\x1b", 1);
        tui::KeyEvent e;
        ok &= expect(!d.next(e), "lone ESC fired before its tail could arrive");
        d.feed("[", 1);
        d.feed("A", 1);
        ok &= expect(d.next(e) && e.key == tui::Key::up,
                     "split CSI sequence is not reassembled");
    }

    // ── одиночный ESC отличается от хвоста таймаутом ────────────────────────
    ok &= expect(keys_equal(decode("\x1b", true), {tui::Key::esc}),
                 "lone ESC after timeout is not Esc");
    ok &= expect(keys_equal(decode("\x1b\x1b[A", true),
                            {tui::Key::esc, tui::Key::up}),
                 "ESC followed by CSI lost one of the events");

    // ── мусорные CSI съедаются молча, не рассыпаясь текстом ─────────────────
    {
        const auto events = decode("\x1b[38;5;245mq");
        ok &= expect(events.size() == 1 && events[0].key == tui::Key::chr &&
                         events[0].ch == U'q',
                     "unknown CSI leaked garbage events");
    }

    // ── Shift-Tab → Tab; Delete → del ───────────────────────────────────────
    ok &= expect(keys_equal(decode("\x1b[Z\x1b[3~"),
                            {tui::Key::tab, tui::Key::del}),
                 "Shift-Tab/Delete are not decoded");

    // ═════ канва ═════════════════════════════════════════════════════════════
    {
        tui::Canvas c;
        c.begin_frame({20, 3});
        c.blit(0, 0, std::string("hello"), 20);
        c.blit(0, 10, std::string("world"), 20);
        c.blit(1, 2, std::string("рамка"), 3);      // клип по бюджету
        const std::string first = c.end_frame();
        ok &= expect(first.find("\x1b[2J") != std::string::npos,
                     "first frame is not a full repaint");
        ok &= expect(first.find("hello     world") != std::string::npos,
                     "blit did not pad the gap between columns");
        // Клип режет до бюджета и ставит «…» (между ними может быть reset).
        const std::size_t ra = first.find("ра");
        ok &= expect(ra != std::string::npos &&
                         first.find("рам") == std::string::npos &&
                         first.find("…", ra) != std::string::npos,
                     "clip did not truncate wide text to budget");

        // Тот же кадр → пустой дифф (только паркуем курсор? нет: без изменений
        // вывод пуст).
        c.begin_frame({20, 3});
        c.blit(0, 0, std::string("hello"), 20);
        c.blit(0, 10, std::string("world"), 20);
        c.blit(1, 2, std::string("рамка"), 3);
        ok &= expect(c.end_frame().empty(),
                     "identical frame produced non-empty diff");

        // Изменилась одна строка → перерисовывается только она.
        c.begin_frame({20, 3});
        c.blit(0, 0, std::string("hello"), 20);
        c.blit(0, 10, std::string("world"), 20);
        c.blit(1, 2, std::string("другое"), 20);
        const std::string diff = c.end_frame();
        ok &= expect(diff.find("\x1b[2;1H") != std::string::npos &&
                         diff.find("\x1b[1;1H") == std::string::npos,
                     "diff repainted unchanged rows");

        // Resize → полный кадр заново.
        c.begin_frame({30, 3});
        c.blit(0, 0, std::string("hello"), 30);
        ok &= expect(c.end_frame().find("\x1b[2J") != std::string::npos,
                     "resize did not force a full repaint");
    }

    // ── клип ANSI-строки закрывает цвет на границе ──────────────────────────
    {
        tui::Canvas c;
        c.begin_frame({10, 1});
        eye::detail::StyledLine styled{
            std::string("\x1b[32m") + "greengreen" + "\x1b[0m", 10};
        c.blit(0, 0, styled, 4);
        const std::string frame = c.end_frame();
        const std::size_t cut = frame.find("gre");
        ok &= expect(cut != std::string::npos &&
                         frame.find("\x1b[0m", cut) != std::string::npos,
                     "ANSI clip did not close the color");
    }

    if (ok) std::cout << "tui_core_tests: OK\n";
    return ok ? 0 : 1;
}
