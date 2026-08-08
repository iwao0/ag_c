enum { declaration_value = 7 };

static int inspect_enumerator_scope(void) {
  enum {
    declaration_value = declaration_value + 1,
    following_value = declaration_value + 1
  };
  return declaration_value * 10 + following_value;
}

int main(void) {
  return inspect_enumerator_scope() != 89;
}
