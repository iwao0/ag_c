#include <assert.h>

static char return_char(int value) {
  return value;
}

static signed char return_signed_char(int value) {
  return value;
}

static unsigned char return_unsigned_char(int value) {
  return value;
}

static short return_short(int value) {
  return value;
}

static unsigned short return_unsigned_short(int value) {
  return value;
}

static _Bool return_bool(int value) {
  return value;
}

typedef char (*char_return_fn)(int);
typedef signed char (*signed_char_return_fn)(int);
typedef unsigned char (*unsigned_char_return_fn)(int);
typedef short (*short_return_fn)(int);
typedef unsigned short (*unsigned_short_return_fn)(int);
typedef _Bool (*bool_return_fn)(int);

static int promote_signed_char(signed_char_return_fn function, int value) {
  return function(value);
}

static unsigned int promote_unsigned_char(
    unsigned_char_return_fn function, int value) {
  return function(value);
}

static int promote_short(short_return_fn function, int value) {
  return function(value);
}

static unsigned int promote_unsigned_short(
    unsigned_short_return_fn function, int value) {
  return function(value);
}

int main(void) {
  volatile int char_input = 200;
  volatile int signed_char_input = 200;
  volatile int unsigned_char_input = 511;
  volatile int short_input = 65535;
  volatile int unsigned_short_input = 70000;
  volatile int bool_input = -9;

  assert(return_char(char_input) == -56);
  assert(return_signed_char(signed_char_input) == -56);
  assert(return_unsigned_char(unsigned_char_input) == 255);
  assert(return_short(short_input) == -1);
  assert(return_unsigned_short(unsigned_short_input) == 4464);
  assert(return_bool(bool_input) == 1);

  char_return_fn char_function = return_char;
  signed_char_return_fn signed_char_function = return_signed_char;
  unsigned_char_return_fn unsigned_char_function = return_unsigned_char;
  short_return_fn short_function = return_short;
  unsigned_short_return_fn unsigned_short_function = return_unsigned_short;
  bool_return_fn bool_function = return_bool;

  assert(char_function(char_input) == -56);
  assert(promote_signed_char(signed_char_function, signed_char_input) == -56);
  assert(promote_unsigned_char(unsigned_char_function, unsigned_char_input) ==
         255u);
  assert(promote_short(short_function, short_input) == -1);
  assert(promote_unsigned_short(
             unsigned_short_function, unsigned_short_input) == 4464u);
  assert(bool_function(bool_input) == 1);

  return 0;
}
