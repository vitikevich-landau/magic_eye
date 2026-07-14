// ============================================================================
//  ОКО МАГА — M1: Рентген памяти
// ============================================================================
//  Задача этапа: увидеть объект таким, каким его видит машина — как плоский
//  массив байтов. Без типов, без полей, без смысла. Просто байты.
//
//  Главный законный инструмент: любой объект можно читать через
//  unsigned char* — стандарт это явно разрешает ([basic.lval]/11).
//  Это тот же принцип, что в твоих write_u32be/read_u32be из fileshare.
//
//  Сборка:  g++ -std=c++20 -Wall -Wextra -Wpedantic main.cpp -o m1 && ./m1
// ============================================================================

// --- Слой платформенной совместимости (см. подробный комментарий в M0) -------
#if (defined(__GNUC__) || defined(__clang__)) && !defined(_MSC_VER)
#  include <cxxabi.h>    // деманглер Itanium ABI (GCC/Clang)
#  define EYE_ITANIUM_ABI 1
#else
#  define EYE_ITANIUM_ABI 0   // MSVC
#endif
#if defined(_WIN32)
#  define NOMINMAX
#  define WIN32_LEAN_AND_MEAN
#  include <io.h>
#  include <windows.h>
#else
#  include <unistd.h>
#endif

#include <cctype>        // std::isprint
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <typeinfo>

// --- цвета и type_name: скопированы из M0 (каждый этап автономен) -----------
namespace clr {
inline bool enabled() {
    static const bool on = [] {
#if defined(_WIN32)
        if (!_isatty(_fileno(stdout))) return false;
        SetConsoleOutputCP(CP_UTF8);   // UTF-8: иначе кириллица/рамки — кракозябры
        // Включаем ANSI-escape (VT); не вышло — цвета выключаем (см. M0).
        const HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        if (h == INVALID_HANDLE_VALUE || !GetConsoleMode(h, &mode)) return false;
        return SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
        return isatty(fileno(stdout)) != 0;
#endif
    }();
    return on;
}
inline const char* code(const char* c) { return enabled() ? c : ""; }
inline const char* reset()  { return code("\033[0m");        }
inline const char* gold()   { return code("\033[38;5;178m"); }
inline const char* cyan()   { return code("\033[36m");       }
inline const char* green()  { return code("\033[32m");       }
inline const char* grey()   { return code("\033[38;5;245m"); }
inline const char* violet() { return code("\033[35m");       }
} // namespace clr

template <typename T>
std::string type_name() {
#if EYE_ITANIUM_ABI
    int status = 0;
    std::unique_ptr<char, void (*)(void*)> demangled(
        abi::__cxa_demangle(typeid(T).name(), nullptr, nullptr, &status), std::free);
    return status == 0 ? demangled.get() : typeid(T).name();
#else
    // MSVC: имя уже читаемо — чистим служебные слова (см. M0).
    std::string name = typeid(T).name();
    for (const char* junk : {"class ", "struct ", "enum ", " __ptr64"}) {
        const auto len = std::char_traits<char>::length(junk);
        for (auto p = name.find(junk); p != std::string::npos; p = name.find(junk))
            name.erase(p, len);
    }
    return name;
#endif
}

