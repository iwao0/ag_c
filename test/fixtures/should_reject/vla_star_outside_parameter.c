/* A [*] VLA declarator is valid only in function prototype scope. */
int values[*];

int main(void) {
  return 0;
}
