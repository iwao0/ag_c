#pragma pack(push, 1)
struct callback_return_layout_payload {
  char tag;
  int value;
};
#pragma pack(pop)

_Static_assert(
    sizeof(struct callback_return_layout_payload) == 5,
    "packed callback return payload size");

typedef struct callback_return_layout_payload
    callback_return_layout_factory_t(void);

int invoke_callback_return_layout_factory(
    callback_return_layout_factory_t *factory);

static struct callback_return_layout_payload
make_callback_return_layout_payload(void) {
  struct callback_return_layout_payload value = {'x', 42};
  return value;
}

int main(void) {
  return invoke_callback_return_layout_factory(
             make_callback_return_layout_payload) == 42
             ? 0
             : 1;
}
