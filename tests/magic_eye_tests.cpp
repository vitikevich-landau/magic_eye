#include <eye/magic_eye.hpp>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// Наследование проверяем на типах в ГЛОБАЛЬНОЙ области — тогда имена типов
// коротки ("BaseA", а не "(anonymous namespace)::BaseA") и метки совпадают.
// --- множественное наследование (два полиморфных базовых под-объекта) -------
struct BaseA { int a = 1; virtual ~BaseA() = default; EYE_DESCRIBE(BaseA, a) };
struct BaseB { int b = 2; virtual ~BaseB() = default; EYE_DESCRIBE(BaseB, b) };
struct Derived : BaseA, BaseB {
    int c = 3;
    EYE_BASES(Derived, BaseA, BaseB)
    EYE_DESCRIBE(Derived, c)
};

// --- ромб с общей virtual-базой ---------------------------------------------
struct VBase { int soul = 7; virtual ~VBase() = default; EYE_DESCRIBE(VBase, soul) };
struct Left  : virtual VBase { int l = 8; EYE_BASES(Left, VBase) EYE_DESCRIBE(Left, l) };
struct Right : virtual VBase { int r = 9; EYE_BASES(Right, VBase) EYE_DESCRIBE(Right, r) };
struct Diamond : Left, Right {
    int d = 10;
    EYE_BASES(Diamond, Left, Right)
    EYE_DESCRIBE(Diamond, d)
};

// --- 3-уровневая НЕвиртуальная цепочка (регресс: база-в-базе на offset 0) -----
struct GBase { int g = 1; EYE_DESCRIBE(GBase, g) };
struct GMid : GBase { int mid = 2; EYE_BASES(GMid, GBase) EYE_DESCRIBE(GMid, mid) };
struct GLeaf : GMid { int leaf = 3; EYE_BASES(GLeaf, GMid) EYE_DESCRIBE(GLeaf, leaf) };

// --- повторяющаяся НЕвиртуальная база (два разных под-объекта одного типа) ----
// Имя базы (Coin) НЕ префикс имён наследников — иначе подсчёт «из базы Coin»
// поймал бы и «из базы RepL».
struct Coin { int x = 5; EYE_DESCRIBE(Coin, x) };
struct RepL : Coin { int rl = 6; EYE_BASES(RepL, Coin) EYE_DESCRIBE(RepL, rl) };
struct RepR : Coin { int rr = 7; EYE_BASES(RepR, Coin) EYE_DESCRIBE(RepR, rr) };
struct RepDerived : RepL, RepR {
    int rd = 8;
    EYE_BASES(RepDerived, RepL, RepR)
    EYE_DESCRIBE(RepDerived, rd)
};

// --- НЕполиморфная virtual-база (регресс: указатель vbase, не padding) --------
struct NPBase { int soul = 1; EYE_DESCRIBE(NPBase, soul) };
struct NPDerived : virtual NPBase {
    int mana = 2;
    EYE_BASES(NPDerived, NPBase)
    EYE_DESCRIBE(NPDerived, mana)
};

// --- EYE_BASES без своего EYE_DESCRIBE: унаследованный eye_describe() НЕ должен
//     задвоить поля базы (поля приходят из рекурсии в базу) --------------------
struct SoleBase { int val = 11; EYE_DESCRIBE(SoleBase, val) };
struct NoOwnFields : SoleBase { EYE_BASES(NoOwnFields, SoleBase) };

// --- незарегистрированная база: её байты помечаются СКРЫТЫМИ, не padding'ом ---
struct RawBase { int raw = 22; };  // без EYE_DESCRIBE
struct WithRawBase : RawBase {
    int own = 33;
    EYE_BASES(WithRawBase, RawBase)
    EYE_DESCRIBE(WithRawBase, own)
};

