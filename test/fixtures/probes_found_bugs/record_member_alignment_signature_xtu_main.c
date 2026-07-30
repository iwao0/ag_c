// Corresponding members in compatible record definitions must carry
// equivalent alignment specifiers across translation units.

#ifndef AG_C_RECORD_MEMBER_ALIGNMENT_SIGNATURE_XTU_TYPES
#define AG_C_RECORD_MEMBER_ALIGNMENT_SIGNATURE_XTU_TYPES
struct aligned_payload {
  _Alignas(4) int value;
};
#endif

_Static_assert(
    sizeof(struct aligned_payload) == sizeof(int),
    "member alignment must not change the payload size");
_Static_assert(
    _Alignof(struct aligned_payload) == _Alignof(int),
    "member alignment must match the natural payload alignment");

int consume_aligned_payload(struct aligned_payload value);

int main(void) {
  struct aligned_payload value = {42};
  return consume_aligned_payload(value) == 42 ? 0 : 1;
}
