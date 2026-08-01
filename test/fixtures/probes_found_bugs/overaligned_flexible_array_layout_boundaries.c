/*
 * A flexible array starts at its member offset even when the enclosing
 * structure has trailing padding from an over-aligned fixed prefix.
 * sizeof, alignment, fixed storage durations, and dynamically allocated
 * payload access must all agree on that layout.
 */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

struct aligned_u16_packet {
  unsigned char tag;
  _Alignas(16) unsigned long long sequence;
  unsigned short values[];
};

struct aligned_u64_packet {
  _Alignas(32) unsigned char tag;
  unsigned long long values[];
};

_Static_assert(_Alignof(struct aligned_u16_packet) == 16,
               "16-byte member alignment becomes record alignment");
_Static_assert(offsetof(struct aligned_u16_packet, sequence) == 16,
               "over-aligned fixed member offset");
_Static_assert(offsetof(struct aligned_u16_packet, values) == 24,
               "flexible member follows the fixed member");
_Static_assert(sizeof(struct aligned_u16_packet) == 32,
               "record size includes trailing alignment padding");

_Static_assert(_Alignof(struct aligned_u64_packet) == 32,
               "32-byte member alignment becomes record alignment");
_Static_assert(offsetof(struct aligned_u64_packet, values) == 8,
               "flexible element alignment is independent of record padding");
_Static_assert(sizeof(struct aligned_u64_packet) == 32,
               "large record alignment rounds the fixed prefix size");

static struct aligned_u16_packet global_u16 = {3, 101};
static struct aligned_u64_packet global_u64 = {5};

static uintptr_t align_up(uintptr_t value, size_t alignment) {
  uintptr_t mask = (uintptr_t)alignment - 1;
  return (value + mask) & ~mask;
}

static void *allocate_aligned_flexible(size_t fixed_size,
                                       size_t payload_size,
                                       size_t alignment,
                                       void **raw_allocation) {
  unsigned char *raw = malloc(fixed_size + payload_size + alignment - 1);
  assert(raw != NULL);
  *raw_allocation = raw;
  return (void *)align_up((uintptr_t)raw, alignment);
}

static unsigned int sum_u16(const struct aligned_u16_packet *packet,
                            size_t count) {
  unsigned int result = packet->tag + (unsigned int)packet->sequence;
  for (size_t i = 0; i < count; i++)
    result += packet->values[i];
  return result;
}

static unsigned long long sum_u64(const struct aligned_u64_packet *packet,
                                  size_t count) {
  unsigned long long result = packet->tag;
  for (size_t i = 0; i < count; i++)
    result += packet->values[i];
  return result;
}

static void verify_fixed_objects(void) {
  struct aligned_u16_packet local_u16 = {7, 103};
  struct aligned_u64_packet local_u64 = {11};
  static struct aligned_u16_packet static_u16 = {13, 107};
  static struct aligned_u64_packet static_u64 = {17};

  assert((uintptr_t)&global_u16 % 16 == 0);
  assert((uintptr_t)&local_u16 % 16 == 0);
  assert((uintptr_t)&static_u16 % 16 == 0);
  assert((uintptr_t)&global_u64 % 32 == 0);
  assert((uintptr_t)&local_u64 % 32 == 0);
  assert((uintptr_t)&static_u64 % 32 == 0);

  assert(global_u16.tag == 3 && global_u16.sequence == 101);
  assert(local_u16.tag == 7 && local_u16.sequence == 103);
  assert(static_u16.tag == 13 && static_u16.sequence == 107);
  assert(global_u64.tag == 5);
  assert(local_u64.tag == 11);
  assert(static_u64.tag == 17);
}

static void verify_u16_payload(void) {
  enum { value_count = 5 };
  void *raw = NULL;
  struct aligned_u16_packet *packet = allocate_aligned_flexible(
      sizeof(*packet), (size_t)value_count * sizeof(packet->values[0]),
      _Alignof(struct aligned_u16_packet), &raw);

  assert((uintptr_t)packet % (uintptr_t)16 == (uintptr_t)0);
  assert((unsigned char *)&packet->values[0] ==
         (unsigned char *)packet +
             offsetof(struct aligned_u16_packet, values));
  packet->tag = 19;
  packet->sequence = 109;
  for (size_t i = 0; i < (size_t)value_count; i++)
    packet->values[i] = (unsigned short)(i * 3 + 1);
  assert(sum_u16(packet, value_count) ==
         19U + 109U + 1U + 4U + 7U + 10U + 13U);
  free(raw);
}

static void verify_u64_payload(void) {
  enum { value_count = 4 };
  void *raw = NULL;
  struct aligned_u64_packet *packet = allocate_aligned_flexible(
      sizeof(*packet), (size_t)value_count * sizeof(packet->values[0]),
      _Alignof(struct aligned_u64_packet), &raw);

  assert((uintptr_t)packet % (uintptr_t)32 == (uintptr_t)0);
  assert((unsigned char *)&packet->values[0] ==
         (unsigned char *)packet +
             offsetof(struct aligned_u64_packet, values));
  packet->tag = 23;
  for (size_t i = 0; i < (size_t)value_count; i++)
    packet->values[i] = (unsigned long long)(i + 2) * 1000000001ULL;
  assert(sum_u64(packet, value_count) ==
         23ULL + 2ULL * 1000000001ULL + 3ULL * 1000000001ULL +
             4ULL * 1000000001ULL + 5ULL * 1000000001ULL);
  free(raw);
}

int main(void) {
  verify_fixed_objects();
  verify_u16_payload();
  verify_u64_payload();
  return 0;
}
