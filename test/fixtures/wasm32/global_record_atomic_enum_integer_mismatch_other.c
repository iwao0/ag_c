// Paired with global_record_atomic_enum_integer_mismatch_main.c.

struct global_atomic_enum_record {
  _Atomic(int) value;
};

struct global_atomic_enum_record
    global_atomic_enum_integer_mismatch = {42};
