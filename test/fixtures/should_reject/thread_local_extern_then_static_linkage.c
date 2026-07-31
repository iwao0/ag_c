/* A thread-local object cannot change from external to internal linkage. */
extern _Thread_local int value;
static _Thread_local int value;

int main(void) {
  return 0;
}
