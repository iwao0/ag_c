// Pointer-bearing aggregate values must follow the target pointer width while
// preserving object, function, pointer-to-array, and multilevel pointer shape.
// Expected: exit=0
#include <assert.h>
#include <stdarg.h>

typedef int callback_t(int);

struct pointer_pair {
  int *object;
  callback_t *callback;
};

struct pointer_shapes {
  int *objects[2];
  int **object_link;
  callback_t *callbacks[2];
  callback_t **callback_link;
  int (*row)[3];
  int (*rows[2])[3];
  const char *text;
  void *opaque;
  long tail;
};

union pointer_choice {
  struct pointer_pair pair;
  void *slots[2];
};

typedef struct pointer_pair pointer_pair_callback_t(
    struct pointer_pair, int);
typedef struct pointer_shapes pointer_shapes_callback_t(
    struct pointer_shapes, int);
typedef union pointer_choice pointer_choice_callback_t(
    union pointer_choice, int);

static int object_values[4] = {11, 22, 33, 44};
static int matrix[2][3] = {
    {101, 102, 103},
    {201, 202, 203},
};

static int increment(int value) {
  return value + 1;
}

static int triple(int value) {
  return value * 3;
}

static int *object_links[2] = {
    &object_values[0],
    &object_values[2],
};
static callback_t *callback_links[2] = {
    increment,
    triple,
};
static const char *texts[2] = {
    "left",
    "right",
};

static struct pointer_pair make_pair(int seed) {
  struct pointer_pair value = {
      .object = &object_values[seed & 3],
      .callback = (seed & 1) ? triple : increment,
  };
  return value;
}

static int pair_is(struct pointer_pair value,
                   int seed) {
  int argument = 5;
  int expected_callback =
      (seed & 1) ? argument * 3 : argument + 1;
  return value.object == &object_values[seed & 3] &&
         *value.object == object_values[seed & 3] &&
         value.callback(argument) == expected_callback;
}

static struct pointer_shapes make_shapes(int seed) {
  int parity = seed & 1;
  struct pointer_shapes value = {
      .objects = {
          &object_values[seed & 3],
          &object_values[(seed + 1) & 3],
      },
      .object_link = &object_links[parity],
      .callbacks = {increment, triple},
      .callback_link = &callback_links[parity],
      .row = &matrix[parity],
      .rows = {&matrix[0], &matrix[1]},
      .text = texts[parity],
      .opaque = &object_values[(seed + 2) & 3],
      .tail = 1000 + seed,
  };
  return value;
}

static int shapes_is(struct pointer_shapes value,
                     int seed) {
  int parity = seed & 1;
  int linked_index = parity ? 2 : 0;
  int linked_callback = parity ? 15 : 6;
  return value.objects[0] == &object_values[seed & 3] &&
         value.objects[1] == &object_values[(seed + 1) & 3] &&
         *value.objects[0] == object_values[seed & 3] &&
         *value.objects[1] == object_values[(seed + 1) & 3] &&
         value.object_link == &object_links[parity] &&
         **value.object_link == object_values[linked_index] &&
         value.callbacks[0](5) == 6 &&
         value.callbacks[1](5) == 15 &&
         value.callback_link == &callback_links[parity] &&
         (**value.callback_link)(5) == linked_callback &&
         value.row == &matrix[parity] &&
         (*value.row)[0] == matrix[parity][0] &&
         (*value.row)[2] == matrix[parity][2] &&
         value.rows[0] == &matrix[0] &&
         value.rows[1] == &matrix[1] &&
         (*value.rows[0])[1] == 102 &&
         (*value.rows[1])[1] == 202 &&
         value.text == texts[parity] &&
         value.text[0] == (parity ? 'r' : 'l') &&
         value.opaque == &object_values[(seed + 2) & 3] &&
         value.tail == 1000 + seed;
}

static union pointer_choice make_choice(int seed) {
  union pointer_choice value = {
      .pair = {
          .object = &object_values[seed & 3],
          .callback = (seed & 1) ? triple : increment,
      },
  };
  return value;
}

static int choice_is(union pointer_choice value,
                     int seed) {
  return pair_is(value.pair, seed);
}

static struct pointer_pair roundtrip_pair(
    struct pointer_pair value, int marker) {
  assert(marker == 101);
  return value;
}

