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

// ─── глифы интерфейса: EYE_ASCII=1 — бедные шрифты без Unicode-стрелок ──────
// Без кэша: чтение env дёшево, а живое значение позволяет переключать режим
// между сессиями одного процесса (снапшот-тесты).
inline bool ascii_ui() {
    const std::string v = env_value("EYE_ASCII");
    return !v.empty() && v.front() == '1';
}
inline const char* gl(const char* unicode, const char* fallback) {
    return ascii_ui() ? fallback : unicode;
}
inline const char* gl_open()   { return gl("▾ ", "v "); }   // узел раскрыт
inline const char* gl_closed() { return gl("▸ ", "> "); }   // узел свёрнут
inline const char* gl_cursor() { return gl("►", ">"); }     // курсорная строка
inline const char* gl_spine()  { return gl("║", "|"); }     // граница зон
inline const char* gl_mark()   { return gl("◈", "*"); }     // маркер шапки/тоста

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
    bool help = false;             // ? / F1 — экран помощи
    bool search_edit = false;      // / — строка поиска забирает ввод
    std::string query;             // последний запрос (для n/N)
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
    s += n.can_expand || !it->kids.empty()
             ? (it->expanded ? gl_open() : gl_closed())
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
        std::string plain = gl_cursor() + tree_row_plain(it);
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
    l.col(clr::grey(), arrow ? (it->expanded ? gl_open() : gl_closed()) : "  ");
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

