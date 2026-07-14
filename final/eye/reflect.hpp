// ============================================================================
//   ОКО МАГА / eye/reflect.hpp — МОДЕЛЬ: факты об объекте (рефлексия)
// ============================================================================
//   Здесь только «ЧТО за объект»: имя типа, поля с offset'ами и значениями,
//   раскладка vtable, паспорт-трейты. Ни одного ANSI-кода и ни одной рамки —
//   за «КАК это выглядит» отвечает eye/render.hpp. Такое деление model↔view
//   позволяет менять внешний вид, не трогая логику разбора, и переиспользовать
//   модель отдельно (например, отдать те же факты в JSON).
//
//   Собрано из приёмов этапов M0–M4. Кроссплатформенно: C++20, GCC/Clang и
//   MSVC. Секция vtable — исследование Itanium ABI (GCC/Clang); на MSVC
//   отдаётся только портируемая часть (динамический тип через typeid).
// ============================================================================
#pragma once

// --- Платформа: деманглер имён (Itanium ABI, GCC/Clang) ----------------------
// EYE_ITANIUM_ABI=1 там, где есть __cxa_demangle и знакомая раскладка vtable.
#if (defined(__GNUC__) || defined(__clang__)) && !defined(_MSC_VER)
#  include <cxxabi.h>    // abi::__cxa_demangle
#  define EYE_ITANIUM_ABI 1
#else
#  define EYE_ITANIUM_ABI 0   // MSVC: своя объектная модель, деманглер не нужен
#endif

#include <cctype>
#include <cstddef>
#include <cstring>       // std::memcpy, std::char_traits
#include <memory>        // std::unique_ptr
#include <sstream>
#include <string>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace eye::detail {

// Разбор vptr/vtable рассчитан на 64-битную модель (указатель = 8 байт).
// На 32-битной сборке карта памяти врала бы — падаем громко ещё при компиляции.
static_assert(sizeof(void*) == 8,
              "magic_eye: секция vptr/vtable рассчитана на 64-битную (LP64/LLP64) "
              "сборку (x86-64)");

// ════════════════════════════════════════════════════════════════════════════
//  Имя типа (M0): typeid + деманглер, с очеловечиванием std::string
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
// дописыванием веток (рефлексии нет — число имён пишем руками).
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
//  Структуры данных модели (их рисует eye/render.hpp)
// ════════════════════════════════════════════════════════════════════════════
struct FieldInfo {
    std::string name;    // "#0" в M2-режиме; настоящее имя из реестра
    std::size_t offset;
    std::size_t size;
    std::string type;
    std::string value;   // уже отформатированное значение (см. stringify)
};

struct Passport {        // ответы компилятора о типе (M0)
    std::size_t size;
    std::size_t align;
    bool polymorphic;
    bool aggregate;
    bool trivially_copyable;
};

struct VtableInfo {      // то, что удалось узнать про полиморфный объект (M3)
    const void*    vptr = nullptr;
    std::string    dyn_type;              // динамический тип (typeid) — портируемо
    bool           itanium = false;       // доступны ли сырые ячейки?
    std::ptrdiff_t offset_to_top = 0;     // vtable[-2] (только Itanium)
    const void*    slot0 = nullptr;       // vtable[0]  (только Itanium)
};

// Одна запись реестра EYE_DESCRIBE: имя поля + указатель-на-член.
// Здесь НЕ std::pair намеренно. У std::tuple есть deduction guide
// `tuple(pair<A,B>) -> tuple<A,B>`, из-за которого EYE_DESCRIBE с ОДНИМ полем
// («tuple из одной пары») схлопывался в двухэлементный tuple и не
// компилировался. У своей структуры такого guide нет: один FieldRef всегда
// остаётся одним элементом tuple. (В M4 то же на std::pair — это выросшая
// версия, где однополевой реестр больше не ломается.)
template <class MemPtr>
struct FieldRef {
    const char* name;   // из #f на препроцессоре (M4)
    MemPtr      ptr;    // &Self::поле — указатель-на-член (M4)
};
template <class MemPtr> FieldRef(const char*, MemPtr) -> FieldRef<MemPtr>;

