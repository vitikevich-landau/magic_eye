// ОКО МАГА / eye/detail/platform.hpp — ОС-слой: консоль, ширина терминала, env.
// ЕДИНСТВЕННЫЙ заголовок, тянущий <windows.h>/<io.h> или <sys/ioctl.h> —
// так модель и большая часть вида за них не платят.
#pragma once

#if defined(_WIN32)
#  define NOMINMAX             // чтобы windows.h не переопределил std::min/max
#  define WIN32_LEAN_AND_MEAN
#  include <io.h>             // _isatty, _fileno
#  include <windows.h>        // кодовая страница, ANSI (VT), ширина консоли
#else
#  include <sys/ioctl.h>      // TIOCGWINSZ — ширина терминала
#  include <unistd.h>         // isatty, fileno — POSIX
#endif
#include <cstdlib>   // std::getenv / _dupenv_s, std::free
#include <string>

namespace eye::detail {

// getenv помечен MSVC как deprecated. Возвращаем владеющую строку: на Windows
// используем _dupenv_s, на POSIX сразу копируем значение из окружения.
inline std::string env_value(const char* name) {
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) return {};
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string(value);
#endif
}

} // namespace eye::detail
