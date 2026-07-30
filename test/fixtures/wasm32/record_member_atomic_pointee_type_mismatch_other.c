struct atomic_pointee_record {
  int *pointee;
};

static int value = 42;

struct atomic_pointee_record
    record_member_atomic_pointee_value = {&value};
