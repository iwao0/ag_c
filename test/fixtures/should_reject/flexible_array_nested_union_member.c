struct inner {
  int prefix;
  int data[];
};

union holder {
  struct inner value;
  int fallback;
};

// A union containing such a structure cannot itself be a structure member.
struct outer {
  union holder value;
};

int main(void) {
  return 0;
}
