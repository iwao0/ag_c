// Aggregate values containing nested array members must preserve every
// subobject across direct and indirect calls, returns, and variadic access.
// Expected: exit=0
#include <assert.h>
#include <stdarg.h>

struct byte_grid {
  unsigned char cells[2][3];
};

struct float_grid {
  float cells[2][2];
};

struct mixed_grid {
  long values[2][2];
  unsigned short tags[3];
  int (*operations[2])(int);
  const char *names[2];
};

union grid_choice {
  unsigned short cells[2][3];
  unsigned long long words[2];
};

typedef struct byte_grid byte_grid_callback_t(struct byte_grid, int);
typedef struct float_grid float_grid_callback_t(struct float_grid, float);
typedef struct mixed_grid mixed_grid_callback_t(struct mixed_grid, long);
typedef union grid_choice grid_choice_callback_t(union grid_choice,
                                                 unsigned short);

static int increment(int value) {
  return value + 1;
}

static int triple(int value) {
  return value * 3;
}

static int byte_grid_is(struct byte_grid value,
                        unsigned char base) {
  int row;
  int column;
  for (row = 0; row < 2; row++) {
    for (column = 0; column < 3; column++) {
      unsigned char expected =
          (unsigned char)(base + row * 3 + column);
      if (value.cells[row][column] != expected) {
        return 0;
      }
    }
  }
  return 1;
}

static int float_grid_is(struct float_grid value,
                         float base) {
  int row;
  int column;
  for (row = 0; row < 2; row++) {
    for (column = 0; column < 2; column++) {
      float expected = base + (float)(row * 2 + column);
      if (value.cells[row][column] != expected) {
        return 0;
      }
    }
  }
  return 1;
}

static int text_is(const char *value,
                   char first, char second) {
  return value[0] == first && value[1] == second &&
         value[2] == '\0';
}

static int mixed_grid_is(struct mixed_grid value,
                         long base,
                         unsigned short tag_base,
                         int first_result,
                         int second_result,
                         char first_name,
                         char second_name) {
  int row;
  int column;
  for (row = 0; row < 2; row++) {
    for (column = 0; column < 2; column++) {
      long expected = base + row * 2 + column;
      if (value.values[row][column] != expected) {
        return 0;
      }
    }
  }
  if (value.tags[0] != tag_base ||
      value.tags[1] != (unsigned short)(tag_base + 1) ||
      value.tags[2] != (unsigned short)(tag_base + 2)) {
    return 0;
  }
  if (value.operations[0](5) != first_result ||
      value.operations[1](5) != second_result) {
    return 0;
  }
  return text_is(value.names[0], first_name, '0') &&
         text_is(value.names[1], second_name, '1');
}

static int grid_choice_is(union grid_choice value,
                          unsigned short base) {
  int row;
  int column;
  for (row = 0; row < 2; row++) {
    for (column = 0; column < 3; column++) {
      unsigned short expected =
          (unsigned short)(base + row * 3 + column);
      if (value.cells[row][column] != expected) {
        return 0;
      }
    }
  }
  return 1;
}

static struct byte_grid make_byte_grid(unsigned char base) {
  struct byte_grid value;
  int row;
  int column;
  for (row = 0; row < 2; row++) {
    for (column = 0; column < 3; column++) {
      value.cells[row][column] =
          (unsigned char)(base + row * 3 + column);
    }
  }
  return value;
}

static struct float_grid make_float_grid(float base) {
  struct float_grid value;
  int row;
  int column;
  for (row = 0; row < 2; row++) {
    for (column = 0; column < 2; column++) {
      value.cells[row][column] =
          base + (float)(row * 2 + column);
    }
  }
  return value;
}

