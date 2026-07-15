// ОКО МАГА / eye/detail/view_memory.hpp — СХЕМА ПАМЯТИ: карта ║ кодекс, шкала.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>   // std::uintptr_t — сравнение адресов без UB-серости
#include <string>
#include <vector>
#include "field_mark.hpp"   // add_field_mark — общий с панелью-спутником
#include "frame.hpp"
#include "model_types.hpp"

namespace eye::detail {

// ════════════════════════════════════════════════════════════════════════════
//  СХЕМА ПАМЯТИ — сердце Гримуара v2.
//  Регион (vptr / поле / под-часть строки / padding / скрытое) = блок строк.
// ════════════════════════════════════════════════════════════════════════════
struct Region {
    enum class R { field, padding, vptr, opaque, vbase };
    R what;
    std::size_t off, size;
    const FieldInfo* f;             // для field
    bool shade;                     // чередование █/▓ между соседними полями
    int strpart;                    // 1 = .ptr, 2 = .len, 3 = .buf (std::string)
    std::size_t field_no;           // стабильный номер поля в порядке памяти
    std::string why;                // причина padding

    Region(R w, std::size_t o, std::size_t s, const FieldInfo* fi = nullptr,
           bool sh = false, int sp = 0, std::size_t no = 0)
        : what(w), off(o), size(s), f(fi), shade(sh), strpart(sp),
          field_no(no) {}
};

// Диапазон включительный: так без мысленного вычитания видно, какой именно
// последний байт охватывает скобка справа.
inline std::string byte_range(std::size_t off, std::size_t size) {
    const std::size_t last = size == 0 ? off : off + size - 1;
    return "+" + hex4(off) + "…+" + hex4(last);
}

// Роль → глиф кирпича и цвет.
inline const char* region_glyph(const Region& r) {
    switch (r.what) {
        case Region::R::padding: return "░";
        case Region::R::vptr:    return "▒";
        case Region::R::vbase:   return "▒";
        case Region::R::opaque:  return "▓";
        // Поле — ВСЕГДА █. Соседние поля различаются только ОТТЕНКОМ цвета
        // (см. region_color), а не глифом: так ▓ остаётся однозначным «туманом»
        // (opaque), и глаз не ищет несуществующий смысл в █ против ▓.
        default:                 return "█";
    }
}
inline const char* region_color(const Region& r) {
    switch (r.what) {
        case Region::R::padding: return clr::red();
        case Region::R::vptr:    return clr::violet();
        case Region::R::vbase:   return clr::violet();
        case Region::R::opaque:  return clr::grey();
        default:                 return r.shade ? clr::cyan2() : clr::cyan();
    }
}

// Разбить поля объекта на регионы. Каждый vptr-сайт (у множественного
// наследования их несколько) занимает свои 8 байт. std::string на libstdc++
// раскрываем в .ptr / .len / .buf: каждая часть получает СВОИ строки и СВОЮ
// выноску (иначе подписи съезжают с байтов). fields ожидаются отсортированными
// по offset — тогда чередование тонов █/▓ идёт в порядке памяти.
inline std::vector<Region> build_regions(const std::vector<FieldInfo>& fields,
                                         std::size_t total,
                                         const std::vector<std::size_t>& vptr_offsets,
                                         const std::vector<std::size_t>& vbase_offsets,
                                         bool opaque,
                                         const std::string& opaque_why = "",
                                         const std::vector<OpaqueSpan>& opaque_spans = {}) {
    // Дыра [start, start+size): весь объект непрозрачен (vector) → opaque;
    // иначе если старт попал в под-объект неразобранной базы → opaque с её
    // именем (закрашиваем только НЕпокрытые байты — сами регионы уже размещены);
    // иначе честный padding с причиной от следующего региона.
    auto classify_gap = [&](std::size_t start, std::size_t size,
                            const Region* next) -> Region {
        if (opaque) { Region p{Region::R::opaque, start, size}; p.why = opaque_why; return p; }
        // Самый ТЕСНЫЙ диапазон: больший offset (глубже), при равном — меньший
        // размер (под-объект базы точнее, чем «своё хранилище» всего объекта).
        const OpaqueSpan* owner = nullptr;
        for (const OpaqueSpan& s : opaque_spans)
            if (start >= s.offset && start < s.offset + s.size)
                if (owner == nullptr || s.offset > owner->offset ||
                    (s.offset == owner->offset && s.size < owner->size))
                    owner = &s;
        if (owner != nullptr) {
            Region p{Region::R::opaque, start, size};
            p.why = owner->self
                ? "свои поля не описаны «" + clip(owner->name, 16) +
                      "» — нужен EYE_DESCRIBE"
                : "непрозрачная база «" + clip(owner->name, 20) +
                      "» — нужен EYE_DESCRIBE";
            return p;
        }
        Region p{Region::R::padding, start, size};
        if (next == nullptr)
            p.why = "добивка sizeof до кратного выравниванию";
        else if (next->what == Region::R::vptr)
            p.why = "выравнивание под vptr под-объекта";
        else if (next->what == Region::R::vbase)
            p.why = "выравнивание под указатель virtual-базы";
        else if (next->f != nullptr)
            p.why = clip(next->f->name, 12) + " требует адрес, кратный " +
                    std::to_string(next->f->align);
        return p;
    };
    // 1) Все «занятые» регионы: vptr-сайты + указатели vbase + поля (сплит строк).
    std::vector<Region> placed;
    for (std::size_t off : vptr_offsets)
        placed.push_back({Region::R::vptr, off, sizeof(void*)});
    for (std::size_t off : vbase_offsets)
        placed.push_back({Region::R::vbase, off, sizeof(void*)});
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const FieldInfo& f = fields[i];
        const bool shade = i % 2 != 0;
        if (f.kind == FieldInfo::Kind::str && f.str_layout &&
            f.size == 32 && f.offset % 8 == 0) {
            placed.push_back({Region::R::field, f.offset,      8, &f, shade, 1, i + 1});
            placed.push_back({Region::R::field, f.offset + 8,  8, &f, shade, 2, i + 1});
            placed.push_back({Region::R::field, f.offset + 16, 16, &f, shade, 3, i + 1});
        } else {
            placed.push_back({Region::R::field, f.offset, f.size, &f, shade, 0, i + 1});
        }
    }
    // 2) По возрастанию offset; при равных — vptr раньше поля (stable хранит
    //    порядок частей строки и уже отсортированных полей).
    std::stable_sort(placed.begin(), placed.end(),
                     [](const Region& a, const Region& b) { return a.off < b.off; });

