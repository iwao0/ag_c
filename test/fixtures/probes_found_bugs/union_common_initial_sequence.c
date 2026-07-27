/*
 * A union may expose the common initial sequence of inactive structure
 * members.  Matching ordinary members and matching-width bit-fields remain
 * readable through either structure view.
 */
#include <assert.h>
#include <stddef.h>

struct compact_header {
  unsigned char tag;
  unsigned char flags;
  unsigned short length;
};

struct extended_header {
  unsigned char tag;
  unsigned char flags;
  unsigned short length;
  unsigned long payload;
};

union header_view {
  struct compact_header compact;
  struct extended_header extended;
};

struct narrow_bits {
  unsigned kind : 3;
  unsigned ready : 1;
  int value;
};

struct wide_bits {
  unsigned kind : 3;
  unsigned ready : 1;
  double value;
};

union bit_view {
  struct narrow_bits narrow;
  struct wide_bits wide;
};

static union header_view global_header = {
    .extended = {9, 5, 1024, 0x1234UL}};

_Static_assert(offsetof(struct compact_header, tag) ==
                   offsetof(struct extended_header, tag),
               "common tag offset");
_Static_assert(offsetof(struct compact_header, flags) ==
                   offsetof(struct extended_header, flags),
               "common flags offset");
_Static_assert(offsetof(struct compact_header, length) ==
                   offsetof(struct extended_header, length),
               "common length offset");

int main(void) {
  union header_view local_header = {
      .extended = {3, 6, 4096, 0x5678UL}};
  union bit_view bits = {.wide = {5, 1, 2.5}};

  assert(global_header.compact.tag == 9);
  assert(global_header.compact.flags == 5);
  assert(global_header.compact.length == 1024);

  assert(local_header.compact.tag == 3);
  assert(local_header.compact.flags == 6);
  assert(local_header.compact.length == 4096);

  local_header.compact.tag = 7;
  local_header.compact.flags = 2;
  local_header.compact.length = 77;
  assert(local_header.extended.tag == 7);
  assert(local_header.extended.flags == 2);
  assert(local_header.extended.length == 77);

  assert(bits.narrow.kind == 5);
  assert(bits.narrow.ready == 1);
  bits.narrow.kind = 2;
  bits.narrow.ready = 0;
  assert(bits.wide.kind == 2);
  assert(bits.wide.ready == 0);
  return 0;
}
