// ОКО МАГА / eye/detail/reflect_impl.hpp — ДВИЖОК рефлексии: факты об объекте.
//   stringify/annotate/vector_info/passport_of/collect/gather/model_of — как
//   ДОСТАТЬ данные (имена, offset'ы, значения, vptr-сайты, под-объекты баз).
#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>
#include "abi.hpp"
#include "model_types.hpp"
#include "type_name.hpp"
#include "field_count.hpp"
#include "traits.hpp"

namespace eye::detail {

// Разбор vptr/vtable рассчитан на 64-битную модель (указатель = 8 байт).
// На 32-битной сборке карта памяти врала бы — падаем громко ещё при компиляции.
static_assert(sizeof(void*) == 8,
              "magic_eye: секция vptr/vtable рассчитана на 64-битную (LP64/LLP64) "
              "сборку (x86-64)");

// ════════════════════════════════════════════════════════════════════════════
//  Значение поля → строка (безопасно для инспектора байтов)
//  Порядок веток важен: символьные типы, указатели и массивы обязаны быть
//  перехвачены ДО общей ветки printable, иначе ostream сделает не то и опасное.
// ════════════════════════════════════════════════════════════════════════════
template <class FT>
std::string stringify(const FT& field) {
    using U = std::remove_cvref_t<FT>;
    if constexpr (std::is_same_v<U, char> || std::is_same_v<U, signed char> ||
                  std::is_same_v<U, unsigned char>) {
        // char/signed char/unsigned char — ТРИ разных типа, и ostream печатает
        // каждый как ГЛИФ, а не число. Значение 0x1B (ESC) утащило бы терминал
        // в ANSI-последовательность. Поэтому все три (в т.ч. uint8_t) — руками.
        std::ostringstream oss;
        const auto byte = static_cast<unsigned char>(field);
        if (std::isprint(byte))
            oss << '\'' << static_cast<char>(byte) << '\'';
        else
            oss << "char(" << static_cast<int>(byte) << ')';  // управляющий → код
        return oss.str();
    } else if constexpr (std::is_pointer_v<U> &&
                         std::is_function_v<std::remove_pointer_t<U>>) {
        // Указатель на функцию: static_cast в void* для него ill-formed.
        // reinterpret_cast — conditionally-supported, но работает на всех
        // трёх наших компиляторах (GCC/Clang/MSVC).
        std::ostringstream oss;
        oss << reinterpret_cast<const void*>(field);
        return oss.str();
    } else if constexpr (std::is_pointer_v<U>) {
        // Указатель (включая char*!) — как АДРЕС, а не разыменовываем: os<<(char*)
        // прочитал бы чужую память как C-строку (мусор / выход за буфер / краш).
        std::ostringstream oss;
        // Сначала сохраняем volatile, затем явно снимаем его только для
        // стандартного ostream-overload const void*. Сам адрес не читаем.
        const volatile void* p = static_cast<const volatile void*>(field);
        oss << const_cast<const void*>(p);
        return oss.str();
    } else if constexpr (std::is_array_v<U>) {
        // C-массив: char[N] распался бы в const char* и читался как строка за
        // границей массива (UB, ловится ASan'ом). Показываем размер; байты — ниже.
        return "[массив " + std::to_string(sizeof(U)) + " байт]";
    } else if constexpr (std::is_same_v<U, std::string>) {
        // В кавычках и БЕЗ управляющих байтов: '\n' порвал бы рамку панели,
        // а 0x1b (ESC) инжектил бы живую ANSI-последовательность в терминал.
        std::string s;
        s.reserve(field.size() + 2);
        s += '"';
        for (unsigned char c : field)
            s += (c < 0x20 || c == 0x7f) ? '.' : static_cast<char>(c);
        s += '"';
        return s;
    } else if constexpr (std::is_enum_v<U>) {
        // enum (в т.ч. scoped: он не printable и не integral) — показываем
        // численное значение подлежащего типа; имя элемента без рефлексии не
        // достать. Унарный + промотирует char-based underlying до int, иначе
        // печатался бы глиф вместо числа.
        std::ostringstream oss;
        oss << +static_cast<std::underlying_type_t<U>>(field);
        return oss.str();
    } else if constexpr (printable<U>) {
        std::ostringstream oss;
        if constexpr (std::is_same_v<U, bool>) oss << std::boolalpha;
        oss << field;
        return oss.str();
    } else {
        return "—";  // непечатаемый тип (вложенная структура и т.п.) — честно
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  Аннотации поля: семантика, которую вид превратит в выноски и стрелки.
//  Всё вычисляется здесь, в модели, в момент осмотра (пока объект жив).
// ════════════════════════════════════════════════════════════════════════════
template <class FT>
void annotate(FieldInfo& fi, const FT& field) {
    using U = std::remove_cvref_t<FT>;
    fi.align = alignof(U);

    if constexpr ((std::is_integral_v<U> || std::is_enum_v<U>) &&
                  !std::is_same_v<U, bool> && sizeof(U) > 1) {
        // Целое (или enum) шире байта: рядом с десятичным пригодится hex — по
        // нему видно little-endian в дампе (младший байт лежит первым).
        fi.integral = true;
        char b[24];
        unsigned long long v = 0;
        std::memcpy(&v, &field, sizeof(field));   // без знаковых сюрпризов
        std::snprintf(b, sizeof(b), "0x%0*llx", static_cast<int>(sizeof(U) * 2), v);
        fi.alt = b;
    } else if constexpr (std::is_same_v<U, std::string>) {
        fi.kind    = FieldInfo::Kind::str;
        fi.target  = field.data();
        fi.str_len = field.size();
        fi.str_cap = field.capacity();
        // SSO: буфер лежит ВНУТРИ футпринта самой строки? Сравниваем адреса
        // как числа — сравнение «сырых» указателей из разных блоков не для if.
        const auto fb = reinterpret_cast<std::uintptr_t>(&field);
        const auto db = reinterpret_cast<std::uintptr_t>(field.data());
        fi.sso = db >= fb && db < fb + sizeof(field);
#if defined(__GLIBCXX__)
        // libstdc++: знакомая раскладка {ptr, len, union{buf16|cap}} —
        // вид сможет подписать под-регионы поля.
        fi.str_layout = true;
#endif
        if (!fi.sso && field.data() != nullptr) {
            // Буфер в куче: снимем превью для панели-спутника (+1 — '\0').
            const auto* p = reinterpret_cast<const unsigned char*>(field.data());
            const std::size_t n = std::min<std::size_t>(field.size() + 1, 48);
            fi.heap_bytes.assign(p, p + n);
        }
    } else if constexpr (std::is_pointer_v<U> &&
                         !std::is_function_v<std::remove_pointer_t<U>>) {
        fi.kind   = FieldInfo::Kind::pointer;
        const volatile void* p = static_cast<const volatile void*>(field);
        fi.target = const_cast<const void*>(p);
        // Произвольный сырой указатель нельзя безопасно проверить перед
        // разыменованием: он может быть висячим, но всё ещё ненулевым. Поэтому
        // инспектор показывает адрес, однако чужую память сам не читает.
    }
}

// std::vector: получаем точную семантику через public API, а затем ищем внутри
// объекта последовательность трёх машинных слов {data, end, capacity_end}.
// Поиск, а не жёсткие offset'ы, переживает разницу GCC/MSVC и Debug/Release.
template <class E, class A>
VectorInfo vector_info(const std::vector<E, A>& v) {
    VectorInfo info;
    info.element_type = type_name<E>();
    info.size = v.size();
    info.capacity = v.capacity();
    info.element_size = sizeof(E);
    info.bit_packed = std::is_same_v<E, bool>;

    const std::size_t preview = std::min<std::size_t>(v.size(), 8);
    info.elements.reserve(preview);
    for (std::size_t i = 0; i < preview; ++i) {
        VectorElementInfo element;
        element.index = i;
        if constexpr (std::is_same_v<E, bool>) {
            const bool value = v[i];
            element.value = stringify(value);
        } else {
            const E& value = v[i];
            element.value = stringify(value);
            const auto* bytes = reinterpret_cast<const unsigned char*>(
                std::addressof(value));
            const std::size_t n = std::min<std::size_t>(sizeof(E), 8);
            element.bytes.assign(bytes, bytes + n);
        }
        info.elements.push_back(std::move(element));
    }

    if constexpr (std::is_same_v<E, bool>) {
        // vector<bool> хранит упакованные биты и не предоставляет data().
        return info;
    } else {
        info.data = static_cast<const void*>(v.data());
        info.heap_used = v.size() * sizeof(E);
        info.heap_reserved = v.capacity() * sizeof(E);

        if (info.data == nullptr || sizeof(v) < 3 * sizeof(void*)) return info;

        const auto begin = reinterpret_cast<std::uintptr_t>(info.data);
        const auto end = begin + info.heap_used;
        const auto capacity_end = begin + info.heap_reserved;
        const auto* object = reinterpret_cast<const unsigned char*>(
            std::addressof(v));

        std::size_t match = sizeof(v);
        for (std::size_t off = 0; off + 3 * sizeof(void*) <= sizeof(v);
             off += alignof(void*)) {
            std::uintptr_t words[3]{};
            for (std::size_t i = 0; i < 3; ++i)
                std::memcpy(&words[i], object + off + i * sizeof(void*),
                            sizeof(void*));
            if (words[0] == begin && words[1] == end &&
                words[2] == capacity_end) {
                match = off;
                break;
            }
        }
        if (match == sizeof(v)) return info;

        auto add_slot = [&](std::size_t off, std::string name,
                            std::string type, std::uintptr_t value,
                            FieldInfo::Kind kind = FieldInfo::Kind::plain) {
            FieldInfo field;
            field.name = std::move(name);
            field.offset = off;
            field.size = sizeof(void*);
            field.align = alignof(void*);
            field.type = std::move(type);
            field.value = stringify(reinterpret_cast<const void*>(value));
            field.inferred = true;
            field.kind = kind;
            if (kind == FieldInfo::Kind::pointer) {
                field.target = info.data;
                if (!info.elements.empty())
                    field.pointee = "#0 = " + info.elements.front().value;
            }
            info.slots.push_back(std::move(field));
        };

        add_slot(match, "≈ data()/begin", info.element_type + " *", begin,
                 FieldInfo::Kind::pointer);
        add_slot(match + sizeof(void*), "≈ end = data + size", "граница", end);
        add_slot(match + 2 * sizeof(void*), "≈ capacity_end", "граница",
                 capacity_end);
        info.slots_matched = true;
        return info;
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  Сбор фактов об объекте
// ════════════════════════════════════════════════════════════════════════════

// Паспорт: всё, что известно о типе на этапе компиляции (объект не нужен).
template <class T>
Passport passport_of() {
    return Passport{sizeof(T), alignof(T), std::is_polymorphic_v<T>,
                    std::is_aggregate_v<T>, std::is_trivially_copyable_v<T>};
}

// Поля из реестра EYE_DESCRIBE (M4) — с именами, видит private. offset'ы
// считаются от md_base (начала САМОГО ПРОИЗВОДНОГО объекта), чтобы при
// наследовании поля базы легли на верные абсолютные смещения.
template <described T>
void append_described(const T& obj, const unsigned char* md_base,
                      const std::string& owner, int depth,
                      std::vector<FieldInfo>& out) {
    std::apply(
        [&](auto... entry) {
            (..., [&](auto e) {
                const auto& field = obj.*(e.ptr);
                using FT = std::remove_cvref_t<decltype(field)>;
                FieldInfo fi;
                fi.name   = e.name;
                fi.offset = static_cast<std::size_t>(
                    reinterpret_cast<const unsigned char*>(
                        std::addressof(field)) - md_base);
                fi.size  = sizeof(field);
                fi.type  = type_name<FT>();
                fi.value = stringify<FT>(field);
                fi.owner = owner;
                fi.base_depth = depth;
                annotate<FT>(fi, field);
                out.push_back(std::move(fi));
            }(entry));
        },
        T::eye_describe());
}

template <described T>
std::vector<FieldInfo> collect(const T& obj) {
    std::vector<FieldInfo> fields;
    append_described<T>(obj, reinterpret_cast<const unsigned char*>(&obj),
                        type_name<T>(), 0, fields);
    return fields;
}

// Поля автоматикой M2 — имена компилятор стёр, нумеруем #0, #1, ...
template <auto_inspectable T>
std::vector<FieldInfo> collect(const T& obj) {
    std::vector<FieldInfo> fields;
    const auto* base = reinterpret_cast<const unsigned char*>(&obj);
    std::size_t idx = 0;
    visit_fields(obj, [&](const auto& field) {
        using FT = std::remove_cvref_t<decltype(field)>;
        FieldInfo fi;
        fi.name   = "#" + std::to_string(idx++);
        fi.offset = static_cast<std::size_t>(
            reinterpret_cast<const unsigned char*>(std::addressof(field)) -
            base);
        fi.size  = sizeof(field);
        fi.type  = type_name<FT>();
        fi.value = stringify<FT>(field);
        annotate<FT>(fi, field);
        fields.push_back(std::move(fi));
    });
    return fields;
}

// Элементы std::array — как поля #0, #1, … Все лежат ВНУТРИ объекта подряд
// (offset i*sizeof(E)), кучи нет. type/value/аннотации — как у обычного поля.
template <class E, std::size_t N>
std::vector<FieldInfo> collect_array(const std::array<E, N>& arr) {
    std::vector<FieldInfo> fields;
    // Тело цикла инстанцируется ДАЖЕ при N == 0 (цикл рантаймовый, не
    // if constexpr), а std::array<E,0> с НЕПОЛНЫМ E законен и полезен — на нём
    // рвались sizeof(E)/type_name<E>. Элементов там нет, показывать нечего.
    if constexpr (N > 0) {
        const auto* base =
            reinterpret_cast<const unsigned char*>(std::addressof(arr));
        for (std::size_t i = 0; i < N; ++i) {
            const E& e = arr[i];
            FieldInfo fi;
            fi.name   = "#" + std::to_string(i);
            fi.offset = static_cast<std::size_t>(
                reinterpret_cast<const unsigned char*>(std::addressof(e)) - base);
            fi.size  = sizeof(E);
            fi.type  = type_name<E>();
            fi.value = stringify<E>(e);
            annotate<E>(fi, e);
            fields.push_back(std::move(fi));
        }
    }
    return fields;
}

// Весь объект как ОДНО «поле» — для скаляров, указателей и std::string,
// у которых нет разбираемых полей, но схема памяти всё равно нужна.
template <class T>
FieldInfo self_field(const T& obj) {
    FieldInfo fi;
    fi.name   = std::is_same_v<std::remove_cvref_t<T>, std::string>
                    ? "строка" : "значение";
    fi.offset = 0;
    fi.size   = sizeof(T);
    fi.type   = type_name<T>();
    fi.value  = stringify<T>(obj);
    annotate<T>(fi, obj);
    return fi;
}

// Разбор одного vptr-сайта (M3). vptr и динамический тип — портируемо (обе
// ABI); сырые служебные ячейки читаем только под Itanium. obj — ссылка на
// под-объект (для вторичной базы vptr лежит в НАЧАЛЕ её под-объекта).
template <class T>
    requires std::is_polymorphic_v<T>
VtableSite read_vtable_site(const T& obj, std::size_t offset,
                            const std::string& owner) {
    VtableSite s;
    s.offset = offset;
    s.owner  = owner;
    void* vptr = nullptr;
    std::memcpy(&vptr, std::addressof(obj), sizeof(vptr));  // начало под-объекта = vptr
    s.vptr = vptr;
    s.dyn_type = type_name_of(typeid(obj));  // динамический тип (RTTI, самый производный)
#if EYE_ITANIUM_ABI
    s.itanium = true;
    void** vtable = static_cast<void**>(vptr);
    std::memcpy(&s.offset_to_top, vtable - 2, sizeof(s.offset_to_top));  // [-2]
    std::memcpy(&s.slot0, vtable, sizeof(s.slot0));                      // [0]
#endif
    return s;
}

// ════════════════════════════════════════════════════════════════════════════
//  Модель объекта: поля (свои + унаследованные), под-объекты баз, vptr-сайты.
//  Собирается РЕКУРСИВНО по реестрам EYE_DESCRIBE/EYE_BASES.
// ════════════════════════════════════════════════════════════════════════════

// Рекурсивный обход. md_base — начало самого производного объекта; все offset'ы
// считаются от него. seen — адреса уже учтённых VIRTUAL-баз: общий vbase (ромб)
// встречается по двум путям, но в память лёг ОДИН раз — сверяем по адресу и
// второй раз внутрь не спускаемся. НЕвиртуальные базы не дедупим: под-объект
// базы-в-базе на offset 0 имеет тот же адрес, что и наследник, — совпадение
// адресов у них нормально и НЕ означает общий под-объект.
template <class T>
void gather(const T& obj, ObjectModel& m, const unsigned char* md_base,
            int depth, std::vector<const void*>& seen) {
    const auto* self = reinterpret_cast<const unsigned char*>(std::addressof(obj));
    const std::size_t self_off = static_cast<std::size_t>(self - md_base);

    // 1) vptr этого под-объекта. Первым пишем сайт самого производного (offset 0),
    //    поэтому общий с primary-базой vptr достаётся производному, а не базе.
    if constexpr (std::is_polymorphic_v<T>) {
        bool dup = false;
        for (const auto& s : m.vptrs)
            if (s.offset == self_off) { dup = true; break; }
        if (!dup)
            m.vptrs.push_back(read_vtable_site(obj, self_off, type_name<T>()));
    }

    // 2) базы (глубже) — их поля лягут по абсолютным offset'ам, порядок неважен:
    //    перед отрисовкой всё сортируется по offset. Берём ТОЛЬКО свой реестр
    //    баз — унаследованный eye_bases() относится к базе, не к T.
    bool self_has_vbase = false;
    if constexpr (own_bases<T>) {
        std::apply(
            [&](auto... tag) {
                (..., [&](auto t) {
                    using B = typename decltype(t)::type;
                    const B& b = static_cast<const B&>(obj);
                    const auto* baddr =
                        reinterpret_cast<const unsigned char*>(std::addressof(b));
                    BaseInfo bi;
                    bi.type = type_name<B>();
                    bi.offset = static_cast<std::size_t>(baddr - md_base);
                    bi.depth = depth;
                    bi.polymorphic = std::is_polymorphic_v<B>;
                    bi.virtual_base = is_virtual_base_v<T, B>;
                    if (bi.virtual_base) { self_has_vbase = true; m.has_virtual_base = true; }
                    // Дедуп по адресу — ТОЛЬКО для виртуальных баз.
                    bi.shared =
                        bi.virtual_base &&
                        std::find(seen.begin(), seen.end(),
                                  static_cast<const void*>(baddr)) != seen.end();
                    m.bases.push_back(bi);
                    if (!bi.shared) {
                        if (bi.virtual_base)
                            seen.push_back(static_cast<const void*>(baddr));
                        // База без своего EYE_DESCRIBE не даёт полей для СВОЕГО
                        // хранилища → помечаем её под-объект скрытым, чтобы вид
                        // не выдал байты за padding. (Свои под-базы, если есть,
                        // соберёт рекурсия; вид закрасит только непокрытое.)
                        if constexpr (!own_described<B>)
                            m.opaque_bases.push_back(
                                {bi.offset, sizeof(B), type_name<B>()});
                        gather<B>(b, m, md_base, depth + 1, seen);
                    }
                }(tag));
            },
            T::eye_bases());
    }

    // 2b) Служебный указатель на virtual-базу. Где он лежит — зависит от ABI:
    //   • Itanium: у НЕполиморфного типа — в начале под-объекта; у полиморфного
    //     смещения vbase зашиты в его же vtable (vptr уже учтён) — отдельного нет.
    //   • MSVC: vbptr есть ВСЕГДА при virtual-базе. У неполиморфного — на offset 0;
    //     у полиморфного — отдельным словом ПОСЛЕ vfptr (vfptr@0, vbptr@8).
    if (self_has_vbase) {
        bool record = true;
        std::size_t vb_off = self_off;
        if constexpr (std::is_polymorphic_v<T>) {
#if EYE_ITANIUM_ABI
            record = false;                     // vbase-смещения внутри vtable
#else
            vb_off = self_off + sizeof(void*);  // MSVC: vbptr после vfptr
#endif
        }
        if (record) {
            bool dup = false;
            for (std::size_t o : m.vbase_ptrs)
                if (o == vb_off) { dup = true; break; }
            if (!dup) m.vbase_ptrs.push_back(vb_off);
        }
    }

    // 3) собственные поля T (offset'ы от md_base). Только СВОЙ реестр — иначе
    //    у наследника без своего EYE_DESCRIBE подхватился бы унаследованный
    //    eye_describe() и поля базы задвоились бы (их уже собрала рекурсия в п.2).
    //    Незарегистрированную базу НЕ разбираем автоматикой: structured bindings
    //    ill-formed для агрегата «база + своё поле», а надёжно отличить плоский
    //    агрегат от агрегата-с-базой на этапе компиляции нельзя. Её байты уже
    //    помечены скрытыми (opaque_bases в п.2) — честнее, чем разложить наугад.
    if constexpr (own_described<T>)
        append_described<T>(obj, md_base, type_name<T>(), depth, m.fields);

    // Свои поля НЕ описаны (тип объявил только EYE_BASES, но добавил члены). Для
    // ВЕРХНЕГО уровня помечаем собственное хранилище скрытым — иначе его члены
    // ушли бы в padding. (У баз то же делает opaque-span от родителя в п.2.)
    if constexpr (!own_described<T>)
        if (depth == 0)
            m.opaque_bases.push_back({self_off, sizeof(T), type_name<T>(), true});
}

template <class T>
ObjectModel model_of(const T& obj) {
    ObjectModel m;
    std::vector<const void*> seen;
    gather<T>(obj, m, reinterpret_cast<const unsigned char*>(std::addressof(obj)),
              0, seen);
    return m;
}

} // namespace eye::detail
