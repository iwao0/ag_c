// Paired with global_atomic_mutual_record_distinct_enum_mismatch_main.c.

enum global_atomic_mutual_actual_enum {
  GLOBAL_ATOMIC_MUTUAL_ACTUAL_ZERO = 0,
  GLOBAL_ATOMIC_MUTUAL_ACTUAL_VALUE = 42
};

struct global_atomic_mutual_right;

struct global_atomic_mutual_left {
  int value;
  _Atomic(struct global_atomic_mutual_right *) next;
};

struct global_atomic_mutual_right {
  enum global_atomic_mutual_actual_enum value;
  _Atomic(struct global_atomic_mutual_left *) next;
};

static struct global_atomic_mutual_right right;
struct global_atomic_mutual_left
    global_atomic_mutual_record_enum = {0, &right};
static struct global_atomic_mutual_right right = {
    GLOBAL_ATOMIC_MUTUAL_ACTUAL_VALUE,
    &global_atomic_mutual_record_enum};
