struct global_callback_factory_alignment_payload {
  _Alignas(4) int value;
};

_Static_assert(
    sizeof(struct global_callback_factory_alignment_payload) == sizeof(int),
    "aligned global callback factory payload size");
_Static_assert(
    _Alignof(struct global_callback_factory_alignment_payload) ==
        _Alignof(int),
    "aligned global callback factory payload alignment");

typedef int global_callback_factory_alignment_target_t(
    struct global_callback_factory_alignment_payload value);
typedef global_callback_factory_alignment_target_t
    *global_callback_factory_alignment_factory_t(void);

extern global_callback_factory_alignment_factory_t
    *global_callback_factory_alignment_factory;

int main(void) {
  global_callback_factory_alignment_target_t *target =
      global_callback_factory_alignment_factory();
  struct global_callback_factory_alignment_payload value = {42};
  return target(value) == 42 ? 0 : 1;
}
