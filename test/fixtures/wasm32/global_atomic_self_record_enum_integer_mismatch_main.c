enum global_atomic_self_record_unsigned_enum {
  GLOBAL_ATOMIC_SELF_RECORD_ZERO = 0,
  GLOBAL_ATOMIC_SELF_RECORD_VALUE = 42
};

struct global_atomic_self_record_node {
  enum global_atomic_self_record_unsigned_enum value;
  _Atomic(struct global_atomic_self_record_node *) next;
};

extern struct global_atomic_self_record_node
    global_atomic_self_record_enum_integer;

int main(void) {
  return global_atomic_self_record_enum_integer.value;
}
