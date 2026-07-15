// ============================================================================
//  ОКО МАГА — M2: Разбор по косточкам
// ============================================================================
//  Задача этапа: для агрегата вывести КАЖДОЕ поле — тип, значение, offset,
//  размер — и карту padding'а. Без единого макроса и без регистрации.
//
//  В C++ нет рефлексии, поэтому работают два трюка на компиляторе:
//
//    Трюк 1. ПОДСЧЁТ ПОЛЕЙ. Спрашиваем компилятор: "а скомпилируется ли
//            T{x}? а T{x, x}? а T{x, x, x}?" Максимальное число аргументов,
//            при котором агрегатная инициализация валидна, и есть число полей.
//
//    Трюк 2. ДОСТУП К ПОЛЯМ. Structured bindings: auto& [a, b, c] = obj;
//            даёт ссылки на реальные члены объекта — с настоящими адресами.
//
//  Ровно так внутри устроен boost::pfr. Мы собираем его учебную версию.
//
//  Сборка:  g++ -std=c++20 -Wall -Wextra -Wpedantic main.cpp -o m2 && ./m2
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

#include <cctype>        // std::isprint
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

// --- цвета и type_name из M0 -------------------------------------------------
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
inline const char* red()    { return code("\033[38;5;131m"); } // padding
} // namespace clr

template <typename T>
std::string type_name() {
#if EYE_ITANIUM_ABI
    int status = 0;
    std::unique_ptr<char, void (*)(void*)> demangled(
        abi::__cxa_demangle(typeid(T).name(), nullptr, nullptr, &status), std::free);
    std::string name = status == 0 ? demangled.get() : typeid(T).name();
#else
    // MSVC: имя уже читаемо — чистим служебные слова (см. M0).
    std::string name = typeid(T).name();
    for (const char* junk : {"class ", "struct ", "enum ", " __ptr64"}) {
        const auto len = std::char_traits<char>::length(junk);
        for (auto p = name.find(junk); p != std::string::npos; p = name.find(junk))
            name.erase(p, len);
    }
#endif

    // Деманглер честен до жестокости: std::string у libstdc++ —
    // "std::__cxx11::basic_string<char, std::char_traits<char>, ...>", у MSVC —
    // "std::basic_string<char,std::char_traits<char>,...>". Сворачиваем ОБЕ
    // формы обратно в короткую.
    for (const std::string& ugly :
         {std::string("std::__cxx11::basic_string<char, std::char_traits<char>, "
                      "std::allocator<char> >"),
          std::string("std::basic_string<char,std::char_traits<char>,"
                      "std::allocator<char> >")})
        for (std::size_t pos = name.find(ugly); pos != std::string::npos;
             pos = name.find(ugly))
            name.replace(pos, ugly.size(), "std::string");
    return name;
}

// ============================================================================
//  ТРЮК 1: подсчёт полей
// ============================================================================

// Хамелеон: тип, который ПРИТВОРЯЕТСЯ конвертируемым во что угодно.
// Тела у оператора нет — оно и не нужно: requires-выражение ничего не
// исполняет, оно только спрашивает "а СКОМПИЛИРОВАЛОСЬ БЫ?".
struct any_init {
    template <class T>
    constexpr operator T() const;  // объявление без определения
};

// Псевдоним, зависящий от индекса, — чтобы размножить any_init пачкой
// через index_sequence: any_slot<0>, any_slot<1>, ... все = any_init.
template <std::size_t>
using any_slot = any_init;

// Можно ли построить T из N аргументов?
//
// Тонкость, из-за которой наивный вариант T{arg, arg, ...} ВРЁТ:
// brace elision. Для struct S { int a; char tag[4]; } запись S{1,'x','y'}
// легальна — 'x' и 'y' проваливаются ВНУТРЬ массива, и наивный подсчёт
// выдал бы 5 "полей" вместо 2.
//
// Лекарство: оборачиваем каждый аргумент в СВОИ фигурные скобки —
// T{ {arg}, {arg} }. Явные скобки запрещают проваливание: каждый {arg}
// обязан инициализировать ровно один член целиком. Массив? — {arg}
// инициализирует ВЕСЬ массив. Вложенный агрегат? — тоже целиком.
template <class T, std::size_t... Is>
constexpr bool braced_constructible(std::index_sequence<Is...>) {
    return requires { T{ { any_slot<Is>{} }... }; };
}

// Линейный поиск: первое N, при котором N аргументов — можно,
// а N+1 — уже нельзя. Это и есть число полей.
template <class T, std::size_t N = 0>
constexpr std::size_t field_count() {
    static_assert(N <= sizeof(T) + 1, "field_count: подозрительно много полей");
    if constexpr (braced_constructible<T>(std::make_index_sequence<N>{})
              && !braced_constructible<T>(std::make_index_sequence<N + 1>{}))
        return N;
    else
        return field_count<T, N + 1>();
}

