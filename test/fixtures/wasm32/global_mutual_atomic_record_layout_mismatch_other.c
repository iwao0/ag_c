struct mutual_atomic_layout_right;

struct mutual_atomic_layout_left {
  char tag;
  int value;
  _Atomic(struct mutual_atomic_layout_right *) next;
};

struct mutual_atomic_layout_right {
  char tag;
  int value;
  _Atomic(struct mutual_atomic_layout_left *) next;
};

static struct mutual_atomic_layout_right partner;
struct mutual_atomic_layout_left
    global_mutual_atomic_record_layout = {
        'l', 42, &partner};
static struct mutual_atomic_layout_right partner = {
    'r', 0, &global_mutual_atomic_record_layout};
