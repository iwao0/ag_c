/* An automatic union initializer cannot select more than one member. */
union Item { int first; long second; };

int main(void) {
  union Item item = {1, 2};
  return item.first;
}
