/* A bit-field cannot have array type. Expect ag_c E3064. */
struct array_bitfield {
  unsigned int values[2] : 3;
};

int main(void) {
  return 0;
}
