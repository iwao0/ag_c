#pragma pack(push, 1)
struct global_atomic_layout_payload {
  char tag;
  int value;
};
#pragma pack(pop)

extern _Atomic(struct global_atomic_layout_payload *)
    global_atomic_pointer_record_layout;

int main(void) {
  return global_atomic_pointer_record_layout->value;
}
