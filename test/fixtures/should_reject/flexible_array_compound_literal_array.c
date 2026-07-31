struct value {
  int prefix;
  int data[];
};

// A compound literal cannot construct an array of flexible-array structures.
int main(void) {
  return ((struct value[2]){{1}, {2}})[0].prefix;
}
