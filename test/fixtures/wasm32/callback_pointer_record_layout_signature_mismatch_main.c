#pragma pack(push, 1)
struct callback_pointer_layout_payload {
  char tag;
  int value;
};
#pragma pack(pop)

_Static_assert(
    sizeof(struct callback_pointer_layout_payload) == 5,
    "packed callback pointer payload size");

typedef int callback_pointer_layout_reader_t(
    const struct callback_pointer_layout_payload *value);

int invoke_callback_pointer_layout_reader(
    callback_pointer_layout_reader_t *reader);

static int read_callback_pointer_layout_payload(
    const struct callback_pointer_layout_payload *value) {
  return value->value;
}

int main(void) {
  return invoke_callback_pointer_layout_reader(
             read_callback_pointer_layout_payload) == 42
             ? 0
             : 1;
}
