struct pair {
  int first;
  int second;
};

struct pair value = {.missing = 7};

int main(void) {
  return value.first;
}
