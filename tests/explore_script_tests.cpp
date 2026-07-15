// Снапшот-тесты странствия (eye::explore / Gallery) в режиме EYE_SCRIPT:
// клавиши исполняются из строки, кадры печатаются в stdout — терминал не нужен.
#include <eye/magic_eye.hpp>

#include <cstdlib>
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
