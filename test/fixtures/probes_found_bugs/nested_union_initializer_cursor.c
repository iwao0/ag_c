#include <assert.h>

struct cursor_pair {
  int first;
  int second;
};

union cursor_value {
  struct cursor_pair pair;
  int scalar;
};

struct cursor_holder {
  union cursor_value items[3];
  int tail;
};

union cursor_matrix {
  int cells[2][2];
  long long wide;
};

union anonymous_pair_value {
  struct {
    int first;
    int second;
  };
  long long wide;
};

union short_anonymous_value {
  struct {
    short first;
    short second;
  };
  long long wide;
};

union text_anonymous_value {
  struct {
    char text[3];
    int value;
  };
  long long wide;
};

union bitfield_anonymous_value {
  struct {
    unsigned int first : 3;
    unsigned int second : 3;
    unsigned int third : 3;
  };
  long long wide;
};

struct anonymous_cursor_holder {
  union {
    struct {
      int first;
      int second;
    };
    long long wide;
  };
  int tail;
};

struct deep_anonymous_cursor_holder {
  union {
    struct {
      union {
        struct {
          int first;
          int second;
        };
        long long wide;
      };
    };
    double alternate;
  };
  int tail;
};

static struct cursor_holder global_scalar_boundary = {
    .items[0].pair.first = 1,
    2,
    .items[1].scalar = 3,
    4,
    5,
    6,
};

static struct cursor_holder global_pair_boundary = {
    .items[0].pair.second = 7,
    8,
    9,
    10,
    11,
    12,
};

static union cursor_matrix global_matrix = {
    .cells[0][1] = 13,
    14,
    15,
};

static union cursor_matrix global_matrix_array[] = {
    [1].cells[1][1] = 16,
    17,
    18,
    19,
    20,
};

static struct anonymous_cursor_holder global_anonymous = {
    .first = 21,
    22,
    23,
};

static union anonymous_pair_value global_anonymous_array[] = {
    [1].first = 55,
    56,
    57,
    58,
};

static union short_anonymous_value global_short_anonymous = {
    61,
    62,
};

static union short_anonymous_value global_short_anonymous_array[] = {
    [1].second = 63,
    64,
    65,
};

static union text_anonymous_value global_text_anonymous = {
    .text = "Q",
    66,
};

static union bitfield_anonymous_value global_bitfield_anonymous = {
    1,
    2,
    3,
};

static struct deep_anonymous_cursor_holder global_deep_anonymous = {
    .first = 67,
    68,
    69,
};

static struct anonymous_cursor_holder global_promoted_switch = {
    .wide = -1,
    .second = 81,
    82,
};

static struct anonymous_cursor_holder global_promoted_repeat = {
    .first = 83,
    .wide = -1,
    .first = 84,
    85,
    86,
};

static union anonymous_pair_value global_root_switch = {
    .wide = -1,
    .second = 87,
};

static union anonymous_pair_value global_array_switch[] = {
    [1].wide = -1,
    [1].first = 88,
    89,
    90,
    91,
};

