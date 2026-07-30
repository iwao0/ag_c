// Cross-TU unprototyped function addresses whose prototype definitions use
// atomic parameter types. The functions are not called through the
// unprototyped pointers because argument compatibility is call-site specific.
// Expected with the companion TU: exit=42.

typedef int callback_t();

int atomic_char_target();
int atomic_float_target();

static callback_t *callbacks[2] = {
    atomic_char_target,
    atomic_float_target,
};

int main(void) {
  return callbacks[0] != 0 &&
                 callbacks[1] != 0 &&
                 callbacks[0] != callbacks[1]
             ? 42
             : 1;
}
