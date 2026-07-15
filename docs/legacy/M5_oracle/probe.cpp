// ============================================================================
//  ОКО МАГА — M5: probe.cpp — образцы для допроса компилятора
// ============================================================================
//  Этот файл НЕ запускают. Его скармливают g++ с флагом -fdump-lang-class,
//  и компилятор выкладывает СВОЁ видение классов: точный layout, vtable,
//  выравнивание. Это и есть оракул — первоисточник, с которым мы сверяем
//  всё, что Око показало в M2 и M3.
//
//  Запуск: ./oracle.sh   (или команды из README руками)
// ============================================================================

// Те же типы, что были в M2 — сверим padding.
struct SloppyStack {
    char grade;
    double damage;
    char is_upgraded;
    int count;
};

struct TidyStack {
    double damage;
    int count;
    char grade;
    char is_upgraded;
};

// Та же иерархия, что была в M3 — сверим vtable.
struct Hero {
    virtual void attack() const;
    virtual void taunt() const;
    virtual ~Hero() = default;
    int level = 1;
};

struct CragHack : Hero {
    void attack() const override;
    int rage = 99;
};

// Определения нужны, чтобы линковщик не ругался при -c... а он и не будет:
// мы компилируем без линковки. Но чтобы файл был честным translation unit —
// пусть будут.
void Hero::attack() const {}
void Hero::taunt() const {}
void CragHack::attack() const {}

// Якорим типы, чтобы компилятор точно построил layout всех четырёх.
SloppyStack sloppy_anchor;
TidyStack   tidy_anchor;
CragHack    crag_anchor;
