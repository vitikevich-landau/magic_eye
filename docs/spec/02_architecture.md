# 02 · Архитектура: слои, файлы, поток данных

## 1. Инварианты (что не ломаем)

1. **Header-only, C++20, без внешних зависимостей.** Никаких ncurses/FTXUI:
   весь TUI пишем сами поверх ANSI/VT + WinAPI — в духе проекта
   «вытащить максимум легальными приёмами».
2. **`eye::inspect()` не меняет вывод.** Байт-в-байт (регрессии в
   `tests/` обязаны проходить без правок эталонов, кроме осознанных).
3. **Граница «модель ↔ вид» проверяется компилятором.** Вид по-прежнему
   включает из модели только `model_types.hpp`. Новый слой навигации
   получает свои явные права (см. §3).
4. **Один файл — одна ответственность**; `<windows.h>` остаётся только в
   platform-слое.

## 2. Слои после изменения

Сегодня: `модель (reflect) → вид (render) → std::cout`.
Станет — четыре слоя, два новых шва:

```
 ┌───────────────────────────────────────────────────────────────┐
 │ МОДЕЛЬ  reflect.hpp → detail/reflect_impl.hpp                 │
 │   факты: ObjectModel, Passport, VectorInfo (без изменений API,│
 │   + типизированные thunks для указателей, см. 03)             │
 └──────────────┬────────────────────────────────────────────────┘
                │ model_types.hpp (шов №1 — как сейчас)
 ┌──────────────▼────────────────────────────────────────────────┐
 │ НАВИГАЦИЯ  detail/nav/…                                       │
 │   NavNode-граф: ленивые дети, история, курсор, поиск.         │
 │   ЕДИНСТВЕННЫЙ слой, которому можно и модель, и вид.          │
 └──────────────┬────────────────────────────────────────────────┘
                │ Surface (шов №2 — стилизованные строки)
 ┌──────────────▼────────────────────────────────────────────────┐
 │ ВИД  render.hpp → detail/view_*.hpp                           │
 │   рисуют СЕКЦИИ в Surface (а не в cout). Не знают про TUI.    │
 └──────────────┬────────────────────────────────────────────────┘
                │
      ┌─────────┴──────────┐
 ┌────▼─────┐        ┌─────▼─────────────────────────────────────┐
 │ ПЕЧАТЬ   │        │ TUI  detail/tui/…                         │
 │ inspect: │        │ терминал raw, канва, декодер клавиш,      │
 │ Surface→ │        │ event-loop, панели. Знает Surface и       │
 │ cout     │        │ NavNode; про рефлексию не знает.          │
 └──────────┘        └───────────────────────────────────────────┘
```

## 3. Файловая структура

```
include/eye/
  magic_eye.hpp          ← + eye::explore(), eye::Gallery (тонкая склейка)
  reflect.hpp            ← без изменений API
  render.hpp             ← умбрелла вида; view_* теперь пишут в Surface
  explore.hpp            ← НОВОЕ: умбрелла навигации+TUI (не тянется из inspect)
  detail/
    …существующие…       ← точечные правки (см. §4, §5)
    surface.hpp          ← НОВОЕ (шов №2): StyledLine, Surface, сброс в поток
    nav/
      nav_node.hpp       ← НОВОЕ: NavNode, NodeKind, ленивые дети (см. 03)
      nav_build.hpp      ← НОВОЕ: ObjectModel+thunks → дерево NavNode
      nav_session.hpp    ← НОВОЕ: курсор, раскрытие, история, поиск, циклы
    tui/
      term.hpp           ← НОВОЕ: RAII raw-режим, alt-screen, restore (см. 04)
      keys.hpp           ← НОВОЕ: enum Key + декодер ESC-последовательностей
      canvas.hpp         ← НОВОЕ: буфер ячеек, стили, дифф-отрисовка
      widgets.hpp        ← НОВОЕ: TreeView, ScrollPane, StatusBar, Toast, Help
      app.hpp            ← НОВОЕ: event-loop, раскладка зон, режимы панели
```

Правила включений (проверяются самой структурой инклюдов):

- `view_*.hpp` включают `model_types.hpp` + `surface.hpp` (+ text/frame/
  palette/geometry) — **не** включают nav/ и tui/.
- `tui/*.hpp` включают `surface.hpp`, `nav/*.hpp`, `platform.hpp` — **не**
  включают `reflect_impl.hpp` напрямую.
- `nav/nav_build.hpp` — единственное место, где встречаются движок модели
  и конструирование графа (аналог сегодняшнего `magic_eye.hpp`, который
  склеивает model_of с рендером).
