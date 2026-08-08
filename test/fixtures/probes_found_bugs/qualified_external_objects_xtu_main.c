extern volatile int shared_volatile_value;
extern int *restrict shared_restrict_pointer;

extern int *shared_restrict_target(void);
extern int qualified_external_sum(void);

int main(void) {
  if (shared_volatile_value != 5) return 1;
  if (shared_restrict_pointer[0] != 10 || shared_restrict_pointer[1] != 20) {
    return 2;
  }
  if (shared_restrict_target() != shared_restrict_pointer) return 3;

  shared_volatile_value = 7;
  shared_restrict_pointer[0] = 13;
  shared_restrict_pointer[1] = 22;
  if (qualified_external_sum() != 42) return 4;
  return 42;
}