// ============================================================================
//  ТРЮК 2: обход полей через structured bindings
// ============================================================================

// Позвать f для каждого поля obj. Число полей известно на этапе компиляции,
// поэтому if constexpr выбирает НУЖНУЮ ветку, остальные даже не
// инстанцируются. Да, лесенка. Нет, красивее в C++20 нельзя — число имён
// в auto& [a, b, c] обязано быть написано буквально. Это цена отсутствия
// рефлексии; boost::pfr внутри выглядит так же, только веток у него 100+.
template <class T, class F>
void visit_fields(const T& obj, F&& f) {
    constexpr std::size_t N = field_count<T>();
    if constexpr (N == 0) {
        (void)obj; (void)f;
    } else if constexpr (N == 1) {
        const auto& [a] = obj; f(a);
    } else if constexpr (N == 2) {
        const auto& [a, b] = obj; f(a); f(b);
    } else if constexpr (N == 3) {
        const auto& [a, b, c] = obj; f(a); f(b); f(c);
    } else if constexpr (N == 4) {
        const auto& [a, b, c, d] = obj; f(a); f(b); f(c); f(d);
    } else if constexpr (N == 5) {
        const auto& [a, b, c, d, e] = obj; f(a); f(b); f(c); f(d); f(e);
    } else if constexpr (N == 6) {
        const auto& [a, b, c, d, e, g] = obj; f(a); f(b); f(c); f(d); f(e); f(g);
    } else if constexpr (N == 7) {
        const auto& [a, b, c, d, e, g, h] = obj;
        f(a); f(b); f(c); f(d); f(e); f(g); f(h);
    } else if constexpr (N == 8) {
        const auto& [a, b, c, d, e, g, h, i] = obj;
        f(a); f(b); f(c); f(d); f(e); f(g); f(h); f(i);
    } else {
        static_assert(N <= 8, "visit_fields: добавь ветку для большего числа полей");
    }
}

// ============================================================================
//  Сборка картинки: таблица полей + карта памяти
// ============================================================================

// Печатать значение умеем не для всего: int — да, вложенную структуру — нет.
// Концепт спрашивает: "существует ли os << value?"
template <class V>
concept printable = requires(std::ostream& os, const V& v) { os << v; };

struct FieldInfo {
    std::size_t offset;
    std::size_t size;
    std::string type;
    std::string value;
};

