#include <assert.h>

struct deep_level_9 {
  int value;
};

struct deep_level_8 {
  struct deep_level_9 next;
};

struct deep_level_7 {
  struct deep_level_8 next;
};

struct deep_level_6 {
  struct deep_level_7 next;
};

struct deep_level_5 {
  struct deep_level_6 next;
};

struct deep_level_4 {
  struct deep_level_5 next;
};

struct deep_level_3 {
  struct deep_level_4 next;
};

struct deep_level_2 {
  struct deep_level_3 next;
};

struct deep_level_1 {
  struct deep_level_2 next;
};

struct deep_level_0 {
  struct deep_level_1 next;
};

struct deep_array_holder {
  int values[2][2][2][2][2][2][2][2][2];
};

static int global_values[2][2][2][2][2][2][2][2][2][2] = {
    [1][0][1][0][1][0][1][0][1][0] = 101,
    [0][1][0][1][0][1][0][1][0][1] = 202,
};

static struct deep_level_0 global_record = {
    .next.next.next.next.next.next.next.next.next.value = 303,
};

static struct deep_array_holder global_holder = {
    .values[1][0][1][0][1][0][1][0][1] = 404,
};

static int inferred_global_values[][1][1][1][1][1][1][1][1][1] = {
    [3][0][0][0][0][0][0][0][0][0] = 909,
};

static struct deep_level_0 inferred_global_records[] = {
    [2].next.next.next.next.next.next.next.next.next.value = 1001,
};

static int global_values_match(void) {
  return global_values[1][0][1][0][1][0][1][0][1][0] == 101 &&
         global_values[0][1][0][1][0][1][0][1][0][1] == 202 &&
         global_values[0][0][0][0][0][0][0][0][0][0] == 0 &&
         global_record.next.next.next.next.next.next.next.next.next.value ==
             303 &&
         global_holder.values[1][0][1][0][1][0][1][0][1] == 404 &&
         global_holder.values[0][0][0][0][0][0][0][0][0] == 0 &&
         sizeof(inferred_global_values) /
                 sizeof(inferred_global_values[0]) ==
             4 &&
         inferred_global_values[3][0][0][0][0][0][0][0][0][0] == 909 &&
         inferred_global_values[0][0][0][0][0][0][0][0][0][0] == 0 &&
         sizeof(inferred_global_records) /
                 sizeof(inferred_global_records[0]) ==
             3 &&
         inferred_global_records[2]
                 .next.next.next.next.next.next.next.next.next.value ==
             1001 &&
         inferred_global_records[0]
                 .next.next.next.next.next.next.next.next.next.value ==
             0;
}

static int static_local_values_match(void) {
  static int values[2][2][2][2][2][2][2][2][2][2] = {
      [1][1][0][0][1][1][0][0][1][1] = 505,
  };
  static struct deep_level_0 record = {
      .next.next.next.next.next.next.next.next.next.value = 606,
  };
  static int inferred_values[][1][1][1][1][1][1][1][1][1] = {
      [2][0][0][0][0][0][0][0][0][0] = 1111,
  };
  static struct deep_level_0 inferred_records[] = {
      [1].next.next.next.next.next.next.next.next.next.value = 1212,
  };
  return values[1][1][0][0][1][1][0][0][1][1] == 505 &&
         values[0][0][0][0][0][0][0][0][0][0] == 0 &&
         record.next.next.next.next.next.next.next.next.next.value == 606 &&
         sizeof(inferred_values) / sizeof(inferred_values[0]) == 3 &&
         inferred_values[2][0][0][0][0][0][0][0][0][0] == 1111 &&
         inferred_values[0][0][0][0][0][0][0][0][0][0] == 0 &&
         sizeof(inferred_records) / sizeof(inferred_records[0]) == 2 &&
         inferred_records[1]
                 .next.next.next.next.next.next.next.next.next.value ==
             1212 &&
         inferred_records[0]
                 .next.next.next.next.next.next.next.next.next.value ==
             0;
}

static int compound_literals_match(void) {
  int (*values)[1][1][1][1][1][1][1][1][1] =
      (int[][1][1][1][1][1][1][1][1][1]){
          [2][0][0][0][0][0][0][0][0][0] = 1515,
      };
  struct deep_level_0 *records =
      (struct deep_level_0[]){
          [2].next.next.next.next.next.next.next.next.next.value = 1616,
      };

  return values[2][0][0][0][0][0][0][0][0][0] == 1515 &&
         values[0][0][0][0][0][0][0][0][0][0] == 0 &&
         records[2].next.next.next.next.next.next.next.next.next.value ==
             1616 &&
         records[0].next.next.next.next.next.next.next.next.next.value == 0 &&
         sizeof((int[][1][1][1][1][1][1][1][1][1]){
                    [2][0][0][0][0][0][0][0][0][0] = 1,
                }) /
                 sizeof(values[0]) ==
             3 &&
         sizeof((struct deep_level_0[]){
                    [2].next.next.next.next.next.next.next.next.next.value = 1,
                }) /
                 sizeof(records[0]) ==
             3;
}

int main(void) {
  int local_values[2][2][2][2][2][2][2][2][2][2] = {
      [1][1][1][0][0][0][1][1][1][0] = 707,
  };
  struct deep_level_0 local_record = {
      .next.next.next.next.next.next.next.next.next.value = 808,
  };
  int inferred_values[][1][1][1][1][1][1][1][1][1] = {
      [1][0][0][0][0][0][0][0][0][0] = 1313,
  };
  struct deep_level_0 inferred_records[] = {
      [2].next.next.next.next.next.next.next.next.next.value = 1414,
  };

  assert(global_values_match());
  assert(static_local_values_match());
  assert(compound_literals_match());
  assert(local_values[1][1][1][0][0][0][1][1][1][0] == 707);
  assert(local_values[0][0][0][0][0][0][0][0][0][0] == 0);
  assert(local_record.next.next.next.next.next.next.next.next.next.value ==
         808);
  assert(sizeof(inferred_values) / sizeof(inferred_values[0]) == 2);
  assert(inferred_values[1][0][0][0][0][0][0][0][0][0] == 1313);
  assert(inferred_values[0][0][0][0][0][0][0][0][0][0] == 0);
  assert(sizeof(inferred_records) / sizeof(inferred_records[0]) == 3);
  assert(inferred_records[2]
             .next.next.next.next.next.next.next.next.next.value == 1414);
  assert(inferred_records[0]
             .next.next.next.next.next.next.next.next.next.value == 0);
  return 0;
}
