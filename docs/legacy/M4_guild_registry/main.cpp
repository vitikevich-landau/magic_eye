// ============================================================================
//  ОКО МАГА — M4: Реестр гильдии
// ============================================================================
//  Задача этапа: получить то, что НИКАКИМИ трюками из компилятора не
//  вытащить — ИМЕНА полей и доступ к private. Препроцессор стирает
//  программу до того, как компилятор увидит имена? Значит, ловим имена
//  ЕЩЁ НА ПРЕПРОЦЕССОРЕ: #поле превращает имя в строку.
//
//  Цена: класс должен сам зарегистрироваться макросом
//      EYE_DESCRIBE(Hero, level, mana, name)
//  Ровно так работает boost::describe, так же устроена половина
//  сериализаторов на C++. Это не костыль — это состояние индустрии
//  до прихода рефлексии в C++26.
//
//  Попутно — тема, которую любят на собесах и которой боятся кандидаты:
//  УКАЗАТЕЛИ НА ЧЛЕНЫ КЛАССА. int Hero::* и void (Hero::*)() const.
//
//  Сборка:  g++ -std=c++20 -Wall -Wextra -Wpedantic main.cpp -o m4 && ./m4
// ============================================================================

// --- Слой платформенной совместимости (см. подробный комментарий в M0) -------
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
#include <iostream>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <utility>

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

template <typename T>
std::string type_name() {
#if EYE_ITANIUM_ABI
    int status = 0;
    std::unique_ptr<char, void (*)(void*)> d(
        abi::__cxa_demangle(typeid(T).name(), nullptr, nullptr, &status), std::free);
    std::string name = status == 0 ? d.get() : typeid(T).name();
#else
    // MSVC: имя уже читаемо — чистим служебные слова (см. M0).
    std::string name = typeid(T).name();
    for (const char* junk : {"class ", "struct ", "enum ", " __ptr64"}) {
        const auto len = std::char_traits<char>::length(junk);
        for (auto p = name.find(junk); p != std::string::npos; p = name.find(junk))
            name.erase(p, len);
    }
#endif
    // Свернуть длинную форму std::string обеих стандартных библиотек (см. M2).
    for (const std::string& ugly :
         {std::string("std::__cxx11::basic_string<char, std::char_traits<char>, "
                      "std::allocator<char> >"),
          std::string("std::basic_string<char,std::char_traits<char>,"
                      "std::allocator<char> >")})
        for (std::size_t p = name.find(ugly); p != std::string::npos; p = name.find(ugly))
            name.replace(p, ugly.size(), "std::string");
    return name;
}

// ============================================================================
//  Препроцессорный цирк: FOR_EACH по __VA_ARGS__
// ============================================================================
//  У препроцессора нет циклов. Их имитируют лесенкой макросов:
//  EYE_FE_3(M, a, b, c) → M(a) + EYE_FE_2(M, b, c) → ... и так далее.
//
//  А как узнать, СКОЛЬКО аргументов пришло? Трюк EYE_PICK: подставляем
//  __VA_ARGS__ ПЕРЕД списком имён макросов и берём 9-й по счёту аргумент.
//  Чем длиннее __VA_ARGS__, тем дальше сдвигается список — и 9-м
//  оказывается макрос с нужным номером. Прими, проникнись и посочувствуй
//  всем, кто ждал рефлексию тридцать лет.
//
//  EYE_EXPAND — костыль под legacy-препроцессор MSVC (без /Zc:preprocessor):
//  он передаёт __VA_ARGS__ дальше как ОДИН токен, ломая счёт. Лишний
//  проход-разворот EYE_EXPAND(...) чинит это; на GCC/Clang он безвреден.

