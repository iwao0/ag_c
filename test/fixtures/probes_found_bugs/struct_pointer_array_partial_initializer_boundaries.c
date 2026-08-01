/*
 * A string literal initializes the leading pointer member, not the following
 * character array. Every omitted array element must still be zero-initialized
 * for static and automatic aggregate objects.
 */
#include <assert.h>
#include <string.h>

struct Narrow {
  const char *data;
  char bytes[3];
};

struct Wide {
  const char *data;
  int words[2];
};

struct Bundle {
  struct Narrow narrow_items[2];
  struct Wide wide_item;
};

static struct Narrow global_narrow = {"global"};
static struct Wide global_wide = {"wide"};
static struct Narrow global_full = {"full", {'x', 'y', 'z'}};
static struct Bundle global_bundle = {
    .narrow_items = {{"first"}, {"second", {'s'}}},
    .wide_item = {"third"},
};

static void check_static_local(void) {
  static struct Narrow narrow = {"static"};
  static struct Wide wide = {"static-wide", {40}};

  assert(strcmp(narrow.data, "static") == 0);
  assert(narrow.bytes[0] == 0);
  assert(narrow.bytes[1] == 0);
  assert(narrow.bytes[2] == 0);
  assert(strcmp(wide.data, "static-wide") == 0);
  assert(wide.words[0] == 40);
  assert(wide.words[1] == 0);
}

int main(void) {
  struct Narrow automatic_narrow = {"automatic"};
  struct Wide automatic_wide = {"automatic-wide"};
  struct Narrow compound = (struct Narrow){"compound"};
  struct Bundle local_bundle = {
      .narrow_items = {{"left"}, {"right", {'r', 's'}}},
      .wide_item = {"tail", {41}},
  };

  assert(strcmp(global_narrow.data, "global") == 0);
  assert(global_narrow.bytes[0] == 0);
  assert(global_narrow.bytes[1] == 0);
  assert(global_narrow.bytes[2] == 0);
  assert(strcmp(global_wide.data, "wide") == 0);
  assert(global_wide.words[0] == 0);
  assert(global_wide.words[1] == 0);
  assert(global_full.bytes[0] == 'x');
  assert(global_full.bytes[1] == 'y');
  assert(global_full.bytes[2] == 'z');

  assert(strcmp(global_bundle.narrow_items[0].data, "first") == 0);
  assert(global_bundle.narrow_items[0].bytes[2] == 0);
  assert(global_bundle.narrow_items[1].bytes[0] == 's');
  assert(global_bundle.narrow_items[1].bytes[1] == 0);
  assert(strcmp(global_bundle.wide_item.data, "third") == 0);
  assert(global_bundle.wide_item.words[0] == 0);
  assert(global_bundle.wide_item.words[1] == 0);

  assert(strcmp(automatic_narrow.data, "automatic") == 0);
  assert(automatic_narrow.bytes[0] == 0);
  assert(automatic_narrow.bytes[1] == 0);
  assert(automatic_narrow.bytes[2] == 0);
  assert(strcmp(automatic_wide.data, "automatic-wide") == 0);
  assert(automatic_wide.words[0] == 0);
  assert(automatic_wide.words[1] == 0);
  assert(strcmp(compound.data, "compound") == 0);
  assert(compound.bytes[0] == 0);
  assert(compound.bytes[1] == 0);
  assert(compound.bytes[2] == 0);

  assert(strcmp(local_bundle.narrow_items[0].data, "left") == 0);
  assert(local_bundle.narrow_items[0].bytes[0] == 0);
  assert(local_bundle.narrow_items[1].bytes[0] == 'r');
  assert(local_bundle.narrow_items[1].bytes[1] == 's');
  assert(local_bundle.narrow_items[1].bytes[2] == 0);
  assert(strcmp(local_bundle.wide_item.data, "tail") == 0);
  assert(local_bundle.wide_item.words[0] == 41);
  assert(local_bundle.wide_item.words[1] == 0);

  check_static_local();
  return 0;
}
