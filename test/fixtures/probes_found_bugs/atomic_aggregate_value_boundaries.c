// Atomic aggregate load/store, promotion, and lvalue-path boundaries.
// Expected: exit=0
#include <assert.h>

struct byte1 {
  unsigned char a;
};

struct bytes2 {
  unsigned char a;
  unsigned char b;
};

struct bytes3 {
  unsigned char a;
  unsigned char b;
  unsigned char c;
};

struct bytes5 {
  unsigned char a;
  unsigned char b;
  unsigned char c;
  unsigned char d;
  unsigned char e;
};

struct pair {
  int x;
  int y;
};

struct words3 {
  unsigned int a;
  unsigned int b;
  unsigned int c;
};

typedef struct bytes3 atomic_bytes_callback_t(
    _Atomic(struct bytes3));
typedef struct words3 atomic_words_callback_t(
    _Atomic(struct words3));

struct callback_holder {
  atomic_bytes_callback_t *bytes;
  atomic_words_callback_t *words;
};

struct words4 {
  unsigned int a;
  unsigned int b;
  unsigned int c;
  unsigned int d;
};

union word {
  unsigned int bits;
  float value;
};

union wide {
  unsigned long long values[2];
  unsigned char bytes[16];
};

struct holder {
  _Atomic(struct pair) pair;
  _Atomic(struct bytes3) bytes;
  _Atomic(struct words4) words;
};

static _Atomic(struct byte1) global_byte1 =
    (struct byte1){1};
static _Atomic(struct bytes2) global_bytes2 =
    (struct bytes2){2, 3};
static _Atomic(struct bytes3) global_bytes3 =
    (struct bytes3){4, 5, 6};
static _Atomic(struct bytes5) global_bytes5 =
    (struct bytes5){7, 8, 9, 10, 11};
static _Atomic(struct pair) global_pair =
    (struct pair){12, 13};
static _Atomic(struct words3) global_words3 =
    (struct words3){14, 15, 16};
static _Atomic(struct words4) global_words4 =
    (struct words4){17, 18, 19, 20};
static _Atomic(union word) global_word =
    (union word){.bits = 0x12345678u};
static _Atomic(union wide) global_wide =
    (union wide){.values = {0x1122334455667788ULL,
                            0x99aabbccddeeff00ULL}};
static _Atomic(struct pair) pair_slots[2];
static int lhs_evaluations;
static int rhs_evaluations;

static int is_pair(struct pair value, int x, int y) {
  return value.x == x && value.y == y;
}

static int is_words3(
    struct words3 value, unsigned int a,
    unsigned int b, unsigned int c) {
  return value.a == a && value.b == b && value.c == c;
}

static int is_words4(
    struct words4 value, unsigned int a, unsigned int b,
    unsigned int c, unsigned int d) {
  return value.a == a && value.b == b &&
         value.c == c && value.d == d;
}

static struct bytes3 rotate_register_atomic_bytes(
    register _Atomic(struct bytes3) value) {
  struct bytes3 snapshot = value;
  value = (struct bytes3){
      snapshot.b, snapshot.c, snapshot.a};
  return value;
}

static struct words3 rotate_register_atomic_words(
    register _Atomic(struct words3) value) {
  struct words3 snapshot = value;
  value = (struct words3){
      snapshot.b, snapshot.c, snapshot.a};
  return value;
}

static struct words3 reverse_register_atomic_words(
    register _Atomic(struct words3) value) {
  struct words3 snapshot = value;
  value = (struct words3){
      snapshot.c, snapshot.b, snapshot.a};
  return value;
}

static atomic_bytes_callback_t *global_bytes_callback =
    rotate_register_atomic_bytes;
static atomic_words_callback_t *global_words_callbacks[2] = {
    rotate_register_atomic_words,
    rotate_register_atomic_words,
};
static struct callback_holder global_callback_holder = {
    rotate_register_atomic_bytes,
    rotate_register_atomic_words,
};

static struct bytes3 apply_atomic_bytes(
    atomic_bytes_callback_t *callback, struct bytes3 value) {
  return callback(value);
}

static struct words3 apply_atomic_words(
    atomic_words_callback_t *callback, struct words3 value) {
  return callback(value);
}

static atomic_bytes_callback_t *select_atomic_bytes_callback(void) {
  return rotate_register_atomic_bytes;
}

static atomic_words_callback_t *select_atomic_words_callback(void) {
  return rotate_register_atomic_words;
}

static _Atomic(struct pair) *selected_pair(void) {
  lhs_evaluations++;
  return &pair_slots[1];
}

static struct pair next_pair(void) {
  rhs_evaluations++;
  return (struct pair){31, 32};
}

