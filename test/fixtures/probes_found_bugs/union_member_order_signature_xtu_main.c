// Compatible union definitions in separate translation units may declare
// corresponding members in different orders. Structures do not have this
// exception.

#ifndef AG_C_UNION_MEMBER_ORDER_SIGNATURE_XTU_TYPES
#define AG_C_UNION_MEMBER_ORDER_SIGNATURE_XTU_TYPES
union payload {
  _Alignas(4) int integer;
  unsigned int bits;
  const int *pointer;
  unsigned int low : 7;
};
#endif

int read_payload(union payload value);

int main(void) {
  union payload value;
  value.integer = 42;
  return read_payload(value) == 42 ? 0 : 1;
}