    // 3) Прогон: между занятыми регионами вставляем padding / скрытое.
    std::vector<Region> rs;
    std::size_t cursor = 0;
    for (const Region& r : placed) {
        if (r.off > cursor && r.off <= total)        // дыра перед регионом
            rs.push_back(classify_gap(cursor, r.off - cursor, &r));
        rs.push_back(r);
        if (r.off + r.size > cursor) cursor = r.off + r.size;
    }
    if (cursor < total)                              // хвостовая дыра
        rs.push_back(classify_gap(cursor, total - cursor, nullptr));
    return rs;
}

// Строки региона: режем по АБСОЛЮТНОЙ 8-байтовой сетке (байт со смещением N
// печатается в колонке N%8 — выравнивание видно колонками). Длинные регионы
// сворачиваем: 2 строки + «⋯ ещё …» + последняя (EYE_FULL=1 отключает).
struct MRow {
    std::size_t at = 0;    // offset первого байта строки
    std::size_t n = 0;     // байт в строке (0 → строка-фолд)
    std::size_t skip = 0;  // сколько байт спрятано (для фолда)
};
inline std::vector<MRow> region_rows(std::size_t off, std::size_t size) {
    std::vector<MRow> rows;
    const std::size_t end = off + size;
    for (std::size_t line = off / 8 * 8; line < end; line += 8)
        rows.push_back({std::max(off, line), std::min(end, line + 8) -
                                             std::max(off, line), 0});
    if (!geo().full && rows.size() > 4) {
        std::size_t hidden = 0;
        for (std::size_t i = 2; i + 1 < rows.size(); ++i) hidden += rows[i].n;
        std::vector<MRow> cut(rows.begin(), rows.begin() + 2);
        cut.push_back({rows[2].at, 0, hidden});
        cut.push_back(rows.back());
        return cut;
    }
    return rows;
}

