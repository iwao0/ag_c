struct value {
  int prefix;
  int data[];
};

// An array type cannot have a flexible-array structure as its element type.
struct value (*pointer)[2];

int main(void) {
  return 0;
}
