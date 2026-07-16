// ОКО МАГА / eye/detail/field_count.hpp — подсчёт и обход полей агрегата (M2).
#pragma once
#include <cstddef>
#include <type_traits>
#include <utility>    // std::index_sequence, std::make_index_sequence

namespace eye::detail {

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

// Тот же счётчик, но пробник БЕЗ внутренних скобок: T{ any, ... } вместо
// T{ {any}, ... }. Нужен ТОЛЬКО как детектор (для разбора берётся обычный
// field_count). У «нормальных» агрегатов оба счётчика дают одно число. А член,
// который не терпит копирующую list-инициализацию из {any} — std::atomic,
// std::mutex и прочие некопируемые, — заваливает пробник СО скобками раньше
// времени, и field_count недосчитывается. Расхождение = «этот тип structured
// bindings не разберут так, как посчитал field_count».
template <class T, std::size_t... Is>
constexpr bool flat_constructible(std::index_sequence<Is...>) {
    return requires { T{ (static_cast<void>(Is), any_init{})... }; };
}
template <class T, std::size_t N = 0>
constexpr std::size_t flat_field_count() {
    static_assert(N <= sizeof(T) + 1, "flat_field_count: разбег");
    if constexpr (flat_constructible<T>(std::make_index_sequence<N>{})
              && !flat_constructible<T>(std::make_index_sequence<N + 1>{}))
        return N;
    else
        return flat_field_count<T, N + 1>();
}
template <class T>
inline constexpr bool brace_probe_consistent_v =
    field_count<T>() == flat_field_count<T>();

// Обход полей через structured bindings (M2). Лимит — 8, поднимается
// дописыванием веток (рефлексии нет — число имён пишем руками).
template <class T, class F>
void visit_fields(const T& obj, F&& f) {
    // Ранняя ВНЯТНАЯ диагностика вместо простыни из шаблонов. Ловит агрегаты с
    // членом, который автоматика не разберёт: std::atomic, std::mutex и прочие
    // некопируемые ломают подсчёт полей (field_count недосчитывается — см.
    // brace_probe_consistent_v). Лечится одной строкой EYE_DESCRIBE. Иначе тип
    // молча ушёл бы в кривой structured binding и упал бы глубоко внутри.
    static_assert(brace_probe_consistent_v<T>,
        "ОКО МАГА: этот агрегат нельзя разобрать автоматически — в нём есть "
        "член, ломающий подсчёт полей (обычно std::atomic / std::mutex или "
        "иной некопируемый тип). РЕШЕНИЕ: добавь в тип макрос "
        "EYE_DESCRIBE(ИмяТипа, поле1, поле2, ...) — Око возьмёт поля из реестра, "
        "минуя structured bindings, и покажет их с именами (в т.ч. private). "
        "Если тип чужой и править нельзя — оберни его в свой struct с "
        "EYE_DESCRIBE, либо смотри по указателю только адрес.");
    // Если сборка упала на одной из строк-раскладок НИЖЕ с «cannot decompose
    // class type … anonymous union member» — у типа анонимный union, и это
    // единственный случай, который не отличить трейтами (он неотличим от
    // обычного агрегата). Лечится тем же EYE_DESCRIBE(ИмяТипа, поля...).
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

} // namespace eye::detail