// Одна hex-строка схемы: off ␣ кирпич ␣ [сетка hex] ␣␣ [сетка ascii].
inline Line mem_row(const MRow& r, const Region& reg, const unsigned char* base) {
    const char* c = region_color(reg);
    Line ln;
    ln.col(clr::grey(), hex4(r.at)).sp();
    ln.col(c, std::string(region_glyph(reg)) + region_glyph(reg));
    if (r.n == 0) {   // строка-фолд
        ln.to(MEM_HEX_COL);
        ln.col(c, "⋯ ещё " + std::to_string(r.skip) + " Б ⋯");
        return ln;
    }
    // hex: байт со смещением o — в колонке MEM_HEX_COL + (o%8)*3.
    ln.to(MEM_HEX_COL + (r.at % 8) * 3);
    std::string hex;
    bool has_print = false;
    std::string ascii;
    for (std::size_t i = 0; i < r.n; ++i) {
        const unsigned char b = base[r.at + i];
        hex += hex2(b);
        if (i + 1 < r.n) hex += ' ';
        const bool pr = std::isprint(b) != 0;
        has_print = has_print || pr;
        ascii += pr ? static_cast<char>(b) : '.';
    }
    ln.col(c, hex);
    // ascii: той же сеткой; сплошные точки тушим — глаз ищет буквы (Solmyr!).
    ln.to(MEM_ASCII_COL + r.at % 8);
    ln.col(has_print ? c : clr::dim(), ascii);
    return ln;
}

// ---- Выноски: текст сбоку от рамки (или внутри — компактный режим) ---------

// «имя · тип · N Б = значение» в жёсткий бюджет. Длинное имя можно вынести
// отдельной строкой (см. region_notes), поэтому здесь имя занимает до половины.
inline Line field_headline(const FieldInfo& f, std::size_t budget,
                           bool with_alt = true, bool with_name = true,
                           std::size_t field_no = 0) {
    const std::size_t name_cap = std::max<std::size_t>(1, budget / 2);
    const std::string name = with_name ? clip(f.name, name_cap) : "";
    const std::string sz = std::to_string(f.size) + " Б";
    // «= [массив N байт]» дублирует размер — у массивов значение опускаем.
    const bool no_val = f.value.rfind("[массив", 0) == 0;

    Line l;
    if (with_name) {
        if (field_no != 0) add_field_mark(l, f, field_no);
        l.col(clr::green(), name).col(clr::grey(), " · ");
    }

    const std::size_t val_keep =
        no_val ? 0 : std::min<std::size_t>(vwidth(f.value), 6);
    const std::size_t suffix = 3 + vwidth(sz) + (no_val ? 0 : 3 + val_keep);
    const std::size_t type_room =
        budget > l.w + suffix ? budget - l.w - suffix : 1;
    l.col(clr::cyan(), clip(f.type, type_room)).col(clr::grey(), " · ")
     .col(clr::grey(), sz);
    if (no_val) return l;
    l.col(clr::grey(), " = ");
    l.col(clr::green(), clip(f.value, budget > l.w ? budget - l.w : 0));
    if (with_alt && f.integral && !f.alt.empty() &&
        l.w + vwidth(f.alt) + 3 <= budget)
        l.col(clr::grey(), " (" + f.alt + ")");
    return l;
}

// Куда смотрит указатель: в никуда / внутрь этого объекта / наружу.
inline void pointer_notes(std::vector<Line>& out, const FieldInfo& f,
                          const unsigned char* base, std::size_t total,
                          std::size_t budget) {
    if (f.target == nullptr) {
        Line l; l.col(clr::grey(), "× nullptr — связь обрывается");
        out.push_back(l);
        return;
    }
    const auto b = reinterpret_cast<std::uintptr_t>(base);
    const auto t = reinterpret_cast<std::uintptr_t>(f.target);
    const bool inside = t >= b && t < b + total;
    if (inside) {
        Line l;
        l.col(clr::gold(), "↩ внутрь объекта: начало +" +
                               hex4(static_cast<std::size_t>(t - b)));
        out.push_back(l);
    } else {
        Line l;
        l.col(clr::gold(), "► внешняя память @ ").plain(hexptr(f.target));
        out.push_back(l);
    }
    if (!f.pointee.empty()) {
        Line l;
        l.col(clr::grey(), "по адресу лежит: ")
         .col(clr::green(), clip(f.pointee, budget > 17 ? budget - 17 : 0));
        out.push_back(l);
    } else if (inside) {
        // Цель — внутри осматриваемого объекта: её байты уже видны в дампе
        // выше, читать нечего. Про валидность речи нет (это тот же объект).
        Line l;
        l.col(clr::grey(), clip("цель в этом объекте — байты видны выше", budget));
        out.push_back(l);
    } else {
        // Внешний адрес: инспектор намеренно НЕ разыменовывает чужую память
        // (она может быть висячей), поэтому показывает только адрес.
        Line l;
        l.col(clr::grey(), clip("цель не читаем: адрес может быть невалиден", budget));
        out.push_back(l);
    }
}

