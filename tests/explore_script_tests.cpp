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
