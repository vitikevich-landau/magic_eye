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

#include <algorithm>     // std::min (превью кучи)
#include <array>         // адаптер std::array
#include <cctype>
#include <cstddef>
#include <cstdint>       // std::uintptr_t — сравнение адресов без UB-серости
#include <cstdio>        // std::snprintf (hex-запись целых)
#include <cstdlib>       // std::free
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

// Есть ли реестр EYE_DESCRIBE (M4)? (в т.ч. УНАСЛЕДОВАННЫЙ от базы —
// eye_describe() статическая, имя находится и по наследству.)
template <class T>
concept described = requires { T::eye_describe(); };

// Реестр объявлен в САМОМ T, а не подхвачен от базы. EYE_DESCRIBE ставит
// `using Self = Type;`, поэтому у наследника без своего EYE_DESCRIBE T::Self
// указывает на базу. Без этой проверки поля базы добавились бы дважды: один
// раз при рекурсии в базу, второй — через унаследованный eye_describe().
template <class T>
concept own_described =
    described<T> && requires { requires std::is_same_v<typename T::Self, T>; };

// Агрегат, который Око умеет разбирать автоматически (M2)?
template <class T>
concept auto_inspectable = std::is_aggregate_v<T> && !described<T>;

// Специализации стандартных типов идут отдельными адаптерами: их private-поля
// недоступны, зато публичный API даёт достаточно фактов для честной схемы.
template <class T> struct is_std_vector_impl : std::false_type {};
template <class E, class A>
struct is_std_vector_impl<std::vector<E, A>> : std::true_type {};
template <class T>
inline constexpr bool is_std_vector_v =
    is_std_vector_impl<std::remove_cvref_t<T>>::value;

// std::array<E,N> — агрегат из ОДНОГО C-массива, но structured bindings его
// раскладывают по tuple-протоколу (на N частей), а не по единственному члену.
// Из-за этого автоматика M2 на нём спотыкается — даём отдельный адаптер.
template <class T> struct is_std_array_impl : std::false_type {};
template <class E, std::size_t N>
struct is_std_array_impl<std::array<E, N>> : std::true_type {};
template <class T>
inline constexpr bool is_std_array_v =
    is_std_array_impl<std::remove_cvref_t<T>>::value;

// ════════════════════════════════════════════════════════════════════════════
//  Наследование: реестр баз EYE_BASES и определение virtual-баз
// ════════════════════════════════════════════════════════════════════════════
// Тип-метка базы: EYE_BASES(T, A, B) отдаёт tuple<BaseTag<A>, BaseTag<B>>.
template <class B> struct BaseTag { using type = B; };

// Есть ли у класса реестр баз EYE_BASES (в т.ч. УНАСЛЕДОВАННЫЙ — eye_bases()
// статическая, находится по наследству)?
template <class T>
concept has_bases = requires { T::eye_bases(); };

// Реестр баз объявлен в САМОМ T, а не подхвачен от базы. EYE_BASES(T, …) ставит
// `using eye_bases_self = T;`. Без этой проверки у наследника без своего
// EYE_BASES (Leaf : Mid) has_bases был бы true, и gather применил бы реестр
// Mid к Leaf — привёл бы Leaf к базам Mid, пропустив сам под-объект Mid.
template <class T>
concept own_bases =
    has_bases<T> &&
    requires { requires std::is_same_v<typename T::eye_bases_self, T>; };

// Виртуальная ли база B у D? Приём: обратный привод B*→D* СУЩЕСТВУЕТ для
// невиртуальной базы и ill-formed для виртуальной (нельзя спуститься из vbase).
// Проверяем через SFINAE. Зовём только для реальных доступных баз, поэтому
// «ill-formed по другой причине» здесь не путается с виртуальностью.
template <class D, class B, class = void>
struct is_virtual_base_impl : std::true_type {};
template <class D, class B>
struct is_virtual_base_impl<
    D, B, std::void_t<decltype(static_cast<D*>(std::declval<B*>()))>>
    : std::false_type {};
