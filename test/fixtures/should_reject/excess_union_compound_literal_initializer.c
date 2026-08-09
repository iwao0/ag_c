/* A union compound literal initializer cannot select more than one member. */
union Item { int first; long second; };

int main(void) {
  return ((union Item){1, 2}).first;
}