// --- НЕагрегатная (private + ctor) непрозрачная база — байты скрыты, не padding
class PrivBase {
    int hidden = 1;
public:
    PrivBase() = default;
};
struct WithPrivBase : PrivBase {
    int own = 44;
    EYE_BASES(WithPrivBase, PrivBase)
    EYE_DESCRIBE(WithPrivBase, own)
};

// --- база-агрегат СО СВОЕЙ базой + полем: не разлагается structured bindings;
//     раньше валила компиляцию, теперь помечается скрытой ----------------------
struct DeepBase { int d = 1; };
struct DeepMid : DeepBase { int m = 2; };      // агрегат с базой + своим полем
struct DeepTop : DeepMid {
    int t = 3;
    EYE_BASES(DeepTop, DeepMid)
    EYE_DESCRIBE(DeepTop, t)
};

// --- регресс (Codex): наследник, лишь УНАСЛЕДОВАВШИЙ EYE_DESCRIBE + своё поле.
//     Агрегат «база + своё поле» structured bindings не разложат — авторазбор
//     на нём НЕ должен запускаться (иначе не компилируется). Свои макросы не
//     объявлены нарочно. ---------------------------------------------------
struct InhBase { int v = 44; EYE_DESCRIBE(InhBase, v) };
struct InhChild : InhBase { int c = 55; };

// --- регресс (Codex): унаследованный eye_bases() НЕ должен применяться к Leaf.
//     RegLeaf наследует eye_bases() от RegMid, но своего EYE_BASES не объявлял —
//     реестр Mid к нему неприменим, тип должен стать «непрозрачным». ---------
struct RegBase { int rb = 1; EYE_DESCRIBE(RegBase, rb) };
struct RegMid : RegBase { int rm = 2; EYE_BASES(RegMid, RegBase) EYE_DESCRIBE(RegMid, rm) };
struct RegLeaf : RegMid { int rlf = 3; };  // без своих макросов

// --- регресс (Codex): наследник, унаследовавший eye_bases() от базы БЕЗ
//     eye_describe(), обязан компилироваться (agg-fallback не должен трогать
//     агрегат-с-базой) и быть непрозрачным. ---------------------------------
struct BasesOnlyB { int bb = 1; };                                    // без макросов
struct BasesOnlyM : BasesOnlyB { EYE_BASES(BasesOnlyM, BasesOnlyB) }; // только EYE_BASES
struct BasesOnlyL : BasesOnlyM { int x = 2; };                        // без своих макросов

// --- scoped enum: рендерится через underlying-значение, а не «—» ------------
enum class Color : std::uint16_t { Red = 1, Green = 2, Blue = 4 };
struct HasEnum { Color c = Color::Green; int n = 5; EYE_DESCRIBE(HasEnum, c, n) };

// --- агрегат-с-базой БЕЗ макросов на верхнем уровне: НЕ standard-layout, не
//     раскладывается structured bindings — должен компилироваться и быть
//     непрозрачным, а не ронять сборку. -------------------------------------
struct PlainBase2 { int pb = 1; };
struct PlainDerived : PlainBase2 { int pd = 2; };

// --- агрегат, наследующий ПУСТУЮ базу: standard-layout, но field_count == 0
//     (пустая база «съедает» braced-слот счётчика) — поле не должно уйти в
//     padding, тип показывается непрозрачным. --------------------------------
struct EmptyBase {};
struct EmptyDerived : EmptyBase { int x = 42; };

// --- own EYE_BASES без своего EYE_DESCRIBE, но со СВОИМ полем: поле базы видно,
//     а собственный член d не должен уйти в padding (помечается скрытым). ------
struct OwnBaseSrc { int base_v = 1; EYE_DESCRIBE(OwnBaseSrc, base_v) };
struct OwnBasesOnly : OwnBaseSrc {
    int d = 2;
    EYE_BASES(OwnBasesOnly, OwnBaseSrc)   // без своего EYE_DESCRIBE
};

namespace {

struct LongNames {
    int this_is_a_deliberately_long_field_name = 7;
    std::string text = "A deliberately long heap string for the satellite panel";

