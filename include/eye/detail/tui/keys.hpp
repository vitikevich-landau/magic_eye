// ОКО МАГА / eye/detail/tui/keys.hpp — ДЕКОДЕР КЛАВИШ: байты → события.
//   Чистый конечный автомат над байтовым потоком, без единого системного
//   вызова: кормишь feed() байтами — забираешь события next(). Поэтому он
//   тестируется юнитами без терминала, а Windows-слой может синтезировать
//   те же VT-последовательности из ReadConsoleInput и пользоваться ОДНИМ
//   декодером с POSIX.
//
//   Понимает: CSI (\x1b[A…, \x1b[5~…), SS3 (\x1bOA… — старые xterm),
//   Enter/Tab/Backspace, UTF-8 любой длины (кириллица в поиске), одиночный
//   ESC (отличается от хвоста последовательности таймаутом — см.
//   flush_timeout). Неизвестные CSI молча съедаются целиком, чтобы их
//   хвост не рассыпался мусором по строке поиска.
#pragma once
#include <cstddef>
#include <cstdlib>   // std::atoi — номер CSI-параметра
#include <deque>
#include <string>

namespace eye::detail::tui {

enum class Key {
    none,
    up, down, left, right,
    pgup, pgdn, home, end,
    enter, backspace, tab, esc, del, f1,
    chr,       // печатаемый символ (поле ch) — включая управляющие Ctrl-байты
    resize,    // синтетическое: терминал изменил размер (шлёт слой term)
};

struct KeyEvent {
    Key      key = Key::none;
    char32_t ch  = 0;    // для Key::chr: кодовая точка (Ctrl-C = U+0003)
};

class KeyDecoder {
public:
    // Скормить порцию байтов (сколько прочитал read — хоть по одному).
    void feed(const char* data, std::size_t n) {
        buf_.append(data, n);
        parse();
    }

    // Забрать следующее готовое событие. false — событий пока нет.
    bool next(KeyEvent& out) {
        if (events_.empty()) return false;
        out = events_.front();
        events_.pop_front();
        return true;
    }

    bool pending() const { return !buf_.empty(); }

    // Истёк таймаут ожидания хвоста: незавершённая последовательность больше
    // не придёт. Одиночный ESC становится клавишей Esc, остаток буфера
    // перечитывается как обычные байты.
    void flush_timeout() {
        if (buf_.empty()) return;
        if (buf_[0] == '\x1b') {
            events_.push_back({Key::esc, 0});
            buf_.erase(0, 1);
        }
        parse();
        // Всё ещё висит незавершённый UTF-8-огрызок? Дальше он не соберётся.
        if (!buf_.empty() && buf_[0] != '\x1b') buf_.clear();
    }

private:
    std::string buf_;
    std::deque<KeyEvent> events_;

    void emit(Key k, char32_t c = 0) { events_.push_back({k, c}); }

    // Снять с начала буфера одно событие. Возвращает съеденную длину;
    // 0 — байтов не хватает (ждём следующего feed / flush_timeout).
    std::size_t take_one() {
        const unsigned char b0 = static_cast<unsigned char>(buf_[0]);

        if (b0 == 0x1b) return take_escape();

        if (b0 == '\r' || b0 == '\n') { emit(Key::enter);     return 1; }
        if (b0 == '\t')               { emit(Key::tab);       return 1; }
        if (b0 == 0x7f || b0 == 0x08) { emit(Key::backspace); return 1; }
        if (b0 < 0x20) { emit(Key::chr, b0); return 1; }   // Ctrl-байты — как есть

        if (b0 < 0x80) { emit(Key::chr, b0); return 1; }   // печатаемый ASCII

        return take_utf8();
    }

    std::size_t take_escape() {
        if (buf_.size() < 2) return 0;                     // ждём хвост или таймаут
        const char b1 = buf_[1];

        if (b1 == '[') return take_csi();
        if (b1 == 'O') return take_ss3();

        // ESC + произвольный байт (Alt+клавиша): отдаём Esc, байт пойдёт
        // следующим событием.
        emit(Key::esc);
        return 1;
    }

    std::size_t take_csi() {
        // \x1b [ <параметры/промежуточные 0x20..0x3f>* <финальный 0x40..0x7e>
        std::size_t j = 2;
        while (j < buf_.size()) {
            const unsigned char c = static_cast<unsigned char>(buf_[j]);
            if (c >= 0x40 && c <= 0x7e) break;             // финальный байт
            if (c < 0x20 || c > 0x3f) {                    // мусор — не CSI
                emit(Key::esc);
                return 1;
            }
            ++j;
        }
        if (j >= buf_.size()) return 0;                    // хвост ещё едет

        const char fin = buf_[j];
        const std::string params = buf_.substr(2, j - 2);
        switch (fin) {
            case 'A': emit(Key::up);    break;
            case 'B': emit(Key::down);  break;
            case 'C': emit(Key::right); break;
            case 'D': emit(Key::left);  break;
            case 'H': emit(Key::home);  break;
            case 'F': emit(Key::end);   break;
            case 'Z': emit(Key::tab);   break;             // Shift-Tab — как Tab
            case '~': {
                const int p = params.empty() ? 0 : std::atoi(params.c_str());
                switch (p) {
                    case 1: case 7: emit(Key::home); break;
                    case 4: case 8: emit(Key::end);  break;
                    case 3:  emit(Key::del);  break;
                    case 5:  emit(Key::pgup); break;
                    case 6:  emit(Key::pgdn); break;
                    case 11: emit(Key::f1);   break;
                    default: break;                        // неизвестное — молчок
                }
                break;
            }
            default: break;                                // прочие CSI — съесть
        }
        return j + 1;
    }

    std::size_t take_ss3() {
        if (buf_.size() < 3) return 0;
        switch (buf_[2]) {
            case 'A': emit(Key::up);    break;
            case 'B': emit(Key::down);  break;
            case 'C': emit(Key::right); break;
            case 'D': emit(Key::left);  break;
            case 'H': emit(Key::home);  break;
            case 'F': emit(Key::end);   break;
            case 'P': emit(Key::f1);    break;
            default: break;
        }
        return 3;
    }

    std::size_t take_utf8() {
        const unsigned char b0 = static_cast<unsigned char>(buf_[0]);
        std::size_t len = 0;
        char32_t cp = 0;
        if      ((b0 >> 5) == 0x6) { len = 2; cp = b0 & 0x1f; }
        else if ((b0 >> 4) == 0xe) { len = 3; cp = b0 & 0x0f; }
        else if ((b0 >> 3) == 0x1e) { len = 4; cp = b0 & 0x07; }
        else return 1;                                     // одинокий continuation
        if (buf_.size() < len) return 0;                   // ждём остаток символа
        for (std::size_t i = 1; i < len; ++i) {
            const unsigned char c = static_cast<unsigned char>(buf_[i]);
            if ((c & 0xc0) != 0x80) return 1;              // битый UTF-8 — съесть байт
            cp = (cp << 6) | (c & 0x3f);
        }
        emit(Key::chr, cp);
        return len;
    }

    void parse() {
        while (!buf_.empty()) {
            const std::size_t eaten = take_one();
            if (eaten == 0) return;                        // ждём байтов
            buf_.erase(0, eaten);
        }
    }
};

} // namespace eye::detail::tui
