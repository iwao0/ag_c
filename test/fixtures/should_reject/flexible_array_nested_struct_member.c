struct inner {
  int prefix;
  int data[];
};

// A structure containing a flexible array cannot be a structure member.
struct outer {
  struct inner value;
};

int main(void) {
  return 0;
}
