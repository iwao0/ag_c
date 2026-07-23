#include <assert.h>

struct pair {
  int first;
  int second;
};

union pair_first {
  struct pair pair;
  int scalar;
};

union scalar_first {
  int scalar;
  struct pair pair;
};

struct holder {
  union pair_first value;
  int tail;
};

union text_first {
  char text[2];
  char larger[4];
};

struct text_row {
  char text[2];
  int value;
};

union bitfields_first {
  struct {
    unsigned int first : 3;
    unsigned int second : 3;
    unsigned int third : 3;
  } fields;
  long long wide;
};

union scalar_or_text {
  int scalar;
  char text[2];
};

union float_inner {
  struct {
    float first;
    float second;
  } pair;
  int scalar;
};

union float_outer {
  union float_inner value;
  double number;
};

static union pair_first global_designated = {
    .pair.first = 1,
    2,
};

static union pair_first global_elided = {
    3,
    4,
};

static union scalar_first global_scalar_array[] = {
    5,
    6,
};

static union scalar_first global_designated_array[] = {
    [1].pair.first = 7,
    8,
    9,
};

static struct holder global_nested = {
    .value.pair.first = 10,
    11,
    12,
};

static union text_first global_texts[] = {
    "A",
    "B",
};

static struct text_row global_rows[] = {
    "C",
    40,
    "D",
    41,
};

static union bitfields_first global_bitfields[] = {
    1, 2, 3,
    4, 5, 6,
};

static union scalar_or_text global_designated_texts[] = {
    [1].text = "O",
    44,
};

static union float_outer global_nested_float = {
    .value.pair.first = 1.5f,
    2.5f,
};

static int check_global_objects(void) {
  assert(global_designated.pair.first == 1);
  assert(global_designated.pair.second == 2);
  assert(global_elided.pair.first == 3);
  assert(global_elided.pair.second == 4);
  assert(sizeof(global_scalar_array) /
             sizeof(global_scalar_array[0]) ==
         2);
  assert(global_scalar_array[0].scalar == 5);
  assert(global_scalar_array[1].scalar == 6);
  assert(sizeof(global_designated_array) /
             sizeof(global_designated_array[0]) ==
         3);
  assert(global_designated_array[1].pair.first == 7);
  assert(global_designated_array[1].pair.second == 8);
  assert(global_designated_array[2].scalar == 9);
  assert(global_nested.value.pair.first == 10);
  assert(global_nested.value.pair.second == 11);
  assert(global_nested.tail == 12);
  assert(sizeof(global_texts) / sizeof(global_texts[0]) == 2);
  assert(global_texts[0].text[0] == 'A');
  assert(global_texts[0].text[1] == '\0');
  assert(global_texts[1].text[0] == 'B');
  assert(global_texts[1].text[1] == '\0');
  assert(sizeof(global_rows) / sizeof(global_rows[0]) == 2);
  assert(global_rows[0].text[0] == 'C');
  assert(global_rows[0].text[1] == '\0');
  assert(global_rows[0].value == 40);
  assert(global_rows[1].text[0] == 'D');
  assert(global_rows[1].text[1] == '\0');
  assert(global_rows[1].value == 41);
  assert(sizeof(global_bitfields) /
             sizeof(global_bitfields[0]) ==
         2);
  assert(global_bitfields[0].fields.first == 1);
  assert(global_bitfields[0].fields.second == 2);
  assert(global_bitfields[0].fields.third == 3);
  assert(global_bitfields[1].fields.first == 4);
  assert(global_bitfields[1].fields.second == 5);
  assert(global_bitfields[1].fields.third == 6);
  assert(sizeof(global_designated_texts) /
             sizeof(global_designated_texts[0]) ==
         3);
  assert(global_designated_texts[1].text[0] == 'O');
  assert(global_designated_texts[1].text[1] == '\0');
  assert(global_designated_texts[2].scalar == 44);
  assert(global_nested_float.value.pair.first == 1.5f);
  assert(global_nested_float.value.pair.second == 2.5f);
  return 0;
}

static int check_static_local_objects(void) {
  static union pair_first designated = {
      .pair.first = 13,
      14,
  };
  static union pair_first elided = {
      15,
      16,
  };
  static union scalar_first scalar_array[] = {
      17,
      18,
  };
  static union scalar_first designated_array[] = {
      [1].pair.first = 19,
      20,
      21,
  };
  static union text_first texts[] = {
      "E",
      "F",
  };
  static union bitfields_first bitfields[] = {
      2, 3, 4,
      5, 6, 7,
  };

  assert(designated.pair.first == 13);
  assert(designated.pair.second == 14);
  assert(elided.pair.first == 15);
  assert(elided.pair.second == 16);
  assert(sizeof(scalar_array) / sizeof(scalar_array[0]) == 2);
  assert(scalar_array[0].scalar == 17);
  assert(scalar_array[1].scalar == 18);
  assert(sizeof(designated_array) /
             sizeof(designated_array[0]) ==
         3);
  assert(designated_array[1].pair.first == 19);
  assert(designated_array[1].pair.second == 20);
  assert(designated_array[2].scalar == 21);
  assert(sizeof(texts) / sizeof(texts[0]) == 2);
  assert(texts[0].text[0] == 'E');
  assert(texts[0].text[1] == '\0');
  assert(texts[1].text[0] == 'F');
  assert(texts[1].text[1] == '\0');
  assert(sizeof(bitfields) / sizeof(bitfields[0]) == 2);
  assert(bitfields[0].fields.first == 2);
  assert(bitfields[0].fields.second == 3);
  assert(bitfields[0].fields.third == 4);
  assert(bitfields[1].fields.first == 5);
  assert(bitfields[1].fields.second == 6);
  assert(bitfields[1].fields.third == 7);
  return 0;
}

