# Учебные этапы M0–M5 (архив)

Как инспектор `magic_eye` собирался с нуля — приём за приёмом. Каждый этап это
автономная программа в один `main.cpp` со своим README (теория, разбор,
задания «проверь себя»). **В сборку проекта эти папки не входят** — рабочая
библиотека живёт в `include/eye/`, примеры в `examples/` (см. корневой README).

| Этап | Папка | Что осваивается |
|------|-------|-----------------|
| M0 | `M0_passport/` | `typeid` + деманглер ABI, `sizeof`/`alignof`, type_traits |
| M1 | `M1_xray/` | hex-дамп байтов, endianness, SSO у `std::string` |
| M2 | `M2_anatomy/` | подсчёт полей агрегата, structured bindings, карта padding |
| M3 | `M3_vptr_lair/` | vptr, vtable, ручной виртуальный вызов, RTTI изнутри |
| M4 | `M4_guild_registry/` | макрос-реестр, указатели на члены, доступ к private |
| M5 | `M5_oracle/` | сверка с `-fdump-lang-class` и pahole |

Порядок прохождения: M0 → M1 → M2 → M3 → M4 → M5. Каждый следующий опирается на
предыдущие и ведёт к финалу — библиотеке `eye::inspect(obj)`.

Собрать этап руками (пример):

```bash
cd M2_anatomy
g++ -std=c++20 -Wall -Wextra -Wpedantic main.cpp -o m2 && ./m2
```

`final_README.md` — снимок README библиотеки на момент, когда она ещё жила в
папке `final/`. Актуальная документация — в корневом `README.md`.
