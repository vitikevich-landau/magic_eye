// ОКО МАГА / eye/detail/text.hpp — ПРИМИТИВЫ ТЕКСТА: ширина, clip, hex, Line.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <sstream>
#include <string>
#include "palette.hpp"    // clr::reset() в Line/clip_ansi

namespace eye::detail {

// ════════════════════════════════════════════════════════════════════════════
//  Низкоуровневые примитивы текста
// ════════════════════════════════════════════════════════════════════════════
// Управляющим байтам не место внутри рамки: '\n' ломает геометрию, ESC может
// внедрить ANSI-код из пользовательской подписи. UTF-8-байты >= 0x80 сохраняем.
inline std::string clean_text(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (const unsigned char c : s)
        out += (c < 0x20 || c == 0x7f) ? ' ' : static_cast<char>(c);
    return out;
}

// Видимая ширина простой (без ANSI) строки = число кодовых точек UTF-8.
// Считаем все байты, кроме continuation-байтов 10xxxxxx. Для нашего набора
// (ASCII, кириллица, рамки, блоки) 1 кодовая точка = 1 колонка.
inline std::size_t vwidth(const std::string& s) {
    std::size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;
    return n;
}

// Обрезать ПРОСТУЮ строку (без ANSI) до maxcp колонок; при обрезке ставим «…».
inline std::string clip(const std::string& s, std::size_t maxcp) {
    if (maxcp == 0) return "";
    const std::string safe = clean_text(s);
    if (vwidth(safe) <= maxcp) return safe;
    std::string out;
    std::size_t cps = 0;
    for (std::size_t i = 0; i < safe.size() && cps + 1 < maxcp; ) {
        const unsigned char c = safe[i];
        const std::size_t len =
            c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : 4;
        out.append(safe, i, len);
        i += len;
        ++cps;
    }
    return out + "…";
}

// То же для уже собранной строки с ANSI-кодами. Коды CSI не занимают колонок;
// если контент неожиданно длиннее бюджета, правая рамка всё равно не съедет.
inline std::string clip_ansi(const std::string& s, std::size_t maxcp) {
    if (maxcp == 0) return "";
    std::string out;
    std::size_t cps = 0;
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] == '\033' && i + 1 < s.size() && s[i + 1] == '[') {
            const std::size_t end = s.find('m', i + 2);
            if (end != std::string::npos) {
                out.append(s, i, end - i + 1);
                i = end + 1;
                continue;
            }
        }
        if (cps + 1 >= maxcp) {
            out += clr::reset();
            out += "…";
            return out;
        }
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const std::size_t wanted =
            c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : 4;
        const std::size_t len = std::min(wanted, s.size() - i);
        out.append(s, i, len);
        i += len;
        ++cps;
    }
    return out;
}

inline std::string ljust(std::string s, std::size_t w) {
    const std::size_t v = vwidth(s);
    if (v < w) s.append(w - v, ' ');
    return s;
}
inline std::string rjust(std::string s, std::size_t w) {
    const std::size_t v = vwidth(s);
    if (v < w) s.insert(0, std::string(w - v, ' '));
    return s;
}
inline std::string hex4(std::size_t x) {          // 0x001c
    char b[16];
    std::snprintf(b, sizeof(b), "0x%04zx", static_cast<std::size_t>(x));
    return b;
}
inline std::string hexptr(const void* p) {         // 0x55f3...
    std::ostringstream o; o << p; return o.str();
}
inline std::string hex2(unsigned char b) {         // 1b
    char s[4];
    std::snprintf(s, sizeof(s), "%02x", static_cast<unsigned>(b));
    return s;
}

// Конструктор строки контента: копит текст и ОТДЕЛЬНО видимую ширину, чтобы
// цветовые коды не сбивали расчёт паддинга.
struct Line {
    std::string s;
    std::size_t w = 0;
    Line& plain(const std::string& t) {
        const std::string safe = clean_text(t);
        s += safe; w += vwidth(safe); return *this;
    }
    Line& col(const char* c, const std::string& t) {
        const std::string safe = clean_text(t);
        s += c; s += safe; s += clr::reset(); w += vwidth(safe); return *this;
    }
    Line& sp(std::size_t n = 1) { s.append(n, ' '); w += n; return *this; }
    // Дополнить пробелами до колонки x (внутри рамки).
    Line& to(std::size_t x) { if (w < x) sp(x - w); return *this; }
};

inline std::string dashes(std::size_t n) {
    std::string r; r.reserve(n * 3);
    for (std::size_t i = 0; i < n; ++i) r += "─";
    return r;
}

} // namespace eye::detail
