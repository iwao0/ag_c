extern int *restrict global_restrict_pointer_qualifier_value;

int main(void) {
  return *global_restrict_pointer_qualifier_value;
}
