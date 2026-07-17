// ОКО МАГА / eye/detail/panel_object.hpp — ПАНЕЛЬ ОБЪЕКТА: модель+вид разом.
//   render_object_panel<T> — та самая панель, которую печатает eye::inspect:
//   картуш, паспорт, иерархия, память, vtable, спутники кучи. Вынесена из
//   magic_eye.hpp, потому что нужна ДВУМ потребителям: печатному inspect и
//   панели деталей интерактивного странствия (туда она уходит через Surface).
//   Это единственный, вместе с nav/, слой-склейка: он знает и движок модели,
//   и вид.
#pragma once
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "field_count.hpp"
#include "frame.hpp"
#include "model_types.hpp"
#include "reflect_impl.hpp"
#include "traits.hpp"
#include "type_name.hpp"
#include "view_hierarchy.hpp"
#include "view_memory.hpp"
#include "view_passport.hpp"
#include "view_satellites.hpp"
#include "view_vtable.hpp"

namespace eye::detail {

// Полный осмотр живого объекта: рамка + все секции (см. eye::inspect).
// Рисует в активный Surface (или в cout, если его нет) по текущей geo().
template <class T>
void render_object_panel(const T& obj, const std::string& label = "") {
    namespace d = eye::detail;
    const std::string title = label.empty() ? d::type_name<T>() : label;

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
    std::string opaque_note;    // ТОЧНАЯ причина «скрыто» (пусто — общая)
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
        // Отдельный случай: это ПЛОСКИЙ агрегат (все поля публичны, своих баз
        // в реестре нет), и отвергла его ровно проверка standard-layout выше.
        // standard-layout ЗАРАЗЕН: его теряет весь агрегат, если хоть одно поле
        // не standard-layout. Живой пример — std::string в MSVC Debug (/MDd →
        // _ITERATOR_DEBUG_LEVEL=2 добавляет в строку debug-proxy: 40 Б вместо
        // 32 и SL=0), тогда как на libstdc++ и в MSVC Release SL=1 и тот же тип
        // разбирается автоматикой. Отсюда «на Linux работает, в VS непрозрачный».
        // Врать про «private/конструкторы» тут нельзя: их нет. Вторая причина
        // сюда же — агрегат с базой, несущей данные (structured bindings его не
        // раскладывают), поэтому std::string помечен вопросом, а не утверждением.
        // Строка короткая: колонка выносок узкая (клип по бюджету ~53), и фразу
        // «добавь EYE_DESCRIBE» держим — на неё смотрит регрессия.
        if constexpr (std::is_aggregate_v<T> && !d::described<T> &&
                      !d::has_bases<T>)
            opaque_note = "не standard-layout (std::string?) добавь EYE_DESCRIBE";
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
        // !vbase_offsets.empty() — записан ли ОТДЕЛЬНЫЙ указатель vbase-ptr:
        // так заметка про virtual-базу не соврёт для НЕполиморфных ромбов.
        d::render_hierarchy(bases, has_vbase, !vbase_offsets.empty());
    }

    d::frame_sep("память · объект @ " + d::hexptr(&obj) + src);
    d::render_memory(fields, sizeof(T), alignof(T), vptr_offsets, vbase_offsets,
                     &obj, opaque, standalone,
                     vector_mode ? &vector : nullptr, opaque_bases, opaque_note);

    // --- vtable для полиморфных (у множественного наследования — несколько) ---
    if (!sites.empty()) {
        d::frame_sep("vtable — гримуар рода (как работает virtual)");
        d::render_vtables(sites, sizeof(T));
    }

    d::frame_bottom();

    // --- спутники: буферы строк, живущие в куче (НЕ внутри объекта) ------------
    d::render_satellites(fields, &obj);
    if (vector_mode) d::render_vector_satellite(vector, &obj);
}

// Осмотр типа без объекта: только то, что известно на этапе компиляции.
template <class T>
void render_type_panel() {
    namespace d = eye::detail;
    d::frame_top(d::type_name<T>() + "  · статика");
    d::frame_sep("паспорт");
    d::render_passport(d::passport_of<T>());
    if constexpr (std::is_class_v<T> && std::is_aggregate_v<T>)
        d::put_text("полей: " + std::to_string(d::field_count<T>()));
    d::put_text("объекта нет → значений, offset'ов и байтов нет");
    d::frame_bottom();
}

} // namespace eye::detail
