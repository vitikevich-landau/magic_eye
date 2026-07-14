// ============================================================================
//   ОКО МАГА / magic_eye.hpp — визуальный инспектор объектов для консоли
// ============================================================================
//   Header-only. Подключил — и смотришь внутрь объектов:
//
//       #include "magic_eye.hpp"
//       eye::inspect(obj);        // паспорт + поля + карта памяти + байты
//       eye::inspect(obj, "имя"); // то же, со своей подписью в заголовке
//       eye::inspect<T>();        // только статика типа (объект не нужен)
//
//   Что покажет — зависит от типа (Око само разберётся):
//     * скаляр                → паспорт + байты
//     * агрегат               → + таблица полей, offset'ы, карта padding
//     * класс с EYE_DESCRIBE  → + ИМЕНА полей, включая private
//     * полиморфный класс     → + vptr, динамический тип, слот vtable
//
//   Устройство: этот файл — тонкая СКЛЕЙКА. Вся логика поделена на два слоя:
//     * eye/reflect.hpp — МОДЕЛЬ: как достать факты об объекте (рефлексия);
//     * eye/render.hpp  — ВИД:   как эти факты нарисовать (тема «Гримуар»).
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
#include "eye/render.hpp"    // вид: рамки, цвет, отрисовка

namespace eye {

// ════════════════════════════════════════════════════════════════════════════
//  ПУБЛИЧНЫЙ ИНТЕРФЕЙС
// ════════════════════════════════════════════════════════════════════════════

// Полный осмотр живого объекта.
template <class T>
void inspect(const T& obj, const std::string& label = "") {
    namespace d = detail;
    const std::string title = label.empty() ? d::type_name<T>() : label;

    std::cout << '\n';
    d::frame_top(title);
    if (!label.empty()) {  // подпись задана — покажем ещё и настоящий тип
        d::Line l;
        l.col(clr::grey(), "тип: ").col(clr::cyan(),
                                        d::clip(d::type_name<T>(), d::FRAME_W - 5));
        d::put(l);
    }

    // --- паспорт (всегда) -----------------------------------------------------
    d::frame_sep("паспорт");
    d::render_passport(d::passport_of<T>());

    // --- поля: реестр > автоматика > честное «не вижу» ------------------------
    if constexpr (d::described<T>) {
        d::frame_sep("поля · реестр EYE_DESCRIBE");
        d::render_fields(d::collect(obj), sizeof(T), std::is_polymorphic_v<T>);
    } else if constexpr (std::is_class_v<T> && std::is_aggregate_v<T>) {
        if constexpr (d::field_count<T>() <= 8) {
            d::frame_sep("поля · агрегат (имена стёрты)");
            d::render_fields(d::collect(obj), sizeof(T), std::is_polymorphic_v<T>);
        } else {
            d::frame_sep("поля");
            d::put_text("полей больше 8 — подними лимит в visit_fields");
        }
    } else if constexpr (std::is_class_v<T>) {
        d::frame_sep("поля");
        d::put_text("непрозрачный класс (конструкторы/private/базы).");
        d::put_text("Хочешь видеть поля — добавь EYE_DESCRIBE.");
    }

    // --- vtable для полиморфных ------------------------------------------------
    if constexpr (std::is_polymorphic_v<T>) {
        d::frame_sep("vtable · Itanium ABI, за пределами стандарта");
        d::render_vtable(d::vtable_info(obj));
    }

    // --- сырые байты (всегда) --------------------------------------------------
    d::frame_sep("байты");
    d::render_bytes(&obj, sizeof(T));

    d::frame_bottom();
    std::cout << '\n';
}

// Осмотр типа без объекта: только то, что известно на этапе компиляции.
template <class T>
void inspect() {
    namespace d = detail;
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
