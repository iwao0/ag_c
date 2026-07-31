/* A compound literal cannot have a variable length array type. */
int main(int argc, char **argv) {
  (void)argv;
  int count = argc + 1;
  (void)(int[count]){1};
  return 0;
}
