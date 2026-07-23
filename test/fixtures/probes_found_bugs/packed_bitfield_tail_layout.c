#include <assert.h>
#include <stddef.h>

#pragma pack(push, 1)
struct packed_one_mixed {
  unsigned char prefix;
  unsigned int first : 10;
  unsigned long second : 40;
  unsigned char tail;
};

struct packed_one_zero_width {
  unsigned char prefix;
  unsigned int first : 5;
  unsigned int : 0;
  unsigned int second : 6;
  unsigned char tail;
};

struct packed_one_terminal {
  unsigned char prefix;
  unsigned int bits : 5;
};

union packed_one_union {
  unsigned int bits : 5;
  unsigned char byte;
};
#pragma pack(pop)

#pragma pack(push, 2)
struct packed_two_mixed {
  unsigned char prefix;
  unsigned short first : 9;
  unsigned int second : 15;
  unsigned char tail;
};

struct packed_two_crossing {
  unsigned char prefix;
  unsigned long first : 20;
  unsigned int second : 20;
  unsigned char tail;
};

struct packed_two_terminal {
  unsigned char prefix;
  unsigned long bits : 20;
};

union packed_two_union {
  unsigned long bits : 20;
  unsigned char byte;
};
#pragma pack(pop)

static struct packed_one_mixed global_one_mixed = {
    .prefix = 11, .first = 777, .second = 0xabcde12345UL, .tail = 12,
};
static struct packed_one_zero_width global_one_zero = {
    .prefix = 13, .first = 17, .second = 45, .tail = 14,
};
static struct packed_one_terminal global_one_terminal = {
    .prefix = 15, .bits = 19,
};
static union packed_one_union global_one_union = {
    .bits = 21,
};
static struct packed_two_mixed global_two_mixed = {
    .prefix = 16, .first = 0x101, .second = 0x4567, .tail = 17,
};
static struct packed_two_crossing global_two_crossing = {
    .prefix = 18, .first = 0xabcdeUL, .second = 0x54321, .tail = 19,
};
static struct packed_two_terminal global_two_terminal = {
    .prefix = 20, .bits = 0x34567UL,
};
static union packed_two_union global_two_union = {
    .bits = 0x45678UL,
};

static int check_layout(void) {
  assert(sizeof(struct packed_one_mixed) == 9);
  assert(_Alignof(struct packed_one_mixed) == 1);
  assert(offsetof(struct packed_one_mixed, tail) == 8);
  assert(sizeof(struct packed_one_zero_width) == 6);
  assert(_Alignof(struct packed_one_zero_width) == 1);
  assert(offsetof(struct packed_one_zero_width, tail) == 5);
  assert(sizeof(struct packed_one_terminal) == 2);
  assert(_Alignof(struct packed_one_terminal) == 1);
  assert(sizeof(union packed_one_union) == 1);
  assert(_Alignof(union packed_one_union) == 1);

  assert(sizeof(struct packed_two_mixed) == 6);
  assert(_Alignof(struct packed_two_mixed) == 2);
  assert(offsetof(struct packed_two_mixed, tail) == 4);
  assert(sizeof(struct packed_two_crossing) == 8);
  assert(_Alignof(struct packed_two_crossing) == 2);
  assert(offsetof(struct packed_two_crossing, tail) == 6);
  assert(sizeof(struct packed_two_terminal) == 4);
  assert(_Alignof(struct packed_two_terminal) == 2);
  assert(sizeof(union packed_two_union) == 4);
  assert(_Alignof(union packed_two_union) == 2);
  return 0;
}

static int check_global_values(void) {
  assert(global_one_mixed.prefix == 11);
  assert(global_one_mixed.first == 777);
  assert(global_one_mixed.second == 0xabcde12345UL);
  assert(global_one_mixed.tail == 12);
  assert(global_one_zero.prefix == 13);
  assert(global_one_zero.first == 17);
  assert(global_one_zero.second == 45);
  assert(global_one_zero.tail == 14);
  assert(global_one_terminal.prefix == 15);
  assert(global_one_terminal.bits == 19);
  assert(global_one_union.bits == 21);

  assert(global_two_mixed.prefix == 16);
  assert(global_two_mixed.first == 0x101);
  assert(global_two_mixed.second == 0x4567);
  assert(global_two_mixed.tail == 17);
  assert(global_two_crossing.prefix == 18);
  assert(global_two_crossing.first == 0xabcdeUL);
  assert(global_two_crossing.second == 0x54321);
  assert(global_two_crossing.tail == 19);
  assert(global_two_terminal.prefix == 20);
  assert(global_two_terminal.bits == 0x34567UL);
  assert(global_two_union.bits == 0x45678UL);
  return 0;
}

static int check_automatic_values(void) {
  struct packed_one_mixed one_mixed = {
      .prefix = 21, .first = 779, .second = 0x8765432101UL, .tail = 22,
  };
  struct packed_one_terminal one_terminal = {
      .prefix = 23, .bits = 25,
  };
  struct packed_two_crossing two_crossing = {
      .prefix = 24, .first = 0x76543UL, .second = 0x23456, .tail = 25,
  };
  union packed_two_union two_union = {
      .bits = 0x56789UL,
  };

  assert(one_mixed.prefix == 21);
  assert(one_mixed.first == 779);
  assert(one_mixed.second == 0x8765432101UL);
  assert(one_mixed.tail == 22);
  assert(one_terminal.prefix == 23);
  assert(one_terminal.bits == 25);
  assert(two_crossing.prefix == 24);
  assert(two_crossing.first == 0x76543UL);
  assert(two_crossing.second == 0x23456);
  assert(two_crossing.tail == 25);
  assert(two_union.bits == 0x56789UL);
  return 0;
}

int main(void) {
  assert(check_layout() == 0);
  assert(check_global_values() == 0);
  assert(check_automatic_values() == 0);
  return 0;
}
