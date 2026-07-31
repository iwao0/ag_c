/* sizeof cannot be applied to a function designator. */
int function(void);

int main(void) {
  return (int)sizeof(function);
}
