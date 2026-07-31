#include <assert.h>

static struct static_tag;
extern union extern_tag;
auto struct auto_tag;
register union register_tag;
_Thread_local struct thread_tag;

struct static_tag { int value; };
union extern_tag { int value; };
struct auto_tag { int value; };
union register_tag { int value; };
struct thread_tag { int value; };

int main(void) {
  static struct block_static_tag;
  extern union block_extern_tag;
  auto struct block_auto_tag;
  register union block_register_tag;
  _Thread_local struct block_thread_tag;

  struct block_static_tag { int value; };
  union block_extern_tag { int value; };
  struct block_auto_tag { int value; };
  union block_register_tag { int value; };
  struct block_thread_tag { int value; };

  struct static_tag a = {1};
  union extern_tag b = {2};
  struct auto_tag c = {3};
  union register_tag d = {4};
  struct thread_tag e = {5};
  struct block_static_tag f = {6};
  union block_extern_tag g = {7};
  struct block_auto_tag h = {8};
  union block_register_tag i = {9};
  struct block_thread_tag j = {10};

  assert(a.value + b.value + c.value + d.value + e.value == 15);
  assert(f.value + g.value + h.value + i.value + j.value == 40);
  return 0;
}