template <class D, class B>
inline constexpr bool is_virtual_base_v =
    std::is_base_of_v<B, D> && is_virtual_base_impl<D, B>::value;

// ════════════════════════════════════════════════════════════════════════════
//  Структуры данных модели (их рисует eye/render.hpp)
// ════════════════════════════════════════════════════════════════════════════
struct FieldInfo {
    std::string name;        // "#0" в M2-режиме; настоящее имя из реестра
    std::size_t offset = 0;
    std::size_t size   = 0;
    std::size_t align  = 1;  // alignof поля — объясняет дыры ПЕРЕД ним
    std::string type;
    std::string value;       // уже отформатированное значение (см. stringify)
    bool inferred = false;   // не private-поле, а совпавший адресный слот ABI

    // --- принадлежность (наследование) --------------------------------------
    std::string owner;         // класс-владелец поля (метка при наследовании)
    int         base_depth = 0;  // 0 = поле самого производного; >0 — из базы

    // --- аннотации для вида (заполняет annotate) ----------------------------
    enum class Kind { plain, pointer, str };
    Kind kind = Kind::plain;

    bool        integral = false;  // целое → вид покажет hex и little-endian
    std::string alt;               // альтернативная запись значения (hex)

    // kind == pointer | str: куда смотрит (значение указателя / data() строки)
    const void* target = nullptr;
    std::string pointee;           // что лежит по адресу (скалярный pointee)

    // kind == str:
    bool        sso = false;       // буфер внутри объекта (SSO), не в куче
    std::size_t str_len = 0;
    std::size_t str_cap = 0;
    bool        str_layout = false;         // раскладка ptr/len/buf известна
    std::vector<unsigned char> heap_bytes;  // превью буфера из кучи (спутник)
};

struct Passport {        // ответы компилятора о типе (M0)
    std::size_t size;
    std::size_t align;
    bool polymorphic;
    bool aggregate;
    bool trivially_copyable;
};

// Один vptr-сайт. У класса с множественным наследованием их несколько: каждый
// полиморфный под-объект базы держит СВОЙ vptr в начале своего под-объекта.
struct VtableSite {
    std::size_t    offset = 0;            // где в объекте лежит этот vptr
    const void*    vptr = nullptr;
    std::string    owner;                 // чей это vptr ("" = самый производный)
    std::string    dyn_type;              // динамический тип (typeid) — портируемо
    bool           itanium = false;       // доступны ли сырые ячейки?
    std::ptrdiff_t offset_to_top = 0;     // vtable[-2] (только Itanium; ≠0 → вторичная база)
    const void*    slot0 = nullptr;       // vtable[0]  (только Itanium)
};

// Под-объект базового класса внутри наследника (из реестра EYE_BASES).
struct BaseInfo {
    std::string type;                     // имя базового класса
    std::size_t offset = 0;               // смещение под-объекта в наследнике
    int         depth = 0;                // уровень вложенности (для отступа)
    bool        polymorphic = false;      // есть ли у базы свой vptr
    bool        virtual_base = false;     // виртуальная база (ромб)?
    bool        shared = false;           // общий vbase, уже показан выше
};

struct VectorElementInfo {
    std::size_t index = 0;
    std::string value;
    std::vector<unsigned char> bytes;  // первые байты живого элемента
};

