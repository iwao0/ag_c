// Simultaneously active recursive frames must own distinct, correctly aligned
// VLA storage and preserve every ancestor frame's contents.
// Expected: exit=0.
#include <assert.h>
#include <stdint.h>

#define FRAME_COUNT 12

struct aligned64_vla_cell {
  _Alignas(64) unsigned long long value;
  unsigned int tag;
};

_Static_assert(_Alignof(struct aligned64_vla_cell) == 64,
               "recursive VLA element alignment");
_Static_assert(sizeof(struct aligned64_vla_cell) == 64,
               "recursive VLA element stride");

static struct aligned64_vla_cell *active_cells[FRAME_COUNT];
static unsigned char *active_bytes[FRAME_COUNT];
static int active_cell_counts[FRAME_COUNT];
static int active_byte_counts[FRAME_COUNT];

static int is_aligned_recursive_vla(
    const void *pointer, uintptr_t alignment) {
  return (uintptr_t)pointer % alignment == 0;
}

static int ranges_are_disjoint(
    const void *left, unsigned long left_size,
    const void *right, unsigned long right_size) {
  uintptr_t left_begin = (uintptr_t)left;
  uintptr_t left_end = left_begin + left_size;
  uintptr_t right_begin = (uintptr_t)right;
  uintptr_t right_end = right_begin + right_size;
  return left_end <= right_begin || right_end <= left_begin;
}

static void verify_recursive_vla_frame(int depth) {
  int cell_count = active_cell_counts[depth];
  int byte_count = active_byte_counts[depth];
  struct aligned64_vla_cell *cells = active_cells[depth];
  unsigned char *bytes = active_bytes[depth];

  assert(is_aligned_recursive_vla(cells, 64));
  assert(is_aligned_recursive_vla(bytes, 128));
  for (int index = 0; index < cell_count; index++) {
    assert(is_aligned_recursive_vla(&cells[index], 64));
    assert(cells[index].value ==
           1000ULL + (unsigned long long)depth * 100ULL +
               (unsigned long long)index * 3ULL);
    assert(cells[index].tag == (unsigned int)(depth + index + 5));
  }
  for (int index = 0; index < byte_count; index++) {
    assert(bytes[index] ==
           (unsigned char)(depth * 17 + index * 5 + 1));
  }
}

static unsigned long long recurse_overaligned_vlas(int depth) {
  int cell_count = depth % 5 + 1;
  int byte_count = cell_count * 7 + 3;
  unsigned long long before = 0x1122334455667788ULL +
                              (unsigned long long)depth;
  struct aligned64_vla_cell cells[cell_count];
  _Alignas(128) unsigned char bytes[byte_count];
  unsigned long long after = 0x8877665544332211ULL -
                             (unsigned long long)depth;

  assert(is_aligned_recursive_vla(cells, 64));
  assert(is_aligned_recursive_vla(bytes, 128));
  assert(sizeof(cells) ==
         (unsigned long)cell_count * sizeof(struct aligned64_vla_cell));
  assert(sizeof(bytes) == (unsigned long)byte_count);

  for (int ancestor = 0; ancestor < depth; ancestor++) {
    unsigned long ancestor_cell_size =
        (unsigned long)active_cell_counts[ancestor] *
        sizeof(struct aligned64_vla_cell);
    unsigned long ancestor_byte_size =
        (unsigned long)active_byte_counts[ancestor];
    assert(ranges_are_disjoint(
        cells, sizeof(cells),
        active_cells[ancestor], ancestor_cell_size));
    assert(ranges_are_disjoint(
        cells, sizeof(cells),
        active_bytes[ancestor], ancestor_byte_size));
    assert(ranges_are_disjoint(
        bytes, sizeof(bytes),
        active_cells[ancestor], ancestor_cell_size));
    assert(ranges_are_disjoint(
        bytes, sizeof(bytes),
        active_bytes[ancestor], ancestor_byte_size));
  }

  for (int index = 0; index < cell_count; index++) {
    cells[index].value =
        1000ULL + (unsigned long long)depth * 100ULL +
        (unsigned long long)index * 3ULL;
    cells[index].tag = (unsigned int)(depth + index + 5);
  }
  for (int index = 0; index < byte_count; index++) {
    bytes[index] = (unsigned char)(depth * 17 + index * 5 + 1);
  }

  active_cells[depth] = cells;
  active_bytes[depth] = bytes;
  active_cell_counts[depth] = cell_count;
  active_byte_counts[depth] = byte_count;
  for (int ancestor = 0; ancestor <= depth; ancestor++)
    verify_recursive_vla_frame(ancestor);

  unsigned long long total = cells[0].value + bytes[0];
  if (depth + 1 < FRAME_COUNT)
    total += recurse_overaligned_vlas(depth + 1);

  for (int ancestor = 0; ancestor <= depth; ancestor++)
    verify_recursive_vla_frame(ancestor);
  assert(before == 0x1122334455667788ULL +
                       (unsigned long long)depth);
  assert(after == 0x8877665544332211ULL -
                      (unsigned long long)depth);

  active_cells[depth] = 0;
  active_bytes[depth] = 0;
  active_cell_counts[depth] = 0;
  active_byte_counts[depth] = 0;
  return total;
}

int main(void) {
  assert(recurse_overaligned_vlas(0) == 19734ULL);
  for (int depth = 0; depth < FRAME_COUNT; depth++) {
    assert(active_cells[depth] == 0);
    assert(active_bytes[depth] == 0);
  }
  return 0;
}
