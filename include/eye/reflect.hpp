// ============================================================================
//   ОКО МАГА / eye/reflect.hpp — МОДЕЛЬ: факты об объекте (рефлексия)
// ============================================================================
//   Тонкая УМБРЕЛЛА над eye/detail/: «одна ответственность = один заголовок».
//   Здесь только «ЧТО за объект»: имя типа, поля с offset'ами и значениями,
//   раскладка vtable, паспорт-трейты. Ни ANSI, ни рамок — за «КАК это выглядит»
//   отвечает eye/render.hpp. Такое деление model↔view позволяет менять вид,
//   не трогая разбор, и переиспользовать модель (например, отдать факты в JSON).
//
//   Слои:
//     detail/model_types.hpp — структуры данных (их и рисует вид);
//     detail/abi.hpp         — EYE_ITANIUM_ABI (Itanium против MSVC);
//     detail/type_name.hpp   — имя типа: деманглер + очеловечивание std::string;
//     detail/field_count.hpp — подсчёт/обход полей агрегата (M2);
//     detail/traits.hpp      — концепты: реестр, std-адаптеры, virtual-база;
//     detail/reflect_impl.hpp— движок: stringify/annotate/gather/model_of.
//   Ниже — макросы EYE_DESCRIBE / EYE_BASES (реестр имён полей и баз).
//
//   Кроссплатформенно: C++20, GCC/Clang и MSVC. Секция vtable — исследование
//   Itanium ABI; на MSVC отдаётся портируемая часть (динамический тип typeid).
// ============================================================================
#pragma once

#include "detail/model_types.hpp"
#include "detail/abi.hpp"
#include "detail/type_name.hpp"
#include "detail/field_count.hpp"
#include "detail/traits.hpp"
#include "detail/reflect_impl.hpp"

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
