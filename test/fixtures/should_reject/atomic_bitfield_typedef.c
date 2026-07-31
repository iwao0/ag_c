/* A typedef does not make an atomic type valid for a bit-field. */
typedef _Atomic unsigned int atomic_uint;

struct flags {
  atomic_uint value : 3;
};

int main(void) {
  return 0;
}
