#pragma pack(push, 1)
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
#pragma pack(pop)

extern struct mutual_atomic_layout_left
    global_mutual_atomic_record_layout;

int main(void) {
  return global_mutual_atomic_record_layout.value;
}
