struct atomic_record_pointee_payload {
  int value;
};

static struct atomic_record_pointee_payload value = {42};

struct atomic_record_pointee_payload *
    global_atomic_record_pointee = &value;
