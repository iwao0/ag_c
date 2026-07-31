/* An object with external linkage cannot be redeclared with internal linkage. */
extern int value;
static int value;

int main(void) {
  return 0;
}
