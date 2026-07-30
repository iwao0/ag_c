struct incomplete_callback_sibling_context;

#pragma pack(push, 1)
struct callback_sibling_layout_payload {
  char tag;
  int values[4];
};
#pragma pack(pop)

_Static_assert(
    sizeof(struct callback_sibling_layout_payload) == 17,
    "packed callback sibling payload size");

typedef int incomplete_callback_sibling_reader_t(
    struct incomplete_callback_sibling_context *context,
    struct callback_sibling_layout_payload value);

int invoke_incomplete_callback_sibling_reader(
    incomplete_callback_sibling_reader_t *reader);

static int read_incomplete_callback_sibling(
    struct incomplete_callback_sibling_context *context,
    struct callback_sibling_layout_payload value) {
  return context ? 0 : value.values[3];
}

int main(void) {
  return invoke_incomplete_callback_sibling_reader(
             read_incomplete_callback_sibling) == 42
             ? 0
             : 1;
}
