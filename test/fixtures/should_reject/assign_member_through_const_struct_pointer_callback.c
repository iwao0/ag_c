// An indirect call result retains the pointed-to record's const qualification.
struct Item {
  int value;
};

static const struct Item item = {1};

static const struct Item *get_item(void) {
  return &item;
}

int main(void) {
  const struct Item *(*callback)(void) = get_item;
  callback()->value = 2;
  return callback()->value;
}
