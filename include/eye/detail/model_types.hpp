// ============================================================================
//   ОКО МАГА / eye/detail/model_types.hpp — СЛОВАРЬ ДАННЫХ модели
// ============================================================================
//   Только plain-структуры фактов об объекте: поля, паспорт, vptr-сайты, базы,
//   vector, opaque-диапазоны, модель. Ни ANSI, ни логики рефлексии, ни
//   <windows.h>. ВИД (eye/render.hpp) включает ТОЛЬКО этот заголовок из модели,
//   поэтому граница «модель ↔ вид» проверяется компилятором: рендер знает
//   данные, но не движок (gather/field_count/концепты).
// ============================================================================
#pragma once

#include <cstddef>   // std::size_t, std::ptrdiff_t
#include <string>
#include <vector>

namespace eye::detail {

struct FieldInfo {
    std::string name;        // "#0" в M2-режиме; настоящее имя из реестра
    std::size_t offset = 0;
    std::size_t size   = 0;
    std::size_t align  = 1;  // alignof поля — объясняет дыры ПЕРЕД ним
    std::string type;
    std::string value;       // уже отформатированное значение (см. stringify)
    bool inferred = false;   // не private-поле, а совпавший адресный слот ABI

    // --- принадлежность (наследование) --------------------------------------
    std::string owner;         // класс-владелец поля (метка при наследовании)
    int         base_depth = 0;  // 0 = поле самого производного; >0 — из базы

    // --- аннотации для вида (заполняет annotate) ----------------------------
    enum class Kind { plain, pointer, str };
    Kind kind = Kind::plain;

    bool        integral = false;  // целое → вид покажет hex и little-endian
    std::string alt;               // альтернативная запись значения (hex)

    // kind == pointer | str: куда смотрит (значение указателя / data() строки)
    const void* target = nullptr;
    std::string pointee;           // что лежит по адресу (скалярный pointee)

    // kind == str:
    bool        sso = false;       // буфер внутри объекта (SSO), не в куче
    std::size_t str_len = 0;
    std::size_t str_cap = 0;
    bool        str_layout = false;         // раскладка ptr/len/buf известна
    std::vector<unsigned char> heap_bytes;  // превью буфера из кучи (спутник)
};

struct Passport {        // ответы компилятора о типе (M0)
    std::size_t size;
    std::size_t align;
    bool polymorphic;
    bool aggregate;
    bool trivially_copyable;
};

// Один vptr-сайт. У класса с множественным наследованием их несколько: каждый
// полиморфный под-объект базы держит СВОЙ vptr в начале своего под-объекта.
struct VtableSite {
    std::size_t    offset = 0;            // где в объекте лежит этот vptr
    const void*    vptr = nullptr;
    std::string    owner;                 // чей это vptr ("" = самый производный)
    std::string    dyn_type;              // динамический тип (typeid) — портируемо
    bool           itanium = false;       // доступны ли сырые ячейки?
    std::ptrdiff_t offset_to_top = 0;     // vtable[-2] (только Itanium; ≠0 → вторичная база)
    const void*    slot0 = nullptr;       // vtable[0]  (только Itanium)
};

// Под-объект базового класса внутри наследника (из реестра EYE_BASES).
struct BaseInfo {
    std::string type;                     // имя базового класса
    std::size_t offset = 0;               // смещение под-объекта в наследнике
    int         depth = 0;                // уровень вложенности (для отступа)
    bool        polymorphic = false;      // есть ли у базы свой vptr
    bool        virtual_base = false;     // виртуальная база (ромб)?
    bool        shared = false;           // общий vbase, уже показан выше
};

struct VectorElementInfo {
    std::size_t index = 0;
    std::string value;
    std::vector<unsigned char> bytes;  // первые байты живого элемента
};

// Семантический адаптер std::vector. size/capacity/data и элементы — точные
// факты публичного API. slots — осторожная корреляция этих адресов с сырыми
// словами самого объекта; она помечается знаком ≈ и не выдаётся за стандарт ABI.
struct VectorInfo {
    std::string element_type;
    std::size_t size = 0;
    std::size_t capacity = 0;
    std::size_t element_size = 0;
    std::size_t heap_used = 0;
    std::size_t heap_reserved = 0;
    const void* data = nullptr;
    bool bit_packed = false;       // std::vector<bool>: data() намеренно нет
    bool slots_matched = false;
    std::vector<FieldInfo> slots;
    std::vector<VectorElementInfo> elements;
};

// Одна запись реестра EYE_DESCRIBE: имя поля + указатель-на-член.
// Здесь НЕ std::pair намеренно. У std::tuple есть deduction guide
// `tuple(pair<A,B>) -> tuple<A,B>`, из-за которого EYE_DESCRIBE с ОДНИМ полем
// («tuple из одной пары») схлопывался в двухэлементный tuple и не
// компилировался. У своей структуры такого guide нет: один FieldRef всегда
// остаётся одним элементом tuple. (В M4 то же на std::pair — это выросшая
// версия, где однополевой реестр больше не ломается.)
template <class MemPtr>
struct FieldRef {
    const char* name;   // из #f на препроцессоре (M4)
    MemPtr      ptr;    // &Self::поле — указатель-на-член (M4)
};
template <class MemPtr> FieldRef(const char*, MemPtr) -> FieldRef<MemPtr>;

// Диапазон под-объекта НЕразобранной базы (нет своего EYE_DESCRIBE): её байты
// нельзя выдавать за padding — помечаем как «скрытое». size = sizeof базы;
// перекрытие с соседями и вложенными полями вид разрешает сам (закрашивает
// только НЕпокрытые байты этого диапазона).
struct OpaqueSpan {
    std::size_t offset = 0;
    std::size_t size = 0;
    std::string name;   // имя типа — для подписи
    bool self = false;  // это СВОЁ хранилище типа (не под-объект базы)
};

struct ObjectModel {
    std::vector<FieldInfo>  fields;      // все поля с АБСОЛЮТНЫМИ offset'ами
    std::vector<BaseInfo>   bases;       // под-объекты баз (метки/иерархия)
    std::vector<VtableSite> vptrs;       // все vptr-сайты (у MI их несколько)
    std::vector<std::size_t> vbase_ptrs; // служебные указатели на virtual-базу
    std::vector<OpaqueSpan> opaque_bases;// под-объекты неразобранных баз
    bool has_virtual_base = false;       // есть ли хоть одна virtual-база (ромб)
};

} // namespace eye::detail
