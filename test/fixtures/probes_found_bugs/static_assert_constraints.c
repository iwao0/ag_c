enum truth {
  TRUE_VALUE = 1
};

_Static_assert(-1, "negative integers are true");
_Static_assert(TRUE_VALUE, "enum constant");
_Static_assert((int)1.9 == 1, "floating constant cast");
_Static_assert((unsigned char)255.9 == 255, "unsigned cast");
_Static_assert((_Bool)0.0 == 0, "false conversion");
_Static_assert((_Bool)0.5 == 1, "true conversion");
_Static_assert(sizeof(int[3]) == 12, "array size");
_Static_assert(1, "concatenated " "message");
_Static_assert(1, L"wide message");

struct holder {
  _Static_assert(sizeof(int) == 4, "member declaration");
  int value;
};

union choice {
  _Static_assert(sizeof(int) == 4, "member declaration");
  int value;
};

int main(void) {
  _Static_assert(sizeof(struct holder) == sizeof(int),
                 "block declaration");
  return 0;
}
