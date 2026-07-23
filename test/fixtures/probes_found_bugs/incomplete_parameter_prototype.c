struct record;
union choice;

typedef int record_reader(struct record value);

static record_reader read_record;
static int read_choice(union choice value);

struct record {
  int value;
};

union choice {
  int value;
  long other;
};

static int read_record(struct record value) {
  return value.value;
}

static int read_choice(union choice value) {
  return value.value;
}

int main(void) {
  struct record record = {20};
  union choice choice = {.value = 22};
  return read_record(record) + read_choice(choice) != 42;
}
