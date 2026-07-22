#include <assert.h>

struct sparse_record {
  unsigned char tag;
  unsigned int value;
  unsigned char tail;
};

union sparse_payload {
  struct sparse_record record;
  double number;
  unsigned char raw[24];
};

struct sparse_packet {
  unsigned char prefix;
  union sparse_payload items[5];
  unsigned char suffix;
};

static union sparse_payload global_items[5] = {
    [4].record = {4, 0x456789abu, 5},
    [1].number = 2.5,
    [3].record = {3, 0x3456789au, 4},
    [0].raw[7] = 17,
};

static struct sparse_packet global_packet = {
    .prefix = 18,
    .items = {
        [4].record = {8, 0x89abcdefu, 9},
        [1].number = 3.5,
        [3].record = {6, 0x6789abcdu, 7},
        [0].raw[11] = 19,
    },
    .suffix = 20,
};

static int items_match(const union sparse_payload *items,
                       unsigned char raw_index, unsigned char raw_value,
                       double number, unsigned char middle_tag,
                       unsigned int middle_value, unsigned char middle_tail,
                       unsigned char last_tag, unsigned int last_value,
                       unsigned char last_tail) {
  return items[0].raw[raw_index] == raw_value &&
         items[1].number == number &&
         items[2].record.tag == 0 &&
         items[2].record.value == 0 &&
         items[2].record.tail == 0 &&
         items[3].record.tag == middle_tag &&
         items[3].record.value == middle_value &&
         items[3].record.tail == middle_tail &&
         items[4].record.tag == last_tag &&
         items[4].record.value == last_value &&
         items[4].record.tail == last_tail;
}

static const union sparse_payload *static_items(void) {
  static union sparse_payload items[5] = {
      [4].record = {12, 0xcdef0123u, 13},
      [1].number = 4.5,
      [3].record = {10, 0xabcdef01u, 11},
      [0].raw[15] = 21,
  };
  return items;
}

int main(void) {
  union sparse_payload local_items[5] = {
      [4].record = {16, 0x01234567u, 17},
      [1].number = 5.5,
      [3].record = {14, 0xef012345u, 15},
      [0].raw[19] = 22,
  };

  assert(items_match(global_items, 7, 17, 2.5,
                     3, 0x3456789au, 4,
                     4, 0x456789abu, 5));
  assert(items_match(static_items(), 15, 21, 4.5,
                     10, 0xabcdef01u, 11,
                     12, 0xcdef0123u, 13));
  assert(items_match(local_items, 19, 22, 5.5,
                     14, 0xef012345u, 15,
                     16, 0x01234567u, 17));
  assert(global_packet.prefix == 18);
  assert(items_match(global_packet.items, 11, 19, 3.5,
                     6, 0x6789abcdu, 7,
                     8, 0x89abcdefu, 9));
  assert(global_packet.suffix == 20);
  return 0;
}
