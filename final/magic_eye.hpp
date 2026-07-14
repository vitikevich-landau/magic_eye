// ============================================================================
//   ОКО МАГА / magic_eye.hpp — визуальный инспектор объектов для консоли
// ============================================================================
//   Header-only. Подключил — и смотришь внутрь объектов:
//
//       #include "magic_eye.hpp"
//       eye::inspect(obj);        // паспорт + поля + карта памяти + байты
//       eye::inspect<T>();        // только статика типа (объект не нужен)
//
//   Что покажет — зависит от типа (Око само разберётся):
//     * скаляр                → паспорт + байты
//     * агрегат               → + таблица полей, offset'ы, карта padding
//     * класс с EYE_DESCRIBE  → + ИМЕНА полей, включая private
//     * полиморфный класс     → + vptr, динамический тип, слот vtable
//
//   Собрано из этапов M0–M4 учебной лабы. Кроссплатформенно: C++20 на
//   Linux/macOS (GCC/Clang) и Windows (MSVC). Платформенные различия —
//   деманглер, isatty/ANSI, раскладка vtable — спрятаны за #ifdef ниже.
//   Секция vtable исследовательская: сырые ячейки читаются только под
//   Itanium ABI (GCC/Clang); на MSVC показывается динамический тип через
//   typeid и честная заметка. Рассчитано на 64-битную сборку.
//   Windows/MSVC: нужны флаги /std:c++20 /utf-8 /Zc:preprocessor (см. README).
// ============================================================================
#pragma once

// --- Слой платформенной совместимости ---------------------------------------
// Деманглер и isatty — из Itanium ABI / POSIX (Linux, GCC/Clang). На Windows/MSVC
// их нет: имя типа читаемо через typeid, а isatty и ANSI берём из WinAPI.
// EYE_ITANIUM_ABI=1 там, где доступен __cxa_demangle и раскладка vtable Itanium.
#if (defined(__GNUC__) || defined(__clang__)) && !defined(_MSC_VER)
#  include <cxxabi.h>    // abi::__cxa_demangle — деманглер Itanium ABI
#  define EYE_ITANIUM_ABI 1
#else
#  define EYE_ITANIUM_ABI 0   // MSVC: своя объектная модель
#endif
#if defined(_WIN32)
#  define NOMINMAX             // чтобы windows.h не переопределил std::min/max
#  define WIN32_LEAN_AND_MEAN
#  include <io.h>             // _isatty, _fileno
#  include <windows.h>        // включить обработку ANSI-escape (VT) в консоли
#else
#  include <unistd.h>         // isatty, fileno — POSIX
#endif

