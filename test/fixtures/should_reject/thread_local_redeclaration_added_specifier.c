/* A non-thread-local object cannot later be redeclared as _Thread_local. */
extern int value;
extern _Thread_local int value;

int main(void) {
  return 0;
}