static struct pointer_shapes roundtrip_shapes(
    struct pointer_shapes value, int marker) {
  assert(marker == 102);
  return value;
}

static union pointer_choice roundtrip_choice(
    union pointer_choice value, int marker) {
  assert(marker == 103);
  return value;
}

static int check_variadic_pointer_values(int marker, ...) {
  va_list arguments;
  va_start(arguments, marker);
  struct pointer_pair pair =
      va_arg(arguments, struct pointer_pair);
  struct pointer_shapes shapes =
      va_arg(arguments, struct pointer_shapes);
  union pointer_choice choice =
      va_arg(arguments, union pointer_choice);
  va_end(arguments);
  return marker == 104 &&
         pair_is(pair, 4) &&
         shapes_is(shapes, 5) &&
         choice_is(choice, 6);
}

static struct pointer_pair pair_sources[2];
static struct pointer_shapes shape_sources[2];
static union pointer_choice choice_sources[2];
static int pair_selections;
static int shape_selections;
static int choice_selections;

static struct pointer_pair *select_pair(int index) {
  pair_selections++;
  return &pair_sources[index];
}

static struct pointer_shapes *select_shapes(int index) {
  shape_selections++;
  return &shape_sources[index];
}

static union pointer_choice *select_choice(int index) {
  choice_selections++;
  return &choice_sources[index];
}

int main(void) {
  pointer_pair_callback_t *pair_callback = roundtrip_pair;
  pointer_shapes_callback_t *shapes_callback =
      roundtrip_shapes;
  pointer_choice_callback_t *choice_callback =
      roundtrip_choice;
  struct pointer_pair pair =
      pair_callback(make_pair(1), 101);
  struct {
    unsigned long before;
    struct pointer_shapes value;
    unsigned long after;
  } guarded_shapes = {
      .before = 0x11223344UL,
      .value = {0},
      .after = 0x55667788UL,
  };
  struct {
    unsigned long before;
    union pointer_choice value;
    unsigned long after;
  } guarded_choice = {
      .before = 0x99aabbccUL,
      .value = {0},
      .after = 0xddeeff00UL,
  };

  assert(pair_is(pair, 1));
  guarded_shapes.value =
      shapes_callback(make_shapes(2), 102);
  assert(guarded_shapes.before == 0x11223344UL);
  assert(guarded_shapes.after == 0x55667788UL);
  assert(shapes_is(guarded_shapes.value, 2));
  guarded_choice.value =
      choice_callback(make_choice(3), 103);
  assert(guarded_choice.before == 0x99aabbccUL);
  assert(guarded_choice.after == 0xddeeff00UL);
  assert(choice_is(guarded_choice.value, 3));

  assert(check_variadic_pointer_values(
      104, make_pair(4), make_shapes(5), make_choice(6)));

  pair_sources[0] = make_pair(7);
  pair_sources[1] = make_pair(8);
  shape_sources[0] = make_shapes(9);
  shape_sources[1] = make_shapes(10);
  choice_sources[0] = make_choice(11);
  choice_sources[1] = make_choice(12);

  assert(pair_is(
      1 ? *select_pair(0) : *select_pair(1), 7));
  assert(pair_selections == 1);
  assert(shapes_is(
      0 ? *select_shapes(0) : *select_shapes(1), 10));
  assert(shape_selections == 1);
  assert(choice_is(
      1 ? *select_choice(0) : *select_choice(1), 11));
  assert(choice_selections == 1);

  assert(pair_is(
      (pair_selections++, pair_sources[1]), 8));
  assert(pair_selections == 2);
  assert(shapes_is(
      (shape_selections++, shape_sources[0]), 9));
  assert(shape_selections == 2);
  assert(choice_is(
      (choice_selections++, choice_sources[1]), 12));
  assert(choice_selections == 2);

  pair = make_pair(13);
  assert(pair_is((pair = make_pair(14)), 14));
  guarded_shapes.value = make_shapes(15);
  assert(shapes_is(
      (guarded_shapes.value = make_shapes(16)), 16));
  guarded_choice.value = make_choice(17);
  assert(choice_is(
      (guarded_choice.value = make_choice(18)), 18));
  assert(guarded_shapes.before == 0x11223344UL);
  assert(guarded_shapes.after == 0x55667788UL);
  assert(guarded_choice.before == 0x99aabbccUL);
  assert(guarded_choice.after == 0xddeeff00UL);
  return 0;
}