static struct mixed_grid make_mixed_grid(long base) {
  struct mixed_grid value;
  int row;
  int column;
  for (row = 0; row < 2; row++) {
    for (column = 0; column < 2; column++) {
      value.values[row][column] = base + row * 2 + column;
    }
  }
  value.tags[0] = (unsigned short)(base + 10);
  value.tags[1] = (unsigned short)(base + 11);
  value.tags[2] = (unsigned short)(base + 12);
  value.operations[0] = increment;
  value.operations[1] = triple;
  value.names[0] = "A0";
  value.names[1] = "B1";
  return value;
}

static union grid_choice make_grid_choice(
    unsigned short base) {
  union grid_choice value;
  int row;
  int column;
  for (row = 0; row < 2; row++) {
    for (column = 0; column < 3; column++) {
      value.cells[row][column] =
          (unsigned short)(base + row * 3 + column);
    }
  }
  return value;
}

static struct byte_grid shift_byte_grid(
    struct byte_grid value, int amount) {
  int row;
  int column;
  for (row = 0; row < 2; row++) {
    for (column = 0; column < 3; column++) {
      value.cells[row][column] =
          (unsigned char)(value.cells[row][column] + amount);
    }
  }
  return value;
}

static struct float_grid shift_float_grid(
    struct float_grid value, float amount) {
  int row;
  int column;
  for (row = 0; row < 2; row++) {
    for (column = 0; column < 2; column++) {
      value.cells[row][column] += amount;
    }
  }
  return value;
}

static struct mixed_grid shift_mixed_grid(
    struct mixed_grid value, long amount) {
  int row;
  int column;
  for (row = 0; row < 2; row++) {
    for (column = 0; column < 2; column++) {
      value.values[row][column] += amount;
    }
  }
  value.tags[0] = (unsigned short)(value.tags[0] + amount);
  value.tags[1] = (unsigned short)(value.tags[1] + amount);
  value.tags[2] = (unsigned short)(value.tags[2] + amount);
  return value;
}

static union grid_choice shift_grid_choice(
    union grid_choice value, unsigned short amount) {
  int row;
  int column;
  for (row = 0; row < 2; row++) {
    for (column = 0; column < 3; column++) {
      value.cells[row][column] =
          (unsigned short)(value.cells[row][column] + amount);
    }
  }
  return value;
}

static int check_variadic_grids(int marker, ...) {
  va_list arguments;
  struct byte_grid bytes;
  struct float_grid floats;
  struct mixed_grid mixed;
  union grid_choice choice;
  va_start(arguments, marker);
  bytes = va_arg(arguments, struct byte_grid);
  floats = va_arg(arguments, struct float_grid);
  mixed = va_arg(arguments, struct mixed_grid);
  choice = va_arg(arguments, union grid_choice);
  va_end(arguments);
  return marker == 91 &&
         byte_grid_is(bytes, 10) &&
         float_grid_is(floats, 20.5f) &&
         mixed_grid_is(mixed, 30, 40, 6, 15, 'A', 'B') &&
         grid_choice_is(choice, 50);
}

static struct byte_grid byte_sources[2];
static struct float_grid float_sources[2];
static struct mixed_grid mixed_sources[2];
static union grid_choice choice_sources[2];
static int byte_selections;
static int float_selections;
static int mixed_selections;
static int choice_selections;

static struct byte_grid *select_byte_grid(int index) {
  byte_selections++;
  return &byte_sources[index];
}

static struct float_grid *select_float_grid(int index) {
  float_selections++;
  return &float_sources[index];
}

static struct mixed_grid *select_mixed_grid(int index) {
  mixed_selections++;
  return &mixed_sources[index];
}

static union grid_choice *select_grid_choice(int index) {
  choice_selections++;
  return &choice_sources[index];
}

