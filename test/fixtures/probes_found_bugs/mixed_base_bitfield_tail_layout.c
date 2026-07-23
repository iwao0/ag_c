#include <assert.h>
#include <stddef.h>

struct short_to_int {
  unsigned char prefix;
  unsigned short first : 3;
  unsigned int second : 7;
  unsigned char tail;
};

struct int_to_long {
  unsigned char prefix;
  unsigned int first : 10;
  unsigned long second : 40;
  unsigned char tail;
};

struct long_to_int {
  unsigned char prefix;
  unsigned long first : 20;
  unsigned int second : 10;
  unsigned char tail;
};

struct char_to_int {
  unsigned char prefix;
  unsigned char first : 3;
  unsigned int second : 7;
  unsigned char tail;
};

struct zero_width_mixed {
  unsigned char prefix;
  unsigned short first : 9;
  unsigned int : 0;
  unsigned long second : 17;
  unsigned char tail;
};

static struct short_to_int global_short_to_int = {
    .prefix = 11, .first = 5, .second = 99, .tail = 22,
};
static struct int_to_long global_int_to_long = {
    .prefix = 23, .first = 777, .second = 0xabcde12345UL, .tail = 24,
};
static struct long_to_int global_long_to_int = {
    .prefix = 25, .first = 0xabcdeUL, .second = 779, .tail = 26,
};
static struct char_to_int global_char_to_int = {
    .prefix = 27, .first = 6, .second = 101, .tail = 28,
};
static struct zero_width_mixed global_zero_width_mixed = {
    .prefix = 29, .first = 0x101, .second = 0x12345UL, .tail = 30,
};

static int check_layout(void) {
  assert(sizeof(struct short_to_int) == 4);
  assert(offsetof(struct short_to_int, tail) == 3);
  assert(sizeof(struct int_to_long) == 16);
  assert(offsetof(struct int_to_long, tail) == 8);
  assert(sizeof(struct long_to_int) == 8);
  assert(offsetof(struct long_to_int, tail) == 6);
  assert(sizeof(struct char_to_int) == 4);
  assert(offsetof(struct char_to_int, tail) == 3);
  assert(sizeof(struct zero_width_mixed) == 8);
  assert(offsetof(struct zero_width_mixed, tail) == 7);
  return 0;
}

static int check_global_values(void) {
  assert(global_short_to_int.prefix == 11);
  assert(global_short_to_int.first == 5);
  assert(global_short_to_int.second == 99);
  assert(global_short_to_int.tail == 22);

  assert(global_int_to_long.prefix == 23);
  assert(global_int_to_long.first == 777);
  assert(global_int_to_long.second == 0xabcde12345UL);
  assert(global_int_to_long.tail == 24);

  assert(global_long_to_int.prefix == 25);
  assert(global_long_to_int.first == 0xabcdeUL);
  assert(global_long_to_int.second == 779);
  assert(global_long_to_int.tail == 26);

  assert(global_char_to_int.prefix == 27);
  assert(global_char_to_int.first == 6);
  assert(global_char_to_int.second == 101);
  assert(global_char_to_int.tail == 28);

  assert(global_zero_width_mixed.prefix == 29);
  assert(global_zero_width_mixed.first == 0x101);
  assert(global_zero_width_mixed.second == 0x12345UL);
  assert(global_zero_width_mixed.tail == 30);
  return 0;
}

static int check_automatic_values(void) {
  struct short_to_int short_to_int_value = {
      .prefix = 31, .first = 3, .second = 103, .tail = 32,
  };
  struct int_to_long int_to_long_value = {
      .prefix = 33, .first = 781, .second = 0x8765432101UL, .tail = 34,
  };
  struct long_to_int long_to_int_value = {
      .prefix = 35, .first = 0x54321UL, .second = 783, .tail = 36,
  };
  struct char_to_int char_to_int_value = {
      .prefix = 37, .first = 4, .second = 105, .tail = 38,
  };
  struct zero_width_mixed zero_width_value = {
      .prefix = 39, .first = 0x103, .second = 0x12347UL, .tail = 40,
  };

  assert(short_to_int_value.prefix == 31);
  assert(short_to_int_value.first == 3);
  assert(short_to_int_value.second == 103);
  assert(short_to_int_value.tail == 32);

  assert(int_to_long_value.prefix == 33);
  assert(int_to_long_value.first == 781);
  assert(int_to_long_value.second == 0x8765432101UL);
  assert(int_to_long_value.tail == 34);

  assert(long_to_int_value.prefix == 35);
  assert(long_to_int_value.first == 0x54321UL);
  assert(long_to_int_value.second == 783);
  assert(long_to_int_value.tail == 36);

  assert(char_to_int_value.prefix == 37);
  assert(char_to_int_value.first == 4);
  assert(char_to_int_value.second == 105);
  assert(char_to_int_value.tail == 38);

  assert(zero_width_value.prefix == 39);
  assert(zero_width_value.first == 0x103);
  assert(zero_width_value.second == 0x12347UL);
  assert(zero_width_value.tail == 40);
  return 0;
}

static int check_static_local_and_compound_values(void) {
  static struct short_to_int static_value = {
      .prefix = 41, .first = 7, .second = 107, .tail = 42,
  };
  struct int_to_long *compound_value =
      &(struct int_to_long){
          .prefix = 43,
          .first = 785,
          .second = 0x7654321012UL,
          .tail = 44,
      };

  assert(static_value.prefix == 41);
  assert(static_value.first == 7);
  assert(static_value.second == 107);
  assert(static_value.tail == 42);
  assert(compound_value->prefix == 43);
  assert(compound_value->first == 785);
  assert(compound_value->second == 0x7654321012UL);
  assert(compound_value->tail == 44);
  return 0;
}

int main(void) {
  assert(check_layout() == 0);
  assert(check_global_values() == 0);
  assert(check_automatic_values() == 0);
  assert(check_static_local_and_compound_values() == 0);
  return 0;
}
