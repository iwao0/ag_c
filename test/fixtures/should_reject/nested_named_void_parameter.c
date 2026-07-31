/* The unnamed 'void' parameter rule also applies to nested function types. */
int function(int (*callback)(void value));

int main(void) {
  return 0;
}