#define EYE_EXPAND(x) x
#define EYE_FE_1(M, x)      M(x)
#define EYE_FE_2(M, x, ...) M(x) EYE_EXPAND(EYE_FE_1(M, __VA_ARGS__))
#define EYE_FE_3(M, x, ...) M(x) EYE_EXPAND(EYE_FE_2(M, __VA_ARGS__))
#define EYE_FE_4(M, x, ...) M(x) EYE_EXPAND(EYE_FE_3(M, __VA_ARGS__))
#define EYE_FE_5(M, x, ...) M(x) EYE_EXPAND(EYE_FE_4(M, __VA_ARGS__))
#define EYE_FE_6(M, x, ...) M(x) EYE_EXPAND(EYE_FE_5(M, __VA_ARGS__))
#define EYE_FE_7(M, x, ...) M(x) EYE_EXPAND(EYE_FE_6(M, __VA_ARGS__))
#define EYE_FE_8(M, x, ...) M(x) EYE_EXPAND(EYE_FE_7(M, __VA_ARGS__))

#define EYE_PICK(_1, _2, _3, _4, _5, _6, _7, _8, NAME, ...) NAME
#define EYE_FOR_EACH(M, ...)                                            \
    EYE_EXPAND(EYE_PICK(__VA_ARGS__, EYE_FE_8, EYE_FE_7, EYE_FE_6,      \
             EYE_FE_5, EYE_FE_4, EYE_FE_3, EYE_FE_2, EYE_FE_1)(M, __VA_ARGS__))

// Одна запись реестра: { "имя_поля", &Класс::имя_поля }
//   #f          — препроцессор превращает level в строку "level"
//   &Self::f    — УКАЗАТЕЛЬ НА ЧЛЕН: не адрес в памяти, а «какое поле»
#define EYE_ENTRY(f) std::pair{#f, &Self::f},

// Сам реестр. Кладётся ВНУТРЬ класса (можно в public-секцию, а видит
// и private — мы же член класса). using Self нужен, чтобы EYE_ENTRY
// не тащил имя класса в каждую строчку.
#define EYE_DESCRIBE(Type, ...)                                         \
    using Self = Type;                                                  \
    static constexpr auto eye_describe() {                              \
        return std::tuple{EYE_FOR_EACH(EYE_ENTRY, __VA_ARGS__)};        \
    }

// ============================================================================
//  Подопытный: класс с private и методами — то, перед чем M2 бессилен
// ============================================================================

class Hero {
    // private! M2 тут ломается дважды: не агрегат + доступа нет.
    int level = 13;
    int mana  = 40;
    std::string name = "Solmyr";

public:
    int strike(int power) const {
        std::cout << "    " << name << " бьёт с силой " << power * level << "!\n";
        return power * level;
    }
    void meditate() { mana += 10; }
    int  current_mana() const { return mana; }

    // Регистрация. Макрос стоит в public, но private-поля ему доступны:
    // eye_describe — член класса, а членам можно всё.
    EYE_DESCRIBE(Hero, level, mana, name)
};

// ============================================================================
//  Читатель реестра
// ============================================================================

// Есть ли у T реестр? Обычный концепт: "скомпилируется ли T::eye_describe()?"
template <class T>
concept described = requires { T::eye_describe(); };

template <described T>
void inspect_registry(const T& obj, const std::string& label) {
    std::cout << clr::gold() << "== " << label << " " << clr::grey()
              << "(" << type_name<T>() << ", sizeof " << sizeof(T) << ")"
              << clr::reset() << '\n';
    std::cout << clr::grey() << "   offset  поле        тип           значение"
              << clr::reset() << '\n';

    // Реестр — это tuple пар. std::apply распаковывает его в пачку
    // аргументов, fold expression (...,) прогоняет по каждому.
    std::apply(
        [&](auto... entry) {
            (..., [&](auto e) {
                const char* fname = e.first;
                auto member_ptr   = e.second;   // вот он, int Hero::* живьём

                // obj.*member_ptr — операция «взять У ЭТОГО объекта ТО поле».
                // member_ptr сам по себе — не адрес; адресом он становится
                // только в паре с конкретным объектом.
                const auto& field = obj.*member_ptr;

                auto offset = reinterpret_cast<const char*>(std::addressof(field))
                            - reinterpret_cast<const char*>(std::addressof(obj));

                using FT = std::remove_cvref_t<decltype(field)>;
                const std::string tname = type_name<FT>();
                // Ширину колонок считаем ЧЕРЕЗ ГАРД. Наивное "12 - name.size()"
                // на длинном имени ушло бы в size_t-underflow (≈2^64) → фатальный
                // std::length_error. Колонка тип ниже уже так защищена — теперь
                // и имя. Заодно так работает std::setw в финальной версии.
                const std::size_t fname_len = std::string(fname).size();
                std::cout << "   " << clr::grey() << "0x" << std::hex
                          << (offset < 16 ? "0" : "") << offset << std::dec
                          << clr::reset() << "    " << clr::green() << fname
                          << clr::reset() << std::string(fname_len < 12 ? 12 - fname_len : 1, ' ')
                          << clr::cyan() << tname << clr::reset()
                          << std::string(tname.size() < 14 ? 14 - tname.size() : 2, ' ')
                          << clr::green() << field << clr::reset() << '\n';
            }(entry));
        },
        T::eye_describe());
    std::cout << '\n';
}

