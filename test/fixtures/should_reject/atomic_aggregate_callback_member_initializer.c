/* A member initializer must preserve an atomic aggregate callback parameter. */
struct bytes3 {
  unsigned char a;
  unsigned char b;
  unsigned char c;
};

typedef struct bytes3 atomic_callback(_Atomic(struct bytes3));

static struct bytes3 plain(struct bytes3 value) {
  return value;
}

struct holder {
  atomic_callback *callback;
};

static struct holder global_holder = {plain};

int main(void) {
  return global_holder.callback != 0;
}
