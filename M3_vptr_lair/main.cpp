// ============================================================================
//  ОКО МАГА — M3: Логово vptr
// ============================================================================
//  Задача этапа: залезть внутрь механики виртуальных вызовов. Достать vptr,
//  распечатать vtable, доказать, что объекты одного типа делят одну таблицу,
//  и — гвоздь программы — позвать виртуальный метод РУКАМИ, минуя компилятор.
//
//  ⚠ ЧЕСТНОЕ ПРЕДУПРЕЖДЕНИЕ. Стандарт C++ вообще не знает слов "vptr" и
//  "vtable" — он описывает только ПОВЕДЕНИЕ виртуальных вызовов, а как их
//  реализовать, решает ABI. Мы исследуем конкретную реализацию: Itanium
//  C++ ABI (GCC/Clang на Linux). По букве стандарта происходящее ниже — UB.
//  Это инструмент ИССЛЕДОВАНИЯ, в прод такое не носят. Зато после этого
//  этапа виртуальность перестаёт быть магией. Проверка через глаза самого
//  компилятора (-fdump-lang-class) — в M5.
//
//  Договорённости Itanium ABI, которые мы увидим живьём:
//    - у полиморфного объекта первые 8 байт — vptr
//    - vptr указывает на массив указателей на функции (vtable)
//    - vtable[-1] — указатель на type_info (так работает typeid/dynamic_cast)
//    - vtable[-2] — offset-to-top (0 при одиночном наследовании)
//    - виртуальный деструктор занимает ДВА слота (complete + deleting)
//
//  Сборка:  g++ -std=c++20 -Wall -Wextra -Wpedantic main.cpp -o m3 && ./m3
// ============================================================================

// --- Слой платформенной совместимости (см. подробный комментарий в M0) -------
// ВАЖНО: сам разбор vtable ниже — Itanium ABI (GCC/Clang). У MSVC объектная
// модель другая, поэтому под MSVC этот этап компилируется, но печатает честную
// заметку вместо сырого дампа — за это отвечает EYE_ITANIUM_ABI.
#if (defined(__GNUC__) || defined(__clang__)) && !defined(_MSC_VER)
#  include <cxxabi.h>    // деманглер Itanium ABI (GCC/Clang)
#  define EYE_ITANIUM_ABI 1
#else
#  define EYE_ITANIUM_ABI 0   // MSVC
#endif
#if defined(_WIN32)
#  define NOMINMAX
#  define WIN32_LEAN_AND_MEAN
#  include <io.h>
#  include <windows.h>
#else
#  include <unistd.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>       // std::memcpy — наш главный легальный лом
#include <iostream>
#include <memory>
#include <string>
#include <typeinfo>

