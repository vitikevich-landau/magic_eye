#!/usr/bin/env bash
# ============================================================================
#  ОКО МАГА — M5: допрос оракула
# ============================================================================
#  Просим g++ выгрузить его ВНУТРЕННЕЕ представление классов из probe.cpp
#  и вырезаем из простыни только интересное: layout и vtable наших типов.
#
#  Использование:  ./oracle.sh
#  Результат:      oracle_report.txt + полный дамп probe.cpp.*.class
# ============================================================================
set -euo pipefail
cd "$(dirname "$0")"

echo "── компилирую probe.cpp с -fdump-lang-class ..."
# Стряхиваем дампы прошлых прогонов: имя probe.cpp.NNNt.class зависит от версии
# g++, и «ls | head» мог бы подцепить устаревший файл от другого компилятора.
rm -f probe.cpp.*.class
# -c        — только компиляция, без линковки (нам нужен анализ, не бинарник)
# -o probe.o — объектник тут же удалим, важен побочный продукт: дамп классов
g++ -std=c++20 -fdump-lang-class -c probe.cpp -o probe.o
rm -f probe.o

# -t: самый свежий первым — берём дамп именно этого прогона.
DUMP=$(ls -t probe.cpp.*.class | head -n 1)
echo "── полный дамп: ${DUMP}"

# Вырезаем секции наших классов: строка "Class X" и всё до пустой строки.
{
  echo "======================================================================"
  echo " ОРАКУЛ (g++ -fdump-lang-class): как компилятор видит типы из probe"
  echo "======================================================================"
  echo
  for cls in SloppyStack TidyStack Hero CragHack; do
    echo "----------------------------------------------------------------------"
    awk -v cls="$cls" '
      $0 ~ ("^(Class|Vtable for) " cls "$") {show=1}
      show && /^$/ {show=0; print ""}
      show {print}
    ' "$DUMP"
  done
} > oracle_report.txt

echo "── выжимка: oracle_report.txt"
echo
echo "Что сверять (подробно — в README):"
echo "  * size/align классов   — с паспортами из M0 и таблицами из M2"
echo "  * base size vs size    — хвостовой padding"
echo "  * Vtable for Hero/CragHack — слоты из M3 (attack, taunt, 2 деструктора)"
echo "  * offset у vptr        — те самые первые 8 байт"
head -n 40 oracle_report.txt
