typedef int prototype_callback(
    struct prototype_record {
      unsigned char prefix;
      int value;
    } *first,
    struct prototype_record *second,
    unsigned char (*bytes)[sizeof(struct prototype_record)]);

static prototype_callback *callback_slot;

int main(void) {
  return callback_slot != 0;
}