template <class T>
void anatomy(const T& obj, const std::string& label) {
    static_assert(std::is_aggregate_v<T>,
                  "anatomy: этот трюк работает только для агрегатов");

    const auto* base = reinterpret_cast<const unsigned char*>(&obj);
    std::vector<FieldInfo> fields;

    // Обходим поля: structured binding дал ссылку на НАСТОЯЩИЙ член,
    // значит &field - &obj — его настоящий offset. Не догадка — измерение.
    visit_fields(obj, [&](const auto& field) {
        using FT = std::remove_cvref_t<decltype(field)>;
        FieldInfo info;
        info.offset = static_cast<std::size_t>(
            reinterpret_cast<const unsigned char*>(std::addressof(field)) - base);
        info.size = sizeof(field);
        info.type = type_name<FT>();

        // Порядок веток важен: символьные типы, указатели и массивы надо
        // перехватить ДО ветки printable, иначе ostream сделает не то и опасное.
        if constexpr (std::is_same_v<FT, char> ||
                      std::is_same_v<FT, signed char> ||
                      std::is_same_v<FT, unsigned char>) {
            // char/signed char/unsigned char — три РАЗНЫХ типа, и ostream печатает
            // каждый как ГЛИФ, а не число. char со значением 1 (или 0x1B=ESC) дал
            // бы кракозябру и мог утащить терминал в ANSI-последовательность.
            // Показываем печатаемый символ, иначе — код.
            std::ostringstream oss;
            const auto byte = static_cast<unsigned char>(field);
            if (std::isprint(byte))
                oss << '\'' << static_cast<char>(byte) << '\'';
            else
                oss << "char(" << static_cast<int>(byte) << ')';
            info.value = oss.str();
        } else if constexpr (std::is_pointer_v<FT>) {
            // Указатель (в т.ч. char*!) — как АДРЕС, а не разыменовываем:
            // os << (char*) прочитал бы чужую память как C-строку.
            std::ostringstream oss;
            oss << static_cast<const void*>(field);
            info.value = oss.str();
        } else if constexpr (std::is_array_v<FT>) {
            // C-массив печатать нельзя: char[N] распался бы в const char* и
            // читался как строка за границей массива (UB, ловится ASan'ом).
            info.value = "[массив " + std::to_string(sizeof(FT)) + " байт]";
        } else if constexpr (printable<FT>) {
            std::ostringstream oss;
            if constexpr (std::is_same_v<FT, bool>) oss << std::boolalpha;
            oss << field;
            info.value = oss.str();
        } else {
            info.value = "—";  // тип непечатаемый (вложенная структура) — честно
        }
        fields.push_back(std::move(info));
    });

    // --- заголовок и таблица --------------------------------------------------
    std::cout << clr::gold() << "== " << label << " "
              << clr::grey() << "(" << type_name<T>() << ", sizeof "
              << sizeof(T) << ", alignof " << alignof(T) << ")"
              << clr::reset() << '\n';
    std::cout << clr::grey()
              << "   offset  size  тип                        значение"
              << clr::reset() << '\n';

    std::size_t covered = 0;   // сколько байт занято полями
    std::size_t cursor  = 0;   // где мы сейчас в памяти

    auto pad_row = [&](std::size_t from, std::size_t len) {
        std::cout << "   " << clr::red() << "0x" << std::hex << std::setw(4)
                  << std::setfill('0') << from << std::dec << std::setfill(' ')
                  << "  " << std::setw(4) << len << "  ░ padding ░"
                  << clr::reset() << '\n';
    };

    for (const auto& f : fields) {
        if (f.offset > cursor) pad_row(cursor, f.offset - cursor);  // дыра ПЕРЕД полем

        std::cout << "   " << clr::grey() << "0x" << std::hex << std::setw(4)
                  << std::setfill('0') << f.offset << std::dec << std::setfill(' ')
                  << clr::reset() << "  " << std::setw(4) << f.size << "  "
                  << clr::cyan() << std::left << std::setw(25) << f.type
                  << clr::reset() << ' ' << clr::green() << f.value
                  << clr::reset() << std::right << '\n';

        covered += f.size;
        cursor = f.offset + f.size;
    }
    if (cursor < sizeof(T)) pad_row(cursor, sizeof(T) - cursor);    // хвостовой padding

    // --- карта памяти: каждый символ = один байт ------------------------------
    // Поля — голубые █ (чередуются с ▓, чтобы видеть границы),
    // padding — красные ░.
    std::string strip(sizeof(T), 'p');
    for (std::size_t idx = 0; idx < fields.size(); ++idx)
        for (std::size_t b = 0; b < fields[idx].size; ++b)
            strip[fields[idx].offset + b] = (idx % 2 == 0) ? 'A' : 'B';

    std::cout << "   ";
    for (std::size_t i = 0; i < strip.size(); ++i) {
        if (i > 0 && i % 32 == 0) std::cout << "\n   ";
        switch (strip[i]) {
            case 'A': std::cout << clr::cyan()   << "█"; break;
            case 'B': std::cout << clr::cyan()   << "▓"; break;
            default:  std::cout << clr::red()    << "░"; break;
        }
    }
    std::cout << clr::reset() << '\n';

    const std::size_t padding = sizeof(T) - covered;
    std::cout << "   " << clr::grey() << "полезных байт: " << covered
              << "   padding: " << padding << " ("
              << (padding * 100 / sizeof(T)) << "%)" << clr::reset() << "\n\n";
}

// ============================================================================
//  Подопытные
// ============================================================================

// Классика собеседований: одинаковые поля, разный порядок — разный размер.
struct SloppyStack {          // как написал усталый рыцарь
    char grade;               // 1 байт, а дальше? double требует адрес,
    double damage;            //   кратный 8 → компилятор вставит дыру
    char is_upgraded;         // и тут дыра: int ниже требует кратности 4
    int count;                // сколько именно байт пропало — покажет Око
};

struct TidyStack {            // как написал бы гном-казначей: от толстых к тонким
    double damage;
    int count;
    char grade;
    char is_upgraded;
};

// Реалистичный агрегат с std::string и bool
struct Creature {
    std::string name;
    int hp;
    int attack;
    bool flying;
};

int main() {
    std::cout << clr::violet() << "\n  ОКО МАГА / M2: разбор по косточкам\n\n" << clr::reset();

    anatomy(SloppyStack{'A', 6.5, 1, 42},  "SloppyStack — поля вразнобой");
    anatomy(TidyStack{6.5, 42, 'A', 1},    "TidyStack — те же поля, отсортированы");

    std::cout << clr::grey()
              << "  Одни и те же четыре поля. Разница только в ПОРЯДКЕ объявления,\n"
              << "  а платим за неё реальной памятью на каждом объекте.\n\n" << clr::reset();

    anatomy(Creature{"Griffin", 25, 8, true}, "Creature — агрегат из Эрафии");

    // Число полей известно на этапе компиляции — можно потрогать напрямую:
    static_assert(field_count<SloppyStack>() == 4);
    static_assert(field_count<Creature>()    == 4);
    std::cout << clr::grey()
              << "  field_count посчитан компилятором: static_assert'ы выше\n"
              << "  проверились ещё до запуска программы.\n\n" << clr::reset();
}
