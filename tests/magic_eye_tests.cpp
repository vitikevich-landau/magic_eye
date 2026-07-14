#include "magic_eye.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

namespace {

struct LongNames {
    int this_is_a_deliberately_long_field_name = 7;
    std::string text = "A deliberately long heap string for the satellite panel";

    EYE_DESCRIBE(LongNames, this_is_a_deliberately_long_field_name, text)
};

struct Links {
    int value = 42;
    int* inside = nullptr;
    int* outside = nullptr;
    int* nowhere = nullptr;

    EYE_DESCRIBE(Links, value, inside, outside, nowhere)
};

void set_env(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

std::string render_at(std::size_t width, const std::string& label) {
    set_env("EYE_WIDTH", std::to_string(width).c_str());
    LongNames value;
    std::ostringstream out;
    std::streambuf* old = std::cout.rdbuf(out.rdbuf());
    eye::inspect(value, label);
    std::cout.rdbuf(old);
    return out.str();
}

bool lines_fit(const std::string& text, std::size_t width) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line))
        if (eye::detail::vwidth(line) > width) return false;
    return true;
}

bool expect(bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

void function_target() {}

} // namespace

int main() {
    set_env("EYE_COLOR", "0");
    set_env("EYE_CENTER", "0");
    set_env("EYE_RESIZE", "0");

    bool ok = true;
    for (const std::size_t width : {64u, 80u, 110u, 118u, 126u})
        ok &= expect(lines_fit(render_at(width, "обычная подпись"), width),
                     "rendered line exceeds terminal width");

    const std::string wide = render_at(126, "обычная подпись");
    ok &= expect(wide.find("this_is_a_deliberately_long_field_name") !=
                     std::string::npos,
                 "long field name was unnecessarily clipped");
    ok &= expect(wide.find("имена из EYE_DESCRIBE") != std::string::npos,
                 "memory section label was unnecessarily clipped");
    ok &= expect(wide.find("итог:") != std::string::npos &&
                     wide.find("· полей 2 · данные") != std::string::npos,
                 "one-line memory summary is missing");
    ok &= expect(wide.find("#1 this_is_a_deliberately_long_field_name") !=
                     std::string::npos,
                 "stable field number is missing");
    ok &= expect(wide.find("в объекте: +0x0000…+0x0003") !=
                     std::string::npos,
                 "inclusive field byte range is missing");
    ok &= expect(wide.find("► КУЧА @ ") != std::string::npos &&
                     wide.find("#2 text.ptr ведёт во внешний блок") !=
                         std::string::npos,
                 "heap string connection is missing");
    ok &= expect(wide.find("◄ диапазон байт  ► наружу  ↩ внутрь  × nullptr") !=
                     std::string::npos,
                 "connection legend is missing");

    const std::string hostile = render_at(126, "строка\n\033[31mслом рамки");
    ok &= expect(hostile.find('\033') == std::string::npos,
                 "raw ANSI escape leaked from a label");
    ok &= expect(lines_fit(hostile, 126),
                 "control characters broke the frame geometry");

    // Ненулевой адрес заведомо нельзя разыменовывать. Тест проходит, если
    // inspect() лишь выводит адрес и не падает на попытке прочитать int.
    int* invalid = reinterpret_cast<int*>(static_cast<std::uintptr_t>(1));
    volatile int live = 9;
    volatile int* volatile_ptr = &live;
    std::ostringstream pointers;
    std::streambuf* old = std::cout.rdbuf(pointers.rdbuf());
    eye::inspect(invalid, "невалидный указатель");
    eye::inspect(volatile_ptr, "указатель на volatile");
    std::cout.rdbuf(old);
    ok &= expect(pointers.str().find("адрес может быть невалиден") !=
                     std::string::npos,
                 "pointer safety note is missing");

    int external = 7;
    Links links;
    links.inside = &links.value;
    links.outside = &external;
    std::ostringstream link_map;
    old = std::cout.rdbuf(link_map.rdbuf());
    eye::inspect(links, "связи указателей");
    std::cout.rdbuf(old);
    ok &= expect(link_map.str().find("↩ этот объект: база+0x0000") !=
                     std::string::npos,
                 "internal pointer connection is missing");
    ok &= expect(link_map.str().find("► внешняя память @ ") !=
                     std::string::npos,
                 "external pointer connection is missing");
    ok &= expect(link_map.str().find("× nullptr — связь обрывается") !=
                     std::string::npos,
                 "null pointer terminator is missing");
    ok &= expect(!eye::detail::stringify(&function_target).empty(),
                 "function pointer formatting failed");

    const std::string escaped =
        eye::detail::stringify(std::string{"line\nansi\033[31m"});
    ok &= expect(escaped.find('\n') == std::string::npos &&
                     escaped.find('\033') == std::string::npos,
                 "string value contains terminal control bytes");

    return ok ? 0 : 1;
}
