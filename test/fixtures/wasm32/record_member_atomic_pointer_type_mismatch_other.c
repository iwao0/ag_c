struct atomic_pointer_record {
  int *pointer;
};

static int value;

struct atomic_pointer_record
    record_member_atomic_pointer_value = {&value};
