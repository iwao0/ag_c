/*
 * Every object representation may be inspected and copied through a pointer
 * to a character type.  Copy a padded aggregate through each of unsigned
 * char, plain char, and signed char, then read the destination normally.
 */
#include <assert.h>
#include <stddef.h>

struct payload {
  unsigned short code;
  double weight;
  unsigned char bytes[3];
};

static void copy_unsigned(unsigned char *destination,
                          const unsigned char *source, size_t size) {
  for (size_t index = 0; index < size; index++)
    destination[index] = source[index];
}

static void copy_plain(char *destination, const char *source, size_t size) {
  for (size_t index = 0; index < size; index++)
    destination[index] = source[index];
}

static void copy_signed(signed char *destination,
                        const signed char *source, size_t size) {
  for (size_t index = 0; index < size; index++)
    destination[index] = source[index];
}

static void assert_payload(const struct payload *value) {
  assert(value->code == 0x1234);
  assert(value->weight == 3.25);
  assert(value->bytes[0] == 7);
  assert(value->bytes[1] == 128);
  assert(value->bytes[2] == 255);
}

int main(void) {
  struct payload source = {0x1234, 3.25, {7, 128, 255}};
  struct payload through_unsigned = {0};
  struct payload through_plain = {0};
  struct payload through_signed = {0};

  copy_unsigned((unsigned char *)&through_unsigned,
                (const unsigned char *)&source, sizeof(source));
  copy_plain((char *)&through_plain, (const char *)&source, sizeof(source));
  copy_signed((signed char *)&through_signed,
              (const signed char *)&source, sizeof(source));

  assert_payload(&through_unsigned);
  assert_payload(&through_plain);
  assert_payload(&through_signed);
  return 0;
}
