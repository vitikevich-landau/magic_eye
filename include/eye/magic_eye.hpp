// ============================================================================
//   ОКО МАГА / magic_eye.hpp — визуальный инспектор объектов для консоли
// ============================================================================
//   Header-only. Подключил — и смотришь внутрь объектов:
//
//       #include <eye/magic_eye.hpp>
//       eye::inspect(obj);        // паспорт + схема памяти (+ vtable, куча)
//       eye::inspect(obj, "имя"); // то же, со своей подписью в заголовке
//       eye::inspect<T>();        // только статика типа (объект не нужен)
//
//   Что покажет — зависит от типа (Око само разберётся):
//     * скаляр / указатель    → паспорт + схема памяти (little-endian, адреса)
//     * агрегат               → + регионы полей, padding-дыры с причинами
//     * класс с EYE_DESCRIBE  → + ИМЕНА полей, включая private
//     * + EYE_BASES           → под-объекты баз, поля с меткой «из базы»
//     * полиморфный класс     → + регион(ы) vptr и блок-диаграмма vtable
//     * множ./ромб-наследов.  → несколько vptr, общая virtual-база (помечена)
//     * std::string           → SSO против кучи; кучный буфер — панель-спутник
//     * std::vector           → адресные слова + внешний массив элементов
//     * std::array            → элементы #0,#1,… внутри объекта (без кучи)
//
//   Вид управляется переменными окружения:
//     EYE_WIDTH=N   — считать терминал N колонок (иначе определяем сами);
//     EYE_CENTER=0  — не центрировать (для отчётов и диффов);
//     EYE_COLOR=1/0 — форсировать цвета вкл/выкл;
//     EYE_FULL=1    — не сворачивать длинные регионы (⋯ ещё N Б ⋯).
//     EYE_RESIZE=0  — не разворачивать Windows-консоль во весь экран.
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

#include "reflect.hpp"   // модель: факты об объекте (+ макрос EYE_DESCRIBE)
#include "render.hpp"    // вид: рамки, цвет, схема памяти, выноски

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

    // --- сбор фактов: реестр > адаптеры std > автоматика > честное «скрыто» ---
    std::vector<d::FieldInfo>  fields;
    std::vector<d::BaseInfo>   bases;    // под-объекты баз (EYE_BASES)
    std::vector<d::VtableSite> sites;    // vptr-сайты (у MI их несколько)
    std::vector<std::size_t>   vbase_offsets;  // служебные указатели на vbase
    std::vector<d::OpaqueSpan> opaque_bases;   // под-объекты неразобранных баз
    bool has_vbase = false;              // есть ли virtual-база (ромб)
    d::VectorInfo vector;
    std::string src;            // откуда взялись имена — приписка к заголовку
    bool opaque = false;        // содержимое скрыто (нет способа разобрать)
    bool standalone = false;    // объект = одно значение (скаляр, строка)
    bool vector_mode = false;

    // Через model_of идут только типы со СВОИМ реестром (own_*): у наследника,
    // лишь УНАСЛЕДОВАВШЕГО eye_describe/eye_bases, чужой реестр к нему неприменим
    // — он честно уходит в «непрозрачный» (иначе карта соврала бы padding'ом).
    if constexpr (d::own_described<T> || d::own_bases<T>) {
        d::ObjectModel model = d::model_of(obj);  // рекурсия по EYE_DESCRIBE/EYE_BASES
        fields = std::move(model.fields);
        bases  = std::move(model.bases);
        sites  = std::move(model.vptrs);
        vbase_offsets = std::move(model.vbase_ptrs);
        opaque_bases = std::move(model.opaque_bases);
        has_vbase = model.has_virtual_base;
        src = d::own_bases<T> ? " · EYE_DESCRIBE + базы"
                              : " · имена из EYE_DESCRIBE";
    } else if constexpr (std::is_same_v<T, std::string>) {
        fields.push_back(d::self_field(obj));
        standalone = true;
    } else if constexpr (d::is_std_vector_v<T>) {
        vector = d::vector_info(obj);
        fields = vector.slots;
        opaque = true;          // нераспознанные слова остаются честно скрытыми
        vector_mode = true;
        src = " · адаптер std::vector";
    } else if constexpr (d::is_std_array_v<T>) {
        fields = d::collect_array(obj);
        src = " · адаптер std::array";
    } else if constexpr (std::is_class_v<T> && std::is_aggregate_v<T> &&
                         std::is_standard_layout_v<T> &&
                         !d::described<T> && !d::has_bases<T>) {
        // Плоский агрегат без реестра. Ключевое условие — standard-layout: у
        // него все нестатические поля лежат в ОДНОМ классе, поэтому structured
        // bindings гарантированно разложат его (это по стандарту). Агрегат с
        // базой, где поля разбиты между базой и наследником, НЕ standard-layout
        // → уходит в «непрозрачный», а не роняет компиляцию. Кроссплатформенно.
        if constexpr (d::field_count<T>() == 0 && !std::is_empty_v<T>) {
            // ПУСТАЯ база «съедает» первый braced-слот счётчика, и field_count
            // даёт 0, хотя поля есть (struct D : Empty { int x; }). Structured
            // bindings разложили бы D, но по счётчику 0 — честнее показать
            // непрозрачным, чем потерять поля в padding.
            opaque = true;
        } else if constexpr (d::field_count<T>() <= 8) {
            fields = d::collect(obj);
            src = " · агрегат (имена стёрты)";
        } else {
            opaque = true;      // полей больше 8 — подними лимит visit_fields
        }
    } else if constexpr (std::is_class_v<T>) {
        opaque = true;          // конструкторы/private/базы/чужой реестр — нужен EYE_DESCRIBE
    } else {
        fields.push_back(d::self_field(obj));   // скаляр или указатель
        standalone = true;
    }

    // Полиморфный тип, НЕ прошедший через model_of (без своего реестра), —
    // один primary vptr. У прошедших через model_of сайты уже собраны.
    if constexpr (std::is_polymorphic_v<T> && !d::own_described<T> && !d::own_bases<T>)
        sites.push_back(d::read_vtable_site(obj, 0, d::type_name<T>()));

    std::vector<std::size_t> vptr_offsets;
    vptr_offsets.reserve(sites.size());
    for (const d::VtableSite& s : sites) vptr_offsets.push_back(s.offset);

    // --- иерархия: под-объекты баз (только при наследовании через EYE_BASES) --
    if (!bases.empty()) {
        d::frame_sep("иерархия — под-объекты баз");
        d::render_hierarchy(bases, has_vbase);
    }

    d::frame_sep("память · объект @ " + d::hexptr(&obj) + src);
    d::render_memory(fields, sizeof(T), alignof(T), vptr_offsets, vbase_offsets,
                     &obj, opaque, standalone,
                     vector_mode ? &vector : nullptr, opaque_bases);

    // --- vtable для полиморфных (у множественного наследования — несколько) ---
    if (!sites.empty()) {
        d::frame_sep("vtable — гримуар рода (как работает virtual)");
        d::render_vtables(sites, sizeof(T));
    }

    d::frame_bottom();

    // --- спутники: буферы строк, живущие в куче (НЕ внутри объекта) ------------
    d::render_satellites(fields, &obj);
    if (vector_mode) d::render_vector_satellite(vector, &obj);
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