// ─── поиск по дереву (по раскрытым узлам) ────────────────────────────────────
inline std::string fold_ascii_case(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

inline bool row_matches(const TreeItem* it, const std::string& q) {
    if (q.empty()) return false;
    const std::string hay = fold_ascii_case(
        it->node.title + " " + it->node.type + " " + it->node.preview);
    return hay.find(fold_ascii_case(q)) != std::string::npos;
}

// Прыжок к совпадению. dir=+1 — вперёд, dir=-1 — назад; from — стартовая
// строка (включительно). true — нашли и перескочили.
inline bool search_jump(App& a, std::size_t from, long dir) {
    const auto& rows = a.nav.visible();
    if (rows.empty() || a.query.empty()) return false;
    const std::size_t n = rows.size();
    std::size_t idx = from % n;
    for (std::size_t step = 0; step < n; ++step) {
        if (row_matches(rows[idx], a.query)) {
            a.nav.set_cursor_index(idx);
            return true;
        }
        idx = dir > 0 ? (idx + 1) % n : (idx + n - 1) % n;
    }
    return false;
}

// char32_t → UTF-8 (обратная сторона декодера — для строки поиска).
inline void append_utf8(std::string& s, char32_t cp) {
    if (cp < 0x80) { s += static_cast<char>(cp); return; }
    if (cp < 0x800) {
        s += static_cast<char>(0xC0 | (cp >> 6));
        s += static_cast<char>(0x80 | (cp & 0x3f));
        return;
    }
    if (cp < 0x10000) {
        s += static_cast<char>(0xE0 | (cp >> 12));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
        s += static_cast<char>(0x80 | (cp & 0x3f));
        return;
    }
    s += static_cast<char>(0xF0 | (cp >> 18));
    s += static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
    s += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
    s += static_cast<char>(0x80 | (cp & 0x3f));
}

inline void pop_utf8(std::string& s) {
    while (!s.empty()) {
        const unsigned char c = static_cast<unsigned char>(s.back());
        s.pop_back();
        if ((c & 0xC0) != 0x80) return;   // снесли ведущий байт — точка целиком
    }
}

// ─── экран помощи (отдельная страница: любая клавиша закрывает) ─────────────
inline void draw_help(App& a, Canvas& canvas, const Layout& l) {
    (void)a;
    canvas.begin_frame(l.size);
    static const char* rows[] = {
        "КАРТА КЛАВИШ — странствие Ока",
        "",
        "  ↑/↓, k/j     курсор по дереву",
        "  →/Enter, l   раскрыть узел · перейти по указателю",
        "  ←, h         свернуть · подняться к родителю",
        "  g            перейти по указателю под курсором",
        "  ⌫, b         назад по истории переходов",
        "  Tab          фокус: дерево ↔ детали (широкий режим)",
        "  PgUp/PgDn    прокрутка фокусной зоны · Home/End — к краю",
        "  m/p/v/x      панель: память / паспорт / vtable / hex",
        "  f            развернуть длинные регионы (живой EYE_FULL)",
        "  e / c        раскрыть ветку рекурсивно / свернуть всё",
        "  1..9         прыжок к N-му корню галереи",
        "  /            поиск по раскрытым узлам · n/N — след./пред.",
        "  ?, F1        эта помощь",
        "  q, Esc       выход — терминал восстановится как был",
        "",
        "  контракт: объекты галереи живут, пока идёт странствие;",
        "  указатели разыменовываются только по вашей команде",
        "",
        "  окружение: EYE_INTERACTIVE=0 · EYE_SCRIPT=\"…\" · EYE_ASCII=1",
        "",
        "  любая клавиша закрывает помощь",
    };
    const std::size_t count = sizeof(rows) / sizeof(rows[0]);
    Line title;
    title.col(clr::violet(), std::string(gl_mark()) + " ");
    title.col(clr::gold(), rows[0]);
    canvas.blit(0, 2, StyledLine{title.s, title.w}, l.size.cols - 2);
    for (std::size_t i = 1; i < count && i < l.size.rows; ++i) {
        Line ln;
        ln.col(clr::grey(), rows[i]);
        canvas.blit(i, 2, StyledLine{ln.s, ln.w}, l.size.cols - 2);
    }
}

// ─── отрисовка кадра ─────────────────────────────────────────────────────────
inline void draw(App& a, Canvas& canvas, const Layout& l) {
    if (a.help) {
        draw_help(a, canvas, l);
        return;
    }
    canvas.begin_frame(l.size);
    if (l.size.rows < 3 || l.size.cols < 20) {
        canvas.blit(0, 0, std::string("окно слишком мало для странствия"),
                    l.size.cols);
        return;
    }

    // Шапка: путь до текущего узла.
    {
        Line h;
        h.col(clr::violet(), std::string(gl_mark()) + " ");
        h.col(clr::gold(), "ОКО МАГА · странствие");
        std::string crumbs = a.nav.breadcrumbs();
        if (ascii_ui())
            for (std::size_t at = crumbs.find("▸"); at != std::string::npos;
                 at = crumbs.find("▸"))
                crumbs.replace(at, std::string("▸").size(), ">");
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
                        StyledLine{std::string(clr::grey()) + gl_spine() +
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

    // Гид / тост / строка поиска.
    {
        Line g;
        if (a.search_edit) {
            g.col(clr::gold(), "поиск: ")
             .col(clr::green(), a.query)
             .col(clr::grey(), "▏  Enter — оставить · Esc — отмена · "
                               "ищет по раскрытым узлам");
        } else if (!a.toast.empty()) {
            g.col(clr::gold(), std::string(gl_mark()) + " " + a.toast);
        } else {
            const char* fmode = a.mode == DetailMode::memory ? "память"
                                : a.mode == DetailMode::passport ? "паспорт"
                                : a.mode == DetailMode::vtable ? "vtable"
                                                               : "hex";
            g.col(clr::grey(),
                  "↑↓ выбор · Enter раскрыть/перейти · ← свернуть · ⌫ назад"
                  " · Tab " +
                      std::string(a.focus_detail ? "[детали]" : "[дерево]") +
                      " · m/p/v/x [" + fmode + "] · / поиск · ? помощь · " +
                      "q выход");
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

// Переход по указателю под курсором (Enter на листе-указателе или g).
inline void act_follow(App& a) {
    const NavSession::FollowOutcome out = a.nav.follow_current();
    switch (out.what) {
        case NavSession::Follow::jumped:
            a.toast = "цикл ⟲ — узел уже в дереве, прыжок к нему";
            break;
        case NavSession::Follow::blocked:
            a.toast = out.reason;
            break;
        default:
            break;
    }
}

inline void act_expand(App& a) {
    const NavSession::Act act = a.nav.expand_current();
    if (act == NavSession::Act::leaf) {
        TreeItem* it = a.nav.current();
        if (it != nullptr &&
            (it->node.can_follow || !it->node.follow_block.empty()))
            act_follow(a);
    }
}

inline void act_back(App& a) {
    if (!a.nav.back()) a.toast = "истории переходов нет";
}

inline void dispatch(App& a, const KeyEvent& e, const Layout& l) {
    a.toast.clear();

    // Экран помощи: любая клавиша закрывает.
    if (a.help) {
        a.help = false;
        return;
    }

    // Строка поиска забирает ввод целиком; прыжок — живой, по мере набора.
    if (a.search_edit) {
        switch (e.key) {
            case Key::enter:
                a.search_edit = false;
                if (!a.query.empty() && !search_jump(a, a.nav.cursor(), 1))
                    a.toast = "не найдено: " + a.query;
                break;
            case Key::esc:
                a.search_edit = false;
                a.query.clear();
                break;
            case Key::backspace:
                pop_utf8(a.query);
                if (!a.query.empty()) search_jump(a, 0, 1);
                break;
            case Key::chr:
                if (e.ch >= 0x20) {
                    append_utf8(a.query, e.ch);
                    search_jump(a, 0, 1);
                }
                break;
            default:
                break;   // стрелки и прочее в режиме набора игнорируем
        }
        return;
    }

    switch (e.key) {
        case Key::f1:
            a.help = true;
            return;
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
            act_back(a);
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
                case U'g': act_follow(a); break;
                case U'b': act_back(a); break;
                case U'e':
                    if (a.nav.expand_current_rec())
                        a.toast = "раскрыто до лимита (6 уровней / 500 узлов)";
                    break;
                case U'c': a.nav.collapse_all(); break;
                case U'?': a.help = true; break;
                case U'/':
                    a.search_edit = true;
                    a.query.clear();
                    break;
                case U'n':
                    if (a.query.empty()) a.toast = "поиска ещё не было: /";
                    else if (!search_jump(a, a.nav.cursor() + 1, 1))
                        a.toast = "не найдено: " + a.query;
                    break;
                case U'N':
                    if (a.query.empty()) a.toast = "поиска ещё не было: /";
                    else if (!search_jump(
                                 a,
                                 a.nav.cursor() == 0
                                     ? (a.nav.visible().empty()
                                            ? 0
                                            : a.nav.visible().size() - 1)
                                     : a.nav.cursor() - 1,
                                 -1))
                        a.toast = "не найдено: " + a.query;
                    break;
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
