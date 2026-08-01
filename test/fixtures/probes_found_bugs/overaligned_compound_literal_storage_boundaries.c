// Over-aligned compound literals must preserve their type alignment in both
// static and automatic storage, including array elements and call temporaries.
// Expected: exit=0
#include <assert.h>
#include <stdint.h>

struct aligned32_value {
  _Alignas(32) unsigned char tag;
  long payload[2];
};

union aligned64_value {
  _Alignas(64) struct {
    long first;
    long second;
    long third;
  } words;
  unsigned char bytes[64];
};

struct aligned32_box {
  unsigned char before;
  struct aligned32_value value;
  unsigned char after;
};

_Static_assert(_Alignof(struct aligned32_value) == 32,
               "compound struct alignment");
_Static_assert(_Alignof(union aligned64_value) == 64,
               "compound union alignment");
_Static_assert(_Alignof(struct aligned32_value[2]) == 32,
               "compound array alignment");
_Static_assert(_Alignof(struct aligned32_box) == 32,
               "nested compound alignment");

static struct aligned32_value *global_struct =
    &(struct aligned32_value){17, {19, 23}};
static union aligned64_value *global_union =
    &(union aligned64_value){.words = {29, 31, 37}};
static struct aligned32_value (*global_array)[2] =
    &(struct aligned32_value[2]){
        {41, {43, 47}},
        {53, {59, 61}},
    };
static struct aligned32_box *global_box =
    &(struct aligned32_box){
        .before = 0x5a,
        .value = {67, {71, 73}},
        .after = 0xa5,
    };

static int is_aligned(const void *pointer, uintptr_t alignment) {
  return (uintptr_t)pointer % alignment == 0;
}

static int check32(struct aligned32_value value,
                   unsigned char tag, long first, long second) {
  return is_aligned(&value, _Alignof(struct aligned32_value)) &&
         value.tag == tag && value.payload[0] == first &&
         value.payload[1] == second;
}

static int check64(union aligned64_value value,
                   long first, long second, long third) {
  return is_aligned(&value, _Alignof(union aligned64_value)) &&
         value.words.first == first && value.words.second == second &&
         value.words.third == third;
}

typedef int check32_fn(struct aligned32_value, unsigned char, long, long);
typedef int check64_fn(union aligned64_value, long, long, long);

static void verify_file_scope_literals(void) {
  assert(is_aligned(global_struct, _Alignof(struct aligned32_value)));
  assert(is_aligned(global_union, _Alignof(union aligned64_value)));
  assert(is_aligned(global_array, _Alignof(struct aligned32_value[2])));
  assert(is_aligned(&(*global_array)[0], _Alignof(struct aligned32_value)));
  assert(is_aligned(&(*global_array)[1], _Alignof(struct aligned32_value)));
  assert((uintptr_t)&(*global_array)[1] - (uintptr_t)&(*global_array)[0] ==
         sizeof(struct aligned32_value));
  assert(is_aligned(global_box, _Alignof(struct aligned32_box)));
  assert(is_aligned(&global_box->value, _Alignof(struct aligned32_value)));

  assert(check32(*global_struct, 17, 19, 23));
  assert(check64(*global_union, 29, 31, 37));
  assert(check32((*global_array)[0], 41, 43, 47));
  assert(check32((*global_array)[1], 53, 59, 61));
  assert(global_box->before == 0x5a && global_box->after == 0xa5);
  assert(check32(global_box->value, 67, 71, 73));
}

static void verify_block_scope_literals(void) {
  struct aligned32_value *local_struct =
      &(struct aligned32_value){79, {83, 89}};
  union aligned64_value *local_union =
      &(union aligned64_value){.words = {97, 101, 103}};
  struct aligned32_value *local_array =
      (struct aligned32_value[2]){
          {107, {109, 113}},
          {127, {131, 137}},
      };
  struct aligned32_box *local_box =
      &(struct aligned32_box){
          .before = 0x3c,
          .value = {139, {149, 151}},
          .after = 0xc3,
      };

  assert(is_aligned(local_struct, _Alignof(struct aligned32_value)));
  assert(is_aligned(local_union, _Alignof(union aligned64_value)));
  assert(is_aligned(local_array, _Alignof(struct aligned32_value)));
  assert(is_aligned(&local_array[1], _Alignof(struct aligned32_value)));
  assert(is_aligned(local_box, _Alignof(struct aligned32_box)));
  assert(is_aligned(&local_box->value, _Alignof(struct aligned32_value)));
  assert(check32(*local_struct, 79, 83, 89));
  assert(check64(*local_union, 97, 101, 103));
  assert(check32(local_array[0], 107, 109, 113));
  assert(check32(local_array[1], 127, 131, 137));
  assert(local_box->before == 0x3c && local_box->after == 0xc3);
  assert(check32(local_box->value, 139, 149, 151));

  check32_fn *indirect32 = check32;
  check64_fn *indirect64 = check64;
  assert(check32((struct aligned32_value){157, {163, 167}},
                 157, 163, 167));
  assert(indirect32((struct aligned32_value){173, {179, 181}},
                    173, 179, 181));
  assert(check64((union aligned64_value){.words = {191, 193, 197}},
                 191, 193, 197));
  assert(indirect64((union aligned64_value){.words = {199, 211, 223}},
                    199, 211, 223));
}

static void verify_reentry_identity(void) {
  struct aligned32_value *first = 0;
  struct aligned32_value *current = 0;
  int iteration = 0;

again:
  current = &(struct aligned32_value){
      (unsigned char)(227 + iteration),
      {229 + iteration, 233 + iteration},
  };
  assert(is_aligned(current, _Alignof(struct aligned32_value)));
  if (iteration == 0) {
    first = current;
    iteration = 1;
    goto again;
  }
  assert(current == first);
  assert(check32(*current, 228, 230, 234));
}

int main(void) {
  verify_file_scope_literals();
  verify_block_scope_literals();
  verify_reentry_identity();
  return 0;
}
