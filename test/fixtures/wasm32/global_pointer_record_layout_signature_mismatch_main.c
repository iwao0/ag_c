#pragma pack(push, 1)
struct global_pointer_layout_payload {
  char tag;
  int value;
};
#pragma pack(pop)

extern struct global_pointer_layout_payload
    *global_pointer_layout_value;

int main(void) {
  return global_pointer_layout_value->value == 42 ? 0 : 1;
}