// Семантический адаптер std::vector. size/capacity/data и элементы — точные
// факты публичного API. slots — осторожная корреляция этих адресов с сырыми
// словами самого объекта; она помечается знаком ≈ и не выдаётся за стандарт ABI.
struct VectorInfo {
    std::string element_type;
    std::size_t size = 0;
    std::size_t capacity = 0;
    std::size_t element_size = 0;
    std::size_t heap_used = 0;
    std::size_t heap_reserved = 0;
    const void* data = nullptr;
    bool bit_packed = false;       // std::vector<bool>: data() намеренно нет
    bool slots_matched = false;
    std::vector<FieldInfo> slots;
    std::vector<VectorElementInfo> elements;
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
    } else if constexpr (std::is_pointer_v<U> &&
                         std::is_function_v<std::remove_pointer_t<U>>) {
        // Указатель на функцию: static_cast в void* для него ill-formed.
        // reinterpret_cast — conditionally-supported, но работает на всех
        // трёх наших компиляторах (GCC/Clang/MSVC).
        std::ostringstream oss;
        oss << reinterpret_cast<const void*>(field);
        return oss.str();
    } else if constexpr (std::is_pointer_v<U>) {
        // Указатель (включая char*!) — как АДРЕС, а не разыменовываем: os<<(char*)
        // прочитал бы чужую память как C-строку (мусор / выход за буфер / краш).
        std::ostringstream oss;
        // Сначала сохраняем volatile, затем явно снимаем его только для
        // стандартного ostream-overload const void*. Сам адрес не читаем.
        const volatile void* p = static_cast<const volatile void*>(field);
        oss << const_cast<const void*>(p);
        return oss.str();
    } else if constexpr (std::is_array_v<U>) {
        // C-массив: char[N] распался бы в const char* и читался как строка за
        // границей массива (UB, ловится ASan'ом). Показываем размер; байты — ниже.
        return "[массив " + std::to_string(sizeof(U)) + " байт]";
    } else if constexpr (std::is_same_v<U, std::string>) {
        // В кавычках и БЕЗ управляющих байтов: '\n' порвал бы рамку панели,
        // а 0x1b (ESC) инжектил бы живую ANSI-последовательность в терминал.
        std::string s;
        s.reserve(field.size() + 2);
        s += '"';
        for (unsigned char c : field)
            s += (c < 0x20 || c == 0x7f) ? '.' : static_cast<char>(c);
        s += '"';
        return s;
    } else if constexpr (std::is_enum_v<U>) {
        // enum (в т.ч. scoped: он не printable и не integral) — показываем
        // численное значение подлежащего типа; имя элемента без рефлексии не
        // достать. Унарный + промотирует char-based underlying до int, иначе
        // печатался бы глиф вместо числа.
        std::ostringstream oss;
        oss << +static_cast<std::underlying_type_t<U>>(field);
        return oss.str();
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
//  Аннотации поля: семантика, которую вид превратит в выноски и стрелки.
//  Всё вычисляется здесь, в модели, в момент осмотра (пока объект жив).
// ════════════════════════════════════════════════════════════════════════════
template <class FT>
void annotate(FieldInfo& fi, const FT& field) {
    using U = std::remove_cvref_t<FT>;
    fi.align = alignof(U);

    if constexpr ((std::is_integral_v<U> || std::is_enum_v<U>) &&
                  !std::is_same_v<U, bool> && sizeof(U) > 1) {
        // Целое (или enum) шире байта: рядом с десятичным пригодится hex — по
        // нему видно little-endian в дампе (младший байт лежит первым).
        fi.integral = true;
        char b[24];
        unsigned long long v = 0;
        std::memcpy(&v, &field, sizeof(field));   // без знаковых сюрпризов
        std::snprintf(b, sizeof(b), "0x%0*llx", static_cast<int>(sizeof(U) * 2), v);
        fi.alt = b;
    } else if constexpr (std::is_same_v<U, std::string>) {
        fi.kind    = FieldInfo::Kind::str;
        fi.target  = field.data();
        fi.str_len = field.size();
        fi.str_cap = field.capacity();
        // SSO: буфер лежит ВНУТРИ футпринта самой строки? Сравниваем адреса
        // как числа — сравнение «сырых» указателей из разных блоков не для if.
        const auto fb = reinterpret_cast<std::uintptr_t>(&field);
        const auto db = reinterpret_cast<std::uintptr_t>(field.data());
        fi.sso = db >= fb && db < fb + sizeof(field);
#if defined(__GLIBCXX__)
        // libstdc++: знакомая раскладка {ptr, len, union{buf16|cap}} —
        // вид сможет подписать под-регионы поля.
        fi.str_layout = true;
#endif
        if (!fi.sso && field.data() != nullptr) {
            // Буфер в куче: снимем превью для панели-спутника (+1 — '\0').
            const auto* p = reinterpret_cast<const unsigned char*>(field.data());
            const std::size_t n = std::min<std::size_t>(field.size() + 1, 48);
            fi.heap_bytes.assign(p, p + n);
        }
    } else if constexpr (std::is_pointer_v<U> &&
                         !std::is_function_v<std::remove_pointer_t<U>>) {
        fi.kind   = FieldInfo::Kind::pointer;
        const volatile void* p = static_cast<const volatile void*>(field);
        fi.target = const_cast<const void*>(p);
        // Произвольный сырой указатель нельзя безопасно проверить перед
        // разыменованием: он может быть висячим, но всё ещё ненулевым. Поэтому
        // инспектор показывает адрес, однако чужую память сам не читает.
    }
}

// std::vector: получаем точную семантику через public API, а затем ищем внутри
// объекта последовательность трёх машинных слов {data, end, capacity_end}.
// Поиск, а не жёсткие offset'ы, переживает разницу GCC/MSVC и Debug/Release.
template <class E, class A>
VectorInfo vector_info(const std::vector<E, A>& v) {
    VectorInfo info;
    info.element_type = type_name<E>();
    info.size = v.size();
    info.capacity = v.capacity();
    info.element_size = sizeof(E);
    info.bit_packed = std::is_same_v<E, bool>;

    const std::size_t preview = std::min<std::size_t>(v.size(), 8);
    info.elements.reserve(preview);
    for (std::size_t i = 0; i < preview; ++i) {
        VectorElementInfo element;
        element.index = i;
        if constexpr (std::is_same_v<E, bool>) {
            const bool value = v[i];
            element.value = stringify(value);
        } else {
            const E& value = v[i];
            element.value = stringify(value);
            const auto* bytes = reinterpret_cast<const unsigned char*>(
                std::addressof(value));
            const std::size_t n = std::min<std::size_t>(sizeof(E), 8);
            element.bytes.assign(bytes, bytes + n);
        }
        info.elements.push_back(std::move(element));
    }

    if constexpr (std::is_same_v<E, bool>) {
        // vector<bool> хранит упакованные биты и не предоставляет data().
        return info;
    } else {
        info.data = static_cast<const void*>(v.data());
        info.heap_used = v.size() * sizeof(E);
        info.heap_reserved = v.capacity() * sizeof(E);

        if (info.data == nullptr || sizeof(v) < 3 * sizeof(void*)) return info;

        const auto begin = reinterpret_cast<std::uintptr_t>(info.data);
        const auto end = begin + info.heap_used;
        const auto capacity_end = begin + info.heap_reserved;
        const auto* object = reinterpret_cast<const unsigned char*>(
            std::addressof(v));

        std::size_t match = sizeof(v);
        for (std::size_t off = 0; off + 3 * sizeof(void*) <= sizeof(v);
             off += alignof(void*)) {
            std::uintptr_t words[3]{};
            for (std::size_t i = 0; i < 3; ++i)
                std::memcpy(&words[i], object + off + i * sizeof(void*),
                            sizeof(void*));
            if (words[0] == begin && words[1] == end &&
                words[2] == capacity_end) {
                match = off;
                break;
            }
        }
        if (match == sizeof(v)) return info;

        auto add_slot = [&](std::size_t off, std::string name,
                            std::string type, std::uintptr_t value,
                            FieldInfo::Kind kind = FieldInfo::Kind::plain) {
            FieldInfo field;
            field.name = std::move(name);
            field.offset = off;
            field.size = sizeof(void*);
            field.align = alignof(void*);
            field.type = std::move(type);
            field.value = stringify(reinterpret_cast<const void*>(value));
            field.inferred = true;
            field.kind = kind;
            if (kind == FieldInfo::Kind::pointer) {
                field.target = info.data;
                if (!info.elements.empty())
                    field.pointee = "#0 = " + info.elements.front().value;
            }
            info.slots.push_back(std::move(field));
        };

        add_slot(match, "≈ data()/begin", info.element_type + " *", begin,
                 FieldInfo::Kind::pointer);
        add_slot(match + sizeof(void*), "≈ end = data + size", "граница", end);
        add_slot(match + 2 * sizeof(void*), "≈ capacity_end", "граница",
                 capacity_end);
        info.slots_matched = true;
        return info;
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

// Поля из реестра EYE_DESCRIBE (M4) — с именами, видит private. offset'ы
// считаются от md_base (начала САМОГО ПРОИЗВОДНОГО объекта), чтобы при
// наследовании поля базы легли на верные абсолютные смещения.
template <described T>
void append_described(const T& obj, const unsigned char* md_base,
                      const std::string& owner, int depth,
                      std::vector<FieldInfo>& out) {
    std::apply(
        [&](auto... entry) {
            (..., [&](auto e) {
                const auto& field = obj.*(e.ptr);
                using FT = std::remove_cvref_t<decltype(field)>;
                FieldInfo fi;
                fi.name   = e.name;
                fi.offset = static_cast<std::size_t>(
                    reinterpret_cast<const unsigned char*>(
                        std::addressof(field)) - md_base);
                fi.size  = sizeof(field);
                fi.type  = type_name<FT>();
                fi.value = stringify<FT>(field);
                fi.owner = owner;
                fi.base_depth = depth;
                annotate<FT>(fi, field);
                out.push_back(std::move(fi));
            }(entry));
        },
        T::eye_describe());
}

template <described T>
std::vector<FieldInfo> collect(const T& obj) {
    std::vector<FieldInfo> fields;
    append_described<T>(obj, reinterpret_cast<const unsigned char*>(&obj),
                        type_name<T>(), 0, fields);
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
        FieldInfo fi;
        fi.name   = "#" + std::to_string(idx++);
        fi.offset = static_cast<std::size_t>(
            reinterpret_cast<const unsigned char*>(std::addressof(field)) -
            base);
        fi.size  = sizeof(field);
        fi.type  = type_name<FT>();
        fi.value = stringify<FT>(field);
        annotate<FT>(fi, field);
        fields.push_back(std::move(fi));
    });
    return fields;
}

// Элементы std::array — как поля #0, #1, … Все лежат ВНУТРИ объекта подряд
// (offset i*sizeof(E)), кучи нет. type/value/аннотации — как у обычного поля.
template <class E, std::size_t N>
std::vector<FieldInfo> collect_array(const std::array<E, N>& arr) {
    std::vector<FieldInfo> fields;
    const auto* base = reinterpret_cast<const unsigned char*>(std::addressof(arr));
    for (std::size_t i = 0; i < N; ++i) {
        const E& e = arr[i];
        FieldInfo fi;
        fi.name   = "#" + std::to_string(i);
        fi.offset = static_cast<std::size_t>(
            reinterpret_cast<const unsigned char*>(std::addressof(e)) - base);
        fi.size  = sizeof(E);
        fi.type  = type_name<E>();
        fi.value = stringify<E>(e);
        annotate<E>(fi, e);
        fields.push_back(std::move(fi));
    }
    return fields;
}

// Весь объект как ОДНО «поле» — для скаляров, указателей и std::string,
// у которых нет разбираемых полей, но схема памяти всё равно нужна.
template <class T>
FieldInfo self_field(const T& obj) {
    FieldInfo fi;
    fi.name   = std::is_same_v<std::remove_cvref_t<T>, std::string>
                    ? "строка" : "значение";
    fi.offset = 0;
    fi.size   = sizeof(T);
    fi.type   = type_name<T>();
    fi.value  = stringify<T>(obj);
    annotate<T>(fi, obj);
    return fi;
}

// Разбор одного vptr-сайта (M3). vptr и динамический тип — портируемо (обе
// ABI); сырые служебные ячейки читаем только под Itanium. obj — ссылка на
// под-объект (для вторичной базы vptr лежит в НАЧАЛЕ её под-объекта).
template <class T>
    requires std::is_polymorphic_v<T>
VtableSite read_vtable_site(const T& obj, std::size_t offset,
                            const std::string& owner) {
    VtableSite s;
    s.offset = offset;
    s.owner  = owner;
    void* vptr = nullptr;
    std::memcpy(&vptr, std::addressof(obj), sizeof(vptr));  // начало под-объекта = vptr
    s.vptr = vptr;
    s.dyn_type = type_name_of(typeid(obj));  // динамический тип (RTTI, самый производный)
#if EYE_ITANIUM_ABI
    s.itanium = true;
    void** vtable = static_cast<void**>(vptr);
    std::memcpy(&s.offset_to_top, vtable - 2, sizeof(s.offset_to_top));  // [-2]
    std::memcpy(&s.slot0, vtable, sizeof(s.slot0));                      // [0]
#endif
    return s;
}

// ════════════════════════════════════════════════════════════════════════════
//  Модель объекта: поля (свои + унаследованные), под-объекты баз, vptr-сайты.
//  Собирается РЕКУРСИВНО по реестрам EYE_DESCRIBE/EYE_BASES.
// ════════════════════════════════════════════════════════════════════════════
// Диапазон под-объекта НЕразобранной базы (нет своего EYE_DESCRIBE): её байты
// нельзя выдавать за padding — помечаем как «скрытое». size = sizeof базы;
// перекрытие с соседями и вложенными полями вид разрешает сам (закрашивает
// только НЕпокрытые байты этого диапазона).
struct OpaqueSpan {
    std::size_t offset = 0;
    std::size_t size = 0;
    std::string name;   // имя базы — для подписи
};

struct ObjectModel {
    std::vector<FieldInfo>  fields;      // все поля с АБСОЛЮТНЫМИ offset'ами
    std::vector<BaseInfo>   bases;       // под-объекты баз (метки/иерархия)
    std::vector<VtableSite> vptrs;       // все vptr-сайты (у MI их несколько)
    std::vector<std::size_t> vbase_ptrs; // служебные указатели на virtual-базу
    std::vector<OpaqueSpan> opaque_bases;// под-объекты неразобранных баз
    bool has_virtual_base = false;       // есть ли хоть одна virtual-база (ромб)
};

// Рекурсивный обход. md_base — начало самого производного объекта; все offset'ы
// считаются от него. seen — адреса уже учтённых VIRTUAL-баз: общий vbase (ромб)
// встречается по двум путям, но в память лёг ОДИН раз — сверяем по адресу и
// второй раз внутрь не спускаемся. НЕвиртуальные базы не дедупим: под-объект
// базы-в-базе на offset 0 имеет тот же адрес, что и наследник, — совпадение
// адресов у них нормально и НЕ означает общий под-объект.
template <class T>
void gather(const T& obj, ObjectModel& m, const unsigned char* md_base,
            int depth, std::vector<const void*>& seen) {
    const auto* self = reinterpret_cast<const unsigned char*>(std::addressof(obj));
    const std::size_t self_off = static_cast<std::size_t>(self - md_base);

    // 1) vptr этого под-объекта. Первым пишем сайт самого производного (offset 0),
    //    поэтому общий с primary-базой vptr достаётся производному, а не базе.
    if constexpr (std::is_polymorphic_v<T>) {
        bool dup = false;
        for (const auto& s : m.vptrs)
            if (s.offset == self_off) { dup = true; break; }
        if (!dup)
            m.vptrs.push_back(read_vtable_site(obj, self_off, type_name<T>()));
    }

    // 2) базы (глубже) — их поля лягут по абсолютным offset'ам, порядок неважен:
    //    перед отрисовкой всё сортируется по offset. Берём ТОЛЬКО свой реестр
    //    баз — унаследованный eye_bases() относится к базе, не к T.
    bool self_has_vbase = false;
    if constexpr (own_bases<T>) {
        std::apply(
            [&](auto... tag) {
                (..., [&](auto t) {
                    using B = typename decltype(t)::type;
                    const B& b = static_cast<const B&>(obj);
                    const auto* baddr =
                        reinterpret_cast<const unsigned char*>(std::addressof(b));
                    BaseInfo bi;
                    bi.type = type_name<B>();
                    bi.offset = static_cast<std::size_t>(baddr - md_base);
                    bi.depth = depth;
                    bi.polymorphic = std::is_polymorphic_v<B>;
                    bi.virtual_base = is_virtual_base_v<T, B>;
                    if (bi.virtual_base) { self_has_vbase = true; m.has_virtual_base = true; }
                    // Дедуп по адресу — ТОЛЬКО для виртуальных баз.
                    bi.shared =
                        bi.virtual_base &&
                        std::find(seen.begin(), seen.end(),
                                  static_cast<const void*>(baddr)) != seen.end();
                    m.bases.push_back(bi);
                    if (!bi.shared) {
                        if (bi.virtual_base)
                            seen.push_back(static_cast<const void*>(baddr));
                        // База без своего EYE_DESCRIBE не даёт полей для СВОЕГО
                        // хранилища → помечаем её под-объект скрытым, чтобы вид
                        // не выдал байты за padding. (Свои под-базы, если есть,
                        // соберёт рекурсия; вид закрасит только непокрытое.)
                        if constexpr (!own_described<B>)
                            m.opaque_bases.push_back(
                                {bi.offset, sizeof(B), type_name<B>()});
                        gather<B>(b, m, md_base, depth + 1, seen);
                    }
                }(tag));
            },
            T::eye_bases());
    }

    // 2b) НЕполиморфный под-объект с virtual-базой всё равно держит служебный
    //     указатель на virtual-базу в своём начале (Itanium: указатель на vtable
    //     со смещениями vbase; MSVC: vbptr). Полиморфный уже учтён как vptr выше.
    if constexpr (!std::is_polymorphic_v<T>) {
        if (self_has_vbase) {
            bool dup = false;
            for (std::size_t o : m.vbase_ptrs)
                if (o == self_off) { dup = true; break; }
            if (!dup) m.vbase_ptrs.push_back(self_off);
        }
    }

    // 3) собственные поля T (offset'ы от md_base). Только СВОЙ реестр — иначе
    //    у наследника без своего EYE_DESCRIBE подхватился бы унаследованный
    //    eye_describe() и поля базы задвоились бы (их уже собрала рекурсия в п.2).
    //    Незарегистрированную базу НЕ разбираем автоматикой: structured bindings
    //    ill-formed для агрегата «база + своё поле», а надёжно отличить плоский
    //    агрегат от агрегата-с-базой на этапе компиляции нельзя. Её байты уже
    //    помечены скрытыми (opaque_bases в п.2) — честнее, чем разложить наугад.
    if constexpr (own_described<T>)
        append_described<T>(obj, md_base, type_name<T>(), depth, m.fields);
}

template <class T>
ObjectModel model_of(const T& obj) {
    ObjectModel m;
    std::vector<const void*> seen;
    gather<T>(obj, m, reinterpret_cast<const unsigned char*>(std::addressof(obj)),
              0, seen);
    return m;
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

// Реестр баз: EYE_BASES(Type, A, B) → метод eye_bases() c tuple<BaseTag<A>,…>.
// Первым идёт САМ тип (как в EYE_DESCRIBE) — по нему помечаем принадлежность
// реестра (eye_bases_self), чтобы унаследованный eye_bases() не приняли за свой.
// Ставится в public-часть НАСЛЕДНИКА рядом с EYE_DESCRIBE. В самом EYE_DESCRIBE
// тогда перечисляются только СОБСТВЕННЫЕ поля — поля каждой базы Око возьмёт из
// ЕЁ реестра (поэтому видны и private-поля базы). Базы должны быть публичными:
// Око приводит объект к базе через static_cast, а он требует доступной базы.
#define EYE_BASE_ENTRY(B) eye::detail::BaseTag<B>{},
#define EYE_BASES(Type, ...)                                          \
    using eye_bases_self = Type;                                      \
    static constexpr auto eye_bases() {                               \
        return std::tuple{EYE_FOR_EACH(EYE_BASE_ENTRY, __VA_ARGS__)}; \
    }
