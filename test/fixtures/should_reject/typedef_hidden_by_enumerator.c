typedef int hidden_enumerator;

int main(void) {
  enum { hidden_enumerator = 3 };
  hidden_enumerator later_object = 4;
  return hidden_enumerator + later_object;
}