int main(void) {
  byte_grid_callback_t *byte_callback = shift_byte_grid;
  float_grid_callback_t *float_callback = shift_float_grid;
  mixed_grid_callback_t *mixed_callback = shift_mixed_grid;
  grid_choice_callback_t *choice_callback = shift_grid_choice;
  struct byte_grid bytes = make_byte_grid(1);
  struct float_grid floats = make_float_grid(2.5f);
  struct {
    unsigned long before;
    struct mixed_grid value;
    unsigned long after;
  } guarded_mixed = {
      .before = 0x11223344UL,
      .value = {0},
      .after = 0x55667788UL,
  };
  struct {
    unsigned long before;
    union grid_choice value;
    unsigned long after;
  } guarded_choice = {
      .before = 0x99aabbccUL,
      .value = {0},
      .after = 0xddeeff00UL,
  };

  assert(byte_grid_is(bytes, 1));
  bytes = byte_callback(bytes, 3);
  assert(byte_grid_is(bytes, 4));
  assert(float_grid_is(floats, 2.5f));
  floats = float_callback(floats, 4.0f);
  assert(float_grid_is(floats, 6.5f));

  guarded_mixed.value = mixed_callback(make_mixed_grid(7), 5);
  assert(guarded_mixed.before == 0x11223344UL);
  assert(guarded_mixed.after == 0x55667788UL);
  assert(mixed_grid_is(guarded_mixed.value,
                       12, 22, 6, 15, 'A', 'B'));

  guarded_choice.value = choice_callback(make_grid_choice(13), 7);
  assert(guarded_choice.before == 0x99aabbccUL);
  assert(guarded_choice.after == 0xddeeff00UL);
  assert(grid_choice_is(guarded_choice.value, 20));

  byte_sources[0] = make_byte_grid(30);
  byte_sources[1] = make_byte_grid(40);
  float_sources[0] = make_float_grid(50.5f);
  float_sources[1] = make_float_grid(60.5f);
  mixed_sources[0] = make_mixed_grid(70);
  mixed_sources[1] = make_mixed_grid(80);
  choice_sources[0] = make_grid_choice(90);
  choice_sources[1] = make_grid_choice(100);

  assert(byte_grid_is(
      1 ? *select_byte_grid(0) : *select_byte_grid(1), 30));
  assert(byte_selections == 1);
  assert(float_grid_is(
      0 ? *select_float_grid(0) : *select_float_grid(1), 60.5f));
  assert(float_selections == 1);
  assert(mixed_grid_is(
      1 ? *select_mixed_grid(0) : *select_mixed_grid(1),
      70, 80, 6, 15, 'A', 'B'));
  assert(mixed_selections == 1);
  assert(grid_choice_is(
      0 ? *select_grid_choice(0) : *select_grid_choice(1), 100));
  assert(choice_selections == 1);

  assert(byte_grid_is(
      (byte_selections++, byte_sources[1]), 40));
  assert(byte_selections == 2);
  assert(float_grid_is(
      (float_selections++, float_sources[0]), 50.5f));
  assert(float_selections == 2);
  assert(mixed_grid_is(
      (mixed_selections++, mixed_sources[1]),
      80, 90, 6, 15, 'A', 'B'));
  assert(mixed_selections == 2);
  assert(grid_choice_is(
      (choice_selections++, choice_sources[0]), 90));
  assert(choice_selections == 2);

  bytes = make_byte_grid(110);
  assert(byte_grid_is((bytes = make_byte_grid(120)), 120));
  floats = make_float_grid(130.5f);
  assert(float_grid_is((floats = make_float_grid(140.5f)),
                       140.5f));
  guarded_mixed.value = make_mixed_grid(150);
  assert(mixed_grid_is(
      (guarded_mixed.value = make_mixed_grid(160)),
      160, 170, 6, 15, 'A', 'B'));
  guarded_choice.value = make_grid_choice(170);
  assert(grid_choice_is(
      (guarded_choice.value = make_grid_choice(180)), 180));

  assert(check_variadic_grids(
      91, make_byte_grid(10), make_float_grid(20.5f),
      make_mixed_grid(30), make_grid_choice(50)));
  return 0;
}
