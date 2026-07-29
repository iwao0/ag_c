/*
 * A _Generic controlling expression undergoes lvalue, array, and function
 * designator conversions without being evaluated.  Top-level const, volatile,
 * and atomic qualifiers disappear from aggregate values, while taking an
 * address or decaying an array retains the pointed-to qualifiers.
 */
#include <assert.h>

struct Record {
  int first;
  int second;
};

union Choice {
  long integer;
  double real;
};

struct Holder {
  const struct Record constant;
  volatile struct Record varying;
  _Atomic(struct Record) atomic;
};

#define IS_RECORD(expression) \
  _Generic((expression), struct Record: 1, default: 0)
#define IS_CHOICE(expression) \
  _Generic((expression), union Choice: 1, default: 0)
#define IS_CONST_RECORD_POINTER(expression) \
  _Generic((expression), const struct Record *: 1, default: 0)
#define IS_VOLATILE_RECORD_POINTER(expression) \
  _Generic((expression), volatile struct Record *: 1, default: 0)
#define IS_ATOMIC_RECORD_POINTER(expression) \
  _Generic((expression), _Atomic(struct Record) *: 1, default: 0)
#define IS_CONST_CHOICE_POINTER(expression) \
  _Generic((expression), const union Choice *: 1, default: 0)
#define IS_ATOMIC_CHOICE_POINTER(expression) \
  _Generic((expression), _Atomic(union Choice) *: 1, default: 0)

const struct Record constant_record = {3, 5};
volatile struct Record varying_record = {7, 11};
_Atomic(struct Record) atomic_record =
    (struct Record){13, 17};
const union Choice constant_choice = {.integer = 19};
volatile union Choice varying_choice = {.real = 23.5};
_Atomic(union Choice) atomic_choice =
    (union Choice){.integer = 29};

const struct Record constant_records[2] = {
    {31, 37},
    {41, 43},
};
_Atomic(struct Record) atomic_records[2] = {
    (struct Record){47, 53},
    (struct Record){59, 61},
};

struct Holder holder = {
    .constant = {67, 71},
    .varying = {73, 79},
    .atomic = (struct Record){83, 89},
};

static int selections;

const struct Record *select_constant_record(void) {
  selections++;
  return &constant_record;
}

_Atomic(struct Record) *select_atomic_record(void) {
  selections++;
  return &atomic_record;
}

_Static_assert(IS_RECORD(constant_record),
               "const aggregate lvalue converts to an unqualified value");
_Static_assert(IS_RECORD(varying_record),
               "volatile aggregate lvalue converts to an unqualified value");
_Static_assert(IS_RECORD(atomic_record),
               "atomic aggregate lvalue converts to its non-atomic value");
_Static_assert(IS_CHOICE(constant_choice),
               "const union lvalue converts to an unqualified value");
_Static_assert(IS_CHOICE(varying_choice),
               "volatile union lvalue converts to an unqualified value");
_Static_assert(IS_CHOICE(atomic_choice),
               "atomic union lvalue converts to its non-atomic value");

_Static_assert(IS_RECORD(holder.constant),
               "const aggregate member undergoes lvalue conversion");
_Static_assert(IS_RECORD(holder.varying),
               "volatile aggregate member undergoes lvalue conversion");
_Static_assert(IS_RECORD(holder.atomic),
               "atomic aggregate member undergoes lvalue conversion");

_Static_assert(IS_CONST_RECORD_POINTER(&constant_record),
               "address-of preserves const qualification");
_Static_assert(IS_VOLATILE_RECORD_POINTER(&varying_record),
               "address-of preserves volatile qualification");
_Static_assert(IS_ATOMIC_RECORD_POINTER(&atomic_record),
               "address-of preserves atomic qualification");
_Static_assert(IS_CONST_CHOICE_POINTER(&constant_choice),
               "union address preserves const qualification");
_Static_assert(IS_ATOMIC_CHOICE_POINTER(&atomic_choice),
               "union address preserves atomic qualification");

_Static_assert(IS_CONST_RECORD_POINTER(constant_records),
               "array decay preserves const element qualification");
_Static_assert(IS_ATOMIC_RECORD_POINTER(atomic_records),
               "array decay preserves atomic element qualification");

_Static_assert(IS_RECORD(((const struct Record){97, 101})),
               "qualified struct compound literal undergoes conversion");
_Static_assert(IS_CHOICE((volatile union Choice){.real = 103.5}),
               "qualified union compound literal undergoes conversion");
_Static_assert(IS_RECORD(((void)0, constant_record)),
               "comma applies aggregate lvalue conversion");
_Static_assert(IS_RECORD(1 ? constant_record : varying_record),
               "conditional operands produce an unqualified aggregate value");

_Static_assert(IS_RECORD(*select_constant_record()),
               "generic controlling expression remains unevaluated");
_Static_assert(IS_RECORD(*select_atomic_record()),
               "unevaluated atomic aggregate is not loaded");

int main(void) {
  assert(selections == 0);

  assert(IS_RECORD(*select_constant_record()));
  assert(IS_RECORD(*select_atomic_record()));
  assert(selections == 0);

  assert(IS_RECORD(constant_record));
  assert(IS_RECORD(varying_record));
  assert(IS_RECORD(atomic_record));
  assert(IS_CHOICE(constant_choice));
  assert(IS_CHOICE(varying_choice));
  assert(IS_CHOICE(atomic_choice));
  assert(IS_CONST_RECORD_POINTER(constant_records));
  assert(IS_ATOMIC_RECORD_POINTER(atomic_records));
  assert(selections == 0);
  return 0;
}