- `magic_eye.hpp` включает `explore.hpp`; программы, которым TUI не нужен,
  могут включать только `reflect.hpp`/`render.hpp` — компиляция TUI-кода
  им не навязывается (все tui-заголовки инстанцируются лениво: шаблоны +
  inline-функции, вызываемые только из `explore`).

## 4. Рефакторинг вида: Surface вместо std::cout (этап M-A)

Сейчас `frame.hpp::put(Line)` печатает строку сразу в `std::cout`.
Вводим шов:

```cpp
// detail/surface.hpp
struct StyledLine {           // строка УЖЕ с ANSI (как сегодня собирает Line)
    std::string text;         // готовый текст с эскейпами
    std::size_t width;        // печатная ширина (для клипа/скролла TUI)
};
struct Surface {
    std::vector<StyledLine> lines;
    void push(StyledLine l);
};
Surface* active_surface();            // nullptr → put печатает в cout (как сейчас)
struct SurfaceScope { … };            // RAII: перенаправить put в данный Surface
```

- `put`/`frame_top`/`frame_sep`/… получают одну правку: если есть активный
  Surface — писать строки туда, иначе — прежний `std::cout`-путь.
  **Вывод inspect не меняется** (активного Surface у него нет).
- Секции (`render_passport`, `render_memory`, `render_vtables`,
  `render_hierarchy`, `render_satellites`) остаются как есть — их вывод
  перехватывается через SurfaceScope. Отдельная задача: параметр «ширина
  рамки» у секций должен браться из явного аргумента, а не только из
  глобального `geo()` — TUI рисует детали в зоне произвольной ширины.
  Решение: `geo()` остаётся, но TUI перед отрисовкой панели выставляет
  временную геометрию (RAII `GeoScope{frame_w}`) — минимальная правка
  без переписывания view_*.
- ANSI в StyledLine остаётся как есть: канва TUI не парсит эскейпы, а
  выводит строку целиком в свою область (клип по ширине через уже
  существующие `text.hpp`-утилиты, которые умеют считать печатную ширину).

## 5. Точечные правки существующих файлов

| Файл | Правка |
|---|---|
| `frame.hpp` | put → active_surface(); ширина из GeoScope |
| `geometry.hpp` | + GeoScope; + расчёт раскладки TUI-зон (дерево/детали/гид) поверх существующих порогов two_zone |
| `platform.hpp` | + isatty(stdin); объявления для tui/term.hpp (сам raw-код — в tui/term.hpp, но windows.h остаётся тут единственным местом → term.hpp включает platform.hpp) |
| `palette.hpp` | + стили TUI: инверсия курсора, приглушённый цвет нефокусной зоны, стиль тоста |
| `model_types.hpp` | + поле `FieldInfo::pointee_thunk_id` (см. 03 — идентификатор, не std::function: шов остаётся plain) |
| `reflect_impl.hpp` | + регистрация типизированных thunks при annotate (см. 03) |
| `magic_eye.hpp` | + include explore.hpp; сам inspect не трогаем |

## 6. Поток данных TUI (кадр)

```
клавиша → keys.hpp (Key) → app.hpp: обновить NavSession
   (курсор/раскрытие/переход/история/поиск)
→ layout: geometry → зоны (дерево | детали | гид)
→ TreeView: NavSession → строки дерева (только видимое окно скролла)
→ DetailPane: NavNode → SurfaceScope + view_* → Surface → ScrollPane
→ canvas: собрать кадр → дифф с предыдущим → минимальный вывод в терминал
```

Производительность: полная перерисовка кадра допустима (объёмы —
сотни строк), дифф-вывод нужен только чтобы не мигать; никакого таймера —
цикл спит в блокирующем чтении клавиши (poll с таймаутом ~100 мс для
обработки resize).

## 7. Публичная склейка (explore.hpp)

```cpp
namespace eye {
class Gallery {
public:
    template <class T> Gallery& add(const T& obj, std::string label = "");
    template <class T> Gallery& add();      // статика типа
    void run();                             // TUI; не-TTY → печать inspect'ов
private:
    std::vector<detail::NavNode> roots_;    // корни строятся сразу (дёшево),
};                                          // дети — лениво при раскрытии

template <class T>
void explore(const T& obj, const std::string& label = "") {
    Gallery g; g.add(obj, label); g.run();
}
} // namespace eye
```

Важно: `Gallery::add` захватывает **ссылку** на объект (адрес + типовые
thunks). Объект обязан жить до конца `run()` — это документируется так же,
как сегодня документирован `inspect` (объект жив на время осмотра).
