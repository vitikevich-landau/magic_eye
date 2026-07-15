// ОКО МАГА / eye/detail/type_name.hpp — имя типа: деманглер + очеловечивание.
#pragma once
#include <cstdlib>    // std::free
#include <cstring>    // std::char_traits (MSVC prettify)
#include <memory>     // std::unique_ptr
#include <string>
#include <typeinfo>
#include "abi.hpp"    // EYE_ITANIUM_ABI (+ <cxxabi.h>: abi::__cxa_demangle)

namespace eye::detail {

// ════════════════════════════════════════════════════════════════════════════
//  Имя типа (M0): typeid + деманглер, с очеловечиванием std::string
// ════════════════════════════════════════════════════════════════════════════
// Развернуть манглированное имя в человеческое (компиляторо-зависимо).
inline std::string demangle_raw(const char* mangled) {
#if EYE_ITANIUM_ABI
    int status = 0;
    std::unique_ptr<char, void (*)(void*)> d(
        abi::__cxa_demangle(mangled, nullptr, nullptr, &status), std::free);
    return status == 0 ? std::string(d.get()) : std::string(mangled);
#else
    // MSVC: typeid().name() уже читаемо ("struct Hero", "int * __ptr64") —
    // снимаем служебные слова, чтобы вид совпал с GCC/Clang.
    std::string name = mangled;
    for (const char* junk : {"class ", "struct ", "enum ", " __ptr64"}) {
        const auto len = std::char_traits<char>::length(junk);
        for (auto p = name.find(junk); p != std::string::npos; p = name.find(junk))
            name.erase(p, len);
    }
    return name;
#endif
}

// Свернуть длинную форму std::string обеих стандартных библиотек:
// libstdc++ (std::__cxx11::basic_string<...>) и MSVC (std::basic_string<...>).
inline std::string prettify(std::string name) {
    for (const std::string& ugly :
         {std::string("std::__cxx11::basic_string<char, std::char_traits<char>, "
                      "std::allocator<char> >"),
          std::string("std::basic_string<char,std::char_traits<char>,"
                      "std::allocator<char> >")})
        for (std::size_t p = name.find(ugly); p != std::string::npos;
             p = name.find(ugly))
            name.replace(p, ugly.size(), "std::string");
    return name;
}

// Имя типа по T (на этапе компиляции) и по живому type_info (в рантайме).
template <typename T>
std::string type_name() { return prettify(demangle_raw(typeid(T).name())); }
inline std::string type_name_of(const std::type_info& ti) {
    return prettify(demangle_raw(ti.name()));
}

} // namespace eye::detail
