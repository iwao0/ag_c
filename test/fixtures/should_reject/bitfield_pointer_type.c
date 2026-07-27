/* A bit-field cannot have pointer type. Expect ag_c E3064. */
struct pointer_bitfield {
  unsigned int *pointer : 3;
};

int main(void) {
  return 0;
}
