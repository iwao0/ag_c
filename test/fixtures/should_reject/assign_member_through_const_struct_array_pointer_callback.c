// An indirect call returning an array pointer preserves record element constness.
struct Item {
  int value;
};

static const struct Item items[1] = {{1}};

static const struct Item (*get_items(void))[1] {
  return &items;
}

int main(void) {
  const struct Item (*(*callback)(void))[1] = get_items;
  (*callback())[0].value = 2;
  return (*callback())[0].value;
}
