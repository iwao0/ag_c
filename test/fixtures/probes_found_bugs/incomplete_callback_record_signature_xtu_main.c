#ifndef AG_C_INCOMPLETE_CALLBACK_RECORD_SIGNATURE_XTU_TYPES
#define AG_C_INCOMPLETE_CALLBACK_RECORD_SIGNATURE_XTU_TYPES
struct incomplete_callback_context;
#endif

typedef int incomplete_callback_reader_t(
    const struct incomplete_callback_context *context);

int invoke_incomplete_callback_reader(
    incomplete_callback_reader_t *reader);

static int read_incomplete_callback_context(
    const struct incomplete_callback_context *context) {
  return context ? 42 : 0;
}

int main(void) {
  return invoke_incomplete_callback_reader(
             read_incomplete_callback_context) == 42
             ? 0
             : 1;
}