    EYE_DESCRIBE(LongNames, this_is_a_deliberately_long_field_name, text)
};

struct Links {
    int value = 42;
    int* inside = nullptr;
    int* outside = nullptr;
    int* nowhere = nullptr;

    EYE_DESCRIBE(Links, value, inside, outside, nowhere)
};

void set_env(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

std::string render_at(std::size_t width, const std::string& label) {
    set_env("EYE_WIDTH", std::to_string(width).c_str());
    LongNames value;
    std::ostringstream out;
    std::streambuf* old = std::cout.rdbuf(out.rdbuf());
    eye::inspect(value, label);
    std::cout.rdbuf(old);
    return out.str();
}

std::string render_vector_at(std::size_t width) {
    set_env("EYE_WIDTH", std::to_string(width).c_str());
    std::vector<int> values{1, 2, 3333};
    values.reserve(6);  // end и capacity_end различаются — проверяем оба слота
    std::ostringstream out;
    std::streambuf* old = std::cout.rdbuf(out.rdbuf());
    eye::inspect(values, "vector regression");
    std::cout.rdbuf(old);
    return out.str();
}

template <class T>
std::string render_obj(const T& obj, std::size_t width, const char* label) {
    set_env("EYE_WIDTH", std::to_string(width).c_str());
    std::ostringstream out;
    std::streambuf* old = std::cout.rdbuf(out.rdbuf());
    eye::inspect(obj, label);
    std::cout.rdbuf(old);
    return out.str();
}

std::size_t count_occurrences(const std::string& text, const std::string& needle) {
    std::size_t n = 0;
    for (std::size_t p = text.find(needle); p != std::string::npos;
         p = text.find(needle, p + needle.size()))
        ++n;
    return n;
}

bool lines_fit(const std::string& text, std::size_t width) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line))
        if (eye::detail::vwidth(line) > width) return false;
    return true;
}

bool expect(bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

void function_target() {}

} // namespace

