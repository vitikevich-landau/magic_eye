// ОКО МАГА / eye/detail/tui/app.hpp — ПРИЛОЖЕНИЕ странствия: зоны, цикл, клавиши.
//   Склейка навигации (nav/) с ядром TUI (term/keys/canvas): раскладка
//   «дерево ║ детали», статус-гид, диспетчер клавиш. Три режима запуска:
//     • живой TUI (терминал);
//     • EYE_SCRIPT="down down enter q" — исполнить клавиши и печатать кадры
//       в stdout (снапшот-тесты и отладка «что нарисовалось»);
//     • не-TTY без скрипта — честная деградация: print_static всех корней.
#pragma once
#include <cstddef>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "../geometry.hpp"
#include "../nav/nav_build.hpp"
#include "../nav/nav_session.hpp"
#include "../palette.hpp"
#include "../surface.hpp"
#include "../text.hpp"
#include "canvas.hpp"
#include "keys.hpp"
#include "term.hpp"

namespace eye::detail::tui {

using nav::DetailMode;
using nav::NavNode;
using nav::NavSession;
using nav::NodeKind;
using nav::TreeItem;

// ─── раскладка экрана ────────────────────────────────────────────────────────
struct Layout {
    TermSize size;
    bool wide = false;             // дерево ║ детали в два столбца
    std::size_t tree_x = 0, tree_w = 0;
    std::size_t det_x = 0, det_w = 0;
    std::size_t body_y = 1;        // первая строка зон (после шапки)
    std::size_t body_h = 0;        // высота зон
    std::size_t status_y = 0;      // строка гида
};

inline Layout compute_layout(TermSize s) {
    Layout l;
    l.size = s;
    l.wide = s.cols >= 100;
    l.body_h = s.rows > 2 ? s.rows - 2 : 1;
    l.status_y = s.rows > 0 ? s.rows - 1 : 0;
    if (l.wide) {
        l.tree_w = std::max<std::size_t>(38, s.cols * 2 / 5);
        l.det_x = l.tree_w + 1;                    // 1 колонка — спайн ║
        l.det_w = s.cols - l.det_x;
    } else {
        l.tree_w = s.cols;
        l.det_x = 0;
        l.det_w = s.cols;
    }
    return l;
}

// ─── состояние приложения ────────────────────────────────────────────────────
struct App {
    NavSession nav;
    DetailMode mode = DetailMode::memory;
    bool focus_detail = false;     // Tab: куда идут PgUp/PgDn и ↑↓
    bool full = false;             // f: не сворачивать длинные регионы
    bool narrow_detail = false;    // узкий режим: детали на весь кадр
    bool alive = true;
    std::size_t tree_scroll = 0;
    std::size_t detail_scroll = 0;
    std::string toast;

    // Кэш панели деталей: перерисовываем только при смене узла/режима/ширины.
    Surface detail;
    const TreeItem* detail_item = nullptr;
    DetailMode detail_mode_key = DetailMode::memory;
    std::size_t detail_w_key = 0;
    bool detail_full_key = false;

