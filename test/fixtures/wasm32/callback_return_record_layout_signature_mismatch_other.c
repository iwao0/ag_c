struct callback_return_layout_payload {
  char tag;
  int value;
};

_Static_assert(
    sizeof(struct callback_return_layout_payload) == 8,
    "plain callback return payload size");

typedef struct callback_return_layout_payload
    callback_return_layout_factory_t(void);

int invoke_callback_return_layout_factory(
    callback_return_layout_factory_t *factory) {
  struct callback_return_layout_payload value = factory();
  return value.value;
}
