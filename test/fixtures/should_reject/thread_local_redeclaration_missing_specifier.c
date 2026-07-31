/* Every declaration of a thread-local object must use _Thread_local. */
_Thread_local int value;
extern int value;

int main(void) {
  return 0;
}
