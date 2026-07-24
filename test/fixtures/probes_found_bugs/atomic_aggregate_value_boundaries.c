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