namespace clr {
inline bool enabled() {
    static const bool on = [] {
#if defined(_WIN32)
        if (!_isatty(_fileno(stdout))) return false;
        SetConsoleOutputCP(CP_UTF8);   // UTF-8: иначе кириллица/рамки — кракозябры
        // Включаем ANSI-escape (VT); не вышло — цвета выключаем (см. M0).
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
inline const char* reset()  { return code("\033[0m");        }
inline const char* gold()   { return code("\033[38;5;178m"); }
inline const char* cyan()   { return code("\033[36m");       }
inline const char* green()  { return code("\033[32m");       }
inline const char* grey()   { return code("\033[38;5;245m"); }
inline const char* violet() { return code("\033[35m");       }
} // namespace clr

std::string demangle(const char* mangled) {
#if EYE_ITANIUM_ABI
    int status = 0;
    std::unique_ptr<char, void (*)(void*)> d(
        abi::__cxa_demangle(mangled, nullptr, nullptr, &status), std::free);
    return status == 0 ? d.get() : mangled;
#else
    // MSVC: typeid().name() уже читаемо — снимаем служебные слова.
    std::string name = mangled;
    for (const char* junk : {"class ", "struct ", "enum ", " __ptr64"}) {
        const auto len = std::char_traits<char>::length(junk);
        for (auto p = name.find(junk); p != std::string::npos; p = name.find(junk))
            name.erase(p, len);
    }
    return name;
#endif
}

// ============================================================================
//  Иерархия из таверны
// ============================================================================
//  Порядок объявления методов = порядок слотов в vtable, поэтому объявляем
//  осознанно: attack — слот 0, taunt — слот 1, деструктор — слоты 2 и 3.

struct Hero {
    virtual void attack() const { std::cout << "    Hero: неуверенный тычок мечом\n"; }
    virtual void taunt()  const { std::cout << "    Hero: \"Ну... сразимся?\"\n"; }
    virtual ~Hero() = default;
    int level = 1;
};

struct CragHack : Hero {
    // attack переопределяем → в vtable CragHack слот 0 будет СВОЙ.
    void attack() const override { std::cout << "    CragHack: СОКРУШИТЕЛЬНЫЙ УДАР!\n"; }
    // taunt НЕ переопределяем → слот 1 будет указывать на Hero::taunt —
    // на ТОТ ЖЕ адрес, что и в таблице Hero. Увидим это глазами.
    int rage = 99;
};

#if EYE_ITANIUM_ABI   // ← весь разбор ниже — про Itanium ABI (GCC/Clang)

// ============================================================================
//  Инструменты вскрытия
// ============================================================================

// Достаём vptr. Никаких кастов объекта — memcpy первых 8 байт.
// Копирование представления через memcpy — легальная операция;
// интерпретация результата как vptr — уже наша ответственность (ABI).
void* extract_vptr(const Hero& obj) {
    void* vptr = nullptr;
    static_assert(sizeof(vptr) == 8, "рассчитано на 64-битный Linux");
    std::memcpy(&vptr, &obj, sizeof(vptr));
    return vptr;
}

// Печатаем таблицу: служебные ячейки (-2, -1) и первые slots слотов.
void dump_vtable(const Hero& obj, const std::string& label, int slots) {
    void** vtable = static_cast<void**>(extract_vptr(obj));

    std::cout << clr::gold() << "-- vtable объекта " << label << clr::reset()
              << clr::grey() << "  (vptr = " << static_cast<void*>(vtable) << ")"
              << clr::reset() << '\n';

    // vtable[-1]: указатель на std::type_info. Именно сюда подглядывает
    // typeid(*ptr), когда определяет динамический тип. RTTI — не магия,
    // а вот эта ячейка.
    const std::type_info* ti = nullptr;
    std::memcpy(&ti, vtable - 1, sizeof(ti));

    // vtable[-2]: offset-to-top. При одиночном наследовании — ноль;
    // ненулевым становится при множественном (тема на подумать).
    std::ptrdiff_t offset_to_top = 0;
    std::memcpy(&offset_to_top, vtable - 2, sizeof(offset_to_top));

    std::cout << "   " << clr::violet() << "[-2] offset-to-top " << clr::reset()
              << offset_to_top << '\n';
    std::cout << "   " << clr::violet() << "[-1] type_info     " << clr::reset()
              << clr::cyan() << demangle(ti->name()) << clr::reset() << '\n';

    static const char* slot_names[] = {"attack()", "taunt()",
                                       "~dtor (complete)", "~dtor (deleting)"};
    for (int s = 0; s < slots; ++s) {
        void* fn = nullptr;
        std::memcpy(&fn, vtable + s, sizeof(fn));
        std::cout << "   [" << s << "]  " << clr::green() << fn << clr::reset()
                  << clr::grey() << "  " << slot_names[s] << clr::reset() << '\n';
    }
    std::cout << '\n';
}

int main() {
    std::cout << clr::violet() << "\n  ОКО МАГА / M3: логово vptr\n\n" << clr::reset();

    Hero     hero;
    CragHack crag1;
    CragHack crag2;

    // --- 1. Один тип — одна таблица -------------------------------------------
    // vtable создаётся ОДНА на класс, на этапе компиляции, и лежит в
    // read-only секции бинарника (ты видел такие секции в objdump).
    // Каждый объект носит лишь 8-байтовый указатель на неё.
    std::cout << clr::gold() << "-- у кого какой vptr" << clr::reset() << '\n';
    std::cout << "   hero  : " << extract_vptr(hero)  << '\n';
    std::cout << "   crag1 : " << extract_vptr(crag1) << '\n';
    std::cout << "   crag2 : " << extract_vptr(crag2) << '\n';
    std::cout << clr::grey()
              << "   crag1 и crag2 — одинаковые (класс один), hero — другой.\n\n"
              << clr::reset();

    // --- 2. Сами таблицы -------------------------------------------------------
    dump_vtable(hero,  "Hero",     4);
    dump_vtable(crag1, "CragHack", 4);
    std::cout << clr::grey()
              << "   Сравни слоты: [0] attack РАЗНЫЙ (переопределён),\n"
              << "   [1] taunt ОДИНАКОВЫЙ — CragHack не трогал taunt, и его\n"
              << "   таблица честно указывает на Hero::taunt.\n\n" << clr::reset();

    // --- 3. Виртуальный вызов через базовый указатель --------------------------
    // Что делает компилятор для base->attack():
    //   1) читает vptr из первых 8 байт объекта
    //   2) берёт vtable[0]
    //   3) зовёт по адресу, передав this
    // Три шага. Сейчас сделаем их сами.
    const Hero* base = &crag1;
    std::cout << clr::gold() << "-- вызов как обычно: base->attack()" << clr::reset() << '\n';
    base->attack();

    // --- 4. Тот же вызов — РУКАМИ ----------------------------------------------
    std::cout << clr::gold() << "\n-- вызов вручную, минуя компилятор" << clr::reset() << '\n';
    void** vtable = static_cast<void**>(extract_vptr(*base));   // шаг 1

    // Шаг 2-3: слот 0 → указатель на функцию. Метод с точки зрения ABI —
    // обычная функция, которой первым (скрытым) аргументом передают this.
    // memcpy вместо каста: reinterpret_cast указателя-на-объект в
    // указатель-на-функцию не переваривает -Wpedantic, а memcpy битов — ок.
    using attack_fn = void (*)(const Hero*);
    attack_fn fn = nullptr;
    std::memcpy(&fn, vtable + 0, sizeof(fn));
    fn(base);   // ← ВИРТУАЛЬНЫЙ ДИСПАТЧ ВРУЧНУЮ. this передали сами.

    std::cout << clr::grey()
              << "   Одинаковый результат. \"Магия\" виртуальности = чтение\n"
              << "   указателя из таблицы + обычный вызов. Всё.\n\n" << clr::reset();

    // --- 5. RTTI тем же путём ---------------------------------------------------
    // typeid(*base) обязан выдать ДИНАМИЧЕСКИЙ тип. Откуда он его берёт?
    // Из vtable[-1] — мы уже печатали эту ячейку. Сверим:
    std::cout << clr::gold() << "-- typeid через базовый указатель" << clr::reset() << '\n'
              << "   typeid(*base).name() → " << clr::cyan()
              << demangle(typeid(*base).name()) << clr::reset() << '\n';
    std::cout << clr::grey()
              << "   base объявлен как Hero*, но RTTI дошёл по vptr до таблицы\n"
              << "   CragHack и достал оттуда правду.\n\n" << clr::reset();

    // Проверь себя (ответ — в README): почему если у Hero убрать слово
    // virtual у attack, ВЕСЬ этот файл перестанет даже компилироваться?
}

#else   // не Itanium ABI (MSVC): объектная модель другая — сырой дамп не строим

int main() {
    std::cout << clr::violet() << "\n  ОКО МАГА / M3: логово vptr\n\n" << clr::reset();
    std::cout << clr::grey()
              << "  Этот этап препарирует Itanium C++ ABI (GCC/Clang). У MSVC\n"
              << "  объектная модель ДРУГАЯ: иначе устроены RTTI, offset-to-top и\n"
              << "  деструкторы, поэтому сырой разбор vtable здесь не запускается.\n"
              << "  Прогони этап под Linux или MinGW-w64 g++, чтобы увидеть таблицы.\n\n"
              << clr::reset();

    // Портируемая часть: динамический диспатч и RTTI работают ОДИНАКОВО везде —
    // это поведение из стандарта, а не деталь ABI.
    Hero     hero;
    CragHack crag;
    const Hero* base = &crag;   // статический тип — Hero*, динамический — CragHack

    std::cout << clr::gold() << "-- виртуальный вызов через базовый указатель"
              << clr::reset() << '\n' << "   base->attack(): ";
    base->attack();
    std::cout << clr::gold() << "-- RTTI: динамический тип из-под Hero*"
              << clr::reset() << '\n'
              << "   typeid(*base).name() → " << clr::cyan()
              << demangle(typeid(*base).name()) << clr::reset() << '\n';
    std::cout << clr::grey()
              << "   base объявлен как Hero*, но зовётся CragHack и typeid выдаёт\n"
              << "   CragHack — механика та же, просто внутренности vtable у MSVC\n"
              << "   свои. (void)hero;\n\n" << clr::reset();
    (void)hero;
}

#endif
