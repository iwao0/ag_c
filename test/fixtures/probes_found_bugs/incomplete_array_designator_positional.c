#include <assert.h>

struct pair {
  int first;
  int second;
};

struct nested_pair {
  int values[2];
  int tail;
};

struct promoted_pair {
  union {
    struct {
      int first;
      int second;
    };
    int values[2];
  };
  int tail;
};

struct bitfield_pair {
  unsigned int first : 3;
  unsigned int second : 3;
  int tail;
};

struct union_box {
  union {
    struct pair pair;
    int scalar;
  } value;
  int tail;
};

static int global_matrix[][2] = {
    [1][0] = 5,
    6,
};

static int global_matrix_at_end[][2] = {
    [1][1] = 7,
    8,
};

static int global_cube[][2][2] = {
    [1][0][0] = 9,
    10,
    11,
    12,
};

static struct pair global_pairs[] = {
    [1].first = 13,
    14,
};

static struct pair global_pairs_at_end[] = {
    [1].second = 15,
    16,
};

static struct nested_pair global_nested[] = {
    [1].values[0] = 17,
    18,
    19,
};

static struct promoted_pair global_promoted[] = {
    [1].first = 20,
    21,
    22,
};

static int global_backward[][2] = {
    [3][1] = 41,
    [0][0] = 42,
    43,
};

static struct bitfield_pair global_bitfields[] = {
    [1].first = 1,
    2,
    44,
};

static struct union_box global_union_scalar[] = {
    [1].value.scalar = 45,
    46,
};

static struct union_box global_union_aggregate[] = {
    [1].value.pair.first = 47,
    48,
    49,
};

static int check_global_objects(void) {
  assert(sizeof(global_matrix) / sizeof(global_matrix[0]) == 2);
  assert(global_matrix[1][0] == 5);
  assert(global_matrix[1][1] == 6);

  assert(sizeof(global_matrix_at_end) /
             sizeof(global_matrix_at_end[0]) ==
         3);
  assert(global_matrix_at_end[1][1] == 7);
  assert(global_matrix_at_end[2][0] == 8);

  assert(sizeof(global_cube) / sizeof(global_cube[0]) == 2);
  assert(global_cube[1][0][0] == 9);
  assert(global_cube[1][0][1] == 10);
  assert(global_cube[1][1][0] == 11);
  assert(global_cube[1][1][1] == 12);

  assert(sizeof(global_pairs) / sizeof(global_pairs[0]) == 2);
  assert(global_pairs[1].first == 13);
  assert(global_pairs[1].second == 14);

  assert(sizeof(global_pairs_at_end) /
             sizeof(global_pairs_at_end[0]) ==
         3);
  assert(global_pairs_at_end[1].second == 15);
  assert(global_pairs_at_end[2].first == 16);

  assert(sizeof(global_nested) / sizeof(global_nested[0]) == 2);
  assert(global_nested[1].values[0] == 17);
  assert(global_nested[1].values[1] == 18);
  assert(global_nested[1].tail == 19);
  assert(sizeof(global_promoted) / sizeof(global_promoted[0]) == 2);
  assert(global_promoted[1].first == 20);
  assert(global_promoted[1].second == 21);
  assert(global_promoted[1].tail == 22);
  assert(sizeof(global_backward) / sizeof(global_backward[0]) == 4);
  assert(global_backward[3][1] == 41);
  assert(global_backward[0][0] == 42);
  assert(global_backward[0][1] == 43);
  assert(sizeof(global_bitfields) / sizeof(global_bitfields[0]) == 2);
  assert(global_bitfields[1].first == 1);
  assert(global_bitfields[1].second == 2);
  assert(global_bitfields[1].tail == 44);
  assert(sizeof(global_union_scalar) /
             sizeof(global_union_scalar[0]) ==
         2);
  assert(global_union_scalar[1].value.scalar == 45);
  assert(global_union_scalar[1].tail == 46);
  assert(sizeof(global_union_aggregate) /
             sizeof(global_union_aggregate[0]) ==
         2);
  assert(global_union_aggregate[1].value.pair.first == 47);
  assert(global_union_aggregate[1].value.pair.second == 48);
  assert(global_union_aggregate[1].tail == 49);
  return 0;
}

static int check_static_local_objects(void) {
  static int matrix[][2] = {
      [2][0] = 21,
      22,
  };
  static struct pair pairs[] = {
      [2].first = 23,
      24,
  };

  assert(sizeof(matrix) / sizeof(matrix[0]) == 3);
  assert(matrix[2][0] == 21);
  assert(matrix[2][1] == 22);
  assert(sizeof(pairs) / sizeof(pairs[0]) == 3);
  assert(pairs[2].first == 23);
  assert(pairs[2].second == 24);
  return 0;
}

static int check_automatic_objects(void) {
  int matrix[][2] = {
      [1][0] = 25,
      26,
  };
  struct pair pairs[] = {
      [1].first = 27,
      28,
  };

  assert(sizeof(matrix) / sizeof(matrix[0]) == 2);
  assert(matrix[1][0] == 25);
  assert(matrix[1][1] == 26);
  assert(sizeof(pairs) / sizeof(pairs[0]) == 2);
  assert(pairs[1].first == 27);
  assert(pairs[1].second == 28);
  return 0;
}

static int check_compound_literals(void) {
  int (*matrix)[2] = (int[][2]){
      [1][0] = 29,
      30,
  };
  struct pair *pairs = (struct pair[]){
      [1].first = 31,
      32,
  };

  assert(matrix[1][0] == 29);
  assert(matrix[1][1] == 30);
  assert(pairs[1].first == 31);
  assert(pairs[1].second == 32);
  assert(sizeof((int[][2]){[1][0] = 1, 2}) /
             sizeof(int[2]) ==
         2);
  assert(sizeof((struct pair[]){[1].first = 1, 2}) /
             sizeof(struct pair) ==
         2);
  return 0;
}

static int check_positional_aggregate_values(void) {
  struct pair first = {33, 34};
  struct pair second = {35, 36};
  struct pair copies[] = {first, second};
  struct pair *literal = (struct pair[]){first, second};

  assert(sizeof(copies) / sizeof(copies[0]) == 2);
  assert(copies[0].first == 33);
  assert(copies[0].second == 34);
  assert(copies[1].first == 35);
  assert(copies[1].second == 36);
  assert(literal[0].first == 33);
  assert(literal[0].second == 34);
  assert(literal[1].first == 35);
  assert(literal[1].second == 36);
  assert(sizeof((struct pair[]){first, second}) /
             sizeof(struct pair) ==
         2);
  return 0;
}

int main(void) {
  assert(check_global_objects() == 0);
  assert(check_static_local_objects() == 0);
  assert(check_automatic_objects() == 0);
  assert(check_compound_literals() == 0);
  assert(check_positional_aggregate_values() == 0);
  return 0;
}
