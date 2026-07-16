// ОКО МАГА / eye/detail/nav/nav_build.hpp — СТРОИТЕЛЬ навигационного графа.
//   Превращает живой объект в дерево NavNode с ленивыми детьми. Диспетчер тот
//   же, что у панели inspect: свой реестр > адаптеры std > автоматика >
//   честное «скрыто». Замыкания захватывают ТИПИЗИРОВАННЫЕ ссылки (адрес +
//   тип известны на этапе компиляции), поэтому раскрытие узла и — в M-D —
//   переход по указателю строят детей без потери типа.
//
//   Слой-склейка (как panel_object.hpp): знает и движок модели, и вид.
//   Контракт времени жизни: объекты галереи и всё достижимое из них живут,
//   пока идёт Gallery::run() — то же требование, что у inspect, растянутое
//   на время странствия.
#pragma once
#include <array>
#include <cstddef>
#include <memory>        // unique_ptr/shared_ptr — адаптер умных указателей
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "../panel_object.hpp"   // render_object_panel + модель + вид
#include "nav_node.hpp"

namespace eye::detail::nav {

inline constexpr std::size_t NAV_PAGE = 100;   // страница элементов коллекции

// ────────────────────────────────────────────────────────────────────────────
//  Переходимость указателей (M-D): по какому U* Око согласно идти дальше
// ────────────────────────────────────────────────────────────────────────────

// Символьные типы: C-строка? Длина неизвестна — чужую память не читаем.
template <class U>
inline constexpr bool is_char_like_v =
    std::is_same_v<U, char> || std::is_same_v<U, signed char> ||
    std::is_same_v<U, unsigned char> || std::is_same_v<U, wchar_t> ||
    std::is_same_v<U, char8_t> || std::is_same_v<U, char16_t> ||
    std::is_same_v<U, char32_t>;

// Полон ли тип В ЭТОЙ единице трансляции? sizeof требует полного типа —
// классический SFINAE-детектор. Нужен, чтобы не трогать трейты у PIMPL-типов.
template <class U, class = void>
struct is_complete_impl : std::false_type {};
template <class U>
struct is_complete_impl<U, std::void_t<decltype(sizeof(U))>> : std::true_type {};
template <class U>
inline constexpr bool is_complete_v = is_complete_impl<U>::value;

// Умные указатели: unique_ptr / shared_ptr — переход через .get().
template <class T> struct smart_pointee { using type = void; };
template <class V, class D> struct smart_pointee<std::unique_ptr<V, D>> {
    using type = V;
};
template <class V> struct smart_pointee<std::shared_ptr<V>> { using type = V; };
// МАССИВНЫЕ специализации гасим: обобщённая ветка выше поймала бы
// unique_ptr<int[]> с V = int[], и `const V* p = field.get()` означало бы
// «инициализировать const int(*)[] из int*» — жёсткая ошибка сборки. Да и
// переходить там некуда: длину массив не несёт (ревью Codex, PR #5).
template <class V, class D> struct smart_pointee<std::unique_ptr<V[], D>> {
    using type = void;
};
template <class V> struct smart_pointee<std::shared_ptr<V[]>> {
    using type = void;
};
template <class T>
inline constexpr bool is_smart_ptr_v =
    !std::is_void_v<typename smart_pointee<std::remove_cvref_t<T>>::type>;

// Умный указатель на МАССИВ — отдельным трейтом, чтобы отказать честно
// (сообщением), а не молча промолчать.
template <class T> struct is_array_smart_ptr : std::false_type {};
template <class V, class D>
struct is_array_smart_ptr<std::unique_ptr<V[], D>> : std::true_type {};
template <class V>
struct is_array_smart_ptr<std::shared_ptr<V[]>> : std::true_type {};
template <class T>
inline constexpr bool is_array_smart_ptr_v =
    is_array_smart_ptr<std::remove_cvref_t<T>>::value;

// Тип, у которого есть осмысленный объектный узел (тот же диспетчер, что у
// панели inspect). Непрозрачные классы не переходимы: карта соврала бы.
//
// Полнота проверяется ОТДЕЛЬНОЙ специализацией, а не через && в инициализаторе:
// && короткозамыкает вычисление, но НЕ инстанцирование, а is_aggregate_v /
// is_standard_layout_v на неполном типе — жёсткая ошибка сборки. Иначе класс с
// полем `Forward* p` (PIMPL) переставал бы компилироваться, хотя eye::inspect
// его показывает (ревью Codex, PR #5).
template <class U, bool Complete = is_complete_v<U>>
struct followable_impl : std::false_type {};
template <class U>
struct followable_impl<U, true>
    : std::bool_constant<
          (std::is_arithmetic_v<U> && !is_char_like_v<U>) ||
          std::is_enum_v<U> || std::is_pointer_v<U> || own_described<U> ||
          own_bases<U> || std::is_same_v<U, std::string> ||
          is_std_vector_v<U> || is_std_array_v<U> ||
          (std::is_class_v<U> && std::is_aggregate_v<U> &&
           std::is_standard_layout_v<U> && !described<U> && !has_bases<U>)> {};
template <class U>
inline constexpr bool followable_v = followable_impl<U>::value;

template <class T>
NavNode make_object_node(const T& obj, std::string label = "",
                         NodeKind kind = NodeKind::object);

// Оснастить узел переходом по УКАЗАТЕЛЮ на U (обычному или умному). Значение
// указателя снято в момент построения узла (пересобирается при свёртке-
// раскрытии родителя); сам pointee читается только при переходе.
template <class U>
void arm_follow_to(NavNode& n, const U* p, const std::string& via) {
    if (p == nullptr) {
        n.follow_block = "переход невозможен: nullptr";
        return;
    }
    // Про указатель на функцию отвечает arm_follow ДО вызова сюда: вывести
    // `const U*` из int(*)() нельзя (const у функционального типа ill-formed),
    // и никакая ветка здесь просто не дожила бы до подстановки.
    if constexpr (std::is_void_v<U>) {
        n.follow_block = "void*: тип стёрт — Око не гадает";
    } else if constexpr (is_char_like_v<U>) {
        n.follow_block = "C-строка? длина неизвестна — чужое не читаем";
    } else if constexpr (std::is_volatile_v<U>) {
        n.follow_block = "volatile: чтение — побочный эффект, не трогаем";
    } else if constexpr (!is_complete_v<std::remove_cv_t<U>>) {
        // PIMPL: определения типа в этой TU нет — ни размера, ни полей.
        n.follow_block = "тип неполный: определения нет в этой TU (PIMPL?)";
    } else if constexpr (!followable_v<std::remove_cv_t<U>>) {
        n.follow_block = "тип непрозрачен — нужен EYE_DESCRIBE";
    } else {
        using V = std::remove_cv_t<U>;
        const V* target = const_cast<const V*>(p);
        n.can_follow = true;
        n.follow = [target, via]() {
            return make_object_node<V>(*target, "*" + via, NodeKind::object);
        };
    }
}

// Разобрать статический тип поля: сырой указатель / умный указатель.
template <class FT>
void arm_follow(NavNode& n, const FT& field, const std::string& via) {
    using U0 = std::remove_cvref_t<FT>;
    if constexpr (std::is_pointer_v<U0> &&
                  std::is_function_v<std::remove_pointer_t<U0>>) {
        // Указатель на функцию отсекаем ЗДЕСЬ, до arm_follow_to: там параметр
        // `const U*`, а вывести U из int(*)() нельзя — const у функционального
        // типа ill-formed, и сборка падала бы ещё на подстановке, не дойдя до
        // объяснения (ревью Codex, PR #5).
        n.follow_block = field == nullptr
                             ? "переход невозможен: nullptr"
                             : "указатель на функцию: там код, не данные";
    } else if constexpr (std::is_pointer_v<U0>) {
        arm_follow_to(n, field, via);
    } else if constexpr (is_array_smart_ptr_v<U0>) {
        n.follow_block = "умный указатель на массив: длина неизвестна";
    } else if constexpr (is_smart_ptr_v<U0>) {
        using V = typename smart_pointee<U0>::type;
        const V* p = field.get();
        n.preview = p == nullptr ? "→ ∅" : "→ " + hexptr(p);
        // Полное имя unique_ptr не влезает в строку дерева — очеловечиваем;
        // точный тип остаётся в панели деталей.
        n.type = "умный указатель на " + type_name<V>();
        arm_follow_to(n, p, via);
    }
}

// ────────────────────────────────────────────────────────────────────────────
//  Панели деталей (рисуют в активный Surface по текущей geo())
// ────────────────────────────────────────────────────────────────────────────

// Чистый hex-дамп диапазона узла (режим [x]): те же строки-сетки, что карта
// памяти, но без выносок — просто байты с абсолютной колонкой offset mod 8.
inline void render_hex_panel(const std::string& title, const void* addr,
                             std::size_t size, std::size_t start_off = 0) {
    frame_top(title + " · hex");
    if (addr == nullptr || size == 0) {
        put_text("байтов нет: у узла нет адреса (или размер 0)");
        frame_bottom();
        return;
    }
    put_text("объём " + std::to_string(size) + " Б @ " + hexptr(addr));
    Line h;
    h.col(clr::grey(), "off").to(MEM_HEX_COL);
    for (int i = 0; i < 8; ++i)
        h.col(clr::dim(), "+" + std::to_string(i)).sp(i == 7 ? 0 : 1);
    h.to(MEM_ASCII_COL).col(clr::dim(), "ascii");
    put(h);
    const auto* base =
        static_cast<const unsigned char*>(addr) - start_off;   // база строк
    FieldInfo probe;               // регион «поле» без выносок — только глиф
    probe.name = title;
    probe.offset = start_off;
    probe.size = size;
    const Region r{Region::R::field, start_off, size, &probe, false, 0, 0};
    for (const MRow& row : region_rows(start_off, size))
        put(mem_row(row, r, base));
    frame_bottom();
}

// Паспорт-панель поля (режим [p]): тип/размер/выравнивание/offset/владелец.
inline void render_field_passport(const FieldInfo& f) {
    frame_top(f.name);
    frame_sep("паспорт поля");
    Line l1;
    l1.col(clr::grey(), "тип ").col(clr::cyan(), clip(f.type, frame_width() - 4));
    put(l1);
    Line l2;
    l2.col(clr::grey(), "размер ").col(clr::cyan(), std::to_string(f.size))
      .col(clr::grey(), " Б · выравнивание ")
      .col(clr::cyan(), std::to_string(f.align))
      .col(clr::grey(), " Б · offset ")
      .col(clr::green(), "+" + hex4(f.offset));
    put(l2);
    if (!f.owner.empty()) {
        Line l3;
        l3.col(clr::grey(), f.base_depth > 0 ? "из базы " : "владелец ")
          .col(clr::cyan(), clip(f.owner, frame_width() - 10));
        put(l3);
    }
    if (f.inferred)
        put_text("≈ адресный слот (корреляция по адресам, не имя из ABI)");
    frame_bottom();
}

// Память-панель поля (режим [m], по умолчанию): байты региона + выноски.
inline void render_field_memory(const FieldInfo& f, const void* obj_base,
                                std::size_t obj_size) {
    frame_top(f.name);
    frame_sep("память · " + byte_range(f.offset, f.size));
    const auto* base = static_cast<const unsigned char*>(obj_base);
    Line h;
    h.col(clr::grey(), "off").to(MEM_HEX_COL);
    for (int i = 0; i < 8; ++i)
        h.col(clr::dim(), "+" + std::to_string(i)).sp(i == 7 ? 0 : 1);
    h.to(MEM_ASCII_COL).col(clr::dim(), "ascii");
    put(h);
    const Region r{Region::R::field, f.offset, f.size, &f, false, 0, 1};
    for (const MRow& row : region_rows(f.offset, f.size))
        put(mem_row(row, r, base));
    const auto notes =
        region_notes(r, base, obj_size, /*standalone*/ true,
                     frame_width() > 8 ? frame_width() - 8 : frame_width());
    for (std::size_t i = 0; i < notes.size(); ++i) {
        Line l;
        l.to(3).col(clr::grey(), i == 0 ? "└► " : "   ");
        l.s += notes[i].s;
        l.w += notes[i].w;
        put(l);
    }
    frame_bottom();
}

// Общий детальный рендер поля: раскидывает по режимам.
inline void render_field_detail(const FieldInfo& f, const void* obj_base,
                                std::size_t obj_size, DetailMode m) {
    switch (m) {
        case DetailMode::passport: render_field_passport(f); break;
        case DetailMode::hex:
            render_hex_panel(f.name,
                             static_cast<const unsigned char*>(obj_base) +
                                 f.offset,
                             f.size, f.offset);
            break;
        default: render_field_memory(f, obj_base, obj_size); break;
    }
}

// vptr-панель: блок-диаграмма vtable этого сайта. obj_base — начало ЖИВОГО
// объекта: hex-режим дампит слот vptr по адресу base+offset (а не копию
// значения в замыкании — по ней не видно, как объект живёт).
inline void render_vptr_detail(const VtableSite& site, const void* obj_base,
                               std::size_t obj_size, DetailMode m) {
    if (m == DetailMode::hex) {
        render_hex_panel(
            "vptr «" + site.owner + "»",
            static_cast<const unsigned char*>(obj_base) + site.offset,
            sizeof(void*), site.offset);
        return;
    }
    frame_top("vptr «" + site.owner + "»");
    frame_sep("vtable — гримуар рода (как работает virtual)");
    render_vtables({site}, obj_size);
    frame_bottom();
}

// Спутник-панель: кучный буфер строки (мини-рамка из view_satellites).
inline void render_satellite_detail(const FieldInfo& f, const void* obj_addr,
                                    DetailMode m) {
    if (m == DetailMode::hex && f.target != nullptr) {
        render_hex_panel("буфер «" + f.name + "»", f.target,
                         f.heap_bytes.size());
        return;
    }
    render_satellites({f}, obj_addr);
}

// ────────────────────────────────────────────────────────────────────────────
//  Узлы-примитивы
// ────────────────────────────────────────────────────────────────────────────

inline NavNode make_note_node(std::string text) {
    NavNode n;
    n.kind = NodeKind::note;
    n.title = std::move(text);
    n.detail = [t = n.title](DetailMode) {
        frame_top("заметка");
        put_text(t);
        frame_bottom();
    };
    return n;
}

inline NavNode make_opaque_node(const OpaqueSpan& span, const void* obj_base) {
    NavNode n;
    n.kind = NodeKind::opaque;
    n.title = span.self ? "свои поля не описаны" : "скрыто: " + span.name;
    n.type = span.name;
    n.suffix = "+" + hex4(span.offset);
    n.addr = static_cast<const unsigned char*>(obj_base) + span.offset;
    n.size = span.size;
    n.detail = [span, obj_base](DetailMode m) {
        if (m == DetailMode::hex) {
            render_hex_panel(span.name,
                             static_cast<const unsigned char*>(obj_base) +
                                 span.offset,
                             span.size, span.offset);
            return;
        }
        frame_top(span.name + " · туман");
        put_text(span.self
                     ? "собственные члены типа не перечислены в EYE_DESCRIBE"
                     : "непрозрачная база: нет своего EYE_DESCRIBE");
        put_text("байты честно скрыты (не выдаём за padding)");
        put_text("добавь EYE_DESCRIBE — Око увидит поля и private");
        frame_bottom();
    };
    return n;
}

// Спутник кучной строки (child поля-строки).
inline NavNode make_satellite_node(const FieldInfo& f, const void* obj_addr) {
    NavNode n;
    n.kind = NodeKind::satellite;
    n.title = "буфер кучи «" + f.name + "»";
    n.type = "куча";
    n.preview = std::to_string(f.str_len) + "/" + std::to_string(f.str_cap);
    n.suffix = "@" + hexptr(f.target);
    n.addr = f.target;
    n.size = f.heap_bytes.size();
    n.detail = [f, obj_addr](DetailMode m) {
        render_satellite_detail(f, obj_addr, m);
    };
    return n;
}

// Узел обычного поля (FieldInfo снят в момент раскрытия родителя; байты в
// панели памяти читаются живьём при каждой отрисовке).
inline NavNode make_field_node(FieldInfo f, const void* obj_base,
                               std::size_t obj_size) {
    NavNode n;
    n.kind = NodeKind::field;
    n.title = f.name;
    n.type = f.type;
    // Превью коротким клипом: полная версия — в панели деталей.
    if (f.value != "—" && !f.value.empty()) n.preview = "= " + clip(f.value, 16);
    n.suffix = "+" + hex4(f.offset);
    n.addr = static_cast<const unsigned char*>(obj_base) + f.offset;
    n.size = f.size;
    if (f.kind == FieldInfo::Kind::str) {
        n.preview = "= " + clip(f.value, 12) + (f.sso ? " (SSO)" : " (куча)");
        if (!f.sso && !f.heap_bytes.empty()) {
            n.can_expand = true;
            n.expand = [f, obj_base]() {
                return std::vector<NavNode>{make_satellite_node(f, obj_base)};
            };
        }
    }
    if (f.kind == FieldInfo::Kind::pointer) {
        n.preview = "→ " + f.value;
        // Переходы по указателям приходят в M-D; причину объясняем уже сейчас.
        n.follow_block = f.target == nullptr
                             ? "переход невозможен: nullptr"
                             : "переход по указателю — в следующем этапе";
    }
    n.detail = [f, obj_base, obj_size](DetailMode m) {
        render_field_detail(f, obj_base, obj_size, m);
    };
    return n;
}

inline NavNode make_vptr_node(const VtableSite& site, const void* obj_base,
                              std::size_t obj_size) {
    NavNode n;
    n.kind = NodeKind::vptr;
    n.title = site.owner.empty() ? "vptr" : "vptr «" + site.owner + "»";
    n.type = "→ vtable " + site.dyn_type;
    n.suffix = "+" + hex4(site.offset);
    n.addr = static_cast<const unsigned char*>(obj_base) + site.offset;
    n.size = sizeof(void*);
    n.has_vtable = true;
    n.detail = [site, obj_base, obj_size](DetailMode m) {
        render_vptr_detail(site, obj_base, obj_size, m);
    };
    return n;
}

// ────────────────────────────────────────────────────────────────────────────
//  Объектные узлы (типизированная рекурсия — прямой родич gather)
// ────────────────────────────────────────────────────────────────────────────

// Типизированный узел поля: FieldInfo + переход, если поле — указатель.
// FT — статический тип поля (его знает только этот шаблон, дальше он стёрт).
template <class FT>
NavNode make_typed_field_node(std::string name, const FT& field,
                              const void* obj_base, std::size_t obj_size) {
    using U = std::remove_cvref_t<FT>;
    FieldInfo fi;
    fi.name = std::move(name);
    fi.offset = static_cast<std::size_t>(
        reinterpret_cast<const unsigned char*>(std::addressof(field)) -
        static_cast<const unsigned char*>(obj_base));
    fi.size = sizeof(field);
    fi.type = type_name<U>();
    fi.value = stringify<U>(field);
    annotate<U>(fi, field);
    NavNode n = make_field_node(std::move(fi), obj_base, obj_size);
    arm_follow(n, field, n.title);
    return n;
}

// Узел НЕпрозрачной базы (нет своего EYE_DESCRIBE/EYE_BASES): байты честно
// скрыты — как «непрозрачная база» на карте родителя. Никакого авторазбора:
// дерево обязано говорить то же, что панель памяти (ревью Codex, PR #5).
template <class B>
NavNode make_opaque_base_node(const B& b) {
    NavNode n;
    n.kind = NodeKind::base;
    n.title = "база " + type_name<B>();
    n.type = type_name<B>();
    n.preview = "▓ скрыто";
    n.addr = std::addressof(b);
    n.size = sizeof(B);
    const B* pb = std::addressof(b);
    n.can_expand = true;
    n.expand = [pb]() {
        std::vector<NavNode> kids;
        kids.push_back(make_note_node(
            "непрозрачная база: нет своего EYE_DESCRIBE"));
        kids.push_back(make_note_node(
            "байты честно скрыты — добавь EYE_DESCRIBE, Око увидит"));
        if constexpr (std::is_polymorphic_v<B>)
            kids.push_back(make_vptr_node(
                read_vtable_site(*pb, 0, type_name<B>()), pb, sizeof(B)));
        return kids;
    };
    n.detail = [pb](DetailMode m) {
        if (m == DetailMode::hex) {
            render_hex_panel("база " + type_name<B>(), pb, sizeof(B));
            return;
        }
        frame_top("база " + type_name<B>() + " · туман");
        frame_sep("паспорт");
        render_passport(passport_of<B>());
        put_text("поля скрыты: у базы нет своего EYE_DESCRIBE");
        put_text("байты не выдаём за padding и не разбираем наугад");
        put_text("добавь EYE_DESCRIBE в " + type_name<B>() + " — Око увидит");
        frame_bottom();
    };
    return n;
}

// Дети объекта со СВОИМ реестром: базы (рекурсивно объектные узлы) + свои
// поля + vptr-сайты (все, с владельцами) + скрытые диапазоны.
template <class T>
    requires(own_described<T> || own_bases<T>)
std::vector<NavNode> make_registry_children(const T& obj) {
    std::vector<NavNode> kids;
    const auto* base = reinterpret_cast<const unsigned char*>(std::addressof(obj));

    // 1) под-объекты баз. База со СВОИМ реестром раскрывается как
    //    самостоятельный объект; база БЕЗ реестра остаётся непрозрачной —
    //    ровно как на родительской карте памяти (модель пометила её байты
    //    скрытыми, и дерево не имеет права «рассекретить» их авторазбором,
    //    перезапустив диспетчер одиночного объекта).
    if constexpr (own_bases<T>) {
        std::apply(
            [&](auto... tag) {
                (..., [&](auto t) {
                    using B = typename decltype(t)::type;
                    const B& b = static_cast<const B&>(obj);
                    const auto off = static_cast<std::size_t>(
                        reinterpret_cast<const unsigned char*>(
                            std::addressof(b)) -
                        base);
                    NavNode bn;
                    if constexpr (own_described<B> || own_bases<B>) {
                        bn = make_object_node<B>(
                            b, "база " + type_name<B>(), NodeKind::base);
                    } else {
                        bn = make_opaque_base_node<B>(b);
                    }
                    bn.suffix = "+" + hex4(off);
                    if (is_virtual_base_v<T, B>)
                        bn.preview += bn.preview.empty() ? "[virtual]"
                                                         : " [virtual]";
                    kids.push_back(std::move(bn));
                }(tag));
            },
            T::eye_bases());
    }

    // 2) свои поля (только собственный реестр — поля баз внутри узлов баз).
    //    Обход типизированный (std::apply по eye_describe): у указателей
    //    сохраняется статический тип pointee — на нём держится переход.
    if constexpr (own_described<T>) {
        std::vector<NavNode> own;
        std::apply(
            [&](auto... entry) {
                (..., [&](auto e) {
                    const auto& field = obj.*(e.ptr);
                    own.push_back(make_typed_field_node(
                        std::string(e.name), field, base, sizeof(T)));
                }(entry));
            },
            T::eye_describe());
        std::sort(own.begin(), own.end(),
                  [](const NavNode& a, const NavNode& b) {
                      return a.addr < b.addr;   // порядок памяти, как в карте
                  });
        for (NavNode& n : own) kids.push_back(std::move(n));
    } else {
        kids.push_back(make_note_node(
            "свои поля не описаны — нужен EYE_DESCRIBE"));
    }

    // 3) vptr-сайты уровня T (модель собирает и сайты баз — те покажут свои).
    if constexpr (std::is_polymorphic_v<T>)
        kids.push_back(make_vptr_node(
            read_vtable_site(obj, 0, type_name<T>()), base, sizeof(T)));

    return kids;
}

// Страница элементов вектора [from, from+NAV_PAGE) + узел «ещё…» при остатке.
// Свободная функция, а не рекурсивная лямбда: замыкание «ещё…» живёт дольше
// кадра, ссылка на локальную лямбду была бы висячей.
template <class E, class A>
std::vector<NavNode> make_vector_elem_page(const std::vector<E, A>* pv,
                                           std::size_t from) {
    std::vector<NavNode> page;
    if constexpr (std::is_same_v<E, bool>) {
        return page;   // биты упакованы, адресов у элементов нет
    } else {
    const std::size_t upto = std::min(from + NAV_PAGE, pv->size());
    const auto* data = reinterpret_cast<const unsigned char*>(pv->data());
    for (std::size_t i = from; i < upto; ++i)
        page.push_back(make_typed_field_node(
            "#" + std::to_string(i), (*pv)[i], data,
            pv->size() * sizeof(E)));
    if (upto < pv->size()) {
        NavNode more;
        more.kind = NodeKind::more;
        more.title = "⋯ ещё " + std::to_string(pv->size() - upto) + " — Enter";
        more.can_expand = true;
        more.expand = [pv, upto]() { return make_vector_elem_page(pv, upto); };
        page.push_back(std::move(more));
    }
    return page;
    }
}

// Дети std::vector: адресные слоты объекта + узел внешнего массива.
// vector<bool> отсечён на КОМПИЛЯЦИИ (if constexpr, а не рантайм-if): у него
// data() удалён, поэтому ветку с внешним массивом для bool нельзя даже
// инстанцировать — рантайм-проверки bit_packed тут мало.
template <class E, class A>
std::vector<NavNode> make_vector_children(const std::vector<E, A>& v) {
    std::vector<NavNode> kids;
    const VectorInfo info = vector_info(v);
    const auto* base = reinterpret_cast<const unsigned char*>(std::addressof(v));
    for (const FieldInfo& slot : info.slots)
        kids.push_back(make_field_node(slot, base, sizeof(v)));

    if constexpr (std::is_same_v<E, bool>) {
        kids.push_back(make_note_node(
            "vector<bool>: биты упакованы, data() недоступен"));
        return kids;
    } else {
        if (!info.slots_matched && info.data != nullptr)
            kids.push_back(make_note_node(
                "≈ адресные слоты не распознаны — не называем их наугад"));
        if (info.data == nullptr) {
            kids.push_back(make_note_node(
                "внешнего массива нет: буфер не выделен"));
            return kids;
        }

        // data != null → память ВЫДЕЛЕНА. size==0 при capacity>0 — буфер
        // зарезервирован, но пуст: узел всё равно показываем (иначе дерево
        // соврало бы про резерв, который виден на карте памяти), но не
        // разворачиваем — живых элементов нет (ревью Codex, PR #5).
        const std::vector<E, A>* pv = std::addressof(v);
        NavNode elems;
        elems.kind = NodeKind::elems;
        elems.title = info.size == 0 ? "внешний массив (зарезервирован, пуст)"
                                     : "элементы";
        elems.type = info.element_type + "[" + std::to_string(info.size) + "]";
        elems.suffix = "@" + hexptr(info.data);
        elems.addr = info.data;
        elems.size = info.heap_reserved;   // резерв целиком, не только живое
        elems.can_expand = info.size > 0;
        if (info.size > 0)
            elems.expand = [pv]() { return make_vector_elem_page(pv, 0); };
        elems.detail = [pv](DetailMode m) {
            if (m == DetailMode::hex) {
                render_hex_panel("элементы", pv->data(),
                                 pv->size() * sizeof(E));
                return;
            }
            render_vector_satellite(vector_info(*pv), pv);
        };
        kids.push_back(std::move(elems));
        return kids;
    }
}

// Дети «просто объекта» — диспетчер по типу (зеркало panel_object).
template <class T>
std::vector<NavNode> make_children(const T& obj) {
    const auto* base = reinterpret_cast<const unsigned char*>(std::addressof(obj));
    if constexpr (own_described<T> || own_bases<T>) {
        return make_registry_children(obj);
    } else if constexpr (std::is_same_v<T, std::string>) {
        std::vector<NavNode> kids;
        FieldInfo f = self_field(obj);
        if (!f.sso && !f.heap_bytes.empty())
            kids.push_back(make_satellite_node(f, base));
        else
            kids.push_back(make_note_node(
                "SSO: буфер прямо в объекте, кучи нет"));
        return kids;
    } else if constexpr (is_std_vector_v<T>) {
        return make_vector_children(obj);
    } else if constexpr (is_std_array_v<T>) {
        std::vector<NavNode> kids;
        for (std::size_t i = 0; i < obj.size(); ++i)
            kids.push_back(make_typed_field_node(
                "#" + std::to_string(i), obj[i], base, sizeof(T)));
        return kids;
    } else if constexpr (std::is_class_v<T> && std::is_aggregate_v<T> &&
                         std::is_standard_layout_v<T> && !described<T> &&
                         !has_bases<T>) {
        std::vector<NavNode> kids;
        if constexpr (field_count<T>() > 0 && field_count<T>() <= 8) {
            std::size_t idx = 0;
            visit_fields(obj, [&](const auto& field) {
                kids.push_back(make_typed_field_node(
                    "#" + std::to_string(idx++), field, base, sizeof(T)));
            });
        } else {
            kids.push_back(make_note_node(
                "агрегат не разобрать автоматикой — добавь EYE_DESCRIBE"));
        }
        if constexpr (std::is_polymorphic_v<T>)
            kids.push_back(make_vptr_node(
                read_vtable_site(obj, 0, type_name<T>()), base, sizeof(T)));
        return kids;
    } else if constexpr (std::is_class_v<T>) {
        std::vector<NavNode> kids;
        // Плоский агрегат, отвергнутый ровно проверкой standard-layout выше, —
        // это НЕ private/конструкторы. Дерево обязано говорить то же, что
        // панель памяти (см. panel_object — там же и разбор причины).
        if constexpr (std::is_aggregate_v<T> && !described<T> && !has_bases<T>) {
            kids.push_back(make_note_node(
                "агрегат, но не standard-layout — авторазбора нет"));
            kids.push_back(make_note_node(
                "причина: поле std::string (MSVC Debug) или база с данными"));
            kids.push_back(make_note_node(
                "добавь EYE_DESCRIBE — Око увидит поля с именами"));
        } else {
            kids.push_back(make_note_node(
                "непрозрачный класс: private/конструкторы — нужен EYE_DESCRIBE"));
        }
        if constexpr (std::is_polymorphic_v<T>)
            kids.push_back(make_vptr_node(
                read_vtable_site(obj, 0, type_name<T>()), base, sizeof(T)));
        return kids;
    } else {
        return {};   // скаляр/указатель — лист, вся правда в панели деталей
    }
}

// Объектный узел: заголовок + полная панель (как inspect) в деталях +
// ленивые дети make_children. kind: root — корень галереи, base — под-объект,
// object — pointee (M-D).
template <class T>
NavNode make_object_node(const T& obj, std::string label, NodeKind kind) {
    NavNode n;
    n.kind = kind;
    n.title = label.empty() ? type_name<T>() : std::move(label);
    n.type = type_name<T>();
    n.addr = std::addressof(obj);
    n.size = sizeof(T);
    n.suffix = "@" + hexptr(std::addressof(obj));
    n.has_vtable = std::is_polymorphic_v<T>;
    const T* p = std::addressof(obj);
    if constexpr (!std::is_class_v<T>) {
        FieldInfo f = self_field(obj);
        n.preview = "= " + clip(f.value, 16);
        // Корень/pointee сам является указателем — с него можно идти дальше.
        arm_follow(n, obj, n.title);
        n.can_expand = true;
        n.expand = [p]() { return make_children<T>(*p); };
    } else if constexpr (is_smart_ptr_v<T> || is_array_smart_ptr_v<T>) {
        // Умный указатель как КОРЕНЬ галереи (Gallery.add(ptr)) ведёт себя как
        // поле-умный-указатель: followable через .get() (g/Enter → *ptr),
        // а НЕ разворачивается в бесполезный «непрозрачный класс». preview и
        // очеловеченный тип ставит сам arm_follow (ревью Codex, PR #5).
        // Массивный (unique_ptr<int[]>) сюда же — но получит честный отказ.
        arm_follow(n, obj, n.title);
    } else {
        n.can_expand = true;
        n.expand = [p]() { return make_children<T>(*p); };
    }
    n.detail = [p, title = n.title](DetailMode m) {
        if (m == DetailMode::hex) {
            render_hex_panel(title, p, sizeof(T));
            return;
        }
        // Паспорт/память/vtable — секции и так внутри полной панели объекта.
        render_object_panel(*p, title);
    };
    n.print_static = [p, title = n.title]() {
        geo_refresh();
        emit_line("");
        render_object_panel(*p, title);
        emit_line("");
    };
    return n;
}

// Корень «статика типа» (объекта нет — только паспорт).
template <class T>
NavNode make_type_node() {
    NavNode n;
    n.kind = NodeKind::root;
    n.title = type_name<T>() + " · статика";
    n.type = type_name<T>();
    n.detail = [](DetailMode) { render_type_panel<T>(); };
    n.print_static = []() {
        geo_refresh();
        emit_line("");
        render_type_panel<T>();
        emit_line("");
    };
    return n;
}

} // namespace eye::detail::nav