// ----------------------------------------------------------------------------
// xray — hex-дамп объекта. Формат как в классических hex-редакторах:
//
//   offset   байты (hex)               ASCII
//   0x0000   43 72 61 67 20 48 61 63   |Crag Hac|
// ----------------------------------------------------------------------------
template <typename T>
void xray(const T& obj, const std::string& label) {
    // Единственный каст всего этапа. reinterpret_cast к unsigned char* —
    // ЛЕГАЛЬНОЕ чтение представления объекта. Писать через такой указатель
    // с ручной арифметикой — путь к беде (ты уже наступал на sizeof vs
    // offsetof), а читать — можно.
    const auto* bytes = reinterpret_cast<const unsigned char*>(&obj);

    std::cout << clr::gold() << "-- " << label << clr::reset()
              << clr::grey() << "  (" << type_name<T>() << ", "
              << sizeof(T) << " байт)" << clr::reset() << '\n';

    for (std::size_t row = 0; row < sizeof(T); row += 8) {
        // колонка смещения
        std::cout << "  " << clr::grey() << "0x" << std::hex << std::setw(4)
                  << std::setfill('0') << row << clr::reset() << "  ";

        // колонка hex-байтов
        for (std::size_t i = row; i < row + 8; ++i) {
            if (i < sizeof(T))
                std::cout << clr::cyan() << std::setw(2) << std::setfill('0')
                          << static_cast<unsigned>(bytes[i]) << clr::reset() << ' ';
            else
                std::cout << "   ";  // добивка последней строки
        }

        // колонка ASCII: печатаемые символы как есть, остальное — точка
        std::cout << " " << clr::green() << '|';
        for (std::size_t i = row; i < row + 8 && i < sizeof(T); ++i) {
            const unsigned char b = bytes[i];
            std::cout << (std::isprint(b) ? static_cast<char>(b) : '.');
        }
        std::cout << '|' << clr::reset() << '\n';
    }
    std::cout << std::dec << std::setfill(' ') << '\n';
}

// ----------------------------------------------------------------------------
// Подопытный со скрытыми дырами — разберём его по полям в M2,
// а пока просто посмотрим на байты.
// ----------------------------------------------------------------------------
struct ArmyStack {
    char grade;      // 1 байт... а за ним?
    int count;       // int требует выравнивания по 4
    double damage;   // double — по 8
};

int main() {
    std::cout << clr::violet() << "\n  ОКО МАГА / M1: рентген памяти\n\n" << clr::reset();

    // --- 1. Endianness --------------------------------------------------------
    // Записываем 0x11223344 и смотрим, в каком ПОРЯДКЕ байты легли в память.
    // На x86-64 увидишь 44 33 22 11 — младший байт по младшему адресу
    // (little-endian). Именно поэтому в fileshare ты руками собираешь
    // big-endian для сети: порядок в памяти и порядок "на проводе" — разные.
    std::uint32_t spell_id = 0x11223344;
    xray(spell_id, "uint32_t = 0x11223344");

    // --- 2. double изнутри ----------------------------------------------------
    // 1.0 в IEEE 754: почти все байты нулевые, в старших — экспонента (3F F0).
    double mana = 1.0;
    xray(mana, "double = 1.0");

    // --- 3. Дыры в структуре --------------------------------------------------
    // grade занимает 1 байт, но следом лежат 3 байта МУСОРА — padding до
    // границы int. Их значение не определено: запусти программу дважды
    // и сравни. Чтение мусорных байт через unsigned char — тот редкий
    // случай, где это безопасно на практике; осмысленности в них ноль.
    ArmyStack stack;
    stack.grade = 'A';
    stack.count = 42;
    stack.damage = 6.5;
    xray(stack, "ArmyStack{'A', 42, 6.5}");
    std::cout << clr::grey()
              << "  1 + 4 + 8 = 13, а sizeof = " << sizeof(ArmyStack)
              << ". Где ещё " << sizeof(ArmyStack) - 13 << " байта? Это padding —\n"
              << "  в M2 Око научится показывать его прицельно.\n\n" << clr::reset();

    // --- 4. Гвоздь программы: std::string и SSO -------------------------------
    // Короткая строка: буквы лежат ПРЯМО ВНУТРИ объекта (Small String
    // Optimization) — увидишь "Crag Hack" в ASCII-колонке.
    std::string short_name = "Crag Hack";
    xray(short_name, "std::string \"Crag Hack\" (короткая)");

    // Длинная строка: внутри объекта букв НЕТ — только указатель на кучу
    // (первые 8 байт) и размер. Вот оно, живьём: то самое "std::move копирует
    // указатель, а данные в куче не двигаются" — двигать-то нечего,
    // в объекте и так лежит лишь адрес.
    std::string long_name = "Crag Hack, the Legendary Barbarian of Krewlod";
    xray(long_name, "std::string (длинная, буквы уехали в кучу)");

    std::cout << clr::grey()
              << "  Сравни ASCII-колонки двух строк: у короткой видно имя,\n"
              << "  у длинной — только адрес кучи и длину (0x2d = 45).\n\n"
              << clr::reset();
}
