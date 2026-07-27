/* A bit-field cannot have function type. Expect ag_c E3064. */
struct function_bitfield {
  unsigned int callback(void) : 3;
};

int main(void) {
  return 0;
}
