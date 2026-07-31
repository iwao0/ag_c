/* A function with external linkage cannot be redeclared with internal linkage. */
extern int function(void);
static int function(void);

int main(void) {
  return 0;
}