static int check_global_objects(void) {
  assert(global_scalar_boundary.items[0].pair.first == 1);
  assert(global_scalar_boundary.items[0].pair.second == 2);
  assert(global_scalar_boundary.items[1].scalar == 3);
  assert(global_scalar_boundary.items[2].pair.first == 4);
  assert(global_scalar_boundary.items[2].pair.second == 5);
  assert(global_scalar_boundary.tail == 6);

  assert(global_pair_boundary.items[0].pair.first == 0);
  assert(global_pair_boundary.items[0].pair.second == 7);
  assert(global_pair_boundary.items[1].pair.first == 8);
  assert(global_pair_boundary.items[1].pair.second == 9);
  assert(global_pair_boundary.items[2].pair.first == 10);
  assert(global_pair_boundary.items[2].pair.second == 11);
  assert(global_pair_boundary.tail == 12);

  assert(global_matrix.cells[0][0] == 0);
  assert(global_matrix.cells[0][1] == 13);
  assert(global_matrix.cells[1][0] == 14);
  assert(global_matrix.cells[1][1] == 15);

  assert(sizeof(global_matrix_array) /
             sizeof(global_matrix_array[0]) ==
         3);
  assert(global_matrix_array[0].cells[0][0] == 0);
  assert(global_matrix_array[1].cells[0][0] == 0);
  assert(global_matrix_array[1].cells[1][1] == 16);
  assert(global_matrix_array[2].cells[0][0] == 17);
  assert(global_matrix_array[2].cells[0][1] == 18);
  assert(global_matrix_array[2].cells[1][0] == 19);
  assert(global_matrix_array[2].cells[1][1] == 20);

  assert(global_anonymous.first == 21);
  assert(global_anonymous.second == 22);
  assert(global_anonymous.tail == 23);
  assert(sizeof(global_anonymous_array) /
             sizeof(global_anonymous_array[0]) ==
         3);
  assert(global_anonymous_array[0].first == 0);
  assert(global_anonymous_array[1].first == 55);
  assert(global_anonymous_array[1].second == 56);
  assert(global_anonymous_array[2].first == 57);
  assert(global_anonymous_array[2].second == 58);
  assert(global_short_anonymous.first == 61);
  assert(global_short_anonymous.second == 62);
  assert(sizeof(global_short_anonymous_array) /
             sizeof(global_short_anonymous_array[0]) ==
         3);
  assert(global_short_anonymous_array[0].first == 0);
  assert(global_short_anonymous_array[1].first == 0);
  assert(global_short_anonymous_array[1].second == 63);
  assert(global_short_anonymous_array[2].first == 64);
  assert(global_short_anonymous_array[2].second == 65);
  assert(global_text_anonymous.text[0] == 'Q');
  assert(global_text_anonymous.text[1] == '\0');
  assert(global_text_anonymous.value == 66);
  assert(global_bitfield_anonymous.first == 1);
  assert(global_bitfield_anonymous.second == 2);
  assert(global_bitfield_anonymous.third == 3);
  assert(global_deep_anonymous.first == 67);
  assert(global_deep_anonymous.second == 68);
  assert(global_deep_anonymous.tail == 69);
  assert(global_promoted_switch.first == 0);
  assert(global_promoted_switch.second == 81);
  assert(global_promoted_switch.tail == 82);
  assert(global_promoted_repeat.first == 84);
  assert(global_promoted_repeat.second == 85);
  assert(global_promoted_repeat.tail == 86);
  assert(global_root_switch.first == 0);
  assert(global_root_switch.second == 87);
  assert(sizeof(global_array_switch) /
             sizeof(global_array_switch[0]) ==
         3);
  assert(global_array_switch[0].first == 0);
  assert(global_array_switch[1].first == 88);
  assert(global_array_switch[1].second == 89);
  assert(global_array_switch[2].first == 90);
  assert(global_array_switch[2].second == 91);
  return 0;
}

static int check_static_local_objects(void) {
  static struct cursor_holder holder = {
      .items[1].pair.second = 24,
      25,
      26,
      27,
  };
  static union cursor_matrix matrices[] = {
      [1].cells[0][1] = 28,
      29,
      30,
  };
  static struct anonymous_cursor_holder anonymous = {
      .second = 31,
      32,
  };
  static union text_anonymous_value text = {
      .text = "R",
      70,
  };
  static struct deep_anonymous_cursor_holder deep = {
      .second = 71,
      72,
  };
  static struct anonymous_cursor_holder promoted_switch = {
      .wide = -1,
      .second = 92,
      93,
  };

  assert(holder.items[0].pair.first == 0);
  assert(holder.items[1].pair.first == 0);
  assert(holder.items[1].pair.second == 24);
  assert(holder.items[2].pair.first == 25);
  assert(holder.items[2].pair.second == 26);
  assert(holder.tail == 27);

  assert(sizeof(matrices) / sizeof(matrices[0]) == 2);
  assert(matrices[1].cells[0][0] == 0);
  assert(matrices[1].cells[0][1] == 28);
  assert(matrices[1].cells[1][0] == 29);
  assert(matrices[1].cells[1][1] == 30);

  assert(anonymous.first == 0);
  assert(anonymous.second == 31);
  assert(anonymous.tail == 32);
  assert(text.text[0] == 'R');
  assert(text.text[1] == '\0');
  assert(text.value == 70);
  assert(deep.first == 0);
  assert(deep.second == 71);
  assert(deep.tail == 72);
  assert(promoted_switch.first == 0);
  assert(promoted_switch.second == 92);
  assert(promoted_switch.tail == 93);
  return 0;
}

