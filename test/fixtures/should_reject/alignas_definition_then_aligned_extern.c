/* A later aligned declaration cannot strengthen an already defined object. */
int value = 1;
_Alignas(16) extern int value;

int main(void) {
  return value;
}
