/* Repeated nested braces do not form a valid scalar initializer. */
int value = {{{7}}};

int main(void) {
  return value;
}
