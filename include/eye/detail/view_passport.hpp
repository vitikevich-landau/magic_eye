// ОКО МАГА / eye/detail/view_passport.hpp — секция «паспорт»: размер + черты.
#pragma once
#include "frame.hpp"
#include "model_types.hpp"

namespace eye::detail {

// ════════════════════════════════════════════════════════════════════════════
//  Паспорт (M0): размер/выравнивание + трейты-чипы
// ════════════════════════════════════════════════════════════════════════════
inline void render_passport(const Passport& p) {
    Line l1;
    l1.col(clr::grey(), "размер ")
      .col(clr::cyan(), std::to_string(p.size)).col(clr::grey(), " Б")
      .col(clr::grey(), "  ·  выравнивание ")
      .col(clr::cyan(), std::to_string(p.align)).col(clr::grey(), " Б")
      .col(clr::dim(), "   (Б — байт)");
    put(l1);

    // Черты типа: КАЖДАЯ строка сама говорит ответ словом (да/нет) и короткий
    // урок — зачем черта нужна. Отдельная легенда-«расшифровка» не требуется
    // (прежнее «●да ○нет» читалось как бессмысленный четвёртый чип).
    auto trait = [](bool v, const char* name, const char* why_yes,
                    const char* why_no) {
        Line l;
        l.col(v ? clr::green() : clr::dim(), v ? "◆ " : "◇ ")
         .col(v ? clr::green() : clr::grey(), name);
        const std::size_t lead = 25;                 // выровнять вердикт столбиком
        if (l.w < lead) l.col(clr::dim(), std::string(lead - l.w, '.'));
        l.col(clr::grey(), " ")
         .col(v ? clr::green() : clr::grey(), v ? "да" : "нет")
         .col(clr::grey(), " · ")
         .col(clr::dim(), clip(v ? why_yes : why_no,
                               frame_width() > l.w ? frame_width() - l.w : 0));
        put(l);
    };
    put_text("черты типа:");
    trait(p.polymorphic, "полиморфный", "есть vptr → таблица virtual (портал ▼)",
          "vtable не нужна");
    trait(p.aggregate, "агрегат", "поля разбираются автоматикой",
          "есть конструктор / private-поля");
    trait(p.trivially_copyable, "тривиально-копируемый", "можно копировать memcpy",
          "копировать memcpy нельзя");
}


} // namespace eye::detail
