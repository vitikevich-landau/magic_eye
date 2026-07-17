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

// Потолок пробников. Точный счёт нужен только до 8 (лимит visit_fields ниже):
// всё, что больше, одинаково уходит в «не разобрать автоматикой», поэтому 9
// означает «больше лимита, сколько именно — не важно». Прежний ограничитель
// static_assert(N <= sizeof(T)+1) предполагал «одно поле — минимум байт» и
// РОНЯЛ СБОРКУ на валидных типах, где полей больше, чем байтов: битовые поля
// (9 × unsigned:1 в 4 байтах) и [[no_unique_address]]-пустышки. Пробник обязан
// быть тотальным: неудобный тип — это «скрыто», а не ошибка компиляции у
// пользователя (ревью Codex, PR #5).
inline constexpr std::size_t FIELD_COUNT_CAP = 9;

template <class T, std::size_t N = 0>
constexpr std::size_t field_count() {
    if constexpr (N >= FIELD_COUNT_CAP)
        return FIELD_COUNT_CAP;
    else if constexpr (braced_constructible<T>(std::make_index_sequence<N>{})
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
    if constexpr (N >= FIELD_COUNT_CAP)     // тот же потолок, что у field_count
        return FIELD_COUNT_CAP;
    else if constexpr (flat_constructible<T>(std::make_index_sequence<N>{})
                   && !flat_constructible<T>(std::make_index_sequence<N + 1>{}))
        return N;
    else
        return flat_field_count<T, N + 1>();
}
template <class T>
inline constexpr bool brace_probe_consistent_v =
    field_count<T>() == flat_field_count<T>();

// Ложный след расхождения: C-МАССИВ. any_init конвертируется во что угодно,
// КРОМЕ массива (conversion operator в массивный тип невыводим), поэтому brace
// elision в flat-пробнике срабатывает только на массивах: char tag[4] честно
// съедает четыре плоских any (flat=4), хотя field_count=1 ВЕРЕН — structured
// bindings отдают массив одной привязкой. Такой агрегат разбирается штатно, и
// детектору тут молчать (ревью Codex, PR #5).
//
// Отличаем массив от atomic-подобного члена пробой «слот принимает {any,any}»:
// массив из ≥2 элементов принимает двухэлементный braced-список, скаляр и
// atomic — нет. (Массив из 1 элемента не принимает — но он и flat-пробу не
// раздувает: расхождения нет.) Вложенный агрегат с ≥2 полями тоже принимает,
// но он расхождения не создаёт (any_init конвертируется в него напрямую, без
// elision), поэтому на детектор не влияет. Редкая комбинация «массив И atomic
// в одном агрегате» остаётся на совести криптоошибки — не врём, просто без
// красивой подсказки.
template <class T, std::size_t... Before>
constexpr bool wide_slot_after(std::index_sequence<Before...>) {
    return requires { T{ { any_slot<Before>{} }..., { any_init{}, any_init{} } }; };
}
template <class T, std::size_t... Pos>
constexpr bool any_wide_slot(std::index_sequence<Pos...>) {
    return (... || wide_slot_after<T>(std::make_index_sequence<Pos>{}));
}
template <class T>
inline constexpr bool has_array_member_v =
    any_wide_slot<T>(std::make_index_sequence<field_count<T>()>{});

// Обход полей через structured bindings (M2). Лимит — 8, поднимается
// дописыванием веток (рефлексии нет — число имён пишем руками).
template <class T, class F>
void visit_fields(const T& obj, F&& f) {
    // Ранняя ВНЯТНАЯ диагностика вместо простыни из шаблонов. Ловит агрегаты с
    // членом, который автоматика не разберёт: std::atomic, std::mutex и прочие
    // некопируемые ломают подсчёт полей (field_count недосчитывается — см.
    // brace_probe_consistent_v). Лечится одной строкой EYE_DESCRIBE. Иначе тип
    // молча ушёл бы в кривой structured binding и упал бы глубоко внутри.
    // C-массив расхождение создаёт ЛОЖНО (см. has_array_member_v) — его
    // пропускаем: field_count для него верен и разбор работает. При нуле полей
    // гейт молчит: раскладки не будет вовсе, а расхождение проб на пустышках
    // ([[no_unique_address]]: braced=0, flat=N) безвредно (Codex, PR #5).
    static_assert(field_count<T>() == 0 || brace_probe_consistent_v<T> ||
                      has_array_member_v<T>,
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
