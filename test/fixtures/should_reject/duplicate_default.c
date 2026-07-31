/* A switch statement can contain at most one default label. */
int main(int argc, char **argv) {
  (void)argv;
  switch (argc) {
    default:
      return 1;
    default:
      return 2;
  }
}