#include <algorithm>  // std::sort (карта памяти), std::min (обрезка hex-дампа)
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace eye {

// ════════════════════════════════════════════════════════════════════════════
//  Палитра (M0). ANSI-коды; при выводе не в терминал отключаются сами.
// ════════════════════════════════════════════════════════════════════════════
namespace clr {
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
inline const char* reset()  { return code("\033[0m");        }
inline const char* gold()   { return code("\033[38;5;178m"); }  // заголовки
inline const char* cyan()   { return code("\033[36m");       }  // типы
inline const char* green()  { return code("\033[32m");       }  // значения
inline const char* grey()   { return code("\033[38;5;245m"); }  // служебное
inline const char* violet() { return code("\033[35m");       }  // vptr / магия
inline const char* red()    { return code("\033[38;5;131m"); }  // padding
} // namespace clr

namespace detail {

// Весь разбор vptr/vtable и ширина скрытого поля рассчитаны на 64-битную
// модель LP64 (указатель = 8 байт), как в M3. На 32-битной сборке vptr был бы
// 4 байта, и карта памяти врала бы — поэтому падаем громко на этапе компиляции,
// а не молча рисуем ерунду.
static_assert(sizeof(void*) == 8,
              "magic_eye: секция vptr/vtable рассчитана на 64-битный (LP64) "
              "Itanium ABI (GCC/Clang, Linux, x86-64)");

// ════════════════════════════════════════════════════════════════════════════
//  Имя типа (M0): typeid + деманглер ABI, с очеловечиванием std::string
// ════════════════════════════════════════════════════════════════════════════
// Развернуть манглированное имя в человеческое (компиляторо-зависимо).
inline std::string demangle_raw(const char* mangled) {
#if EYE_ITANIUM_ABI
    int status = 0;
    std::unique_ptr<char, void (*)(void*)> d(
        abi::__cxa_demangle(mangled, nullptr, nullptr, &status), std::free);
    return status == 0 ? std::string(d.get()) : std::string(mangled);
#else
    // MSVC: typeid().name() уже читаемо ("struct Hero", "int * __ptr64") —
    // снимаем служебные слова, чтобы вид совпал с GCC/Clang.
    std::string name = mangled;
    for (const char* junk : {"class ", "struct ", "enum ", " __ptr64"}) {
        const auto len = std::char_traits<char>::length(junk);
        for (auto p = name.find(junk); p != std::string::npos; p = name.find(junk))
            name.erase(p, len);
    }
    return name;
#endif
}

// Свернуть длинную форму std::string обеих стандартных библиотек:
// libstdc++ (std::__cxx11::basic_string<...>) и MSVC (std::basic_string<...>).
inline std::string prettify(std::string name) {
    for (const std::string& ugly :
         {std::string("std::__cxx11::basic_string<char, std::char_traits<char>, "
                      "std::allocator<char> >"),
          std::string("std::basic_string<char,std::char_traits<char>,"
                      "std::allocator<char> >")})
        for (std::size_t p = name.find(ugly); p != std::string::npos;
             p = name.find(ugly))
            name.replace(p, ugly.size(), "std::string");
    return name;
}

// Имя типа по T (на этапе компиляции) и по живому type_info (в рантайме).
template <typename T>
std::string type_name() { return prettify(demangle_raw(typeid(T).name())); }
inline std::string type_name_of(const std::type_info& ti) {
    return prettify(demangle_raw(ti.name()));
}

// ════════════════════════════════════════════════════════════════════════════
//  Подсчёт полей агрегата (M2): «а скомпилируется ли T{ {x}, {x}, ... }?»
//  Скобки вокруг каждого аргумента защищают от brace elision.
// ════════════════════════════════════════════════════════════════════════════
struct any_init {
    template <class T> constexpr operator T() const;  // только объявление
};
template <std::size_t> using any_slot = any_init;

template <class T, std::size_t... Is>
constexpr bool braced_constructible(std::index_sequence<Is...>) {
    return requires { T{ { any_slot<Is>{} }... }; };
}

template <class T, std::size_t N = 0>
constexpr std::size_t field_count() {
    static_assert(N <= sizeof(T) + 1, "field_count: разбег");
    if constexpr (braced_constructible<T>(std::make_index_sequence<N>{})
              && !braced_constructible<T>(std::make_index_sequence<N + 1>{}))
        return N;
    else
        return field_count<T, N + 1>();
}

// Обход полей через structured bindings (M2). Лимит — 8, поднимается
// дописыванием веток (рефлексии нет — пишем имена руками).
template <class T, class F>
void visit_fields(const T& obj, F&& f) {
    constexpr std::size_t N = field_count<T>();
    if constexpr (N == 0) { (void)obj; (void)f;
    } else if constexpr (N == 1) { const auto& [a] = obj; f(a);
    } else if constexpr (N == 2) { const auto& [a,b] = obj; f(a); f(b);
    } else if constexpr (N == 3) { const auto& [a,b,c] = obj; f(a); f(b); f(c);
    } else if constexpr (N == 4) { const auto& [a,b,c,d] = obj;
        f(a); f(b); f(c); f(d);
    } else if constexpr (N == 5) { const auto& [a,b,c,d,e] = obj;
        f(a); f(b); f(c); f(d); f(e);
    } else if constexpr (N == 6) { const auto& [a,b,c,d,e,g] = obj;
        f(a); f(b); f(c); f(d); f(e); f(g);
    } else if constexpr (N == 7) { const auto& [a,b,c,d,e,g,h] = obj;
        f(a); f(b); f(c); f(d); f(e); f(g); f(h);
    } else if constexpr (N == 8) { const auto& [a,b,c,d,e,g,h,i] = obj;
        f(a); f(b); f(c); f(d); f(e); f(g); f(h); f(i);
    } else {
        static_assert(N <= 8, "visit_fields: добавь ветку");
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  Концепты-помощники
// ════════════════════════════════════════════════════════════════════════════
template <class V>
concept printable = requires(std::ostream& os, const V& v) { os << v; };

// Есть ли реестр EYE_DESCRIBE (M4)?
template <class T>
concept described = requires { T::eye_describe(); };

// Агрегат, который Око умеет разбирать автоматически (M2)?
template <class T>
concept auto_inspectable = std::is_aggregate_v<T> && !described<T>;

// ════════════════════════════════════════════════════════════════════════════
//  Общая кухня отрисовки
// ════════════════════════════════════════════════════════════════════════════
struct FieldInfo {
    std::string name;    // пустое, если имя неизвестно (M2-режим)
    std::size_t offset;
    std::size_t size;
    std::string type;
    std::string value;
};

// Одна запись реестра EYE_DESCRIBE: имя поля + указатель-на-член.
// Здесь НЕ std::pair намеренно. У std::tuple есть deduction guide
// `tuple(pair<A,B>) -> tuple<A,B>`, из-за которого EYE_DESCRIBE с ОДНИМ полем
// («tuple из одной пары») схлопывался в двухэлементный tuple<const char*,
// указатель> — и collect ниже падал с ошибкой компиляции. У своей структуры
// такого guide нет: один FieldRef всегда остаётся одним элементом tuple.
// (В M4 то же делалось через std::pair и .first/.second — это его выросшая
// версия, где однополевой реестр больше не ломается.)
template <class MemPtr>
struct FieldRef {
    const char* name;   // из #f на препроцессоре (M4)
    MemPtr      ptr;    // &Self::поле — указатель-на-член (M4)
};
template <class MemPtr> FieldRef(const char*, MemPtr) -> FieldRef<MemPtr>;

// Порядок веток важен: символьные типы, указатели и массивы обязаны быть
// перехвачены ДО общей ветки printable, иначе ostream сделает не то и опасное.
template <class FT>
std::string stringify(const FT& field) {
    using U = std::remove_cvref_t<FT>;
    if constexpr (std::is_same_v<U, char> || std::is_same_v<U, signed char> ||
                  std::is_same_v<U, unsigned char>) {
        // char, signed char, unsigned char — ТРИ разных типа, и ostream печатает
        // каждый как ГЛИФ, а не число. Для инспектора байтов это беда: значение
        // 0x1B (ESC) утащило бы терминал в ANSI-последовательность и испортило
        // весь вывод. Поэтому все три (в т.ч. uint8_t == unsigned char) — руками.
        std::ostringstream oss;
        const auto byte = static_cast<unsigned char>(field);
        if (std::isprint(byte))
            oss << '\'' << static_cast<char>(byte) << '\'';
        else
            oss << "char(" << static_cast<int>(byte) << ')';  // управляющий → код
        return oss.str();
    } else if constexpr (std::is_pointer_v<U>) {
        // Указатель (включая char*!) показываем как АДРЕС, а не разыменовываем.
        // os << (char*) прочитал бы чужую память как C-строку — от мусора до
        // выхода за буфер (а nullptr в старых реализациях ронял программу).
        // Инспектору нужно само значение указателя.
        std::ostringstream oss;
        oss << static_cast<const void*>(field);
        return oss.str();
    } else if constexpr (std::is_array_v<U>) {
        // C-массив печатать нельзя: char[N] распался бы в const char* и читался
        // как строка ЗА границей массива (UB, ловится ASan'ом), int[N] — как
        // адрес. Показываем размер, а байты видны ниже в hex-дампе.
        return "[массив " + std::to_string(sizeof(U)) + " байт]";
    } else if constexpr (printable<U>) {
        std::ostringstream oss;
        if constexpr (std::is_same_v<U, bool>) oss << std::boolalpha;
        oss << field;
        return oss.str();
    } else {
        return "—";  // непечатаемый тип (вложенная структура и т.п.) — честно
    }
}

inline void section(const std::string& title) {
    std::cout << ' ' << clr::gold() << "· " << title << clr::reset() << '\n';
}

// Таблица полей + карта памяти + сводка по padding (M2).
// Объект не нужен — всё уже собрано в fields, от T берём только sizeof.
// Принимаем по ЗНАЧЕНИЮ: реестр EYE_DESCRIBE может перечислить поля не в
// порядке объявления, а карта памяти рисуется по возрастанию offset'а —
// поэтому сортируем свою локальную копию.
template <class T>
void render_fields(std::vector<FieldInfo> fields) {
    std::sort(fields.begin(), fields.end(),
              [](const FieldInfo& a, const FieldInfo& b) {
                  return a.offset < b.offset;
              });

    std::cout << clr::grey()
              << "     offset  size  поле         тип                значение"
              << clr::reset() << '\n';

    auto pad_row = [](std::size_t from, std::size_t len) {
        std::cout << "     " << clr::red() << "0x" << std::hex << std::setw(4)
                  << std::setfill('0') << from << std::dec << std::setfill(' ')
                  << "  " << std::setw(4) << len << "  ░ padding ░"
                  << clr::reset() << '\n';
    };
    auto vptr_row = []() {
        std::cout << "     " << clr::violet() << "0x0000     8  ▒ vptr ▒"
                  << clr::reset() << clr::grey() << "  (скрытое поле, см. M3)"
                  << clr::reset() << '\n';
    };

    // У полиморфного класса первые 8 байт — не «дыра», а vptr.
    // Честность превыше всего: подписываем их отдельно.
    constexpr bool poly = std::is_polymorphic_v<T>;
    const bool vptr_band =
        poly && (fields.empty() || fields.front().offset >= 8);

    std::size_t cursor = 0;
    if (vptr_band) {
        vptr_row();
        cursor = 8;
    }
    for (const auto& f : fields) {
        // Дыра перед полем. Условие f.offset <= sizeof(T) отсекает «дикий»
        // offset (например, у ссылочного члена, чей адрес — не внутри объекта):
        // тогда мусорную строку padding'а не печатаем.
        if (f.offset > cursor && f.offset <= sizeof(T))
            pad_row(cursor, f.offset - cursor);
        std::cout << "     " << clr::grey() << "0x" << std::hex << std::setw(4)
                  << std::setfill('0') << f.offset << std::dec
                  << std::setfill(' ') << clr::reset() << "  " << std::setw(4)
                  << f.size << "  " << clr::green() << std::left << std::setw(13)
                  << (f.name.empty() ? "?" : f.name) << clr::reset()
                  << clr::cyan() << std::setw(18) << f.type << clr::reset()
                  << ' ' << clr::green() << f.value << clr::reset()
                  << std::right << '\n';
        if (f.offset + f.size > cursor) cursor = f.offset + f.size;
    }
    if (cursor < sizeof(T)) pad_row(cursor, sizeof(T) - cursor);

    // Карта памяти: 1 символ = 1 байт. Поля голубые (█/▓ через одно,
    // чтобы видеть границы), padding — красный ░, vptr — фиолетовый ▒.
    std::string strip(sizeof(T), 'p');
    if (vptr_band)
        for (std::size_t b = 0; b < 8; ++b) strip[b] = 'V';
    for (std::size_t i = 0; i < fields.size(); ++i)
        for (std::size_t b = 0; b < fields[i].size; ++b) {
            const std::size_t at = fields[i].offset + b;
            if (at < strip.size())  // защита от «дикого» offset'а — без OOB
                strip[at] = (i % 2 == 0) ? 'A' : 'B';
        }

    std::cout << "     ";
    for (std::size_t i = 0; i < strip.size(); ++i) {
        if (i > 0 && i % 32 == 0) std::cout << "\n     ";
        switch (strip[i]) {
            case 'A': std::cout << clr::cyan()   << "█"; break;
            case 'B': std::cout << clr::cyan()   << "▓"; break;
            case 'V': std::cout << clr::violet() << "▒"; break;
            default:  std::cout << clr::red()    << "░"; break;
        }
    }
    std::cout << clr::reset() << '\n';

    // Сводку считаем ПО КАРТЕ, а не суммой f.size: так дубли/перекрытия полей
    // (например EYE_DESCRIBE(T, x, x)) не приводят к переполнению size_t в
    // «sizeof - covered». Байт либо покрыт полем, либо vptr, либо padding.
    std::size_t field_bytes = 0, vptr_bytes = 0;
    for (char c : strip) {
        if (c == 'A' || c == 'B') ++field_bytes;
        else if (c == 'V')        ++vptr_bytes;
    }
    const std::size_t padding = strip.size() - field_bytes - vptr_bytes;
    std::cout << "     " << clr::grey() << "полезных: " << field_bytes;
    if (vptr_band) std::cout << "  vptr: " << vptr_bytes;
    std::cout << "  padding: " << padding << " ("
              << padding * 100 / sizeof(T) << "%)" << clr::reset() << '\n';
}

// Сбор полей из реестра EYE_DESCRIBE (M4) — с именами, видит private
template <described T>
std::vector<FieldInfo> collect(const T& obj) {
    std::vector<FieldInfo> fields;
    const auto* base = reinterpret_cast<const unsigned char*>(&obj);
    std::apply(
        [&](auto... entry) {
            (..., [&](auto e) {
                const auto& field = obj.*(e.ptr);
                using FT = std::remove_cvref_t<decltype(field)>;
                fields.push_back(FieldInfo{
                    e.name,
                    static_cast<std::size_t>(
                        reinterpret_cast<const unsigned char*>(
                            std::addressof(field)) - base),
                    sizeof(field), type_name<FT>(), stringify<FT>(field)});
            }(entry));
        },
        T::eye_describe());
    return fields;
}

// Сбор полей автоматикой M2 — имена неизвестны, нумеруем
template <auto_inspectable T>
std::vector<FieldInfo> collect(const T& obj) {
    std::vector<FieldInfo> fields;
    const auto* base = reinterpret_cast<const unsigned char*>(&obj);
    std::size_t idx = 0;
    visit_fields(obj, [&](const auto& field) {
        using FT = std::remove_cvref_t<decltype(field)>;
        fields.push_back(FieldInfo{
            "#" + std::to_string(idx++),
            static_cast<std::size_t>(
                reinterpret_cast<const unsigned char*>(std::addressof(field)) -
                base),
            sizeof(field), type_name<FT>(), stringify<FT>(field)});
    });
    return fields;
}

// Hex-дамп (M1)
inline void render_bytes(const void* addr, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(addr);
    const std::size_t shown = std::min<std::size_t>(size, 128);

    for (std::size_t row = 0; row < shown; row += 8) {
        std::cout << "     " << clr::grey() << "0x" << std::hex << std::setw(4)
                  << std::setfill('0') << row << clr::reset() << "  ";
        for (std::size_t i = row; i < row + 8; ++i) {
            if (i < shown)
                std::cout << clr::cyan() << std::setw(2) << std::setfill('0')
                          << static_cast<unsigned>(bytes[i]) << clr::reset()
                          << ' ';
            else
                std::cout << "   ";
        }
        std::cout << ' ' << clr::green() << '|';
        for (std::size_t i = row; i < row + 8 && i < shown; ++i)
            std::cout << (std::isprint(bytes[i]) ? static_cast<char>(bytes[i])
                                                 : '.');
        std::cout << '|' << clr::reset() << '\n';
    }
    std::cout << std::dec << std::setfill(' ');
    if (shown < size)
        std::cout << "     " << clr::grey() << "... ещё " << (size - shown)
                  << " байт скрыто" << clr::reset() << '\n';
}

// Секция vtable (M3). vptr и динамический тип показываем ВЕЗДЕ (это портируемо),
// а служебные ячейки Itanium (offset-to-top, слот[0]) — только под GCC/Clang:
// у MSVC раскладка vtable другая, туда лезть нельзя.
template <class T>
    requires std::is_polymorphic_v<T>
void render_vtable(const T& obj) {
    void* vptr = nullptr;
    std::memcpy(&vptr, &obj, sizeof(vptr));  // первые 8 байт — vptr (обе ABI)

    // Динамический тип берём ПОРТИРУЕМО, через typeid(obj): для полиморфного
    // объекта это и есть его настоящий (динамический) тип. На Itanium данные
    // лежат в vtable[-1] — это разобрано в M3; typeid добирается до них сам.
    std::string dyn = type_name_of(typeid(obj));

    std::cout << "     " << clr::violet() << "vptr           " << clr::reset()
              << vptr << clr::grey() << "  (первые 8 байт объекта)"
              << clr::reset() << '\n';
    std::cout << "     " << clr::violet() << "динамич. тип   " << clr::reset()
              << clr::cyan() << dyn << clr::reset() << clr::grey()
              << "  (typeid объекта — динамический тип, это и есть RTTI)"
              << clr::reset() << '\n';

#if EYE_ITANIUM_ABI
    void** vtable = static_cast<void**>(vptr);
    std::ptrdiff_t off_to_top = 0;
    std::memcpy(&off_to_top, vtable - 2, sizeof(off_to_top));  // vtable[-2]
    void* slot0 = nullptr;
    std::memcpy(&slot0, vtable, sizeof(slot0));                // vtable[0]
    std::cout << "     " << clr::violet() << "offset-to-top  " << clr::reset()
              << off_to_top << '\n';
    std::cout << "     " << clr::violet() << "слот [0]       " << clr::reset()
              << clr::green() << slot0 << clr::reset() << clr::grey()
              << "  (первая виртуальная функция)" << clr::reset() << '\n';
#else
    std::cout << "     " << clr::grey()
              << "offset-to-top и слоты vtable — деталь Itanium ABI (GCC/Clang);\n"
              << "     у MSVC раскладка другая, поэтому сырые ячейки не читаем."
              << clr::reset() << '\n';
#endif
}

// Компактный паспорт (M0)
template <class T>
void render_passport() {
    auto tick = [](bool v) {
        return std::string(v ? clr::green() : clr::grey()) + (v ? "да" : "нет") +
               clr::reset();
    };
    std::cout << "     size / align   " << clr::cyan() << sizeof(T) << " / "
              << alignof(T) << clr::reset() << '\n';
    std::cout << "     polymorphic " << tick(std::is_polymorphic_v<T>)
              << clr::grey() << " · " << clr::reset() << "aggregate "
              << tick(std::is_aggregate_v<T>) << clr::grey() << " · "
              << clr::reset() << "triv.copyable "
              << tick(std::is_trivially_copyable_v<T>) << '\n';
}

} // namespace detail

// ════════════════════════════════════════════════════════════════════════════
//  Реестр EYE_DESCRIBE (M4): даёт Оку имена полей и доступ к private
// ════════════════════════════════════════════════════════════════════════════
// EYE_EXPAND — костыль под legacy-препроцессор MSVC (без /Zc:preprocessor): он
// передаёт __VA_ARGS__ в другой макрос как ОДИН токен, из-за чего лесенка ниже
// не считает аргументы. Лишний проход-разворот EYE_EXPAND(...) заставляет
// препроцессор перечитать и раскрыть их. На GCC/Clang — безвредная тождественность.
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
// Хвостовой `~` — страховка: при ОДНОМ поле NAME оказался бы последним
// аргументом EYE_PICK, и его `...` получил бы ноль токенов, что под -Wpedantic
// даёт предупреждение «требуется хотя бы один аргумент для '...'». Лишний токен
// уходит в `...` и отбрасывается (EYE_PICK возвращает только NAME), но гарантирует,
// что `...` непустой. Позицию NAME он не сдвигает — стоит после всех счётчиков.
#define EYE_FOR_EACH(M, ...)                                          \
    EYE_EXPAND(EYE_PICK(__VA_ARGS__, EYE_FE_8, EYE_FE_7, EYE_FE_6,    \
             EYE_FE_5, EYE_FE_4, EYE_FE_3, EYE_FE_2, EYE_FE_1, ~)(M, __VA_ARGS__))
// #f → строковый литерал с именем поля; &Self::f → указатель-на-член.
// Обёрнуто в FieldRef (не std::pair) — см. комментарий у FieldRef выше.
#define EYE_ENTRY(f) eye::detail::FieldRef{#f, &Self::f},

#define EYE_DESCRIBE(Type, ...)                                       \
    using Self = Type;                                                \
    static constexpr auto eye_describe() {                            \
        return std::tuple{EYE_FOR_EACH(EYE_ENTRY, __VA_ARGS__)};      \
    }

// ════════════════════════════════════════════════════════════════════════════
//  ПУБЛИЧНЫЙ ИНТЕРФЕЙС
// ════════════════════════════════════════════════════════════════════════════

// Полный осмотр живого объекта
template <class T>
void inspect(const T& obj, const std::string& label = "") {
    using detail::type_name;
    const std::string title = label.empty() ? type_name<T>() : label;

    std::cout << '\n' << clr::gold() << "══ Око мага: " << title << " ══"
              << clr::reset() << '\n';
    if (!label.empty())
        std::cout << ' ' << clr::grey() << "тип: " << type_name<T>()
                  << clr::reset() << '\n';

    detail::section("паспорт");
    detail::render_passport<T>();

    // --- поля: реестр > автоматика > честное «не вижу» ------------------------
    if constexpr (detail::described<T>) {
        detail::section("поля (реестр EYE_DESCRIBE)");
        detail::render_fields<T>(detail::collect(obj));
    } else if constexpr (std::is_class_v<T> && std::is_aggregate_v<T>) {
        if constexpr (detail::field_count<T>() <= 8) {
            detail::section("поля (агрегат, имена компилятор стёр)");
            detail::render_fields<T>(detail::collect(obj));
        } else {
            detail::section("поля");
            std::cout << "     " << clr::grey()
                      << "полей больше 8 — подними лимит в visit_fields"
                      << clr::reset() << '\n';
        }
    } else if constexpr (std::is_class_v<T>) {
        detail::section("поля");
        std::cout << "     " << clr::grey()
                  << "непрозрачный класс (конструкторы/private/базы).\n"
                  << "     Хочешь видеть поля — добавь EYE_DESCRIBE."
                  << clr::reset() << '\n';
    }

    // --- vtable для полиморфных ------------------------------------------------
    if constexpr (std::is_polymorphic_v<T>) {
        detail::section("vtable (Itanium ABI, за пределами стандарта)");
        detail::render_vtable(obj);
    }

    // --- сырые байты — всегда ---------------------------------------------------
    detail::section("байты");
    detail::render_bytes(&obj, sizeof(T));
    std::cout << '\n';
}

// Осмотр типа без объекта: только то, что известно на этапе компиляции
template <class T>
void inspect() {
    using detail::type_name;
    std::cout << '\n' << clr::gold() << "══ Око мага (статика): "
              << type_name<T>() << clr::reset() << '\n';
    detail::section("паспорт");
    detail::render_passport<T>();
    if constexpr (std::is_class_v<T> && std::is_aggregate_v<T>)
        std::cout << "     полей: " << clr::green()
                  << detail::field_count<T>() << clr::reset() << '\n';
    std::cout << ' ' << clr::grey()
              << "объекта нет → значений, offset'ов и байтов нет"
              << clr::reset() << "\n\n";
}

} // namespace eye