static int check_automatic_objects(void) {
  union pair_first first_copy = {.pair = {45, 46}};
  union pair_first second_copy = {.pair = {47, 48}};
  union pair_first copies[] = {first_copy, second_copy};
  union pair_first designated = {
      .pair.first = 22,
      23,
  };
  union pair_first elided = {
      24,
      25,
  };
  union scalar_first scalar_array[] = {
      26,
      27,
  };
  union scalar_first designated_array[] = {
      [1].pair.first = 28,
      29,
      30,
  };
  union text_first texts[] = {
      "G",
      "H",
  };
  struct text_row rows[] = {
      "I",
      42,
      "J",
      43,
  };
  union bitfields_first bitfields[] = {
      7, 6, 5,
      4, 3, 2,
  };

  assert(designated.pair.first == 22);
  assert(designated.pair.second == 23);
  assert(elided.pair.first == 24);
  assert(elided.pair.second == 25);
  assert(sizeof(scalar_array) / sizeof(scalar_array[0]) == 2);
  assert(scalar_array[0].scalar == 26);
  assert(scalar_array[1].scalar == 27);
  assert(sizeof(designated_array) /
             sizeof(designated_array[0]) ==
         3);
  assert(designated_array[1].pair.first == 28);
  assert(designated_array[1].pair.second == 29);
  assert(designated_array[2].scalar == 30);
  assert(sizeof(texts) / sizeof(texts[0]) == 2);
  assert(texts[0].text[0] == 'G');
  assert(texts[1].text[0] == 'H');
  assert(sizeof(rows) / sizeof(rows[0]) == 2);
  assert(rows[0].text[0] == 'I');
  assert(rows[0].value == 42);
  assert(rows[1].text[0] == 'J');
  assert(rows[1].value == 43);
  assert(sizeof(bitfields) / sizeof(bitfields[0]) == 2);
  assert(bitfields[0].fields.first == 7);
  assert(bitfields[0].fields.second == 6);
  assert(bitfields[0].fields.third == 5);
  assert(bitfields[1].fields.first == 4);
  assert(bitfields[1].fields.second == 3);
  assert(bitfields[1].fields.third == 2);
  assert(sizeof(copies) / sizeof(copies[0]) == 2);
  assert(copies[0].pair.first == 45);
  assert(copies[0].pair.second == 46);
  assert(copies[1].pair.first == 47);
  assert(copies[1].pair.second == 48);
  return 0;
}

static int check_compound_literals(void) {
  union pair_first *designated = &(union pair_first){
      .pair.first = 31,
      32,
  };
  union pair_first *elided = &(union pair_first){
      33,
      34,
  };
  union scalar_first *scalar_array = (union scalar_first[]){
      35,
      36,
  };
  union scalar_first *designated_array = (union scalar_first[]){
      [1].pair.first = 37,
      38,
      39,
  };
  union text_first *texts = (union text_first[]){
      "K",
      "L",
  };
  union bitfields_first *bitfields = (union bitfields_first[]){
      1, 3, 5,
      2, 4, 6,
  };

  assert(designated->pair.first == 31);
  assert(designated->pair.second == 32);
  assert(elided->pair.first == 33);
  assert(elided->pair.second == 34);
  assert(sizeof((union scalar_first[]){40, 41}) /
             sizeof(union scalar_first) ==
         2);
  assert(scalar_array[0].scalar == 35);
  assert(scalar_array[1].scalar == 36);
  assert(sizeof((union scalar_first[]){
                    [1].pair.first = 1, 2, 3}) /
             sizeof(union scalar_first) ==
         3);
  assert(designated_array[1].pair.first == 37);
  assert(designated_array[1].pair.second == 38);
  assert(designated_array[2].scalar == 39);
  assert(sizeof((union text_first[]){"M", "N"}) /
             sizeof(union text_first) ==
         2);
  assert(texts[0].text[0] == 'K');
  assert(texts[0].text[1] == '\0');
  assert(texts[1].text[0] == 'L');
  assert(texts[1].text[1] == '\0');
  assert(sizeof((union bitfields_first[]){1, 2, 3, 4, 5, 6}) /
             sizeof(union bitfields_first) ==
         2);
  assert(bitfields[0].fields.first == 1);
  assert(bitfields[0].fields.second == 3);
  assert(bitfields[0].fields.third == 5);
  assert(bitfields[1].fields.first == 2);
  assert(bitfields[1].fields.second == 4);
  assert(bitfields[1].fields.third == 6);
  return 0;
}

int main(void) {
  assert(check_global_objects() == 0);
  assert(check_static_local_objects() == 0);
  assert(check_automatic_objects() == 0);
  assert(check_compound_literals() == 0);
  return 0;
}
