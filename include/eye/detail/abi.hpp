// ============================================================================
//   ОКО МАГА / eye/detail/abi.hpp — какая у нас ABI (Itanium против MSVC)
// ============================================================================
//   EYE_ITANIUM_ABI=1 там, где есть __cxa_demangle и знакомая раскладка vtable
//   (GCC/Clang). Отдельный крошечный заголовок, потому что макрос нужен и
//   МОДЕЛИ (reflect.hpp: деманглер, сырые ячейки vtable), и ВИДу
//   (detail/view_hierarchy.hpp: заметка про virtual-базу под Itanium/MSVC).
// ============================================================================
#pragma once

#if (defined(__GNUC__) || defined(__clang__)) && !defined(_MSC_VER)
#  include <cxxabi.h>    // abi::__cxa_demangle
#  define EYE_ITANIUM_ABI 1
#else
#  define EYE_ITANIUM_ABI 0   // MSVC: своя объектная модель, деманглер не нужен
#endif
