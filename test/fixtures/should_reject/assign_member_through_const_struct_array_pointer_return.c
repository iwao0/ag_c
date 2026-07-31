// A returned pointer to an array preserves the const qualification of its records.
struct Item {
  int value;
};

static const struct Item items[1] = {{1}};

static const struct Item (*get_items(void))[1] {
  return &items;
}

int main(void) {
  (*get_items())[0].value = 2;
  return (*get_items())[0].value;
}
