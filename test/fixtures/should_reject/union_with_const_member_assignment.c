// Every union member participates in the modifiable-lvalue constraint.
union Value {
  int integer;
  const double real;
};

int main(void) {
  union Value left = {.integer = 1};
  union Value right = {.integer = 2};
  left = right;
  return left.integer;
}
