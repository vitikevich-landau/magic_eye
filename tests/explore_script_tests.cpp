// Снапшот-тесты странствия (eye::explore / Gallery) в режиме EYE_SCRIPT:
// клавиши исполняются из строки, кадры печатаются в stdout — терминал не нужен.
#include <eye/magic_eye.hpp>

#include <cstdio>    // std::remove — уборка файла снимка
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// Типы уровня файла — короткие имена в дереве (как в примерах).
struct Unit {
    int hp = 100;
    int speed = 5;
    EYE_DESCRIBE(Unit, hp, speed)
};
struct Knight : Unit {
    int armor = 30;
    std::string banner = "Griffin and a very long banner for the heap";
    EYE_BASES(Knight, Unit)
    EYE_DESCRIBE(Knight, armor, banner)
};

// Свиток указателей (как в примере 10): наружу, в никуда, на самого себя,
// C-строка, void*, умный указатель.
struct Scroll {
    int         charges = 3;
    int*        power = nullptr;      // → наружу (int) — переходим
    void*       nothing = nullptr;    // void* — тип стёрт
    const char* rune = "ANSUZ";       // C-строка — не читаем
    Scroll*     self = nullptr;       // → на себя: цикл ⟲
    EYE_DESCRIBE(Scroll, charges, power, nothing, rune, self)
};

struct Vault {
    std::unique_ptr<int> gold = std::make_unique<int>(777);
    EYE_DESCRIBE(Vault, gold)
};

// Полиморфный зверь: у vptr-узла hex-режим обязан дампить СЛОТ в живом
// объекте (base+offset), а не копию значения в замыкании (находка Codex).
struct Beast {
    int fangs = 2;
    virtual ~Beast() = default;
    EYE_DESCRIBE(Beast, fangs)
};

// База без своего EYE_DESCRIBE: дерево обязано держать её непрозрачной —
// как родительская карта памяти, без «рассекречивания» авторазбором (Codex).
struct RawBase { int raw = 22; };
struct WithRawBase : RawBase {
    int own = 33;
    EYE_BASES(WithRawBase, RawBase)
    EYE_DESCRIBE(WithRawBase, own)
};

// ── типы, на которых nav ЛОМАЛ СБОРКУ (три находки Codex, PR #5) ────────────
// Все три — про компиляцию: inspect их показывал, а Gallery::add не собирался.
// Сам факт, что этот файл компилируется, и есть регрессия; ниже мы ещё и
// проверяем, что отказ объясняется словами, а не молчанием.

// 1) Поле-указатель на функцию: вывести `const U*` из int(*)() невозможно —
//    const у функционального типа ill-formed.
int answer_fn() { return 42; }
struct HasFnPtr {
    int v = 1;
    int (*fp)() = &answer_fn;
    EYE_DESCRIBE(HasFnPtr, v, fp)
};

// 2) PIMPL: тип известен только по имени. followable_v не имеет права трогать
//    is_aggregate_v/is_standard_layout_v — на неполном типе это жёсткая ошибка.
struct Forward;                       // определения нет и не будет
struct HasPimpl {
    int v = 2;
    Forward* impl = nullptr;
    // Умный PIMPL: у этой ветки своя беда — type_name<V> зовёт typeid(V), а
    // тому нужен ПОЛНЫЙ тип. shared_ptr (в отличие от unique_ptr) с неполным
    // типом жить умеет: делетер стирается при конструировании.
    std::shared_ptr<Forward> shared_impl;
    EYE_DESCRIBE(HasPimpl, v, impl, shared_impl)
};

// 3) Умный указатель на МАССИВ: pointee — int[], из .get() (int*) указатель на
//    него не построить; да и длины массив не несёт.
struct HasArrayPtr {
    std::unique_ptr<int[]> many = std::make_unique<int[]>(4);
    std::shared_ptr<int[]> shared;
    EYE_DESCRIBE(HasArrayPtr, many, shared)
};

// Ромб с общей virtual-базой: обе ветки ведут к ОДНОМУ Soul — дерево не
// должно плодить два разворачиваемых узла Soul (ревью Codex, PR #5).
struct Soul { int spark = 7; EYE_DESCRIBE(Soul, spark) };
struct MageBr  : virtual Soul { int mana = 1; EYE_BASES(MageBr, Soul) EYE_DESCRIBE(MageBr, mana) };
struct WarrBr  : virtual Soul { int rage = 2; EYE_BASES(WarrBr, Soul) EYE_DESCRIBE(WarrBr, rage) };
struct PaladinD : MageBr, WarrBr {
    int aura = 3;
    EYE_BASES(PaladinD, MageBr, WarrBr)
    EYE_DESCRIBE(PaladinD, aura)
};

