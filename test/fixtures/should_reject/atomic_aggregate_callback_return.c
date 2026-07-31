/* Return conversion must preserve an atomic aggregate callback parameter. */
struct bytes3 {
  unsigned char a;
  unsigned char b;
  unsigned char c;
};

typedef struct bytes3 atomic_callback(_Atomic(struct bytes3));
typedef struct bytes3 plain_callback(struct bytes3);

static struct bytes3 atomic_identity(_Atomic(struct bytes3) value) {
  return value;
}

static plain_callback *pick_plain(void) {
  return atomic_identity;
}

int main(void) {
  return pick_plain() != 0;
}
