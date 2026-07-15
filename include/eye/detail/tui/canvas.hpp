// ОКО МАГА / eye/detail/tui/canvas.hpp — КАНВА: экранный буфер + дифф-вывод.
//   Кадр собирается построчно (blit кладёт готовые ANSI-строки в позиции),
//   end_frame() отдаёт МИНИМАЛЬНЫЙ вывод: только изменившиеся с прошлого
//   кадра строки, каждая с позиционированием курсора и очисткой хвоста.
//   Так перерисовка не мигает и не гонит весь экран через терминал.
//
//   Правило blit: в пределах строки — слева направо (col не убывает между
//   вызовами). Это осознанное упрощение: виджеты и так рисуют строку слева
//   направо, а канве не приходится резать уже вставленные ANSI-сегменты.
#pragma once
#include <cstddef>
#include <string>
#include <utility>
#include <vector>
#include "../surface.hpp"   // StyledLine
#include "../text.hpp"      // clip_ansi, vwidth_ansi

namespace eye::detail::tui {

struct TermSize {
    std::size_t cols = 0, rows = 0;
    bool operator==(const TermSize&) const = default;
};

class Canvas {
public:
    // Начать кадр. Смена размера сбрасывает память прошлого кадра:
    // терминал сам перетасовал содержимое — диффу верить нельзя.
    void begin_frame(TermSize s) {
        if (s != size_) {
            size_ = s;
            prev_.clear();
            full_repaint_ = true;
        }
        cur_.assign(size_.rows, {});
        curw_.assign(size_.rows, 0);
    }

    TermSize size() const { return size_; }

    // Прямой доступ к строкам ПОСЛЕДНЕГО собранного кадра. КОНТРАКТ: строки
    // валидны с момента отрисовки и ДО СЛЕДУЮЩЕГО begin_frame — end_frame их
    // не трогает (prev_ получает копию, см. ниже). На этом держатся скриптовый
    // режим EYE_SCRIPT и снимок экрана клавишей s (он читает кадр уже после
    // end_frame). Менять копию на move в end_frame нельзя.
    const std::vector<std::string>& rows() const { return cur_; }

    // Положить готовую ANSI-строку в (row, col), не шире max_w колонок.
    // Обрезка закрывает цвет (clip_ansi добавляет reset) — сосед не «красится».
    void blit(std::size_t row, std::size_t col, const StyledLine& line,
              std::size_t max_w) {
        if (row >= cur_.size() || col >= size_.cols) return;
        if (col < curw_[row]) return;               // против правила — молча нет
        const std::size_t budget = std::min(max_w, size_.cols - col);
        if (budget == 0) return;
        if (col > curw_[row]) {
            cur_[row].append(col - curw_[row], ' ');
            curw_[row] = col;
        }
        if (line.w > budget) {
            cur_[row] += clip_ansi(line.s, budget);
            curw_[row] += budget;
        } else {
            cur_[row] += line.s;
            curw_[row] += line.w;
        }
    }
    void blit(std::size_t row, std::size_t col, const std::string& s,
              std::size_t max_w) {
        blit(row, col, StyledLine{s, vwidth_ansi(s)}, max_w);
    }

    // Заполнить строку повторяющимся глифом (разделители зон).
    void hline(std::size_t row, std::size_t col, std::size_t n,
               const char* glyph, const char* color) {
        std::string s{color};
        for (std::size_t i = 0; i < n; ++i) s += glyph;
        s += clr::reset();
        blit(row, col, StyledLine{std::move(s), n}, n);
    }

    // Завершить кадр: вернуть ANSI-вывод (одним куском — под один write).
    // Меняем только строки, отличные от прошлого кадра; \x1b[K стирает хвост.
    std::string end_frame() {
        std::string out;
        const bool full = full_repaint_ || prev_.size() != cur_.size();
        if (full) out += "\x1b[2J";                 // очистить экран целиком
        for (std::size_t r = 0; r < cur_.size(); ++r) {
            if (!full && prev_[r] == cur_[r]) continue;
            out += "\x1b[" + std::to_string(r + 1) + ";1H";  // строка r, колонка 1
            out += "\x1b[K";
            out += cur_[r];
        }
        // Курсор — в угол, чтобы случайный вывод не пачкал середину экрана.
        if (!out.empty() || full)
            out += "\x1b[" + std::to_string(cur_.empty() ? 1 : cur_.size()) + ";1H";
        prev_ = cur_;      // именно КОПИЯ: cur_ обязан пережить end_frame (rows())
        full_repaint_ = false;
        return out;
    }

private:
    TermSize size_;
    std::vector<std::string> cur_, prev_;
    std::vector<std::size_t> curw_;      // видимая ширина набранного в строке
    bool full_repaint_ = true;
};

} // namespace eye::detail::tui
