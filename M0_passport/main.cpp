// ============================================================================
//  ОКО МАГА — M0: Паспорт типа
// ============================================================================
//  Задача этапа: научиться спрашивать у компилятора ВСЁ, что он готов
//  рассказать о типе БЕЗ грязных хаков. Это три источника:
//
//    1. RTTI        — typeid(T) даёт манглированное имя, ABI-деманглер
//                     разворачивает его в человеческое
//    2. sizeof/alignof — размер и выравнивание (см. подробнее в M1/M2)
//    3. <type_traits>  — компилятор отвечает на вопросы "да/нет" о типе
//                        на этапе КОМПИЛЯЦИИ
//
//  Сборка:  g++ -std=c++20 -Wall -Wextra -Wpedantic main.cpp -o m0 && ./m0
// ============================================================================

// --- Слой платформенной совместимости --------------------------------------
// Деманглер имён и isatty — из Itanium ABI / POSIX (Linux, GCC/Clang). На
// Windows/MSVC их нет: имя типа уже читаемо через typeid, а isatty и ANSI
// берём из WinAPI. EYE_ITANIUM_ABI=1 там, где доступен __cxa_demangle.
#if (defined(__GNUC__) || defined(__clang__)) && !defined(_MSC_VER)
#  include <cxxabi.h>    // abi::__cxa_demangle — деманглер Itanium ABI (GCC/Clang)
#  define EYE_ITANIUM_ABI 1
#else
#  define EYE_ITANIUM_ABI 0   // MSVC: своя объектная модель, деманглер не нужен
#endif
#if defined(_WIN32)
#  define NOMINMAX             // чтобы windows.h не переопределил std::min/max
#  define WIN32_LEAN_AND_MEAN
#  include <io.h>             // _isatty, _fileno
#  include <windows.h>        // включить обработку ANSI-escape (VT) в консоли
#else
#  include <unistd.h>         // isatty, fileno — POSIX
#endif

#include <cstdio>        // fileno
#include <cstdlib>       // std::free
#include <iomanip>       // std::setw
#include <iostream>
#include <memory>        // std::unique_ptr для владения C-строкой
#include <string>
#include <type_traits>
#include <typeinfo>      // typeid
#include <vector>

// ----------------------------------------------------------------------------
// Цвета: ANSI escape-коды. Терминал видит байты "\033[36m" и включает голубой.
// Никаких библиотек — это просто текст, который терминал интерпретирует.
// ----------------------------------------------------------------------------
namespace clr {

// Если вывод перенаправлен в файл (./m0 > log.txt), коды превратятся в мусор
// вида "^[[36m". isatty(1) говорит: stdout — это живой терминал или нет.
// static-переменная в функции — те самые "магические статики": инициализация
// один раз, потокобезопасно (C++11).
inline bool enabled() {
    static const bool on = [] {
#if defined(_WIN32)
        // Windows: свой isatty (_isatty) + консоль Windows 10 по умолчанию НЕ
        // трактует ANSI-escape, поэтому явно включаем виртуальный терминал.
        if (!_isatty(_fileno(stdout))) return false;
        // Кодовая страница консоли Windows по умолчанию НЕ UTF-8 — без этого
        // кириллица и рамки (█ ░ ══) выводятся кракозябрами.
        SetConsoleOutputCP(CP_UTF8);
        // Пытаемся включить ANSI-escape (VT). Если консоль его не поддерживает
        // (SetConsoleMode вернёт 0) — цвета ВЫКЛЮЧАЕМ, чтобы не сыпать escape-коды
        // как текст (ровно это и была «кракозябра» в консоли отладки VS).
        const HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        if (h == INVALID_HANDLE_VALUE || !GetConsoleMode(h, &mode)) return false;
        return SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
        return isatty(fileno(stdout)) != 0;
#endif
    }();
    return on;
}

inline const char* code(const char* c) { return enabled() ? c : ""; }

// Палитра проекта (одна и та же во всех этапах):
inline const char* reset()  { return code("\033[0m");        }
inline const char* gold()   { return code("\033[38;5;178m"); } // заголовки
inline const char* cyan()   { return code("\033[36m");       } // типы
inline const char* green()  { return code("\033[32m");       } // значения / "да"
inline const char* grey()   { return code("\033[38;5;245m"); } // второстепенное / "нет"
inline const char* violet() { return code("\033[35m");       } // vptr и магия
} // namespace clr

