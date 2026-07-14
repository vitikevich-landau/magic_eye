// ============================================================================
//   ОКО МАГА / magic_eye.hpp — визуальный инспектор объектов для консоли
// ============================================================================
//   Header-only. Подключил — и смотришь внутрь объектов:
//
//       #include "magic_eye.hpp"
//       eye::inspect(obj);        // паспорт + схема памяти (+ vtable, куча)
//       eye::inspect(obj, "имя"); // то же, со своей подписью в заголовке
//       eye::inspect<T>();        // только статика типа (объект не нужен)
//
//   Что покажет — зависит от типа (Око само разберётся):
//     * скаляр / указатель    → паспорт + схема памяти (little-endian, адреса)
//     * агрегат               → + регионы полей, padding-дыры с причинами
//     * класс с EYE_DESCRIBE  → + ИМЕНА полей, включая private
//     * полиморфный класс     → + регион vptr и блок-диаграмма vtable
//     * std::string           → SSO против кучи; кучный буфер — панель-спутник
//
//   Вид управляется переменными окружения:
//     EYE_WIDTH=N   — считать терминал N колонок (иначе определяем сами);
//     EYE_CENTER=0  — не центрировать (для отчётов и диффов);
//     EYE_COLOR=1/0 — форсировать цвета вкл/выкл;
//     EYE_FULL=1    — не сворачивать длинные регионы (⋯ ещё N Б ⋯).
//     EYE_RESIZE=0  — не расширять обычную Windows-консоль до 126 колонок.
//
//   Устройство: этот файл — тонкая СКЛЕЙКА. Вся логика поделена на два слоя:
//     * eye/reflect.hpp — МОДЕЛЬ: как достать факты об объекте (рефлексия);
//     * eye/render.hpp  — ВИД:   как эти факты нарисовать («Гримуар v2»).
//   Хочешь другой внешний вид — правишь render.hpp; нужны только данные
//   (скажем, для своего вывода в JSON) — берёшь reflect.hpp отдельно.
//
//   Собрано из этапов M0–M4 учебной лабы. Кроссплатформенно: C++20 на
//   Linux/macOS (GCC/Clang) и Windows (MSVC). Секция vtable — исследование
//   Itanium ABI; на MSVC отдаётся портируемая часть (тип через typeid).
//   Windows/MSVC: флаги /std:c++20 /utf-8 /Zc:preprocessor (см. README).
// ============================================================================
#pragma once

#include "eye/reflect.hpp"   // модель: факты об объекте (+ макрос EYE_DESCRIBE)
#include "eye/render.hpp"    // вид: рамки, цвет, схема памяти, выноски

namespace eye {

// ════════════════════════════════════════════════════════════════════════════
//  ПУБЛИЧНЫЙ ИНТЕРФЕЙС
// ════════════════════════════════════════════════════════════════════════════

// Полный осмотр живого объекта.
template <class T>
void inspect(const T& obj, const std::string& label = "") {
    namespace d = detail;
    d::geo_refresh();   // ширина терминала могла измениться между панелями
    const std::string title = label.empty() ? d::type_name<T>() : label;

    std::cout << '\n';
    d::frame_top(title);
    if (!label.empty()) {  // подпись задана — покажем ещё и настоящий тип
        d::Line l;
        l.col(clr::grey(), "тип: ").col(clr::cyan(),
                                        d::clip(d::type_name<T>(),
                                                d::frame_width() - 5));
        d::put(l);
    }

    // --- паспорт (всегда) -----------------------------------------------------
    d::frame_sep("паспорт");
    d::render_passport(d::passport_of<T>());

    // --- схема памяти: реестр > std::string > автоматика > честное «скрыто» ---
    std::vector<d::FieldInfo> fields;
    std::string src;            // откуда взялись имена — приписка к заголовку
    bool opaque = false;        // содержимое скрыто (нет способа разобрать)
    bool standalone = false;    // объект = одно значение (скаляр, строка)

    if constexpr (d::described<T>) {
        fields = d::collect(obj);
        src = " · имена из EYE_DESCRIBE";
    } else if constexpr (std::is_same_v<T, std::string>) {
        fields.push_back(d::self_field(obj));
        standalone = true;
    } else if constexpr (std::is_class_v<T> && std::is_aggregate_v<T>) {
        if constexpr (d::field_count<T>() <= 8) {
            fields = d::collect(obj);
            src = " · агрегат (имена стёрты)";
        } else {
            opaque = true;      // полей больше 8 — подними лимит visit_fields
        }
    } else if constexpr (std::is_class_v<T>) {
        opaque = true;          // конструкторы/private/базы — нужен EYE_DESCRIBE
    } else {
        fields.push_back(d::self_field(obj));   // скаляр или указатель
        standalone = true;
    }

    d::frame_sep("память · объект @ " + d::hexptr(&obj) + src);
    d::render_memory(fields, sizeof(T), alignof(T), std::is_polymorphic_v<T>,
                     &obj, opaque, standalone);

    // --- vtable для полиморфных ------------------------------------------------
    if constexpr (std::is_polymorphic_v<T>) {
        d::frame_sep("vtable — как работает virtual");
        d::render_vtable(d::vtable_info(obj), sizeof(T));
    }

    d::frame_bottom();

    // --- спутники: буферы строк, живущие в куче (НЕ внутри объекта) ------------
    d::render_satellites(fields, &obj);
    std::cout << '\n';
}

// Осмотр типа без объекта: только то, что известно на этапе компиляции.
template <class T>
void inspect() {
    namespace d = detail;
    d::geo_refresh();
    std::cout << '\n';
    d::frame_top(d::type_name<T>() + "  · статика");
    d::frame_sep("паспорт");
    d::render_passport(d::passport_of<T>());
    if constexpr (std::is_class_v<T> && std::is_aggregate_v<T>)
        d::put_text("полей: " + std::to_string(d::field_count<T>()));
    d::put_text("объекта нет → значений, offset'ов и байтов нет");
    d::frame_bottom();
    std::cout << '\n';
}

} // namespace eye
