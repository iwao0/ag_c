extern const int shared_const_value;
extern const unsigned char shared_const_bytes[4];

extern const int *shared_const_value_address(void);
extern const unsigned char *shared_const_bytes_address(void);

int main(void) {
  if (shared_const_value != 37) return 1;
  if (shared_const_bytes[0] != 1 || shared_const_bytes[1] != 2 ||
      shared_const_bytes[2] != 3 || shared_const_bytes[3] != 4) {
    return 2;
  }
  if (shared_const_value_address() != &shared_const_value) return 3;
  if (shared_const_bytes_address() != shared_const_bytes) return 4;
  return 42;
}
