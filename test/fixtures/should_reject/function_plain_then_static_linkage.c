/* A plain function declaration cannot be followed by a static declaration. */
int function(void);
static int function(void);

int main(void) {
  return 0;
}
