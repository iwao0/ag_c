#pragma pack(push, 1)
struct global_record_layout_payload {
  char tag;
  int value;
};
#pragma pack(pop)

_Static_assert(
    sizeof(struct global_record_layout_payload) == 5,
    "packed global payload size");

extern struct global_record_layout_payload
    global_record_layout_value;

int main(void) {
  return global_record_layout_value.value == 42 ? 0 : 1;
}
