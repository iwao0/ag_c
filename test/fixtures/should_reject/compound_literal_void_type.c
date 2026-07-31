/* A compound literal cannot have void type. */
int main(void) {
  (void)(void){0};
  return 0;
}
