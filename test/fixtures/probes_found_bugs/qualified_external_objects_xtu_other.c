static int restrict_storage[2] = {10, 20};

volatile int shared_volatile_value = 5;
int *restrict shared_restrict_pointer = restrict_storage;

int *shared_restrict_target(void) {
  return shared_restrict_pointer;
}

int qualified_external_sum(void) {
  return shared_volatile_value + restrict_storage[0] + restrict_storage[1];
}
