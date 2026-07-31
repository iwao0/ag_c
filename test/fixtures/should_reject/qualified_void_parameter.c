/* A void parameter marker cannot be qualified. */
int function(const void);

int main(void) {
  return 0;
}