inline Line range_note(const Region& r) {
    Line l;
    l.col(clr::grey(), "в объекте: ")
     .col(r.what == Region::R::padding ? clr::red() : region_color(r),
          byte_range(r.off, r.size));
    return l;
}

// Выноски региона (каждая строка ≤ budget колонок).
inline std::vector<Line> region_notes(const Region& r, const unsigned char* base,
                                      std::size_t total, bool standalone,
                                      std::size_t budget, bool with_range = true) {
    std::vector<Line> out;
    // Диапазон «в объекте: +..+» лишний для однострочного региона (offset уже
    // слева от байтов) — двухзонный режим гасит его через with_range=false.
    auto push_range = [&] { if (with_range) out.push_back(range_note(r)); };
    switch (r.what) {
        case Region::R::vptr: {
            Line l1; l1.col(clr::violet(), "vptr (портал) → vtable класса ▼");
            out.push_back(l1); push_range();
            Line l3; l3.col(clr::grey(), "скрытое поле: его вставил virtual");
            out.push_back(l3);
            break;
        }
        case Region::R::vbase: {
            Line l1; l1.col(clr::violet(), "vbase-ptr → указатель на virtual-базу");
            out.push_back(l1); push_range();
            Line l3; l3.col(clr::grey(), "вставил компилятор ради общей vbase");
            out.push_back(l3);
            break;
        }
        case Region::R::padding: {
            Line l1;
            l1.col(clr::red(), "padding (прах) " + std::to_string(r.size) +
                                   " Б — дыра, внутри мусор");
            out.push_back(l1); push_range();
            Line l3; l3.col(clr::grey(), clip(r.why, budget));
            out.push_back(l3);
            break;
        }
        case Region::R::opaque: {
            if (r.why.empty()) {   // непрозрачный класс целиком
                Line l1; l1.col(clr::grey(), "поля скрыты: private/конструкторы");
                out.push_back(l1); push_range();
                Line l3; l3.col(clr::grey(), "добавь EYE_DESCRIBE — Око увидит");
                out.push_back(l3);
            } else {               // база без реестра / служебная часть std-типа
                Line l1; l1.col(clr::grey(), "скрытые байты (туман)");
                out.push_back(l1); push_range();
                Line l3; l3.col(clr::grey(), clip(r.why, budget));
                out.push_back(l3);
            }
            break;
        }
        case Region::R::field: {
            const FieldInfo& f = *r.f;
            if (r.strpart == 1) {          // .ptr — куда смотрит строка
                Line l;
                add_field_mark(l, f, r.field_no);
                l.col(clr::green(), clip(f.name, std::max<std::size_t>(10, budget / 2)) + ".ptr")
                 .col(clr::grey(), " = ").plain(hexptr(f.target));
                out.push_back(l);
                push_range();
                Line l2;
                if (f.sso) {
                    const auto b = reinterpret_cast<std::uintptr_t>(base);
                    const auto t = reinterpret_cast<std::uintptr_t>(f.target);
                    l2.col(clr::gold(), "↩ буфер в объекте: начало +" +
                        hex4(static_cast<std::size_t>(t - b)) +
                        " (ниже ▼)");
                } else {
                    l2.col(clr::gold(), "► КУЧА @ ").plain(hexptr(f.target))
                      .col(clr::gold(), " (панель ниже ▼)");
                }
                out.push_back(l2);
            } else if (r.strpart == 2) {   // .len
                Line l;
                add_field_mark(l, f, r.field_no);
                l.col(clr::green(), clip(f.name, std::max<std::size_t>(10, budget / 2)) + ".len")
                 .col(clr::grey(), " = ")
                 .col(clr::green(), std::to_string(f.str_len))
                 .col(clr::grey(), " — длина строки");
                out.push_back(l);
                push_range();
            } else if (r.strpart == 3) {   // .buf / .cap
                if (f.sso) {
                    Line l;
                    add_field_mark(l, f, r.field_no);
                    l.col(clr::green(), clip(f.name, std::max<std::size_t>(10, budget / 2)) + ".buf")
                     .col(clr::grey(), " = ")
                     .col(clr::green(), clip(f.value, budget > 20 ? budget - 20
                                                                  : 6));
                    out.push_back(l);
                    push_range();
                    Line l2;
                    l2.col(clr::grey(),
                        "строка внутри объекта (SSO, ≤15 символов)");
                    out.push_back(l2);
                } else {
                    Line l;
                    add_field_mark(l, f, r.field_no);
                    l.col(clr::green(), clip(f.name, std::max<std::size_t>(10, budget / 2)) + ".cap")
                     .col(clr::grey(), " = ")
                     .col(clr::green(), std::to_string(f.str_cap))
                     .col(clr::grey(), " — вместимость");
                    out.push_back(l);
                    push_range();
                    Line l2;
                    l2.col(clr::grey(), "SSO-буфер пуст: символы в куче");
                    out.push_back(l2);
                }
            } else {                        // обычное поле
                // Для одиночного значения hex уйдёт отдельной строкой-уроком.
                const bool wrap_name = vwidth(f.name) > budget / 2;
                if (wrap_name) {
                    Line name;
                    add_field_mark(name, f, r.field_no);
                    name.col(clr::green(), clip(f.name,
                        budget > name.w ? budget - name.w : 1));
                    out.push_back(name);
                }
                out.push_back(field_headline(f, budget, !standalone, !wrap_name,
                                             wrap_name ? 0 : r.field_no));
                push_range();
                if (f.base_depth > 0 && !f.owner.empty()) {
                    Line ob;
                    ob.col(clr::grey(), "из базы ")
                      .col(clr::cyan(), clip(f.owner, budget > 8 ? budget - 8 : 6));
                    out.push_back(ob);
                }
                if (f.kind == FieldInfo::Kind::pointer)
                    pointer_notes(out, f, base, total, budget);
                if (f.kind == FieldInfo::Kind::str && !f.str_layout) {
                    Line l2;
                    if (f.sso)
                        l2.col(clr::gold(),
                            "↩ буфер прямо в объекте (SSO — короткая строка)");
                    else
                        l2.col(clr::gold(), "► КУЧА @ ").plain(hexptr(f.target))
                          .col(clr::gold(), " (панель ниже ▼)");
                    out.push_back(l2);
                    Line l3;
                    l3.col(clr::grey(), "длина " + std::to_string(f.str_len) +
                                            " · вместимость " +
                                            std::to_string(f.str_cap));
                    out.push_back(l3);
                }
                if (standalone && f.integral && !f.alt.empty()) {
                    Line l2;
                    l2.col(clr::grey(), "hex: ").col(clr::cyan(), f.alt);
                    out.push_back(l2);
                    Line l3;
                    l3.col(clr::grey(), "в дампе — задом наперёд: little-endian");
                    out.push_back(l3);
                }
            }
            break;
        }
    }
    return out;
}

