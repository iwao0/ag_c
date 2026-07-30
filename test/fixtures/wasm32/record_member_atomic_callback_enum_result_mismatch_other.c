// Paired with record_member_atomic_callback_enum_result_mismatch_main.c.

typedef int record_atomic_callback_t(int value);

struct record_atomic_callback_holder {
  _Atomic(record_atomic_callback_t *) member;
};

static int make_record_atomic_callback_result(int value) {
  return value;
}

struct record_atomic_callback_holder
    record_atomic_callback_enum_result = {
        make_record_atomic_callback_result};
