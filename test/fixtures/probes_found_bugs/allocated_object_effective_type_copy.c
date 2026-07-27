/*
 * Copying a typed object representation into allocated storage establishes
 * the source object's effective type for later non-modifying accesses.
 * C11 permits memcpy, memmove, and a copy as an array of character type.
 */
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

struct point {
  int x;
  double y;
  unsigned short flags;
};

struct envelope {
  unsigned char tag;
  struct point point;
  int samples[3];
};

static void copy_as_characters(void *destination, const void *source,
                               size_t size) {
  unsigned char *output = destination;
  const unsigned char *input = source;

  for (size_t index = 0; index < size; index++)
    output[index] = input[index];
}

static void assert_envelope(const struct envelope *value) {
  assert(value->tag == 7);
  assert(value->point.x == -23);
  assert(value->point.y == 6.25);
  assert(value->point.flags == 0x8123);
  assert(value->samples[0] == 3);
  assert(value->samples[1] == 5);
  assert(value->samples[2] == 8);
}

int main(void) {
  const struct envelope source = {
      7, {-23, 6.25, 0x8123}, {3, 5, 8}};
  struct envelope *through_memcpy = malloc(sizeof(source));
  struct envelope *through_memmove = malloc(sizeof(source));
  struct envelope *through_characters = malloc(sizeof(source));

  assert(through_memcpy != NULL);
  assert(through_memmove != NULL);
  assert(through_characters != NULL);

  assert(memcpy(through_memcpy, &source, sizeof(source)) == through_memcpy);
  assert(memmove(through_memmove, &source, sizeof(source)) == through_memmove);
  copy_as_characters(through_characters, &source, sizeof(source));

  assert_envelope(through_memcpy);
  assert_envelope(through_memmove);
  assert_envelope(through_characters);

  free(through_characters);
  free(through_memmove);
  free(through_memcpy);
  return 0;
}