int main() {
    std::cout << clr::violet() << "\n  ОКО МАГА / M4: реестр гильдии\n\n" << clr::reset();

    Hero solmyr;

    // --- 1. Реестр в деле: имена + private -------------------------------------
    inspect_registry(solmyr, "Hero (поля из реестра, включая private)");
    std::cout << clr::grey()
              << "   Имена полей — настоящие: их сохранил препроцессор (#f),\n"
              << "   единственное место, где имя ещё существует как текст.\n\n"
              << clr::reset();

    // --- 2. Указатели на члены-ДАННЫЕ отдельно, без макросов -------------------
    std::cout << clr::gold() << "-- указатель на поле: int Hero::*" << clr::reset() << '\n';
    // Прочитать level напрямую нельзя (private), но реестр нам его выдал.
    // Возьмём из реестра нулевую запись:
    auto level_ptr = std::get<0>(Hero::eye_describe()).second;
    std::cout << "   solmyr.*level_ptr = " << clr::green()
              << solmyr.*level_ptr << clr::reset() << '\n';
    std::cout << clr::grey()
              << "   sizeof(int Hero::*) = " << sizeof(level_ptr)
              << " — это просто offset внутри класса.\n\n" << clr::reset();

    // --- 3. Указатели на члены-ФУНКЦИИ ------------------------------------------
    // Синтаксис, который надо один раз прожить:
    //   тип:    int (Hero::*)(int) const   — метод Hero, берёт int, возвращает
    //           int, сам const
    //   взять:  &Hero::strike              — скобки НЕ ставить, это не вызов
    //   позвать: (obj.*ptr)(7) или (p->*ptr)(7) — скобки вокруг .*
    //           ОБЯЗАТЕЛЬНЫ: вызов () связывает сильнее, чем .*
    std::cout << clr::gold() << "-- указатель на метод" << clr::reset() << '\n';

    int (Hero::*strike_ptr)(int) const = &Hero::strike;
    (solmyr.*strike_ptr)(3);                       // через объект

    const Hero* p = &solmyr;
    (p->*strike_ptr)(5);                           // через указатель

    std::cout << clr::grey()
              << "   sizeof(указатель на метод) = " << sizeof(strike_ptr)
              << " (!), а на поле было 8.\n"
              << "   Причём Hero тут даже НЕ виртуальный — а указатель всё равно\n"
              << "   16. Представление единое для ЛЮБОГО класса: одно слово —\n"
              << "   адрес функции ЛИБО индекс слота vtable, второе — поправка\n"
              << "   this (нужна при множественном/виртуальном наследовании).\n"
              << "   Два слова, чтобы один &Hero::strike работал и когда метод\n"
              << "   окажется виртуальным. Любимый вопрос с собеса — теперь с\n"
              << "   ответом и «почему».\n\n"
              << clr::reset();

    // --- 4. И указатель на метод тоже можно положить в реестр -------------------
    std::cout << clr::gold() << "-- мини-реестр методов без макросов" << clr::reset() << '\n';
    auto actions = std::tuple{
        std::pair{"strike(2)", [](Hero& h) { h.strike(2); }},
        std::pair{"meditate",  [](Hero& h) { h.meditate(); }},
    };
    std::apply([&](auto... a) { (..., (std::cout << "   вызываю " << a.first << ":\n",
                                       a.second(solmyr))); }, actions);
    std::cout << "   мана после meditate: " << clr::green()
              << solmyr.current_mana() << clr::reset() << "\n\n";
}
