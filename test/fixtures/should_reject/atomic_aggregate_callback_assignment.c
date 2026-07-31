/* A callback with an atomic aggregate parameter is incompatible with a plain callback. */
struct bytes3 {
  unsigned char a;
  unsigned char b;
  unsigned char c;
};

typedef struct bytes3 atomic_callback(_Atomic(struct bytes3));

static struct bytes3 plain(struct bytes3 value) {
  return value;
}

int main(void) {
  atomic_callback *callback = plain;
  return callback != 0;
}
