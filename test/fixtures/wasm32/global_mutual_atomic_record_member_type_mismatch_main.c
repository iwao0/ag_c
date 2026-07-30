struct mutual_atomic_member_right;

struct mutual_atomic_member_left {
  int value;
  _Atomic(struct mutual_atomic_member_right *) next;
};

struct mutual_atomic_member_right {
  int value;
  _Atomic(struct mutual_atomic_member_left *) next;
};

extern struct mutual_atomic_member_left
    global_mutual_atomic_record_member;

int main(void) {
  return global_mutual_atomic_record_member.value;
}
