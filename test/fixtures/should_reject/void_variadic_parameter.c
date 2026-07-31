/* A void parameter marker cannot precede an ellipsis. */
int function(void, ...);

int main(void) {
  return 0;
}
