// ОКО МАГА / eye/detail/view_satellites.hpp — панели-спутники кучи.
#pragma once
#include <cctype>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>
#include "field_mark.hpp"   // add_field_mark — общий со схемой памяти
#include "frame.hpp"
#include "model_types.hpp"

namespace eye::detail {

// ════════════════════════════════════════════════════════════════════════════
//  Панели-спутники: буферы std::string и массивы std::vector, живущие в куче.
//  Печатаются ПОСЛЕ главной панели — внешняя память правда «где-то ещё».
//  Строки уходят через emit_line (шов Surface) — как и рамочные строки frame.
// ════════════════════════════════════════════════════════════════════════════
inline void render_satellites(const std::vector<FieldInfo>& fields,
                              const void* addr) {
    constexpr std::size_t SAT_W = 44;   // внутренняя ширина мини-рамки
    const std::string ind = margin_str() + "   ";

    for (std::size_t field_i = 0; field_i < fields.size(); ++field_i) {
        const FieldInfo& f = fields[field_i];
        if (f.kind != FieldInfo::Kind::str || f.sso || f.heap_bytes.empty())
            continue;
        std::size_t field_no = 1;
        for (std::size_t i = 0; i < fields.size(); ++i)
            if (fields[i].offset < f.offset ||
                (fields[i].offset == f.offset && i < field_i))
                ++field_no;
        // Стрелка-связка от главной панели к спутнику.
        emit_line(ind + clr::grey() + "│" + clr::reset());
        Line link;
        link.col(clr::gold(), "╰──► ");
        add_field_mark(link, f, field_no);
        link.col(clr::green(), f.name + ".ptr")
            .col(clr::grey(), " ведёт во внешний блок; объект остался @ ")
            .plain(hexptr(addr)).col(clr::grey(), ":");
        const std::size_t link_budget =
            term_width() > vwidth(ind) ? term_width() - vwidth(ind) : 1;
        emit_line(ind + (link.w > link_budget ? clip_ansi(link.s, link_budget)
                                              : link.s));

        const std::string pre = ind + "    ";
        auto sat_line = [&](const Line& ln) {
            const std::size_t body_w = std::min(ln.w, SAT_W);
            const std::size_t pad = SAT_W - body_w;
            std::string out;
            out += pre;
            out += clr::grey(); out += "│"; out += clr::reset(); out += ' ';
            out += ln.w > SAT_W ? clip_ansi(ln.s, SAT_W) : ln.s;
            out.append(pad, ' ');
            out += ' ';
            out += clr::grey(); out += "│"; out += clr::reset();
            emit_line(std::move(out));
        };
        // Верх: ╭─◈ куча @ 0x… ─╮
        {
            const std::string t =
                clip("куча @ " + hexptr(f.target) + " · буфер «" + f.name + "»",
                     SAT_W - 3);
            std::string out;
            out += pre;
            out += clr::grey(); out += "╭─"; out += clr::violet(); out += "◈ ";
            out += clr::reset();
            out += clr::gold(); out += t; out += clr::reset(); out += ' ';
            out += clr::grey(); out += dashes(SAT_W - vwidth(t) - 2); out += "╮";
            out += clr::reset();
            emit_line(std::move(out));
        }
        {
            Line info;
            info.col(clr::grey(), "длина ")
                .col(clr::green(), std::to_string(f.str_len))
                .col(clr::grey(), " · вместимость ")
                .col(clr::green(), std::to_string(f.str_cap))
                .col(clr::grey(), " · хвост '\\0'");
            sat_line(info);
        }
        for (std::size_t rowb = 0; rowb < f.heap_bytes.size(); rowb += 8) {
            Line ln;
            std::string hex, ascii;
            bool has_print = false;
            for (std::size_t i = rowb;
                 i < rowb + 8 && i < f.heap_bytes.size(); ++i) {
                const unsigned char b = f.heap_bytes[i];
                hex += hex2(b);
                hex += ' ';
                const bool pr = std::isprint(b) != 0;
                has_print = has_print || pr;
                ascii += pr ? static_cast<char>(b) : '.';
            }
            ln.col(clr::green(), ljust(hex, 24)).sp()
              .col(has_print ? clr::green() : clr::dim(), ascii);
            sat_line(ln);
        }
        if (f.str_len + 1 > f.heap_bytes.size()) {
            Line more;
            more.col(clr::grey(), "⋯ ещё " +
                     std::to_string(f.str_len + 1 - f.heap_bytes.size()) +
                     " Б ⋯");
            sat_line(more);
        }
        emit_line(pre + clr::grey() + "╰" + dashes(SAT_W + 2) + "╯" +
                  clr::reset());
    }
}

// Внешний непрерывный массив std::vector. Показываем только ЖИВЫЕ элементы;
// зарезервированный, но не сконструированный хвост не читаем.
inline void render_vector_satellite(const VectorInfo& vector,
                                    const void* addr) {
    if (vector.bit_packed || vector.data == nullptr || vector.capacity == 0)
        return;

    constexpr std::size_t SAT_W = 52;
    const std::string ind = margin_str() + "   ";
    emit_line(ind + clr::grey() + "│" + clr::reset());

    Line link;
    link.col(clr::gold(), "╰──► ")
        .col(clr::green(), "vector.data()")
        .col(clr::grey(), " ведёт во внешний массив; объект остался @ ")
        .plain(hexptr(addr)).col(clr::grey(), ":");
    const std::size_t link_budget =
        term_width() > vwidth(ind) ? term_width() - vwidth(ind) : 1;
    emit_line(ind + (link.w > link_budget ? clip_ansi(link.s, link_budget)
                                          : link.s));

    const std::string pre = ind + "    ";
    auto sat_line = [&](const Line& ln) {
        const std::size_t body_w = std::min(ln.w, SAT_W);
        const std::size_t pad = SAT_W - body_w;
        std::string out;
        out += pre;
        out += clr::grey(); out += "│"; out += clr::reset(); out += ' ';
        out += ln.w > SAT_W ? clip_ansi(ln.s, SAT_W) : ln.s;
        out.append(pad, ' ');
        out += ' ';
        out += clr::grey(); out += "│"; out += clr::reset();
        emit_line(std::move(out));
    };

    {
        const std::string title = clip(
            "внешний массив @ " + hexptr(vector.data) + " · " +
                vector.element_type + "[]",
            SAT_W - 3);
        std::string out;
        out += pre;
        out += clr::grey(); out += "╭─"; out += clr::violet(); out += "◈ ";
        out += clr::reset();
        out += clr::gold(); out += title; out += clr::reset(); out += ' ';
        out += clr::grey(); out += dashes(SAT_W - vwidth(title) - 2); out += "╮";
        out += clr::reset();
        emit_line(std::move(out));
    }
    {
        Line info;
        info.col(clr::grey(), "size ")
            .col(clr::green(), std::to_string(vector.size))
            .col(clr::grey(), " · capacity ")
            .col(clr::green(), std::to_string(vector.capacity))
            .col(clr::grey(), " · живые ")
            .col(clr::green(), std::to_string(vector.heap_used) + " Б")
            .col(clr::grey(), " / резерв ")
            .col(clr::green(), std::to_string(vector.heap_reserved) + " Б");
        sat_line(info);
    }

    if (vector.elements.empty()) {
        Line empty;
        empty.col(clr::grey(), "элементов нет; память только зарезервирована");
        sat_line(empty);
    }
    for (const VectorElementInfo& element : vector.elements) {
        std::string bytes;
        for (unsigned char byte : element.bytes)
            bytes += hex2(byte) + " ";
        if (element.bytes.size() < vector.element_size) bytes += "… ";

        Line row;
        row.col(clr::gold(), "#" + std::to_string(element.index)).sp()
           .col(clr::grey(), "+" + hex4(element.index * vector.element_size))
           .sp().col(clr::cyan(), ljust(bytes, 25))
           .col(clr::grey(), "= ")
           .col(clr::green(), element.value);
        sat_line(row);
    }
    if (vector.size > vector.elements.size()) {
        Line more;
        more.col(clr::grey(), "⋯ ещё " +
                 std::to_string(vector.size - vector.elements.size()) +
                 " элементов ⋯");
        sat_line(more);
    }
    emit_line(pre + clr::grey() + "╰" + dashes(SAT_W + 2) + "╯" + clr::reset());
}

} // namespace eye::detail
