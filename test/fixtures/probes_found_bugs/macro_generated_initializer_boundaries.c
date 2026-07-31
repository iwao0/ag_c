/*
 * Exercise macro-expanded member and array designators, the continuation
 * cursor after a nested designator, variadic aggregate initializers, compound
 * literals, flexible-array prefixes, and pasted assignment operators.
 */
#define CAT_IMPL(left, right) left##right
#define CAT(left, right) CAT_IMPL(left, right)
#define FIELD(index) CAT(field_, index)
#define SET_FIELD(index, value) .FIELD(index) = (value)
#define MEMBER(name) .name
#define INDEX(value) [value]
#define SELECT(member, index, leaf) MEMBER(member) INDEX(index) MEMBER(leaf)
#define MAKE_MATRIX(name, ...) static int name[2][3] = {__VA_ARGS__}
#define ROW(index, ...) [index] = {__VA_ARGS__}
#define MAKE_RECORD(type, ...) ((type){__VA_ARGS__})
#define PASTE_IMPL(left, right) left##right
#define PASTE(left, right) PASTE_IMPL(left, right)
#define PASTE_THREE_IMPL(first, second, third) first##second##third
#define PASTE_THREE(first, second, third) \
  PASTE_THREE_IMPL(first, second, third)

struct Container {
  int tag;
  union {
    struct {
      int field_0;
      int field_1;
    };
    int raw[2];
  };
  int tail;
};

struct Pair {
  int x;
  int y;
};

struct Box {
  struct Pair pairs[2];
  int tail;
};

struct Record {
  int FIELD(0);
  int FIELD(1);
  int (*apply)(int);
};

struct Header {
  int length;
  union {
    struct {
      int first;
      int second;
    };
    int words[2];
  };
  unsigned char payload[];
};

static struct Container container = {
    .tag = 3,
    SET_FIELD(0, 11),
    SET_FIELD(1, 13),
    .tail = 15,
};

static struct Box box = {
    SELECT(pairs, 0, y) = 5,
    7,
    8,
    MEMBER(tail) = 22,
};

MAKE_MATRIX(matrix,
            ROW(1, 11, 13, 15),
            ROW(0, 1, 2));

static struct Header header = {
    MEMBER(length) = 18,
    MEMBER(words) INDEX(1) = 13,
    MEMBER(words) INDEX(0) = 11,
};

static int double_value(int value) { return value * 2; }

_Static_assert(sizeof(matrix) / sizeof(matrix[0][0]) == 6,
               "variadic initializer preserves dimensions");

int main(void) {
  struct Record *record =
      &MAKE_RECORD(struct Record,
                   .FIELD(1) = 9,
                   .FIELD(0) = 6,
                   .apply = double_value);
  int pasted = 3;
  pasted PASTE(+, =) 4;
  pasted PASTE(*, =) 3;
  pasted PASTE_THREE(<, <, =) 1;

  if (container.tag + container.raw[0] + container.raw[1] +
          container.tail !=
      42) {
    return 1;
  }
  if (box.pairs[0].x != 0 || box.pairs[0].y != 5 ||
      box.pairs[1].x != 7 || box.pairs[1].y != 8 || box.tail != 22) {
    return 2;
  }
  if (matrix[0][0] + matrix[0][1] + matrix[0][2] + matrix[1][0] +
          matrix[1][1] + matrix[1][2] !=
      42) {
    return 3;
  }
  if (record->FIELD(0) + record->FIELD(1) + record->apply(13) + 1 != 42) {
    return 4;
  }
  if (header.length + header.first + header.second != 42) return 5;
  if (pasted != 42) return 6;
  return 0;
}
