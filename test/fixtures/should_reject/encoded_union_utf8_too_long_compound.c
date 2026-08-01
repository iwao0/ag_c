/* A union compound literal cannot truncate a four-byte UTF-8 character. */
union value {
  unsigned long long raw;
  char text[3];
};

int main(void) {
  union value *instance =
      &(union value){.text = u8"\U0001F600"};
  return instance->text[0];
}
