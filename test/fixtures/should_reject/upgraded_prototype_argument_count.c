/* Once a prototype is visible, a call must provide its required argument. */
int function();
int function(int value);

int main(void) {
  return function();
}

int function(int value) {
  return value;
}
