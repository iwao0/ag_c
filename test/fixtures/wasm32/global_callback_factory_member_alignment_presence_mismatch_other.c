struct global_callback_factory_alignment_payload {
  int value;
};

_Static_assert(
    sizeof(struct global_callback_factory_alignment_payload) == sizeof(int),
    "plain global callback factory payload size");
_Static_assert(
    _Alignof(struct global_callback_factory_alignment_payload) ==
        _Alignof(int),
    "plain global callback factory payload alignment");

typedef int global_callback_factory_alignment_target_t(
    struct global_callback_factory_alignment_payload value);
typedef global_callback_factory_alignment_target_t
    *global_callback_factory_alignment_factory_t(void);

static int read_global_callback_factory_alignment_payload(
    struct global_callback_factory_alignment_payload value) {
  return value.value;
}

static global_callback_factory_alignment_target_t
    *make_global_callback_factory_alignment_target(void) {
  return read_global_callback_factory_alignment_payload;
}

global_callback_factory_alignment_factory_t
    *global_callback_factory_alignment_factory =
        make_global_callback_factory_alignment_target;
