/* A typedef name alone cannot declare an anonymous structure member. */
typedef struct {
  int value;
} Inner;

struct Outer {
  Inner;
};

int main(void) { return 0; }
