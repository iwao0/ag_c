/* A typedef name alone cannot declare an anonymous union member. */
typedef union {
  int integer;
  float real;
} Inner;

struct Outer {
  Inner;
};

int main(void) { return 0; }
