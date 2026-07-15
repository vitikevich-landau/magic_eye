// ОКО МАГА / eye/detail/field_mark.hpp — номер поля «#N» (общий для секций вида).
//   Нужен и схеме памяти, и панели-спутнику, поэтому вынесен отдельно — чтобы
//   каждая секция была самодостаточной и не зависела от порядка включения.
#pragma once
#include <algorithm>   // std::all_of
#include <cctype>      // std::isdigit
#include <cstddef>
#include <string>
#include "model_types.hpp"   // FieldInfo
#include "text.hpp"          // Line, clr::

namespace eye::detail {

inline std::string field_mark(std::size_t no) {
    return no == 0 ? "" : "#" + std::to_string(no);
}

inline bool has_automatic_name(const FieldInfo& f) {
    return f.name.size() > 1 && f.name.front() == '#' &&
           std::all_of(f.name.begin() + 1, f.name.end(),
                       [](unsigned char c) { return std::isdigit(c) != 0; });
}

// В авторазборе имя уже выглядит как #0, #1, ... — второй номер только мешал
// бы. Настоящие имена получают #1, #2, ... и совпадают с панелью-спутником.
inline std::string field_mark(const FieldInfo& f, std::size_t no) {
    return has_automatic_name(f) || f.inferred ? "" : field_mark(no);
}

inline void add_field_mark(Line& l, const FieldInfo& f, std::size_t no) {
    const std::string mark = field_mark(f, no);
    if (!mark.empty()) l.col(clr::gold(), mark).sp();
}

} // namespace eye::detail