    explicit App(std::vector<NavNode> roots) : nav(std::move(roots)) {
        const std::string full_env = env_value("EYE_FULL");
        full = !full_env.empty() && full_env.front() == '1';
    }
};

// ─── панель деталей ──────────────────────────────────────────────────────────
inline void ensure_detail(App& a, std::size_t width) {
    TreeItem* it = a.nav.current();
    if (it == a.detail_item && a.mode == a.detail_mode_key &&
        width == a.detail_w_key && a.full == a.detail_full_key)
        return;
    a.detail.clear();
    a.detail_scroll = 0;
    if (it != nullptr && it->node.detail) {
        Geo g = compute_geo(width, a.full);
        g.margin = 0;
        GeoScope scope(g);
        SurfaceScope sink(a.detail);
        it->node.detail(a.mode);
    }
    a.detail_item = it;
    a.detail_mode_key = a.mode;
    a.detail_w_key = width;
    a.detail_full_key = a.full;
}

// ─── строки дерева ───────────────────────────────────────────────────────────
inline const char* kind_color(NodeKind k) {
    switch (k) {
        case NodeKind::root:
        case NodeKind::object:    return clr::gold();
        case NodeKind::base:      return clr::cyan();
        case NodeKind::vptr:      return clr::violet();
        case NodeKind::satellite: return clr::gold();
        case NodeKind::elems:     return clr::gold();
        case NodeKind::opaque:    return clr::grey();
        case NodeKind::more:      return clr::gold();
        case NodeKind::note:      return clr::dim();
        default:                  return clr::green();
    }
}

// Плоский текст строки дерева (для курсора-инверсии и поиска).
inline std::string tree_row_plain(const TreeItem* it) {
    const NavNode& n = it->node;
    std::string s(it->depth * 2, ' ');
    s += n.can_expand || !it->kids.empty() ? (it->expanded ? "▾ " : "▸ ")
                                           : "  ";
    s += n.title;
    if (!n.type.empty()) s += " : " + n.type;
    if (!n.preview.empty()) s += " " + n.preview;
    return s;
}

inline StyledLine tree_row_line(const TreeItem* it, bool cursor,
                                std::size_t width) {
    const NavNode& n = it->node;
    if (cursor) {
        // Курсорная строка — инверсия целиком; внутри без цветов, чтобы
        // вложенные reset'ы не разрывали фон. Стрелка ► дублирует курсор
        // там, где цветов нет (EYE_COLOR=0, снапшот-тесты).
        std::string plain = "►" + tree_row_plain(it);
        const std::size_t sw = vwidth(n.suffix);
        std::size_t pw = vwidth(plain);
        if (pw + sw + 1 > width) {                 // не влезает — режем текст
            plain = clip(plain, width > sw + 2 ? width - sw - 2 : 1);
            pw = vwidth(plain);
        }
        std::string s = plain;
        if (!n.suffix.empty() && pw + sw + 1 <= width)
            s += std::string(width - pw - sw, ' ') + n.suffix;
        else if (pw < width)
            s += std::string(width - pw, ' ');
        return {std::string(clr::code("\x1b[7m")) + s + clr::reset(),
                std::max(width, vwidth(s))};
    }
    Line l;
    l.sp(1 + it->depth * 2);          // 1 колонка — место курсорной стрелки ►
    const bool arrow = n.can_expand || !it->kids.empty();
    l.col(clr::grey(), arrow ? (it->expanded ? "▾ " : "▸ ") : "  ");
    l.col(kind_color(n.kind), n.title);
    if (!n.type.empty()) {
        l.col(clr::grey(), " : ");
        l.col(clr::cyan(), n.type);
    }
    if (!n.preview.empty()) l.col(clr::green(), " " + n.preview);
    const std::size_t sw = vwidth(n.suffix);
    if (!n.suffix.empty() && l.w + sw + 1 <= width) {
        l.to(width - sw);
        l.col(clr::dim(), n.suffix);
    }
    return {l.s, l.w};
}

// ─── отрисовка кадра ─────────────────────────────────────────────────────────
inline void draw(App& a, Canvas& canvas, const Layout& l) {
    canvas.begin_frame(l.size);
    if (l.size.rows < 3 || l.size.cols < 20) {
        canvas.blit(0, 0, std::string("окно слишком мало для странствия"),
                    l.size.cols);
        return;
    }

    // Шапка: путь до текущего узла.
    {
        Line h;
        h.col(clr::violet(), "◈ ");
        h.col(clr::gold(), "ОКО МАГА · странствие");
        const std::string crumbs = a.nav.breadcrumbs();
        if (!crumbs.empty()) {
            h.col(clr::grey(), " ── ");
            h.col(clr::cyan(), crumbs);
        }
        canvas.blit(0, 0, StyledLine{h.s, h.w}, l.size.cols);
    }

    const bool show_tree = !a.narrow_detail || l.wide;
    const bool show_detail = l.wide || a.narrow_detail;
    const std::size_t det_w = a.narrow_detail && !l.wide ? l.size.cols : l.det_w;

    // Дерево (окно прокрутки держит курсор в кадре).
    if (show_tree) {
        const auto& rows = a.nav.visible();
        if (a.nav.cursor() < a.tree_scroll) a.tree_scroll = a.nav.cursor();
        if (a.nav.cursor() >= a.tree_scroll + l.body_h)
            a.tree_scroll = a.nav.cursor() - l.body_h + 1;
        if (a.tree_scroll >= rows.size())
            a.tree_scroll = rows.empty() ? 0 : rows.size() - 1;
        for (std::size_t i = 0; i < l.body_h; ++i) {
            const std::size_t idx = a.tree_scroll + i;
            if (idx >= rows.size()) break;
            const bool cur = idx == a.nav.cursor();
            canvas.blit(l.body_y + i, l.tree_x,
                        tree_row_line(rows[idx], cur, l.tree_w),
                        l.tree_w);
        }
    }

    // Спайн между зонами.
    if (l.wide)
        for (std::size_t i = 0; i < l.body_h; ++i)
            canvas.blit(l.body_y + i, l.tree_w,
                        StyledLine{std::string(clr::grey()) + "║" +
                                       clr::reset(),
                                   1},
                        1);

    // Детали выбранного узла.
    if (show_detail) {
        ensure_detail(a, det_w);
        const std::size_t det_x = a.narrow_detail && !l.wide ? 0 : l.det_x;
        const auto& lines = a.detail.lines;
        if (a.detail_scroll >= lines.size())
            a.detail_scroll = lines.empty() ? 0 : lines.size() - 1;
        for (std::size_t i = 0; i < l.body_h; ++i) {
            const std::size_t idx = a.detail_scroll + i;
            if (idx >= lines.size()) break;
            canvas.blit(l.body_y + i, det_x, lines[idx], det_w);
        }
    }

    // Гид / тост.
    {
        Line g;
        if (!a.toast.empty()) {
            g.col(clr::gold(), "◈ " + a.toast);
        } else {
            const char* fmode = a.mode == DetailMode::memory ? "память"
                                : a.mode == DetailMode::passport ? "паспорт"
                                : a.mode == DetailMode::vtable ? "vtable"
                                                               : "hex";
            g.col(clr::grey(),
                  "↑↓ выбор · →/Enter раскрыть · ← свернуть · Tab фокус " +
                      std::string(a.focus_detail ? "[детали]" : "[дерево]") +
                      " · m/p/v/x панель [" + fmode + "] · f развернуть · " +
                      "1.." + std::to_string(a.nav.root_count()) +
                      " корни · q выход");
        }
        canvas.blit(l.status_y, 0, StyledLine{g.s, g.w}, l.size.cols);
    }
}

// ─── диспетчер клавиш ────────────────────────────────────────────────────────
inline void scroll_detail(App& a, long delta) {
    long s = static_cast<long>(a.detail_scroll) + delta;
    const long max_s = a.detail.lines.empty()
                           ? 0
                           : static_cast<long>(a.detail.lines.size()) - 1;
    if (s < 0) s = 0;
    if (s > max_s) s = max_s;
    a.detail_scroll = static_cast<std::size_t>(s);
}

inline void act_expand(App& a) {
    const NavSession::Act act = a.nav.expand_current();
    if (act == NavSession::Act::leaf) {
        TreeItem* it = a.nav.current();
        if (it != nullptr && it->node.can_follow) {
            // Переходы по указателям подключаются этапом M-D.
            a.toast = "переход: скоро";
        } else if (it != nullptr && !it->node.follow_block.empty()) {
            a.toast = it->node.follow_block;
        }
    }
}

inline void dispatch(App& a, const KeyEvent& e, const Layout& l) {
    a.toast.clear();
    switch (e.key) {
        case Key::up:
            if (a.focus_detail || (a.narrow_detail && !l.wide))
                scroll_detail(a, -1);
            else
                a.nav.move(-1);
            break;
        case Key::down:
            if (a.focus_detail || (a.narrow_detail && !l.wide))
                scroll_detail(a, 1);
            else
                a.nav.move(1);
            break;
        case Key::pgup:
            if (a.focus_detail || (a.narrow_detail && !l.wide))
                scroll_detail(a, -static_cast<long>(l.body_h));
            else
                a.nav.move(-static_cast<long>(l.body_h));
            break;
        case Key::pgdn:
            if (a.focus_detail || (a.narrow_detail && !l.wide))
                scroll_detail(a, static_cast<long>(l.body_h));
            else
                a.nav.move(static_cast<long>(l.body_h));
            break;
        case Key::home:
            if (a.focus_detail) a.detail_scroll = 0;
            else a.nav.cursor_home();
            break;
        case Key::end:
            if (a.focus_detail) scroll_detail(a, 1u << 20);
            else a.nav.cursor_end();
            break;
        case Key::right:
        case Key::enter: {
            TreeItem* it = a.nav.current();
            const bool leafish =
                it != nullptr && !it->node.can_expand &&
                it->node.kind != NodeKind::more;
            if (!l.wide && !a.narrow_detail && leafish) {
                a.narrow_detail = true;    // узкий режим: детали на весь кадр
                break;
            }
            act_expand(a);
            break;
        }
        case Key::left:
            if (a.narrow_detail && !l.wide) { a.narrow_detail = false; break; }
            if (a.focus_detail) { a.focus_detail = false; break; }
            a.nav.collapse_current();
            break;
        case Key::backspace:
            a.nav.collapse_current();      // история переходов появится в M-D
            break;
        case Key::tab:
            a.focus_detail = !a.focus_detail;
            break;
        case Key::esc:
            if (a.narrow_detail && !l.wide) a.narrow_detail = false;
            else a.alive = false;
            break;
        case Key::chr:
            switch (e.ch) {
                case U'q': case U'Q': case 3: a.alive = false; break;
                case U'k': a.nav.move(-1); break;
                case U'j': a.nav.move(1); break;
                case U'h': a.nav.collapse_current(); break;
                case U'l': act_expand(a); break;
                case U'm': a.mode = DetailMode::memory; break;
                case U'p': a.mode = DetailMode::passport; break;
                case U'v': a.mode = DetailMode::vtable; break;
                case U'x': a.mode = DetailMode::hex; break;
                case U'f': a.full = !a.full; break;
                case U'b': a.nav.collapse_current(); break;
                case U'e':
                    if (a.nav.expand_current_rec())
                        a.toast = "раскрыто до лимита (6 уровней / 500 узлов)";
                    break;
                case U'c': a.nav.collapse_all(); break;
                default:
                    if (e.ch >= U'1' && e.ch <= U'9')
                        a.nav.jump_to_root(static_cast<std::size_t>(e.ch - U'1'));
                    break;
            }
            break;
        default:
            break;
    }
}

// ─── три режима запуска ──────────────────────────────────────────────────────
inline std::vector<std::string> script_tokens() {
    std::istringstream in(env_value("EYE_SCRIPT"));
    std::vector<std::string> tokens;
    std::string t;
    while (in >> t) tokens.push_back(t);
    return tokens;
}

inline KeyEvent token_to_key(const std::string& t) {
    if (t == "up") return {Key::up, 0};
    if (t == "down") return {Key::down, 0};
    if (t == "left") return {Key::left, 0};
    if (t == "right") return {Key::right, 0};
    if (t == "enter") return {Key::enter, 0};
    if (t == "esc") return {Key::esc, 0};
    if (t == "tab") return {Key::tab, 0};
    if (t == "pgup") return {Key::pgup, 0};
    if (t == "pgdn") return {Key::pgdn, 0};
    if (t == "home") return {Key::home, 0};
    if (t == "end") return {Key::end, 0};
    if (t == "backspace") return {Key::backspace, 0};
    KeyDecoder d;                          // одиночный символ (в т.ч. UTF-8)
    d.feed(t.data(), t.size());
    d.flush_timeout();
    KeyEvent e;
    if (d.next(e)) return e;
    return {Key::none, 0};
}

// Скриптовый прогон: фиксированный размер из EYE_WIDTH/EYE_HEIGHT, клавиши из
// EYE_SCRIPT, каждый кадр печатается в stdout с разделителем.
inline void run_scripted(App& a, const std::vector<std::string>& tokens) {
    const auto dim = [](const char* name, std::size_t fallback) {
        const std::string v = env_value(name);
        if (!v.empty())
            if (const int n = std::atoi(v.c_str()); n > 0)
                return static_cast<std::size_t>(n);
        return fallback;
    };
    const TermSize size{dim("EYE_WIDTH", 126), dim("EYE_HEIGHT", 40)};
    Canvas canvas;
    const Layout l = compute_layout(size);
    std::size_t frame_no = 0;
    const auto print_frame = [&] {
        draw(a, canvas, l);
        std::cout << "── frame " << frame_no++ << " ──\n";
        for (const std::string& row : canvas.rows()) std::cout << row << '\n';
    };
    print_frame();
    for (const std::string& t : tokens) {
        if (!a.alive) break;
        dispatch(a, token_to_key(t), l);
        print_frame();
    }
}

// Живой TUI: цикл «нарисовать → подождать клавишу → применить».
inline void run_live(App& a) {
    TermSession term;
    KeyDecoder keys;
    Canvas canvas;
    while (a.alive) {
        const Layout l = compute_layout(term.size());
        draw(a, canvas, l);
        term.write(canvas.end_frame());
        const bool got = term.pump(
            [&](const char* d, std::size_t n) { keys.feed(d, n); }, 100);
        if (!got) {
            keys.flush_timeout();
            TermSize now;
            if (term.take_resize(now)) continue;
        }
        KeyEvent e;
        while (a.alive && keys.next(e))
            dispatch(a, e, compute_layout(term.size()));
    }
}

// Точка входа галереи. Правила деградации — в шапке файла.
inline void run_gallery(std::vector<NavNode> roots) {
    const std::vector<std::string> tokens = script_tokens();
    if (!tokens.empty()) {
        App a(std::move(roots));
        run_scripted(a, tokens);
        return;
    }
    if (!TermSession::interactive_capable()) {
        for (NavNode& r : roots)
            if (r.print_static) r.print_static();
        return;
    }
    App a(std::move(roots));
    run_live(a);
}

} // namespace eye::detail::tui