int main() {
    set_env("EYE_COLOR", "0");
    set_env("EYE_CENTER", "0");
    set_env("EYE_RESIZE", "0");

    bool ok = true;
    for (const std::size_t width : {64u, 80u, 110u, 118u, 126u})
        ok &= expect(lines_fit(render_at(width, "обычная подпись"), width),
                     "rendered line exceeds terminal width");
    for (const std::size_t width : {64u, 80u, 110u, 118u, 126u})
        ok &= expect(lines_fit(render_vector_at(width), width),
                     "vector rendering exceeds terminal width");

    const std::string wide = render_at(126, "обычная подпись");
    ok &= expect(wide.find("this_is_a_deliberately_long_field_name") !=
                     std::string::npos,
                 "long field name was unnecessarily clipped");
    ok &= expect(wide.find("имена из EYE_DESCRIBE") != std::string::npos,
                 "memory section label was unnecessarily clipped");
    ok &= expect(wide.find("итог:") != std::string::npos &&
                     wide.find("· полей 2 · данные") != std::string::npos,
                 "one-line memory summary is missing");
    ok &= expect(wide.find("#1 this_is_a_deliberately_long_field_name") !=
                     std::string::npos,
                 "stable field number is missing");
    ok &= expect(wide.find("в объекте: +0x0000…+0x0003") !=
                     std::string::npos,
                 "inclusive field byte range is missing");
    ok &= expect(wide.find("► КУЧА @ ") != std::string::npos &&
                     wide.find("#2 text.ptr ведёт во внешний блок") !=
                         std::string::npos,
                 "heap string connection is missing");
    ok &= expect(wide.find("◄ диапазон байт  ► наружу  ↩ внутрь  × nullptr") !=
                     std::string::npos,
                 "connection legend is missing");

    const std::string vector = render_vector_at(126);
    ok &= expect(vector.find("адаптер std::vector") != std::string::npos &&
                     vector.find("vector: size 3 · capacity 6") !=
                         std::string::npos,
                 "vector public facts are missing");
    ok &= expect(vector.find("≈ data()/begin") != std::string::npos &&
                     vector.find("≈ end = data + size") != std::string::npos &&
                     vector.find("≈ capacity_end") != std::string::npos,
                 "vector address slots were not correlated");
    ok &= expect(vector.find("vector.data() ведёт во внешний массив") !=
                     std::string::npos &&
                     vector.find("#2 +0x0008") != std::string::npos &&
                     vector.find("3333") != std::string::npos,
                 "vector heap satellite is incomplete");

    std::vector<int> empty_vector;
    std::vector<int> reserved_vector;
    reserved_vector.reserve(4);
    std::vector<bool> packed_bits{true, false, true};
    std::ostringstream vector_edges;
    std::streambuf* vector_old = std::cout.rdbuf(vector_edges.rdbuf());
    eye::inspect(empty_vector, "empty vector");
    eye::inspect(reserved_vector, "reserved vector");
    eye::inspect(packed_bits, "vector bool");
    std::cout.rdbuf(vector_old);
    ok &= expect(vector_edges.str().find("внешнего массива нет: vector пуст") !=
                     std::string::npos,
                 "empty vector explanation is missing");
    ok &= expect(vector_edges.str().find(
                     "элементов нет; память только зарезервирована") !=
                     std::string::npos,
                 "reserved but empty vector is not handled");
    ok &= expect(vector_edges.str().find(
                     "vector<bool>: биты упакованы; data() недоступен") !=
                     std::string::npos &&
                     vector_edges.str().find(
                         "элементы через operator[]: [true, false, true]") !=
                         std::string::npos,
                 "vector<bool> specialization is not handled");

    const std::string hostile = render_at(126, "строка\n\033[31mслом рамки");
    ok &= expect(hostile.find('\033') == std::string::npos,
                 "raw ANSI escape leaked from a label");
    ok &= expect(lines_fit(hostile, 126),
                 "control characters broke the frame geometry");

    // Ненулевой адрес заведомо нельзя разыменовывать. Тест проходит, если
    // inspect() лишь выводит адрес и не падает на попытке прочитать int.
    int* invalid = reinterpret_cast<int*>(static_cast<std::uintptr_t>(1));
    volatile int live = 9;
    volatile int* volatile_ptr = &live;
    std::ostringstream pointers;
    std::streambuf* old = std::cout.rdbuf(pointers.rdbuf());
    eye::inspect(invalid, "невалидный указатель");
    eye::inspect(volatile_ptr, "указатель на volatile");
    std::cout.rdbuf(old);
    ok &= expect(pointers.str().find("адрес может быть невалиден") !=
                     std::string::npos,
                 "pointer safety note is missing");

    int external = 7;
    Links links;
    links.inside = &links.value;
    links.outside = &external;
    std::ostringstream link_map;
    old = std::cout.rdbuf(link_map.rdbuf());
    eye::inspect(links, "связи указателей");
    std::cout.rdbuf(old);
    ok &= expect(link_map.str().find("↩ этот объект: база+0x0000") !=
                     std::string::npos,
                 "internal pointer connection is missing");
    ok &= expect(link_map.str().find("► внешняя память @ ") !=
                     std::string::npos,
                 "external pointer connection is missing");
    ok &= expect(link_map.str().find("× nullptr — связь обрывается") !=
                     std::string::npos,
                 "null pointer terminator is missing");
    ok &= expect(!eye::detail::stringify(&function_target).empty(),
                 "function pointer formatting failed");

    const std::string escaped =
        eye::detail::stringify(std::string{"line\nansi\033[31m"});
    ok &= expect(escaped.find('\n') == std::string::npos &&
                     escaped.find('\033') == std::string::npos,
                 "string value contains terminal control bytes");

    // --- наследование: множественное (два полиморфных базовых под-объекта) ---
    Derived derived;
    for (const std::size_t width : {80u, 100u, 126u})
        ok &= expect(lines_fit(render_obj(derived, width, "MI"), width),
                     "multiple-inheritance render exceeds terminal width");
    const std::string mi = render_obj(derived, 126, "MI");
    ok &= expect(mi.find("иерархия — под-объекты баз") != std::string::npos,
                 "hierarchy section is missing");
    ok &= expect(mi.find("из базы BaseA") != std::string::npos &&
                     mi.find("из базы BaseB") != std::string::npos,
                 "inherited field owner labels are missing");
    ok &= expect(mi.find("vptr «BaseB»") != std::string::npos,
                 "secondary base vptr site is missing");
#if EYE_ITANIUM_ABI
    ok &= expect(mi.find("offset-to-top = -") != std::string::npos,
                 "secondary base offset-to-top is missing (Itanium)");
#endif

    // --- наследование: ромб с общей virtual-базой ---
    Diamond diamond_obj;
    const std::string diamond = render_obj(diamond_obj, 126, "diamond");
    ok &= expect(lines_fit(diamond, 126), "diamond render exceeds terminal width");
    ok &= expect(diamond.find("virtual") != std::string::npos &&
                     diamond.find("общий (показан выше)") != std::string::npos,
                 "shared virtual base is not marked as shared");
    // Общий VBase лёг в память ОДИН раз → ровно одна метка «из базы VBase».
    ok &= expect(count_occurrences(diamond, "из базы VBase") == 1,
                 "shared virtual base fields were double-counted");

    // --- регресс #1: 3-уровневая невиртуальная цепочка (база-в-базе на off 0) ---
    // hp/speed базы-в-базе НЕ должны теряться, и её нельзя метить «общий».
    GLeaf leaf_obj;
    const std::string chain = render_obj(leaf_obj, 126, "chain");
    ok &= expect(chain.find("из базы GBase") != std::string::npos &&
                     chain.find("из базы GMid") != std::string::npos,
                 "nested (base-of-base) fields were dropped");
    ok &= expect(chain.find("общий (показан выше)") == std::string::npos,
                 "non-virtual base was wrongly marked as shared");

    // --- регресс #2: повторяющаяся невиртуальная база — оба под-объекта видны ---
    RepDerived rep_obj;
    const std::string rep = render_obj(rep_obj, 126, "rep");
    ok &= expect(count_occurrences(rep, "из базы Coin") == 2,
                 "a repeated non-virtual base subobject was dropped");

    // --- регресс #3: НЕполиморфная virtual-база → указатель vbase, не padding ---
    NPDerived np_obj;
    const std::string np = render_obj(np_obj, 126, "np");
    ok &= expect(np.find("указатель на virtual-базу") != std::string::npos,
                 "virtual-base pointer of a non-polymorphic type mislabeled");
    ok &= expect(np.find("из базы NPBase") != std::string::npos,
                 "virtual base field missing for non-polymorphic derived");

    // --- регресс (Codex): EYE_BASES без своего EYE_DESCRIBE — поле базы 1 раз ---
    NoOwnFields no_own;
    const std::string noown = render_obj(no_own, 126, "noown");
    ok &= expect(count_occurrences(noown, "из базы SoleBase") == 1,
                 "inherited eye_describe double-counted base fields");

    // --- регресс (Codex): база без EYE_DESCRIBE → байты СКРЫТЫ, не padding ------
    WithRawBase raw_obj;
    const std::string raw = render_obj(raw_obj, 126, "raw");
    ok &= expect(raw.find("непрозрачная база «RawBase»") != std::string::npos &&
                     raw.find("= 33") != std::string::npos,
                 "undescribed base bytes not marked opaque (shown as padding)");

    // Непрозрачная (private+ctor, неагрегатная) база — тоже скрытые байты.
    WithPrivBase priv_obj;
    const std::string priv = render_obj(priv_obj, 126, "priv");
    ok &= expect(priv.find("непрозрачная база «PrivBase»") != std::string::npos,
                 "non-aggregate opaque base not marked opaque");

    // Агрегат-со-своей-базой как база: должен КОМПИЛИРОВАТЬСЯ и быть скрытым.
    DeepTop deep_obj;
    const std::string deep = render_obj(deep_obj, 126, "deep");
    ok &= expect(deep.find("непрозрачная база «DeepMid»") != std::string::npos &&
                     deep.find("= 3") != std::string::npos,
                 "aggregate-with-own-base failed to compile or not opaque");

    // --- регресс (Codex): тип с лишь УНАСЛЕДОВАННЫМ EYE_DESCRIBE + своим полем
    //     обязан компилироваться И трактоваться как НЕПРОЗРАЧНЫЙ (не пустая
    //     карта с padding'ом — чужой реестр к нему неприменим) ------------------
    InhChild inh;
    const std::string inh_out = render_obj(inh, 126, "inh");
    ok &= expect(inh_out.find("добавь EYE_DESCRIBE") != std::string::npos,
                 "inherited-only description not treated as opaque");

    // --- регресс (Codex): унаследованный eye_bases() не применяется к наследнику;
    //     тип без своего EYE_BASES/EYE_DESCRIBE тоже непрозрачный (не падение) ---
    RegLeaf reg_leaf;
    const std::string leaf_out = render_obj(reg_leaf, 126, "regleaf");
    ok &= expect(leaf_out.find("добавь EYE_DESCRIBE") != std::string::npos,
                 "inherited eye_bases() was wrongly applied to derived");

    // --- регресс (Codex): унаследованный eye_bases() без eye_describe() —
    //     compile-safe + непрозрачный (agg-fallback не разбирает агрегат-с-базой)
    BasesOnlyL bases_only;
    const std::string bo_out = render_obj(bases_only, 126, "basesonly");
    ok &= expect(bo_out.find("добавь EYE_DESCRIBE") != std::string::npos,
                 "inherited-eye_bases-only type not treated as opaque");

    // --- регресс (Codex): scoped enum рендерится через underlying, не «—» ------
    ok &= expect(eye::detail::stringify(Color::Blue) == "4",
                 "scoped enum not rendered through its underlying value");
    HasEnum has_enum;
    const std::string enum_out = render_obj(has_enum, 126, "enum");
    ok &= expect(enum_out.find("= 2") != std::string::npos &&
                     enum_out.find("(0x0002)") != std::string::npos &&
                     enum_out.find("c · ") != std::string::npos,
                 "enum field value/hex missing in render");

    // --- регресс (Codex): агрегат-с-базой без макросов на верхнем уровне —
    //     компилируется и непрозрачен (не structured-binding hard error) -------
    PlainDerived plain;
    const std::string plain_out = render_obj(plain, 126, "plain");
    ok &= expect(plain_out.find("добавь EYE_DESCRIBE") != std::string::npos,
                 "base-bearing aggregate not opaque (compile error / padding)");

    // --- регресс (Codex): агрегат с ПУСТОЙ базой (field_count==0) — поле не в
    //     padding, тип непрозрачный ---------------------------------------------
    EmptyDerived empty_derived;
    const std::string ed_out = render_obj(empty_derived, 126, "emptyderived");
    ok &= expect(ed_out.find("добавь EYE_DESCRIBE") != std::string::npos &&
                     ed_out.find("дыра, внутри мусор") == std::string::npos,
                 "empty-base aggregate rendered its member as padding");

    // --- регресс (Codex): own EYE_BASES без EYE_DESCRIBE + свой член — база
    //     видна, а собственный член скрыт, не padding --------------------------
    OwnBasesOnly own_bases_only;
    const std::string obo = render_obj(own_bases_only, 126, "ownbasesonly");
    ok &= expect(obo.find("из базы OwnBaseSrc") != std::string::npos &&
                     obo.find("свои поля не описаны") != std::string::npos &&
                     obo.find("дыра, внутри мусор") == std::string::npos,
                 "own-bases-only derived member rendered as padding");

    return ok ? 0 : 1;
}
