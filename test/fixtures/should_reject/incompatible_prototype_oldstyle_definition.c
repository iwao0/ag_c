/* An empty old-style definition is incompatible with a one-parameter prototype. */
int function(char value);

int function() {
  return 0;
}

int main(void) {
  return function(1);
}
