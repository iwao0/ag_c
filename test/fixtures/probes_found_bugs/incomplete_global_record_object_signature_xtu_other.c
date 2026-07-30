struct incomplete_global_record_object {
  int value;
};

struct incomplete_global_record_object
    incomplete_global_record_value = {42};

int check_incomplete_global_record_address(const void *address) {
  return address == &incomplete_global_record_value;
}

int read_incomplete_global_record_value(void) {
  return incomplete_global_record_value.value;
}
