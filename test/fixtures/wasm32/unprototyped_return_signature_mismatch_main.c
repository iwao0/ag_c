// The hidden return pointer ABI must not make distinct C aggregate return
// types compatible across translation units.

struct expected_result {
  long long values[3];
};

typedef struct expected_result callback_t();

struct expected_result build_result();

static callback_t *callback = build_result;

int main(void) {
  struct expected_result result = callback(7, 2.5f);
  return result.values[0] == 7 ? 0 : 1;
}
