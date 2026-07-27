/*
 * Assigning a structure with a flexible array member copies its fixed
 * members only.  Storage following the flexible member belongs to each
 * destination object and must remain unchanged.
 */
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

struct packet {
  unsigned char tag;
  unsigned long count;
  int values[];
};

static struct packet *allocate_packet(size_t value_count) {
  return malloc(sizeof(struct packet) + value_count * sizeof(int));
}

static void copy_packet_header(struct packet *destination,
                               const struct packet *source) {
  *destination = *source;
}

int main(void) {
  struct packet *source = allocate_packet(3);
  struct packet *destination = allocate_packet(3);

  assert(source != NULL);
  assert(destination != NULL);
  assert(offsetof(struct packet, values) == sizeof(struct packet));

  source->tag = 7;
  source->count = 3;
  source->values[0] = 11;
  source->values[1] = 22;
  source->values[2] = 33;

  destination->tag = 1;
  destination->count = 99;
  destination->values[0] = 44;
  destination->values[1] = 55;
  destination->values[2] = 66;

  copy_packet_header(destination, source);
  assert(destination->tag == 7);
  assert(destination->count == 3);
  assert(destination->values[0] == 44);
  assert(destination->values[1] == 55);
  assert(destination->values[2] == 66);

  free(destination);
  free(source);
  return 0;
}
