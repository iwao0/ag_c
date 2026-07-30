struct mutual_atomic_member_right;

struct mutual_atomic_member_left {
  int value;
  _Atomic(struct mutual_atomic_member_right *) next;
};

struct mutual_atomic_member_right {
  unsigned int value;
  _Atomic(struct mutual_atomic_member_left *) next;
};

static struct mutual_atomic_member_right partner;
struct mutual_atomic_member_left
    global_mutual_atomic_record_member = {42, &partner};
static struct mutual_atomic_member_right partner = {
    0, &global_mutual_atomic_record_member};
