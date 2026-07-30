#pragma pack(push, 1)
struct nested_layout_payload {
  char tag;
  int value;
};
#pragma pack(pop)

union nested_layout_envelope {
  struct nested_layout_payload payload;
  long long force_outer_layout;
};

_Static_assert(
    sizeof(struct nested_layout_payload) == 5,
    "packed nested payload size");
_Static_assert(
    sizeof(union nested_layout_envelope) == sizeof(long long),
    "outer union size must match in both translation units");
_Static_assert(
    _Alignof(union nested_layout_envelope) == _Alignof(long long),
    "outer union alignment must match in both translation units");

int read_nested_layout_envelope(
    union nested_layout_envelope value);

int main(void) {
  union nested_layout_envelope value;
  value.payload.tag = 'x';
  value.payload.value = 42;
  return read_nested_layout_envelope(value) == 42 ? 0 : 1;
}
