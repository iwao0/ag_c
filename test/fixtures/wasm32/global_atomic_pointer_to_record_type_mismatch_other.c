struct atomic_pointer_record_payload {
  int value;
};

static struct atomic_pointer_record_payload value = {42};

struct atomic_pointer_record_payload *
    global_atomic_pointer_to_record = &value;
