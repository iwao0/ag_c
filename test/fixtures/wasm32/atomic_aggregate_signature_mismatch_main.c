// The referenced callback parameter is atomic. The Wasm object linker must
// reject a definition that drops this C-level qualifier even when both lower
// to the same physical Wasm signature.
struct words3 {
  unsigned int a;
  unsigned int b;
  unsigned int c;
};

typedef struct words3 callback_t(_Atomic(struct words3));

struct words3 transform_words(_Atomic(struct words3));

static callback_t *callback = transform_words;

int main(void) {
  struct words3 result =
      callback((struct words3){40, 1, 2});
  return (int)(result.a + result.b + result.c);
}