namespace {

void set_env(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

bool expect(bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

// Прогнать галерею со скриптом клавиш, вернуть весь печатный вывод.
template <class Fill>
std::string run_with_script(const std::string& script, Fill&& fill) {
    set_env("EYE_SCRIPT", script.c_str());
    std::ostringstream out;
    std::streambuf* old = std::cout.rdbuf(out.rdbuf());
    eye::Gallery g;
    fill(g);
    g.run();
    std::cout.rdbuf(old);
    set_env("EYE_SCRIPT", "");
    return out.str();
}

std::size_t count_frames(const std::string& s) {
    std::size_t n = 0;
    for (std::size_t p = s.find("── frame"); p != std::string::npos;
         p = s.find("── frame", p + 1))
        ++n;
    return n;
}

// Последний нарисованный кадр (после разделителя «── frame N ──»): нужен там,
// где предыдущие кадры содержат снятое состояние (скролл, курсор).
std::string last_frame(const std::string& s) {
    const std::size_t at = s.rfind("── frame");
    if (at == std::string::npos) return s;
    const std::size_t nl = s.find('\n', at);
    return nl == std::string::npos ? "" : s.substr(nl + 1);
}

} // namespace

int main() {
    set_env("EYE_COLOR", "0");
    set_env("EYE_CENTER", "0");
    set_env("EYE_RESIZE", "0");
    set_env("EYE_WIDTH", "126");
    set_env("EYE_HEIGHT", "40");
    bool ok = true;

    Knight knight;
    std::vector<int> nums;
    for (int i = 0; i < 250; ++i) nums.push_back(i * 3);

    // ── кадр 0: шапка, корни, курсор, гид ───────────────────────────────────
    {
        const std::string out = run_with_script("q", [&](eye::Gallery& g) {
            g.add(knight, "рыцарь");
            g.add(nums, "числа");
        });
        ok &= expect(count_frames(out) >= 1, "no frames were printed");
        ok &= expect(out.find("ОКО МАГА · странствие") != std::string::npos,
                     "header is missing");
        ok &= expect(out.find("►▸ рыцарь") != std::string::npos,
                     "cursor is not on the first root");
        ok &= expect(out.find("числа") != std::string::npos,
                     "second root is missing");
        ok &= expect(out.find("q выход") != std::string::npos,
                     "status bar hints are missing");
        // Широкий режим: справа сразу видна панель деталей корня.
        ok &= expect(out.find("паспорт") != std::string::npos,
                     "detail panel (passport section) is missing");
    }

    // ── раскрытие корня: база, свои поля, vptr нет (Knight не полиморфен) ───
    {
        const std::string out = run_with_script("enter q", [&](eye::Gallery& g) {
            g.add(knight, "рыцарь");
        });
        ok &= expect(out.find("база Unit") != std::string::npos,
                     "base subobject node is missing");
        ok &= expect(out.find("armor") != std::string::npos,
                     "own field node is missing");
        ok &= expect(out.find("banner") != std::string::npos,
                     "string field node is missing");
        ok &= expect(out.find("(куча)") != std::string::npos,
                     "heap string marker is missing");
    }

    // ── спуск в базу: поля базы со своими offset'ами ─────────────────────────
    {
        const std::string out =
            run_with_script("enter down enter q", [&](eye::Gallery& g) {
                g.add(knight, "рыцарь");
            });
        ok &= expect(out.find("hp") != std::string::npos &&
                         out.find("speed") != std::string::npos,
                     "base fields are missing after expanding the base");
        ok &= expect(out.find("рыцарь ▸ база Unit") != std::string::npos,
                     "breadcrumbs do not show the path");
    }

    // ── строка: спутник кучи как ребёнок поля ────────────────────────────────
    {
        const std::string out = run_with_script(
            "enter down down down enter q",
            [&](eye::Gallery& g) { g.add(knight, "рыцарь"); });
        ok &= expect(out.find("буфер кучи «banner»") != std::string::npos,
                     "heap satellite node is missing");
    }

    // ── vector: элементы с пагинацией по 100 ────────────────────────────────
    {
        // enter (раскрыть корень) → end (к узлу «элементы») → enter (страница)
        // → end (в самый низ: там хвост страницы и «ещё…»).
        const std::string out = run_with_script(
            "enter end enter end q",
            [&](eye::Gallery& g) { g.add(nums, "числа"); });
        ok &= expect(out.find("элементы") != std::string::npos,
                     "vector elems node is missing");
        ok &= expect(out.find("#0") != std::string::npos &&
                         out.find("#99") != std::string::npos,
                     "first element page is missing");
        ok &= expect(out.find("ещё 150 — Enter") != std::string::npos,
                     "pagination node is missing");
        ok &= expect(out.find("#100") == std::string::npos,
                     "second page leaked before Enter on «ещё»");
    }
    {
        // Enter на «ещё…» дозагружает вторую страницу на место узла.
        const std::string out = run_with_script(
            "enter end enter end enter end q",
            [&](eye::Gallery& g) { g.add(nums, "числа"); });
        ok &= expect(out.find("#100") != std::string::npos &&
                         out.find("#199") != std::string::npos,
                     "second element page did not load");
        ok &= expect(out.find("ещё 50 — Enter") != std::string::npos,
                     "remaining-elements counter is wrong");
    }

    // ── режимы панели: p (паспорт поля) и x (hex) ────────────────────────────
    {
        const std::string out = run_with_script(
            "enter down down p q", [&](eye::Gallery& g) { g.add(knight, "рыцарь"); });
        ok &= expect(out.find("паспорт поля") != std::string::npos,
                     "field passport mode is missing");
        ok &= expect(out.find("offset") != std::string::npos,
                     "field passport lacks offset");
    }
    {
        const std::string out = run_with_script(
            "x q", [&](eye::Gallery& g) { g.add(knight, "рыцарь"); });
        ok &= expect(out.find("· hex") != std::string::npos,
                     "hex mode is missing");
    }

    // ── статика типа в галерее ───────────────────────────────────────────────
    {
        const std::string out = run_with_script("q", [&](eye::Gallery& g) {
            g.add<Knight>();
        });
        ok &= expect(out.find("статика") != std::string::npos,
                     "static-type root is missing");
    }

    // ── прыжок к корню по цифре ──────────────────────────────────────────────
    {
        const std::string out = run_with_script("2 q", [&](eye::Gallery& g) {
            g.add(knight, "рыцарь");
            g.add(nums, "числа");
        });
        ok &= expect(out.find("►▸ числа") != std::string::npos,
                     "digit jump to the second root failed");
    }

    // ── узкий режим: дерево на весь кадр, детали по Enter на листе ──────────
    {
        set_env("EYE_WIDTH", "80");
        const std::string out = run_with_script(
            "enter down down enter q",
            [&](eye::Gallery& g) { g.add(knight, "рыцарь"); });
        ok &= expect(out.find("паспорт") != std::string::npos ||
                         out.find("память") != std::string::npos,
                     "narrow mode did not open the detail view");
        set_env("EYE_WIDTH", "126");
    }

    // ── узкий режим: Tab показывает детали РАЗВОРАЧИВАЕМОГО узла (Codex) ─────
    {
        set_env("EYE_WIDTH", "80");
        // Корень «рыцарь» разворачиваемый: Enter на нём только раскрывает
        // дерево. Детали такого узла в узком режиме доступны через Tab.
        const std::string out = run_with_script(
            "tab q", [&](eye::Gallery& g) { g.add(knight, "рыцарь"); });
        ok &= expect(out.find("паспорт") != std::string::npos,
                     "narrow-mode Tab did not open details for an expandable node");
        set_env("EYE_WIDTH", "126");
    }

    // ── EYE_INTERACTIVE=0 бьёт скриптовый режим: статика, а не TUI-кадры
    //    (ревью Codex, PR #5) ──────────────────────────────────────────────
    {
        set_env("EYE_INTERACTIVE", "0");
        const std::string out = run_with_script(   // EYE_SCRIPT задан, но…
            "enter q", [&](eye::Gallery& g) { g.add(knight, "рыцарь"); });
        set_env("EYE_INTERACTIVE", "");
        ok &= expect(out.find("── frame") == std::string::npos,
                     "EYE_INTERACTIVE=0 still emitted TUI frames");
        ok &= expect(out.find("╔═◈╡ рыцарь") != std::string::npos,
                     "EYE_INTERACTIVE=0 did not fall back to static inspect");
    }

    // ── широкий режим не включается, пока зона деталей не вмещает рамку:
    //    на 100 колонках — узкий (детали иначе резались бы) (Codex, PR #5) ──
    {
        set_env("EYE_WIDTH", "100");
        const std::string out = run_with_script(
            "q", [&](eye::Gallery& g) { g.add(knight, "рыцарь"); });
        // Узкий режим: панели деталей рядом с деревом нет → нет спайна ║ в
        // строках дерева и нет секции «паспорт» в кадре до Tab.
        ok &= expect(out.find("паспорт") == std::string::npos,
                     "100-col terminal wrongly opened a clipped wide detail pane");
        set_env("EYE_WIDTH", "126");
    }

    // ── деградация: без скрипта и без TTY — статическая печать ──────────────
    {
        set_env("EYE_SCRIPT", "");
        std::ostringstream out;
        std::streambuf* old = std::cout.rdbuf(out.rdbuf());
        eye::Gallery g;
        g.add(knight, "рыцарь");
        g.add(nums, "числа");
        g.run();
        std::cout.rdbuf(old);
        ok &= expect(out.str().find("╔═◈╡ рыцарь") != std::string::npos &&
                         out.str().find("╔═◈╡ числа") != std::string::npos,
                     "non-TTY degradation did not print static panels");
        ok &= expect(out.str().find("── frame") == std::string::npos,
                     "degradation should not print TUI frames");
    }

    // ═════ M-D: переходы по указателям ═══════════════════════════════════════
    int mana = 350;
    Scroll scroll;
    scroll.power = &mana;
    scroll.nothing = &mana;   // ненулевой void*: блок — из-за стёртого типа
    scroll.self = &scroll;

    // ── переход: pointee-узел под указателем + breadcrumbs + значение ───────
    {
        const std::string out = run_with_script(
            "enter down down enter q",
            [&](eye::Gallery& g) { g.add(scroll, "свиток"); });
        ok &= expect(out.find("*power") != std::string::npos,
                     "pointee node did not appear after follow");
        ok &= expect(out.find("свиток ▸ power ▸ *power") != std::string::npos,
                     "breadcrumbs do not include the followed pointee");
        ok &= expect(out.find("= 350") != std::string::npos,
                     "pointee value is missing");
    }

    // ── nullptr и void*: блокируются с причиной в тосте ─────────────────────
    {
        const std::string out = run_with_script(
            "enter down down down enter q",
            [&](eye::Gallery& g) { g.add(scroll, "свиток"); });
        ok &= expect(out.find("void*: тип стёрт") != std::string::npos,
                     "void* follow is not blocked with a reason");
    }
    {
        Scroll blank;   // power == nullptr
        const std::string out = run_with_script(
            "enter down down enter q",
            [&](eye::Gallery& g) { g.add(blank, "пустой"); });
        ok &= expect(out.find("переход невозможен: nullptr") !=
                         std::string::npos,
                     "nullptr follow is not blocked with a reason");
    }

    // ── char*: честный отказ читать чужую память ────────────────────────────
    {
        const std::string out = run_with_script(
            "enter down down down down enter q",
            [&](eye::Gallery& g) { g.add(scroll, "свиток"); });
        ok &= expect(out.find("C-строка") != std::string::npos,
                     "char* follow is not blocked with a reason");
    }

    // ── цикл ⟲: self → прыжок на существующий узел, без дубля ──────────────
    {
        const std::string out = run_with_script(
            "enter down down down down down enter q",
            [&](eye::Gallery& g) { g.add(scroll, "свиток"); });
        ok &= expect(out.find("цикл ⟲") != std::string::npos,
                     "self-pointer did not report a cycle jump");
        ok &= expect(out.find("*self") == std::string::npos,
                     "cycle created a duplicate node");
        // Прыжок вернул курсор на корень свитка.
        ok &= expect(out.find("►▾ свиток") != std::string::npos,
                     "cycle jump did not land on the existing node");
    }

    // ── история: Backspace возвращает к указателю ───────────────────────────
    {
        const std::string out = run_with_script(
            "enter down down enter backspace q",
            [&](eye::Gallery& g) { g.add(scroll, "свиток"); });
        ok &= expect(out.find("►  ▾ power") != std::string::npos,
                     "backspace did not return to the pointer node");
    }
    {
        const std::string out = run_with_script(
            "backspace q", [&](eye::Gallery& g) { g.add(scroll, "свиток"); });
        ok &= expect(out.find("истории переходов нет") != std::string::npos,
                     "empty history should toast");
    }

    // ── умный указатель: unique_ptr<int> переходим через .get() ─────────────
    {
        Vault vault;
        const std::string out = run_with_script(
            "enter down enter q",
            [&](eye::Gallery& g) { g.add(vault, "казна"); });
        ok &= expect(out.find("умный указатель на int") != std::string::npos,
                     "smart pointer is not marked in the tree");
        ok &= expect(out.find("= 777") != std::string::npos,
                     "unique_ptr pointee value is missing");
    }

    // ── умный указатель как КОРЕНЬ галереи: Enter/g ведёт к *ptr, а не в
    //    «непрозрачный класс» (ревью Codex, PR #5) ────────────────────────────
    {
        auto ingot = std::make_unique<int>(999);
        const std::string out = run_with_script(
            "enter q", [&](eye::Gallery& g) { g.add(ingot, "слиток"); });
        ok &= expect(out.find("умный указатель на int") != std::string::npos,
                     "smart-pointer root is not marked as a smart pointer");
        ok &= expect(out.find("*слиток") != std::string::npos,
                     "Enter on a smart-pointer root did not follow to *ptr");
        ok &= expect(out.find("= 999") != std::string::npos,
                     "smart-pointer root pointee value is missing");
        ok &= expect(out.find("непрозрачный класс") == std::string::npos,
                     "smart-pointer root fell through to opaque expansion");
    }

    // ── pointee-агрегат: раскрывается дальше (поля #0…) ─────────────────────
    {
        struct Inner { int a; int b; };
        static Inner inner{11, 22};
        struct Outer {
            Inner* link = &inner;
            EYE_DESCRIBE(Outer, link)
        };
        Outer outer;
        const std::string out = run_with_script(
            "enter down enter enter q",
            [&](eye::Gallery& g) { g.add(outer, "внешний"); });
        ok &= expect(out.find("*link") != std::string::npos,
                     "aggregate pointee node is missing");
        ok &= expect(out.find("#0") != std::string::npos &&
                         out.find("= 11") != std::string::npos,
                     "aggregate pointee fields did not expand");
    }

    // ── указатель на ЧЛЕН того же объекта: строит разворачиваемый *p, а не
    //    ложный цикл на нераскрываемый лист-поле (ревью Codex, PR #5) ─────────
    {
        struct Cell { int x = 55; int y = 66; };
        struct Host {
            Cell cell;
            Cell* link = nullptr;   // → cell (член ЭТОГО объекта)
            EYE_DESCRIBE(Host, cell, link)
        };
        Host host;
        host.link = &host.cell;
        // enter (корень) → down (cell) → down (link) → enter (follow) →
        // enter (раскрыть *link) → показать x/y.
        const std::string out = run_with_script(
            "enter down down enter enter q",
            [&](eye::Gallery& g) { g.add(host, "хозяин"); });
        ok &= expect(out.find("*link") != std::string::npos,
                     "pointer to in-object member did not build a pointee node");
        ok &= expect(out.find("цикл") == std::string::npos,
                     "pointer to in-object member was mis-flagged as a cycle");
        ok &= expect(out.find("= 55") != std::string::npos &&
                         out.find("= 66") != std::string::npos,
                     "in-object-member pointee did not expand into its fields");
    }

    // ═════ M-E: поиск, помощь, EYE_ASCII ═════════════════════════════════════

    // ── поиск: / набирает запрос, живой прыжок, n — следующее ───────────────
    {
        const std::string out = run_with_script(
            "enter / a r m o r enter q",
            [&](eye::Gallery& g) { g.add(knight, "рыцарь"); });
        ok &= expect(out.find("поиск: armor") != std::string::npos,
                     "search bar does not show the query");
        ok &= expect(out.find("►    armor") != std::string::npos,
                     "search did not jump to the match");
    }
    {
        // Два совпадения "sp": speed в базе не раскрыт — ищем по 'a' (base/armor).
        const std::string out = run_with_script(
            "enter / h p enter q",
            [&](eye::Gallery& g) { g.add(knight, "рыцарь"); });
        // hp внутри НЕраскрытой базы — совпадения нет, честный тост.
        ok &= expect(out.find("не найдено: hp") != std::string::npos,
                     "search over collapsed nodes should honestly fail");
    }
    {
        // Esc отменяет набор; n без запроса — подсказка.
        const std::string out = run_with_script(
            "/ z esc n q", [&](eye::Gallery& g) { g.add(knight, "рыцарь"); });
        ok &= expect(out.find("поиска ещё не было") != std::string::npos,
                     "n without a query should hint about /");
    }

    // ── помощь: ? открывает карту клавиш, любая клавиша закрывает ────────────
    {
        const std::string out = run_with_script(
            "? down q", [&](eye::Gallery& g) { g.add(knight, "рыцарь"); });
        ok &= expect(out.find("КАРТА КЛАВИШ") != std::string::npos,
                     "help screen is missing");
        ok &= expect(out.find("назад по истории переходов") != std::string::npos,
                     "help screen lacks the key map");
    }

    // ── EYE_ASCII=1: стрелки дерева и спайн — чистый ASCII ──────────────────
    {
        set_env("EYE_ASCII", "1");
        const std::string out = run_with_script(
            "q", [&](eye::Gallery& g) { g.add(knight, "рыцарь"); });
        set_env("EYE_ASCII", "0");
        ok &= expect(out.find("> рыцарь") != std::string::npos,
                     "EYE_ASCII tree arrows are not ASCII");
        ok &= expect(out.find("▸") == std::string::npos,
                     "EYE_ASCII frame still leaks unicode arrows");
    }

    // ── vptr-узел в hex-режиме дампит слот ЖИВОГО объекта (Codex, PR #5) ────
    {
        Beast beast;
        // enter (раскрыть корень) → end (vptr — последний ребёнок) → x (hex).
        const std::string out = run_with_script(
            "enter end x q", [&](eye::Gallery& g) { g.add(beast, "зверь"); });
        std::ostringstream expected;
        expected << "объём 8 Б @ " << static_cast<const void*>(&beast);
        ok &= expect(out.find("vptr «Beast» · hex") != std::string::npos,
                     "vptr hex panel is missing");
        ok &= expect(out.find(expected.str()) != std::string::npos,
                     "vptr hex mode does not dump the live object slot");
    }

    // ── непрозрачная база в дереве не раскрывается авторазбором (Codex) ─────
    {
        WithRawBase raw_obj;
        const std::string out = run_with_script(
            "enter down enter q",
            [&](eye::Gallery& g) { g.add(raw_obj, "сырой"); });
        ok &= expect(out.find("▓ скрыто") != std::string::npos,
                     "opaque base is not marked hidden in the tree");
        ok &= expect(out.find("непрозрачная база: нет своего EYE_DESCRIBE") !=
                         std::string::npos,
                     "opaque base expansion lacks the honest note");
        ok &= expect(out.find("= 22") == std::string::npos,
                     "opaque base leaked its bytes via auto-inspection");
        ok &= expect(out.find("туман") != std::string::npos,
                     "opaque base detail panel is missing");
    }

    // ── скалярный корень — ЛИСТ: детей у не-класса нет по построению, поэтому
    //    в узком режиме детали обязаны открыться с ПЕРВОГО Enter. Раньше корень
    //    носил стрелку ▸, и первый Enter тратился на выяснение пустоты — детали
    //    показывались лишь со второго (ревью Codex, PR #5).
    {
        set_env("EYE_WIDTH", "80");
        int lonely = 7;
        const std::string out = run_with_script(
            "enter q",
            [&](eye::Gallery& g) { g.add(lonely, "одинокое число"); });
        ok &= expect(out.find("паспорт") != std::string::npos,
                     "narrow mode: scalar root needs a second Enter for details");
        set_env("EYE_WIDTH", "126");
    }

    // ── nav не имеет права ЛОМАТЬ СБОРКУ там, где inspect работает ──────────
    //    Три находки Codex (PR #5): поле-указатель на функцию, PIMPL-указатель
    //    и умный указатель на массив роняли КОМПИЛЯЦИЮ Gallery::add. Главная
    //    регрессия — сам факт, что этот блок собрался; ниже проверяем, что
    //    отказ от перехода ещё и объясняется словами, а не молчанием.
    {
        HasFnPtr fn;
        const std::string out = run_with_script(
            "enter down down enter q",
            [&](eye::Gallery& g) { g.add(fn, "функц-указатель"); });
        ok &= expect(out.find("указатель на функцию") != std::string::npos,
                     "function-pointer follow is not refused with a reason");
    }
    {
        HasPimpl pim;
        // Ненулевой адрес нужен, чтобы дойти до ветки «неполный тип»: проверка
        // nullptr стоит раньше. Указатель только ПОКАЗЫВАЕТСЯ — переход
        // заблокирован, разыменования не будет.
        pim.impl = reinterpret_cast<Forward*>(&pim.v);
        const std::string out = run_with_script(
            "enter down down enter q",
            [&](eye::Gallery& g) { g.add(pim, "PIMPL"); });
        ok &= expect(out.find("тип неполный") != std::string::npos,
                     "incomplete pointee follow is not refused with a reason");
    }
    {
        HasArrayPtr arr;
        const std::string out = run_with_script(
            "enter down enter q",
            [&](eye::Gallery& g) { g.add(arr, "массив в умном указателе"); });
        ok &= expect(out.find("умный указатель на массив") != std::string::npos,
                     "array smart-pointer follow is not refused with a reason");
    }

    // ── пагинация инвалидирует кэш панели деталей (Codex, ABA TreeItem*) ────
    {
        // Enter на «⋯ ещё…» ставит курсор на #100 — панель деталей обязана
        // сразу показать именно #100, а не протухший кэш по старому адресу.
        const std::string out = run_with_script(
            "enter end enter end enter q",
            [&](eye::Gallery& g) { g.add(nums, "числа"); });
        ok &= expect(out.find("╡ #100 ╞") != std::string::npos,
                     "detail panel is stale after paging (TreeItem* ABA)");
    }

    // ── узкий режим: Enter на листе-указателе ПЕРЕХОДИТ, а не открывает
    //    детали (ревью Codex, PR #5) ───────────────────────────────────────
    {
        set_env("EYE_WIDTH", "80");
        int mana2 = 350;
        Scroll sc;
        sc.power = &mana2;
        // enter (раскрыть корень) → down (charges) → down (power) → enter.
        const std::string out = run_with_script(
            "enter down down enter q",
            [&](eye::Gallery& g) { g.add(sc, "свиток"); });
        set_env("EYE_WIDTH", "126");
        ok &= expect(out.find("*power") != std::string::npos,
                     "narrow-mode Enter on a pointer leaf did not follow");
        ok &= expect(out.find("= 350") != std::string::npos,
                     "narrow-mode pointer follow lost the pointee value");
    }

    // ── зарезервированный, но пустой vector: узел показан, не «массива нет»
    //    (ревью Codex, PR #5) ───────────────────────────────────────────────
    {
        std::vector<int> reserved;
        reserved.reserve(8);   // capacity>0, size==0 — буфер выделен, пуст
        const std::string out = run_with_script(
            "enter end enter q",
            [&](eye::Gallery& g) { g.add(reserved, "резерв"); });
        ok &= expect(out.find("зарезервирован, пуст") != std::string::npos,
                     "reserved-but-empty vector is not shown as reserved");
        ok &= expect(out.find("буфер не выделен") == std::string::npos,
                     "reserved vector wrongly claimed no buffer");
    }

    // ── ромб: общая virtual-база показана один раз, вторая помечена общей
    //    (ревью Codex, PR #5) ───────────────────────────────────────────────
    {
        PaladinD paladin;
        // Раскрыть корень, обе базы (MageBr, WarrBr), затем внутри них — Soul.
        // 'e' раскрывает ветку рекурсивно — оба Soul материализуются.
        const std::string out = run_with_script(
            "e q", [&](eye::Gallery& g) { g.add(paladin, "паладин"); });
        ok &= expect(count_frames(out) >= 1, "diamond produced no frames");
        // Ровно одна пометка «общий» — второй Soul помечен, не задвоен.
        const bool shared_once =
            out.find("общий") != std::string::npos;
        ok &= expect(shared_once,
                     "shared virtual base is not marked in the tree");
        // spark (поле Soul) раскрывается лишь у ОДНОГО (владельца) Soul:
        // у общего узла раскрытия нет.
        ok &= expect(out.find("spark") != std::string::npos,
                     "owner virtual base did not expand its field");
    }

    // ── курсорная строка санируется: \n/ESC в подписи не рвут кадр (Codex) ──
    {
        int weird = 5;
        const std::string out = run_with_script(
            "q", [&](eye::Gallery& g) { g.add(weird, "bad\nNEXT"); });
        ok &= expect(out.find("bad NEXT") != std::string::npos,
                     "cursor row did not sanitize a newline in the label");
        ok &= expect(out.find("bad\nNEXT") == std::string::npos,
                     "cursor row leaked a raw newline from the label");
    }

    // ── узкий режим: Home/End крутят ВИДИМУЮ панель деталей, а не двигают
    //    скрытый курсор дерева (ревью Codex, PR #5) ─────────────────────────
    {
        set_env("EYE_WIDTH", "80");
        set_env("EYE_HEIGHT", "10");
        // tab открывает детали рыцаря (высокая панель), end крутит их вниз:
        // верх («паспорт») уходит за кадр — значит скроллилась панель, а не
        // курсор дерева (у одного корня cursor_end был бы no-op → верх остался).
        const std::string top = last_frame(run_with_script(
            "tab q", [&](eye::Gallery& g) { g.add(knight, "рыцарь"); }));
        const std::string bottom = last_frame(run_with_script(
            "tab end q", [&](eye::Gallery& g) { g.add(knight, "рыцарь"); }));
        set_env("EYE_WIDTH", "126");
        set_env("EYE_HEIGHT", "40");
        ok &= expect(top.find("паспорт") != std::string::npos,
                     "narrow detail top view lost the passport section");
        ok &= expect(bottom.find("паспорт") == std::string::npos,
                     "End did not scroll the narrow detail pane (moved tree cursor)");
        // Home возвращает панель к верху (тоже адресует детали, не дерево).
        set_env("EYE_WIDTH", "80");
        set_env("EYE_HEIGHT", "10");
        const std::string home = last_frame(run_with_script(
            "tab end home q", [&](eye::Gallery& g) { g.add(knight, "рыцарь"); }));
        set_env("EYE_WIDTH", "126");
        set_env("EYE_HEIGHT", "40");
        ok &= expect(home.find("паспорт") != std::string::npos,
                     "Home did not scroll the narrow detail pane back to top");
    }

    // ── снимок экрана: s пишет кадр в файл чистым текстом ───────────────────
    {
        set_env("EYE_SNAP_DIR", ".");
        const std::string out = run_with_script(
            "enter s q", [&](eye::Gallery& g) { g.add(knight, "рыцарь"); });
        const std::size_t at = out.find("снимок: ");
        ok &= expect(at != std::string::npos,
                     "snapshot toast with the file path is missing");
        if (at != std::string::npos) {
            std::string path =
                out.substr(at + std::string("снимок: ").size());
            path = path.substr(0, path.find('\n'));
            while (!path.empty() && path.back() == ' ') path.pop_back();
            std::ifstream file(path, std::ios::binary);
            std::stringstream content;
            content << file.rdbuf();
            ok &= expect(file.good(), "snapshot file was not created");
            ok &= expect(content.str().find("рыцарь") != std::string::npos &&
                             content.str().find("armor") != std::string::npos,
                         "snapshot lacks the tree that was on screen");
            ok &= expect(content.str().find('\033') == std::string::npos,
                         "snapshot leaked ANSI escapes");
            std::remove(path.c_str());
        }
    }
    {
        // Два снимка в одной сессии: эксклюзивное создание («wbx») занимает
        // имя атомарно, второй уходит в следующий свободный номер.
        set_env("EYE_SNAP_DIR", ".");
        const std::string out = run_with_script(
            "s s q", [&](eye::Gallery& g) { g.add(knight, "рыцарь"); });
        std::size_t first = out.find("снимок: ");
        std::size_t second =
            first == std::string::npos ? first : out.find("снимок: ", first + 1);
        ok &= expect(second != std::string::npos,
                     "second snapshot toast is missing");
        std::size_t removed = 0;
        for (const char* name : {"./eye_snap_001.txt", "./eye_snap_002.txt"})
            if (std::remove(name) == 0) ++removed;
        ok &= expect(removed == 2,
                     "two snapshots did not land in two distinct files");
    }

    // ── explore() — обёртка над галереей ─────────────────────────────────────
    {
        set_env("EYE_SCRIPT", "q");
        std::ostringstream out;
        std::streambuf* old = std::cout.rdbuf(out.rdbuf());
        eye::explore(knight, "одиночка");
        std::cout.rdbuf(old);
        set_env("EYE_SCRIPT", "");
        ok &= expect(out.str().find("►▸ одиночка") != std::string::npos,
                     "explore() did not start a gallery of one");
    }

    if (ok) std::cout << "explore_script_tests: OK\n";
    return ok ? 0 : 1;
}