// ----------------------------------------------------------------------------
// type_name<T>() — человеческое имя типа.
//
// typeid(T).name() возвращает МАНГЛИРОВАННОЕ имя: "4Hero", "St6vectorI..." —
// то самое, что ты видел в выводе nm и разворачивал через c++filt.
// abi::__cxa_demangle делает то же, что c++filt, но прямо в программе.
// ----------------------------------------------------------------------------
template <typename T>
std::string type_name() {
#if EYE_ITANIUM_ABI
    int status = 0;
    // __cxa_demangle выделяет строку через malloc — освобождать нужно free.
    // unique_ptr с кастомным deleter'ом гарантирует это даже при исключении.
    std::unique_ptr<char, void (*)(void*)> demangled(
        abi::__cxa_demangle(typeid(T).name(), nullptr, nullptr, &status),
        std::free);
    return status == 0 ? demangled.get() : typeid(T).name();
#else
    // MSVC: typeid(T).name() уже человекочитаемо ("struct Hero", "int * __ptr64").
    // Убираем шум, чтобы вид совпал с GCC/Clang.
    std::string name = typeid(T).name();
    for (const char* junk : {"class ", "struct ", "enum ", " __ptr64"}) {
        const auto len = std::char_traits<char>::length(junk);
        for (auto p = name.find(junk); p != std::string::npos; p = name.find(junk))
            name.erase(p, len);
    }
    return name;
#endif
}

// ----------------------------------------------------------------------------
// Одна строка чек-листа: "polymorphic ........ да"
// ----------------------------------------------------------------------------
void trait_row(const char* name, bool value) {
    std::cout << "  " << std::left << std::setw(22) << name
              << (value ? clr::green() : clr::grey())
              << (value ? "да" : "нет") << clr::reset() << '\n';
}

// ----------------------------------------------------------------------------
// Паспорт: всё, что известно о типе на этапе компиляции.
// Обрати внимание: функция принимает НОЛЬ аргументов. Объект не нужен —
// все ответы существуют ещё до запуска программы.
// ----------------------------------------------------------------------------
template <typename T>
void passport() {
    std::cout << clr::gold() << "== " << type_name<T>() << " "
              << std::string(50 - std::min<std::size_t>(50, type_name<T>().size()), '=')
              << clr::reset() << '\n';

    std::cout << "  size / align          " << clr::cyan()
              << sizeof(T) << " / " << alignof(T) << clr::reset() << '\n';

    // Каждый трейт — вопрос компилятору. Ответ вычисляется при сборке,
    // в бинарнике остаётся готовая константа true/false.
    trait_row("polymorphic",        std::is_polymorphic_v<T>);        // есть virtual → есть vptr
    trait_row("aggregate",          std::is_aggregate_v<T>);          // можно T{a, b, c} → разберём в M2
    trait_row("trivially copyable", std::is_trivially_copyable_v<T>); // можно memcpy → важно для M1
    trait_row("standard layout",    std::is_standard_layout_v<T>);    // layout совместим с C
    trait_row("empty",              std::is_empty_v<T>);
    trait_row("virtual destructor", std::has_virtual_destructor_v<T>);
    std::cout << '\n';
}

// ----------------------------------------------------------------------------
// Подопытные из Эрафии
// ----------------------------------------------------------------------------

// Агрегат: нет конструкторов, нет private, нет virtual. Просто данные.
struct Creature {
    std::string name;
    int hp;
    int attack;
};

// Полиморфный: одно слово virtual — и внутри объекта появляется скрытое
// поле vptr (сравни size с Creature). Подробности — в M3.
struct Hero {
    virtual ~Hero() = default;
    int level = 1;
};

// Пустой тип: полей нет, но sizeof == 1. Почему не 0? Потому что два разных
// объекта обязаны иметь разные адреса: Tent a, b; &a != &b.
struct Tent {};

int main() {
    std::cout << clr::violet()
              << "\n  ОКО МАГА / M0: паспорт типа\n\n" << clr::reset();

    passport<int>();
    passport<Creature>();
    passport<Hero>();
    passport<Tent>();
    passport<std::vector<Creature>>();  // работает с ЛЮБЫМ типом, даже не твоим

    std::cout << clr::grey()
              << "  Хозяйке на заметку: у Hero polymorphic=да, у Creature=нет.\n"
              << "  Цена одного слова virtual — скрытый vptr, +8 байт к КАЖДОМУ\n"
              << "  объекту Hero: оттого sizeof(Hero)=16 при единственном int\n"
              << "  level (8 vptr + 4 int + 4 выравнивание). Разбор — M3.\n\n"
              << clr::reset();
}
