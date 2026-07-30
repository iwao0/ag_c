typedef union {
  unsigned int low : 8;
  unsigned int bits;
} anonymous_global_union_bitfield_width_t;

anonymous_global_union_bitfield_width_t
    anonymous_global_union_bitfield_width_value = {.bits = 42U};
