/* A static object cannot be followed by a plain file-scope declaration. */
static int value;
int value;

int main(void) {
  return 0;
}