// ---- Сама секция «память» ---------------------------------------------------
inline void render_memory(std::vector<FieldInfo> fields, std::size_t total,
                          std::size_t talign,
                          const std::vector<std::size_t>& vptr_offsets,
                          const std::vector<std::size_t>& vbase_offsets,
                          const void* addr, bool opaque, bool standalone,
                          const VectorInfo* vector = nullptr,
                          const std::vector<OpaqueSpan>& opaque_spans = {}) {
    const bool poly = !vptr_offsets.empty();
    const auto* base = static_cast<const unsigned char*>(addr);
    // Реестр мог перечислить поля не по порядку — сортируем по offset, иначе
    // регионы и padding посчитаются неверно.
    std::sort(fields.begin(), fields.end(),
              [](const FieldInfo& a, const FieldInfo& b) {
                  return a.offset < b.offset;
              });
    // Наследование (поля из баз или скрытые базы): совет по перестановке полей
    // неприменим — между базами поля не переставить.
    const bool inherited = !opaque_spans.empty() ||
        std::any_of(fields.begin(), fields.end(),
                    [](const FieldInfo& f) { return f.base_depth > 0; });
    const std::string opaque_why = vector == nullptr
        ? ""
        : "роль зависит от STL и режима Debug/Release";
    const auto regions = build_regions(fields, total, vptr_offsets, vbase_offsets,
                                       opaque, opaque_why, opaque_spans);

    bool has_pad = false, has_vptr = false, has_field = false, has_op = false,
         has_vbase_ptr = false;
    std::string strip(total, '.');
    for (const Region& r : regions)
        for (std::size_t b = r.off; b < r.off + r.size && b < total; ++b)
            switch (r.what) {
                case Region::R::field:   strip[b] = 'f'; has_field = true; break;
                case Region::R::padding: strip[b] = 'p'; has_pad = true;   break;
                case Region::R::vptr:    strip[b] = 'v'; has_vptr = true;  break;
                case Region::R::vbase:   strip[b] = 'b'; has_vbase_ptr = true; break;
                case Region::R::opaque:  strip[b] = 'o'; has_op = true;    break;
            }
    std::size_t nf = 0, np = 0, nv = 0, nb = 0;
    for (char c : strip) {
        nf += c == 'f' || c == 'o';
        np += c == 'p';
        nv += c == 'v';
        nb += c == 'b';
    }
    np += static_cast<std::size_t>(
        std::count(strip.begin(), strip.end(), '.'));   // не покрыто = дыра

    // Однострочная карта объекта перед подробностями: сначала общий масштаб,
    // затем уже байты. Это не отдельная таблица и ничего не дублирует.
    Line overview;
    overview.col(clr::grey(), "итог: ")
            .col(clr::cyan(), std::to_string(total) + " Б")
            .col(clr::grey(), " · полей " +
                               (opaque ? std::string("?")
                                       : std::to_string(fields.size())) +
                               " · данные ")
            .col(clr::green(), std::to_string(nf) + " Б");
    if (nv != 0)
        overview.col(clr::grey(), " · vptr ")
                .col(clr::violet(), std::to_string(nv) + " Б");
    if (nb != 0)
        overview.col(clr::grey(), " · vbase-ptr ")
                .col(clr::violet(), std::to_string(nb) + " Б");
    overview.col(clr::grey(), " · padding ")
            .col(np == 0 ? clr::dim() : clr::red(), std::to_string(np) + " Б");
    if (np != 0) {
        const std::size_t pct = total == 0 ? 0 : np * 100 / total;
        overview.col(clr::grey(), " (" + std::to_string(pct) + "%)");
    }
    put(overview);

    // Шкала-DEFRAG: весь объект одной полосой — доли данных/vptr/padding сразу
    // видны глазами (тот самый урок pahole «сколько тебя ушло в дыры»). Доли
    // считаются кумулятивно, поэтому сумма сегментов точно равна ширине полосы.
    if (!opaque && total > 0 && (np != 0 || nv != 0 || nb != 0 ||
                                 fields.size() > 1)) {
        const std::size_t bw = frame_width() > 4 ? frame_width() - 2 : frame_width();
        Line bar;
        bar.col(clr::grey(), "▐");
        std::size_t acc = 0, drawn = 0;
        auto seg = [&](std::size_t bytes, const char* color, const char* glyph) {
            acc += bytes;
            const std::size_t upto = total ? acc * bw / total : 0;
            while (drawn < upto) { bar.col(color, glyph); ++drawn; }
        };
        seg(nf, clr::cyan(), "█");                 // данные (+ opaque как данные)
        seg(nv + nb, clr::violet(), "▒");          // vptr / vbase — «портал»
        seg(np, clr::red(), "░");                  // padding — «прах»
        while (drawn < bw) { bar.col(clr::dim(), "░"); ++drawn; }
        bar.col(clr::grey(), "▌");
        put(bar);
    }

    if (vector != nullptr) {
        Line facts;
        facts.col(clr::grey(), "vector: size ")
             .col(clr::green(), std::to_string(vector->size))
             .col(clr::grey(), " · capacity ")
             .col(clr::green(), std::to_string(vector->capacity))
             .col(clr::grey(), " · элемент ")
             .col(clr::cyan(), vector->element_type);
        if (vector->bit_packed)
            facts.col(clr::grey(), " (1 бит логически)");
        else
            facts.col(clr::grey(), " (" +
                      std::to_string(vector->element_size) + " Б)");
        put(facts);

        Line storage;
        if (vector->bit_packed) {
            storage.col(clr::gold(), "vector<bool>: биты упакованы; data() недоступен");
        } else if (vector->data == nullptr) {
            storage.col(clr::grey(), "внешнего массива нет: vector пуст");
        } else {
            storage.col(clr::grey(), "куча: живые элементы ")
                   .col(clr::green(), std::to_string(vector->heap_used) + " Б")
                   .col(clr::grey(), " · резерв ")
                   .col(clr::green(),
                        std::to_string(vector->heap_reserved) + " Б")
                   .col(clr::grey(), " · data @ ")
                   .plain(hexptr(vector->data));
        }
        put(storage);

        if (vector->bit_packed && !vector->elements.empty()) {
            Line values;
            values.col(clr::grey(), "элементы через operator[]: [");
            for (std::size_t i = 0; i < vector->elements.size(); ++i) {
                if (i != 0) values.col(clr::grey(), ", ");
                values.col(clr::green(), vector->elements[i].value);
            }
            if (vector->size > vector->elements.size())
                values.col(clr::grey(), ", …");
            values.col(clr::grey(), "]");
            put(values);
        }

        if (!vector->bit_packed && vector->data != nullptr &&
            !vector->slots_matched)
            put_text("≈ адресные слоты не распознаны — не называем их наугад");
    }

    // Ключевой урок раскладки: столбец байта = offset mod 8, поэтому
    // выравнивание видно глазами (сдвинутое поле начинается не с +0).
    if (!standalone)
        put_text("колонка = offset mod 8 — выравнивание видно столбиком");

    // Шапка-линейка: подписи колонок стоят РОВНО над своими данными.
    Line h;
    h.col(clr::grey(), "off").to(MEM_HEX_COL);
    for (int i = 0; i < 8; ++i)
        h.col(clr::dim(), "+" + std::to_string(i)).sp(i == 7 ? 0 : 1);
    h.to(MEM_ASCII_COL).col(clr::dim(), "ascii");

    const bool two_zone = geo().two_zone;
    if (two_zone) {
        // Открываем двухзонную полосу: слева карта, справа кодекс-выноски.
        frame_span2("╥");
        Line codex_hd; codex_hd.col(clr::gold(), "КОДЕКС — выноски");
        put_two_zone(h, codex_hd);
    } else {
        put(h);
    }

    bool first_card = true;
    for (const Region& r : regions) {
        const auto rows = region_rows(r.off, r.size);
        if (two_zone) {
            const bool single = rows.size() <= 1;   // не дублировать диапазон
            const auto notes = region_notes(r, base, total, standalone,
                                            geo().codex_w, !single);
            if (!first_card) frame_span2("╫");       // разделитель карточек
            first_card = false;
            const std::size_t k = std::max(rows.size(), notes.size());
            for (std::size_t i = 0; i < k; ++i) {
                const Line lft = i < rows.size() ? mem_row(rows[i], r, base)
                                                 : Line{};
                const Line rgt = i < notes.size() ? notes[i] : Line{};
                put_two_zone(lft, rgt);
            }
        } else {
            // Компактный режим: сперва байты, потом выноски внутри рамки.
            const auto notes = region_notes(r, base, total, standalone,
                                            frame_width() - 8);
            for (const MRow& mr : rows) put(mem_row(mr, r, base));
            for (std::size_t i = 0; i < notes.size(); ++i) {
                Line l;
                l.to(3).col(clr::grey(), i == 0 ? "└► " : "   ");
                l.s += notes[i].s; l.w += notes[i].w;
                put(l);
            }
        }
    }
    if (two_zone) frame_span2("╨");                  // закрываем полосу

    // Роли для «ключа карты» (рисуется в конце секции — см. ниже).
    // Одиночное значение без дыр — ключ не скажет ничего нового.
    const bool trivial = standalone && np == 0 && !has_vptr;
    const bool has_links = std::any_of(
        fields.begin(), fields.end(), [](const FieldInfo& f) {
            return f.kind == FieldInfo::Kind::pointer ||
                   f.kind == FieldInfo::Kind::str;
        });

    // --- урок little-endian на первом же целом поле объекта -------------------
    if (!standalone) {
        for (const FieldInfo& f : fields)
            if (f.integral && !f.alt.empty() && f.size <= 4 &&
                f.offset + f.size <= total) {
                std::string bytes;
                for (std::size_t i = 0; i < f.size; ++i)
                    bytes += hex2(base[f.offset + i]) + (i + 1 < f.size ? " " : "");
                Line le;
                le.col(clr::grey(), "руна наоборот (LE): ")
                  .col(clr::cyan(), bytes)
                  .col(clr::grey(), " → ")
                  .col(clr::cyan(), f.alt);
                if (le.w + vwidth(f.value) + 3 <= frame_width())
                    le.col(clr::grey(), " = ").col(clr::green(), f.value);
                if (le.w <= frame_width()) put(le);
                break;
            }
    }

    // --- совет по перестановке (привет, pahole) --------------------------------
    // При virtual-базе указатель vbase — обязательная служебная ячейка, её
    // перестановкой не убрать; при наследовании поля разных баз не переставить.
    if (np > 0 && !poly && !opaque && fields.size() > 1 &&
        vbase_offsets.empty() && !inherited) {
        auto sorted = fields;
        std::sort(sorted.begin(), sorted.end(),
                  [](const FieldInfo& a, const FieldInfo& b) {
                      return a.align != b.align ? a.align > b.align
                                                : a.size > b.size;
                  });
        std::size_t cur = 0, maxal = talign;
        for (const FieldInfo& f : sorted) {
            if (f.align) cur = (cur + f.align - 1) / f.align * f.align;
            cur += f.size;
            maxal = std::max(maxal, f.align);
        }
        const std::size_t news = (cur + maxal - 1) / maxal * maxal;
        if (news < total) {
            Line a;
            a.col(clr::gold(), "◇ переставь поля по убыванию align: ")
             .col(clr::green(), std::to_string(news) + " Б")
             .col(clr::grey(), " вместо " + std::to_string(total));
            put(a);
            std::string order;
            for (std::size_t i = 0; i < sorted.size(); ++i)
                order += (i ? " · " : "") + sorted[i].name;
            put_text("  порядок: " + order);
        }
    }

    // --- ключ карты: по ЗНАКУ на строку, целыми словами (не обрезается) --------
    // Термин C++ первичен, игровой образ рядом. Один глиф — одна роль.
    if (!trivial) {
        frame_sep("ключ карты — условные знаки");
        auto key = [](const char* gl_color, const char* gl,
                      const std::string& text) {
            Line l;
            l.col(gl_color, gl).sp(2)
             .col(clr::grey(), clip(text, frame_width() > 3 ? frame_width() - 3 : 0));
            put(l);
        };
        if (has_field) {
            if (vector != nullptr)
                key(clr::cyan(), "█", "адресное слово (≈ сопоставлено по адресам)");
            else
                key(clr::cyan(), "█",
                    "поле · у соседа иной оттенок — так видно границы");
        }
        if (has_pad)
            key(clr::red(), "░", "padding (прах) · дыра выравнивания, внутри мусор");
        if (has_vptr)
            key(clr::violet(), "▒",
                "vptr (портал) · скрытый указатель на таблицу virtual ▼");
        if (has_vbase_ptr)
            key(clr::violet(), "▒",
                "vbase-ptr · служебный указатель на virtual-базу");
        if (has_op)
            key(clr::grey(), "▓",
                "скрытое (туман) · private / чужая ABI → EYE_DESCRIBE");
        if (has_links) {
            Line links;
            links.col(clr::gold(), "►").sp().col(clr::grey(), "наружу   ")
                 .col(clr::gold(), "↩").sp().col(clr::grey(), "внутрь объекта   ")
                 .col(clr::gold(), "×").sp().col(clr::grey(), "в никуда (nullptr)");
            put(links);
        }
    } else if (has_links) {
        put_blank();
        Line links;
        links.col(clr::gold(), "►").sp().col(clr::grey(), "наружу   ")
             .col(clr::gold(), "↩").sp().col(clr::grey(), "внутрь   ")
             .col(clr::gold(), "×").sp().col(clr::grey(), "nullptr");
        put(links);
    }
}

} // namespace eye::detail
