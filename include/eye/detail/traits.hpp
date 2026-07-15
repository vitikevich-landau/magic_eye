// ОКО МАГА / eye/detail/traits.hpp — концепты и предикаты типов (реестр/std/vbase).
#pragma once
#include <array>
#include <ostream>    // std::ostream в концепте printable
#include <type_traits>
#include <utility>    // std::declval, std::void_t
#include <vector>

namespace eye::detail {

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

} // namespace eye::detail