// ════════════════════════════════════════════════════════════════════════════
//  Значение поля → строка (безопасно для инспектора байтов)
//  Порядок веток важен: символьные типы, указатели и массивы обязаны быть
//  перехвачены ДО общей ветки printable, иначе ostream сделает не то и опасное.
// ════════════════════════════════════════════════════════════════════════════
template <class FT>
std::string stringify(const FT& field) {
    using U = std::remove_cvref_t<FT>;
    if constexpr (std::is_same_v<U, char> || std::is_same_v<U, signed char> ||
                  std::is_same_v<U, unsigned char>) {
        // char/signed char/unsigned char — ТРИ разных типа, и ostream печатает
        // каждый как ГЛИФ, а не число. Значение 0x1B (ESC) утащило бы терминал
        // в ANSI-последовательность. Поэтому все три (в т.ч. uint8_t) — руками.
        std::ostringstream oss;
        const auto byte = static_cast<unsigned char>(field);
        if (std::isprint(byte))
            oss << '\'' << static_cast<char>(byte) << '\'';
        else
            oss << "char(" << static_cast<int>(byte) << ')';  // управляющий → код
        return oss.str();
    } else if constexpr (std::is_pointer_v<U>) {
        // Указатель (включая char*!) — как АДРЕС, а не разыменовываем: os<<(char*)
        // прочитал бы чужую память как C-строку (мусор / выход за буфер / краш).
        std::ostringstream oss;
        oss << static_cast<const void*>(field);
        return oss.str();
    } else if constexpr (std::is_array_v<U>) {
        // C-массив: char[N] распался бы в const char* и читался как строка за
        // границей массива (UB, ловится ASan'ом). Показываем размер; байты — ниже.
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

// ════════════════════════════════════════════════════════════════════════════
//  Сбор фактов об объекте
// ════════════════════════════════════════════════════════════════════════════

// Паспорт: всё, что известно о типе на этапе компиляции (объект не нужен).
template <class T>
Passport passport_of() {
    return Passport{sizeof(T), alignof(T), std::is_polymorphic_v<T>,
                    std::is_aggregate_v<T>, std::is_trivially_copyable_v<T>};
}

// Поля из реестра EYE_DESCRIBE (M4) — с именами, видит private.
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

// Поля автоматикой M2 — имена компилятор стёр, нумеруем #0, #1, ...
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

// Разбор vtable (M3). vptr и динамический тип — портируемо (обе ABI); сырые
// служебные ячейки читаем только под Itanium (иначе оставляем itanium=false).
template <class T>
    requires std::is_polymorphic_v<T>
VtableInfo vtable_info(const T& obj) {
    VtableInfo vi;
    void* vptr = nullptr;
    std::memcpy(&vptr, &obj, sizeof(vptr));  // первые 8 байт — vptr (обе ABI)
    vi.vptr = vptr;
    vi.dyn_type = type_name_of(typeid(obj)); // динамический тип объекта — RTTI
#if EYE_ITANIUM_ABI
    vi.itanium = true;
    void** vtable = static_cast<void**>(vptr);
    std::memcpy(&vi.offset_to_top, vtable - 2, sizeof(vi.offset_to_top));  // [-2]
    std::memcpy(&vi.slot0, vtable, sizeof(vi.slot0));                      // [0]
#endif
    return vi;
}

} // namespace eye::detail

// ════════════════════════════════════════════════════════════════════════════
//  Реестр EYE_DESCRIBE (M4): даёт Оку имена полей и доступ к private.
//  Это часть МОДЕЛИ (как достать факты), поэтому живёт здесь, а не во view.
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
// аргументом EYE_PICK, и его `...` получил бы ноль токенов (-Wpedantic ругнётся).
// Лишний токен уходит в `...` и отбрасывается; позицию NAME не сдвигает.
#define EYE_FOR_EACH(M, ...)                                          \
    EYE_EXPAND(EYE_PICK(__VA_ARGS__, EYE_FE_8, EYE_FE_7, EYE_FE_6,    \
             EYE_FE_5, EYE_FE_4, EYE_FE_3, EYE_FE_2, EYE_FE_1, ~)(M, __VA_ARGS__))
// #f → строковый литерал с именем поля; &Self::f → указатель-на-член.
#define EYE_ENTRY(f) eye::detail::FieldRef{#f, &Self::f},

#define EYE_DESCRIBE(Type, ...)                                       \
    using Self = Type;                                                \
    static constexpr auto eye_describe() {                            \
        return std::tuple{EYE_FOR_EACH(EYE_ENTRY, __VA_ARGS__)};      \
    }
