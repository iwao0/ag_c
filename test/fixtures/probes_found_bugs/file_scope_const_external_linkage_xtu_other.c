const int shared_const_value = 37;
const unsigned char shared_const_bytes[4] = {1, 2, 3, 4};

const int *shared_const_value_address(void) {
  return &shared_const_value;
}

const unsigned char *shared_const_bytes_address(void) {
  return shared_const_bytes;
}