static int check_automatic_objects(void) {
  struct cursor_holder holder = {
      .items[0].scalar = 33,
      34,
      35,
      36,
      37,
  };
  union cursor_matrix matrices[] = {
      [1].cells[1][0] = 38,
      39,
      40,
      41,
      42,
  };
  struct anonymous_cursor_holder anonymous = {
      .first = 43,
      44,
      45,
  };
  union short_anonymous_value short_value = {
      73,
      74,
  };
  union bitfield_anonymous_value bitfields = {
      4,
      5,
      6,
  };
  struct deep_anonymous_cursor_holder deep = {
      .first = 75,
      76,
      77,
  };
  struct anonymous_cursor_holder promoted_switch = {
      .wide = -1,
      .first = 94,
      95,
      96,
  };

  assert(holder.items[0].scalar == 33);
  assert(holder.items[1].pair.first == 34);
  assert(holder.items[1].pair.second == 35);
  assert(holder.items[2].pair.first == 36);
  assert(holder.items[2].pair.second == 37);
  assert(holder.tail == 0);

  assert(sizeof(matrices) / sizeof(matrices[0]) == 3);
  assert(matrices[1].cells[1][0] == 38);
  assert(matrices[1].cells[1][1] == 39);
  assert(matrices[2].cells[0][0] == 40);
  assert(matrices[2].cells[0][1] == 41);
  assert(matrices[2].cells[1][0] == 42);
  assert(matrices[2].cells[1][1] == 0);

  assert(anonymous.first == 43);
  assert(anonymous.second == 44);
  assert(anonymous.tail == 45);
  assert(short_value.first == 73);
  assert(short_value.second == 74);
  assert(bitfields.first == 4);
  assert(bitfields.second == 5);
  assert(bitfields.third == 6);
  assert(deep.first == 75);
  assert(deep.second == 76);
  assert(deep.tail == 77);
  assert(promoted_switch.first == 94);
  assert(promoted_switch.second == 95);
  assert(promoted_switch.tail == 96);
  return 0;
}

static int check_compound_literals(void) {
  struct cursor_holder *holder = &(struct cursor_holder){
      .items[1].scalar = 46,
      47,
      48,
      49,
  };
  union cursor_matrix *matrices = (union cursor_matrix[]){
      [1].cells[0][1] = 50,
      51,
      52,
  };
  struct anonymous_cursor_holder *anonymous =
      &(struct anonymous_cursor_holder){
          .second = 53,
          54,
      };
  union text_anonymous_value *text =
      &(union text_anonymous_value){
          .text = "S",
          78,
      };
  struct deep_anonymous_cursor_holder *deep =
      &(struct deep_anonymous_cursor_holder){
          .second = 79,
          80,
      };
  struct anonymous_cursor_holder *promoted_switch =
      &(struct anonymous_cursor_holder){
          .wide = -1,
          .second = 97,
          98,
      };

  assert(holder->items[1].scalar == 46);
  assert(holder->items[2].pair.first == 47);
  assert(holder->items[2].pair.second == 48);
  assert(holder->tail == 49);

  assert(sizeof((union cursor_matrix[]){
                    [1].cells[0][1] = 1, 2, 3}) /
             sizeof(union cursor_matrix) ==
         2);
  assert(matrices[1].cells[0][0] == 0);
  assert(matrices[1].cells[0][1] == 50);
  assert(matrices[1].cells[1][0] == 51);
  assert(matrices[1].cells[1][1] == 52);

  assert(anonymous->first == 0);
  assert(anonymous->second == 53);
  assert(anonymous->tail == 54);
  assert(text->text[0] == 'S');
  assert(text->text[1] == '\0');
  assert(text->value == 78);
  assert(deep->first == 0);
  assert(deep->second == 79);
  assert(deep->tail == 80);
  assert(promoted_switch->first == 0);
  assert(promoted_switch->second == 97);
  assert(promoted_switch->tail == 98);
  return 0;
}

int main(void) {
  assert(check_global_objects() == 0);
  assert(check_static_local_objects() == 0);
  assert(check_automatic_objects() == 0);
  assert(check_compound_literals() == 0);
  return 0;
}
