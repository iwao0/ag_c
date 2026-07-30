// Paired with union_member_order_signature_xtu_main.c.

#ifndef AG_C_UNION_MEMBER_ORDER_SIGNATURE_XTU_TYPES
#define AG_C_UNION_MEMBER_ORDER_SIGNATURE_XTU_TYPES
union payload {
  unsigned int low : 7;
  const int *pointer;
  unsigned int bits;
  int integer;
};
#endif

int read_payload(union payload value) {
  return value.integer;
}
