/* A file-scope object cannot change from external to internal linkage. */
int value;
static int value;

int main(void) {
  return 0;
}
