/* A member designator must name a member of the initialized aggregate. */
struct pair {
  int first;
  int second;
};

struct pair value = {.missing = 7};

int main(void) {
  return value.first;
}
