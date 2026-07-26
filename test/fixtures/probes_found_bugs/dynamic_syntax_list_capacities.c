#include <assert.h>

#define JOIN_RAW(a, b) a##b
#define JOIN(a, b) JOIN_RAW(a, b)

#define NAMES_1(p) JOIN(p, 0), JOIN(p, 1)
#define NAMES_2(p) NAMES_1(JOIN(p, 0)), NAMES_1(JOIN(p, 1))
#define NAMES_3(p) NAMES_2(JOIN(p, 0)), NAMES_2(JOIN(p, 1))
#define NAMES_4(p) NAMES_3(JOIN(p, 0)), NAMES_3(JOIN(p, 1))
#define NAMES_5(p) NAMES_4(JOIN(p, 0)), NAMES_4(JOIN(p, 1))
#define NAMES_6(p) NAMES_5(JOIN(p, 0)), NAMES_5(JOIN(p, 1))
#define NAMES_7(p) NAMES_6(JOIN(p, 0)), NAMES_6(JOIN(p, 1))
#define NAMES_8(p) NAMES_7(JOIN(p, 0)), NAMES_7(JOIN(p, 1))
#define NAMES_9(p) NAMES_8(JOIN(p, 0)), NAMES_8(JOIN(p, 1))
#define NAMES_10(p) NAMES_9(JOIN(p, 0)), NAMES_9(JOIN(p, 1))

#define MEMBERS_1(p) int JOIN(p, 0); int JOIN(p, 1);
#define MEMBERS_2(p) MEMBERS_1(JOIN(p, 0)) MEMBERS_1(JOIN(p, 1))
#define MEMBERS_3(p) MEMBERS_2(JOIN(p, 0)) MEMBERS_2(JOIN(p, 1))
#define MEMBERS_4(p) MEMBERS_3(JOIN(p, 0)) MEMBERS_3(JOIN(p, 1))
#define MEMBERS_5(p) MEMBERS_4(JOIN(p, 0)) MEMBERS_4(JOIN(p, 1))
#define MEMBERS_6(p) MEMBERS_5(JOIN(p, 0)) MEMBERS_5(JOIN(p, 1))
#define MEMBERS_7(p) MEMBERS_6(JOIN(p, 0)) MEMBERS_6(JOIN(p, 1))
#define MEMBERS_8(p) MEMBERS_7(JOIN(p, 0)) MEMBERS_7(JOIN(p, 1))
#define MEMBERS_9(p) MEMBERS_8(JOIN(p, 0)) MEMBERS_8(JOIN(p, 1))
#define MEMBERS_10(p) MEMBERS_9(JOIN(p, 0)) MEMBERS_9(JOIN(p, 1))

#define VALUES_1 1, 1
#define VALUES_2 VALUES_1, VALUES_1
#define VALUES_3 VALUES_2, VALUES_2
#define VALUES_4 VALUES_3, VALUES_3
#define VALUES_5 VALUES_4, VALUES_4
#define VALUES_6 VALUES_5, VALUES_5
#define VALUES_7 VALUES_6, VALUES_6
#define VALUES_8 VALUES_7, VALUES_7
#define VALUES_9 VALUES_8, VALUES_8
#define VALUES_10 VALUES_9, VALUES_9
#define VALUES_11 VALUES_10, VALUES_10
#define VALUES_12 VALUES_11, VALUES_11

typedef int top_first, NAMES_10(top_);

struct ManyDeclarators {
  int first, NAMES_10(field_);
};

struct ManyItems {
  int first;
  MEMBERS_10(item_)
};

enum ManyEnumerators {
  enum_first,
  NAMES_10(enum_)
};

static int values[4097] = {1, VALUES_12};

int main(void) {
  typedef int local_first, NAMES_10(local_);
  top_1111111111 global_value = 3;
  local_1111111111 local_value = 5;
  assert(sizeof(struct ManyDeclarators) == 1025 * sizeof(int));
  assert(sizeof(struct ManyItems) == 1025 * sizeof(int));
  assert(enum_first == 0);
  assert(enum_1111111111 == 1024);
  assert(values[0] == 1);
  assert(values[4096] == 1);
  assert(global_value + local_value == 8);
  return 0;
}
