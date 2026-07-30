enum record_atomic_callback_unsigned_enum {
  RECORD_ATOMIC_CALLBACK_UNSIGNED_ZERO = 0,
  RECORD_ATOMIC_CALLBACK_UNSIGNED_VALUE = 42
};

typedef enum record_atomic_callback_unsigned_enum
    record_atomic_callback_t(int value);

struct record_atomic_callback_holder {
  _Atomic(record_atomic_callback_t *) member;
};

extern struct record_atomic_callback_holder
    record_atomic_callback_enum_result;

int main(void) {
  return record_atomic_callback_enum_result.member(42);
}
