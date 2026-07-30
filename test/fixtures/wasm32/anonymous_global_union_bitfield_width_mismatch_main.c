typedef union {
  unsigned int bits;
  unsigned int low : 7;
} anonymous_global_union_bitfield_width_t;

extern anonymous_global_union_bitfield_width_t
    anonymous_global_union_bitfield_width_value;

int main(void) {
  return (int)anonymous_global_union_bitfield_width_value.bits;
}
