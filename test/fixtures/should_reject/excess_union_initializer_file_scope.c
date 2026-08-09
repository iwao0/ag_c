/* A file-scope union initializer cannot select more than one member. */
union Item { int first; long second; };
union Item item = {1, 2};

int main(void) {
  return item.first;
}
