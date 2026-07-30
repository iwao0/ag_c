#ifndef AG_C_INCOMPLETE_CALLBACK_RECORD_SIGNATURE_XTU_TYPES
#define AG_C_INCOMPLETE_CALLBACK_RECORD_SIGNATURE_XTU_TYPES
struct incomplete_callback_context {
  int marker;
};
#endif

typedef int incomplete_callback_reader_t(
    const struct incomplete_callback_context *context);

int invoke_incomplete_callback_reader(
    incomplete_callback_reader_t *reader) {
  struct incomplete_callback_context context = {1};
  return reader(&context);
}