static struct pair snapshot_pair(
    _Atomic(struct pair) *pointer) {
  return *pointer;
}

int main(void) {
  struct byte1 byte1 = global_byte1;
  struct bytes2 bytes2 = global_bytes2;
  struct bytes3 bytes3 = global_bytes3;
  struct bytes5 bytes5 = global_bytes5;
  struct pair pair = global_pair;
  struct words3 words3 = global_words3;
  struct words4 words4 = global_words4;
  union word word = global_word;
  union wide wide = global_wide;
  struct bytes3 register_bytes = rotate_register_atomic_bytes(
      (struct bytes3){21, 22, 23});
  struct words3 register_words = rotate_register_atomic_words(
      (struct words3){31, 32, 33});
  atomic_bytes_callback_t *bytes_callback =
      rotate_register_atomic_bytes;
  atomic_words_callback_t *words_callback =
      rotate_register_atomic_words;
  struct bytes3 callback_bytes = apply_atomic_bytes(
      bytes_callback, (struct bytes3){41, 42, 43});
  struct words3 callback_words = apply_atomic_words(
      words_callback, (struct words3){51, 52, 53});
  struct bytes3 returned_callback_bytes =
      select_atomic_bytes_callback()(
          (struct bytes3){61, 62, 63});
  struct words3 returned_callback_words =
      select_atomic_words_callback()(
          (struct words3){71, 72, 73});
  static atomic_bytes_callback_t *static_bytes_callback =
      rotate_register_atomic_bytes;
  struct bytes3 global_callback_bytes =
      global_bytes_callback((struct bytes3){81, 82, 83});
  struct words3 array_callback_words =
      global_words_callbacks[1](
          (struct words3){91, 92, 93});
  struct bytes3 member_callback_bytes =
      global_callback_holder.bytes(
          (struct bytes3){101, 102, 103});
  struct words3 member_callback_words =
      global_callback_holder.words(
          (struct words3){111, 112, 113});
  struct bytes3 static_callback_bytes =
      static_bytes_callback(
          (struct bytes3){121, 122, 123});
  int choose_rotate_callback = 1;
  int callback_expression_evaluations = 0;
  atomic_words_callback_t *expression_callback =
      rotate_register_atomic_words;
  struct words3 conditional_callback_words =
      (choose_rotate_callback
           ? rotate_register_atomic_words
           : reverse_register_atomic_words)(
          (struct words3){131, 132, 133});
  struct words3 comma_callback_words =
      (callback_expression_evaluations++,
       reverse_register_atomic_words)(
          (struct words3){141, 142, 143});
  struct words3 assignment_callback_words =
      (expression_callback =
           reverse_register_atomic_words)(
          (struct words3){151, 152, 153});
  choose_rotate_callback = 0;
  struct words3 pointer_conditional_callback_words =
      (choose_rotate_callback
           ? expression_callback
           : rotate_register_atomic_words)(
          (struct words3){161, 162, 163});

  assert(byte1.a == 1);
  assert(bytes2.a == 2 && bytes2.b == 3);
  assert(bytes3.a == 4 && bytes3.b == 5 && bytes3.c == 6);
  assert(bytes5.a == 7 && bytes5.b == 8 && bytes5.c == 9 &&
         bytes5.d == 10 && bytes5.e == 11);
  assert(is_pair(pair, 12, 13));
  assert(is_words3(words3, 14, 15, 16));
  assert(is_words4(words4, 17, 18, 19, 20));
  assert(word.bits == 0x12345678u);
  assert(wide.values[0] == 0x1122334455667788ULL);
  assert(wide.values[1] == 0x99aabbccddeeff00ULL);
  assert(register_bytes.a == 22 && register_bytes.b == 23 &&
         register_bytes.c == 21);
  assert(is_words3(register_words, 32, 33, 31));
  assert(callback_bytes.a == 42 && callback_bytes.b == 43 &&
         callback_bytes.c == 41);
  assert(is_words3(callback_words, 52, 53, 51));
  assert(returned_callback_bytes.a == 62 &&
         returned_callback_bytes.b == 63 &&
         returned_callback_bytes.c == 61);
  assert(is_words3(returned_callback_words, 72, 73, 71));
  assert(global_callback_bytes.a == 82 &&
         global_callback_bytes.b == 83 &&
         global_callback_bytes.c == 81);
  assert(is_words3(array_callback_words, 92, 93, 91));
  assert(member_callback_bytes.a == 102 &&
         member_callback_bytes.b == 103 &&
         member_callback_bytes.c == 101);
  assert(is_words3(member_callback_words, 112, 113, 111));
  assert(static_callback_bytes.a == 122 &&
         static_callback_bytes.b == 123 &&
         static_callback_bytes.c == 121);
  assert(is_words3(
      conditional_callback_words, 132, 133, 131));
  assert(is_words3(
      comma_callback_words, 143, 142, 141));
  assert(is_words3(
      assignment_callback_words, 153, 152, 151));
  assert(is_words3(
      pointer_conditional_callback_words, 162, 163, 161));
  assert(callback_expression_evaluations == 1);

  byte1 = (global_byte1 = (struct byte1){14});
  bytes2 = (global_bytes2 = (struct bytes2){15, 16});
  bytes3 = (global_bytes3 = (struct bytes3){17, 18, 19});
  bytes5 =
      (global_bytes5 = (struct bytes5){20, 21, 22, 23, 24});
  pair = (global_pair = (struct pair){25, 26});
  words3 =
      (global_words3 = (struct words3){27, 28, 29});
  words4 =
      (global_words4 = (struct words4){30, 31, 32, 33});
  word = (global_word = (union word){.bits = 0x89abcdefu});
  wide = (global_wide =
      (union wide){.values = {0xfedcba9876543210ULL,
                              0x0123456789abcdefULL}});

  struct byte1 byte1_snapshot = global_byte1;
  assert(byte1.a == 14 && byte1_snapshot.a == 14);
  assert(bytes2.a == 15 && bytes2.b == 16);
  bytes2 = global_bytes2;
  assert(bytes2.a == 15 && bytes2.b == 16);
  assert(bytes3.a == 17 && bytes3.b == 18 && bytes3.c == 19);
  bytes3 = global_bytes3;
  assert(bytes3.a == 17 && bytes3.b == 18 && bytes3.c == 19);
  assert(bytes5.a == 20 && bytes5.b == 21 && bytes5.c == 22 &&
         bytes5.d == 23 && bytes5.e == 24);
  bytes5 = global_bytes5;
  assert(bytes5.a == 20 && bytes5.b == 21 && bytes5.c == 22 &&
         bytes5.d == 23 && bytes5.e == 24);
  assert(is_pair(pair, 25, 26));
  assert(is_words3(words3, 27, 28, 29));
  words3 = global_words3;
  assert(is_words3(words3, 27, 28, 29));
  assert(is_words4(words4, 30, 31, 32, 33));
  words4 = global_words4;
  assert(is_words4(words4, 30, 31, 32, 33));
  union word word_snapshot = global_word;
  assert(word_snapshot.bits == 0x89abcdefu);
  assert(word.bits == 0x89abcdefu);
  union wide wide_snapshot = global_wide;
  assert(wide.values[0] == 0xfedcba9876543210ULL);
  assert(wide.values[1] == 0x0123456789abcdefULL);
  assert(wide_snapshot.values[0] == 0xfedcba9876543210ULL);
  assert(wide_snapshot.values[1] == 0x0123456789abcdefULL);

  _Atomic(struct pair) local_pair =
      (struct pair){27, 28};
  pair = local_pair;
  assert(is_pair(pair, 27, 28));
  pair = (local_pair = (struct pair){29, 30});
  assert(is_pair(pair, 29, 30));
  assert(is_pair(local_pair, 29, 30));

  struct holder holder;
  holder.pair = (struct pair){33, 34};
  holder.bytes = (struct bytes3){35, 36, 37};
  holder.words = (struct words4){38, 39, 40, 41};
  assert(is_pair(holder.pair, 33, 34));
  bytes3 = holder.bytes;
  assert(bytes3.a == 35 && bytes3.b == 36 && bytes3.c == 37);
  assert(is_words4(holder.words, 38, 39, 40, 41));

  pair_slots[0] = (struct pair){42, 43};
  _Atomic(struct pair) *pointer = &pair_slots[0];
  pair = *pointer;
  assert(is_pair(pair, 42, 43));
  pair = (*pointer = (struct pair){44, 45});
  assert(is_pair(pair, 44, 45));
  assert(is_pair(*pointer, 44, 45));

  _Atomic(struct words4) local_words =
      (struct words4){46, 47, 48, 49};
  _Atomic(struct words4) *words_pointer = &local_words;
  words4 = *words_pointer;
  assert(is_words4(words4, 46, 47, 48, 49));
  words4 =
      (*words_pointer = (struct words4){50, 51, 52, 53});
  assert(is_words4(words4, 50, 51, 52, 53));
  assert(is_words4(*words_pointer, 50, 51, 52, 53));

  pair = (*selected_pair() = next_pair());
  assert(lhs_evaluations == 1);
  assert(rhs_evaluations == 1);
  assert(is_pair(pair, 31, 32));
  assert(is_pair(pair_slots[1], 31, 32));
  assert(is_pair(snapshot_pair(&pair_slots[1]), 31, 32));
  return 0;
}
