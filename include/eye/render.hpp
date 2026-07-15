// ============================================================================
//   ОКО МАГА / eye/render.hpp — ВИД: тема «Гримуар v3» (лист героя + две зоны)
// ============================================================================
//   Тонкая УМБРЕЛЛА: подключает слои вида из eye/detail/. Логика раскидана по
//   файлам «одна ответственность = один заголовок», но снаружи по-прежнему
//   один #include. Хочешь другой вид — правишь нужный detail/*.hpp.
//
//   Слои (снизу вверх; каждый включает лишь то, что использует):
//     detail/platform.hpp   ← ОС: консоль, ширина, разворот, env (<windows.h>)
//     detail/palette.hpp    ← ANSI-палитра clr:: (авто-выкл вне терминала)
//     detail/geometry.hpp   ← раскладка: зоны, ширина, geo_refresh
//     detail/text.hpp       ← ширина/clip/hex + строитель Line
//     detail/frame.hpp      ← рамки: put, две зоны, картуш, разделители
//     detail/view_passport.hpp   ← паспорт: размер + черты
//     detail/view_memory.hpp     ← СХЕМА ПАМЯТИ (сердце): карта ║ кодекс, шкала
//     detail/view_hierarchy.hpp  ← иерархия: под-объекты баз
//     detail/view_vtable.hpp     ← vtable: гримуар рода
//     detail/view_satellites.hpp ← панели-спутники кучи
//
//   Важно: вид зависит ТОЛЬКО от detail/model_types.hpp (данные), а не от
//   движка рефлексии — граница «модель ↔ вид» проверяется компилятором.
//   Как рамка сходится справа: каждую строку собираем через Line, отдельно
//   считая ВИДИМУЮ ширину (ANSI-коды = 0 колонок, символ UTF-8 = 1 колонка).
// ============================================================================
#pragma once

#include "detail/palette.hpp"
#include "detail/geometry.hpp"
#include "detail/text.hpp"
#include "detail/frame.hpp"
#include "detail/view_passport.hpp"
#include "detail/view_memory.hpp"
#include "detail/view_hierarchy.hpp"
#include "detail/view_vtable.hpp"
#include "detail/view_satellites.hpp"
