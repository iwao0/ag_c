// continue, break, and goto edges leaving an over-aligned VLA scope must
// restore the pre-allocation stack checkpoint, including alignment padding.
// Expected: exit=0.
#include <assert.h>
#include <stdint.h>

#define EXIT_ITERATIONS 18000

struct aligned64_exit_cell {
  _Alignas(64) unsigned long long value;
  unsigned char tag;
};

_Static_assert(_Alignof(struct aligned64_exit_cell) == 64,
               "scope-exit VLA element alignment");
_Static_assert(sizeof(struct aligned64_exit_cell) == 64,
               "scope-exit VLA element stride");

static int is_aligned_scope_exit(
    const void *pointer, uintptr_t alignment) {
  return (uintptr_t)pointer % alignment == 0;
}

static unsigned long long use_scope_exit_vlas(
    int seed, int count,
    struct aligned64_exit_cell cells[static count],
    int byte_count, unsigned char bytes[static byte_count]) {
  assert(is_aligned_scope_exit(cells, 64));
  assert(is_aligned_scope_exit(bytes, 128));
  assert((uintptr_t)&cells[count - 1] - (uintptr_t)&cells[0] ==
         (uintptr_t)(count - 1) * sizeof(struct aligned64_exit_cell));

  cells[0].value = 1000ULL + (unsigned long long)seed;
  cells[0].tag = (unsigned char)(seed + 3);
  cells[count - 1].value = 2000ULL + (unsigned long long)seed;
  cells[count - 1].tag = (unsigned char)(seed + 5);
  bytes[0] = (unsigned char)(seed + 7);
  bytes[byte_count - 1] = (unsigned char)(seed + 11);

  assert(cells[0].value == 1000ULL + (unsigned long long)seed);
  assert(cells[0].tag == (unsigned char)(seed + 3));
  assert(cells[count - 1].value ==
         2000ULL + (unsigned long long)seed);
  assert(cells[count - 1].tag == (unsigned char)(seed + 5));
  assert(bytes[0] == (unsigned char)(seed + 7));
  assert(bytes[byte_count - 1] == (unsigned char)(seed + 11));
  return cells[0].value + cells[count - 1].value +
         bytes[0] + bytes[byte_count - 1];
}

static unsigned long long check_continue_exits(void) {
  unsigned long long before = 0x1122334455667788ULL;
  unsigned long long after = 0x8877665544332211ULL;
  unsigned long long checksum = 0;
  for (int iteration = 0; iteration < EXIT_ITERATIONS; iteration++) {
    int count = 65 + iteration % 4;
    int byte_count = count * 17 + 3;
    struct aligned64_exit_cell cells[count];
    _Alignas(128) unsigned char bytes[byte_count];
    checksum += use_scope_exit_vlas(
        iteration, count, cells, byte_count, bytes);
    assert(before == 0x1122334455667788ULL);
    assert(after == 0x8877665544332211ULL);
    continue;
  }
  assert(before == 0x1122334455667788ULL);
  assert(after == 0x8877665544332211ULL);
  return checksum;
}

static unsigned long long check_break_exits(void) {
  unsigned long long before = 0x2233445566778899ULL;
  unsigned long long after = 0x9988776655443322ULL;
  unsigned long long checksum = 0;
  for (int iteration = 0; iteration < EXIT_ITERATIONS; iteration++) {
    for (;;) {
      int count = 65 + iteration % 4;
      int byte_count = count * 17 + 3;
      struct aligned64_exit_cell cells[count];
      _Alignas(128) unsigned char bytes[byte_count];
      checksum += use_scope_exit_vlas(
          iteration + 19, count, cells, byte_count, bytes);
      assert(before == 0x2233445566778899ULL);
      assert(after == 0x9988776655443322ULL);
      break;
    }
    assert(before == 0x2233445566778899ULL);
    assert(after == 0x9988776655443322ULL);
  }
  return checksum;
}

static unsigned long long check_goto_exits(void) {
  unsigned long long before = 0x33445566778899aaULL;
  unsigned long long after = 0xaa99887766554433ULL;
  unsigned long long checksum = 0;
  for (int iteration = 0; iteration < EXIT_ITERATIONS; iteration++) {
    {
      int count = 65 + iteration % 4;
      int byte_count = count * 17 + 3;
      struct aligned64_exit_cell cells[count];
      _Alignas(128) unsigned char bytes[byte_count];
      checksum += use_scope_exit_vlas(
          iteration + 37, count, cells, byte_count, bytes);
      assert(before == 0x33445566778899aaULL);
      assert(after == 0xaa99887766554433ULL);
      goto after_vla_scope;
    }
  after_vla_scope:
    assert(before == 0x33445566778899aaULL);
    assert(after == 0xaa99887766554433ULL);
  }
  return checksum;
}

int main(void) {
  unsigned long long continued = check_continue_exits();
  unsigned long long broken = check_break_exits();
  unsigned long long jumped = check_goto_exits();
  assert(continued != 0);
  assert(broken > continued);
  assert(jumped > broken);
  return 0;
}
