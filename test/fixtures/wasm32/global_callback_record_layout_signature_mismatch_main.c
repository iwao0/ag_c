#pragma pack(push, 1)
struct global_callback_layout_payload {
  char tag;
  int value;
};
#pragma pack(pop)

typedef int global_callback_layout_reader_t(
    const struct global_callback_layout_payload *value);

extern global_callback_layout_reader_t
    *global_callback_layout_reader;

int main(void) {
  struct global_callback_layout_payload value = {'x', 42};
  return global_callback_layout_reader(&value) == 42 ? 0 : 1;
}
