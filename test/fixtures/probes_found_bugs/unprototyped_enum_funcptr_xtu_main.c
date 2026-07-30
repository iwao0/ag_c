// Cross-TU unprototyped function addresses with signed and unsigned enum
// prototype definitions. Expected with the companion TU: exit=42.

typedef int callback_t();

int check_signed_enum();
int check_unsigned_enum();

static callback_t *callbacks[2] = {
    check_signed_enum,
    check_unsigned_enum,
};

int main(void) {
  return callbacks[0](-1) + callbacks[1](42U);
}
