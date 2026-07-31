// Discarded atomic enum expressions perform complete loads, while sizeof and
// _Alignof remain unevaluated type queries.
// Expected: exit=0.

enum signed_state {
  SIGNED_STATE_NEGATIVE = -1,
  SIGNED_STATE_VALUE = 17
};

enum unsigned_state {
  UNSIGNED_STATE_ZERO = 0,
  UNSIGNED_STATE_VALUE = 25
};

struct holder {
  _Atomic(enum signed_state) signed_member;
  _Atomic(enum unsigned_state) unsigned_member;
};

static _Atomic(enum signed_state) signed_global =
    SIGNED_STATE_VALUE;
static _Atomic(enum unsigned_state) unsigned_global =
    UNSIGNED_STATE_VALUE;
static _Atomic(enum unsigned_state) unsigned_slots[2] = {
    UNSIGNED_STATE_ZERO, UNSIGNED_STATE_VALUE};
static struct holder global_holder = {
    SIGNED_STATE_NEGATIVE, UNSIGNED_STATE_VALUE};

static int evaluated_selectors;
static int unevaluated_selectors;

static _Atomic(enum signed_state) *select_evaluated(void) {
  evaluated_selectors++;
  return &signed_global;
}

_Atomic(enum unsigned_state) *select_unevaluated(void) {
  unevaluated_selectors++;
  return &unsigned_global;
}

_Static_assert(
    sizeof(_Atomic(enum signed_state)) ==
        sizeof(_Atomic(enum unsigned_state)),
    "atomic enum widths");
_Static_assert(
    _Alignof(_Atomic(enum signed_state)) ==
        _Alignof(_Atomic(enum unsigned_state)),
    "atomic enum alignments");

int main(void) {
  _Atomic(enum signed_state) signed_local =
      SIGNED_STATE_NEGATIVE;
  _Atomic(enum unsigned_state) unsigned_local =
      UNSIGNED_STATE_VALUE;
  _Atomic(enum signed_state) *signed_pointer = &signed_local;
  _Atomic(enum unsigned_state) *unsigned_pointer = &unsigned_local;
  _Atomic(enum signed_state) * volatile volatile_pointer =
      &signed_global;

  (void)signed_global;
  (void)unsigned_global;
  (void)signed_local;
  (void)unsigned_local;
  (void)*signed_pointer;
  (void)*unsigned_pointer;
  (void)*volatile_pointer;
  (void)unsigned_slots[0];
  (void)global_holder.signed_member;
  (void)global_holder.unsigned_member;
  (void)*select_evaluated();

  if (sizeof *select_unevaluated() !=
      sizeof(_Atomic(enum unsigned_state)))
    return 1;
  if (evaluated_selectors != 1 || unevaluated_selectors != 0)
    return 2;
  if (signed_global != SIGNED_STATE_VALUE ||
      unsigned_global != UNSIGNED_STATE_VALUE ||
      signed_local != SIGNED_STATE_NEGATIVE ||
      unsigned_local != UNSIGNED_STATE_VALUE)
    return 3;
  if (unsigned_slots[0] != UNSIGNED_STATE_ZERO ||
      unsigned_slots[1] != UNSIGNED_STATE_VALUE ||
      global_holder.signed_member != SIGNED_STATE_NEGATIVE ||
      global_holder.unsigned_member != UNSIGNED_STATE_VALUE)
    return 4;
  return 0;
}
