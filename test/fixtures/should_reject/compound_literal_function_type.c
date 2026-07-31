/* A compound literal cannot have function type. */
int main(void) {
  (void)(int(void)){0};
  return 0;
}
