// The factory result callback's aggregate result keeps explicit member
// alignment metadata even when it does not change the physical layout.
struct function_return_vla_alignment_cell {
  _Alignas(64) unsigned long long value;
};

struct function_return_vla_alignment_result {
  _Alignas(64) unsigned long long sum;
  unsigned long long count;
};

_Static_assert(
    _Alignof(struct function_return_vla_alignment_result) == 64,
    "factory callback result alignment");
_Static_assert(
    sizeof(struct function_return_vla_alignment_result) == 64,
    "factory callback result size");

typedef struct function_return_vla_alignment_result
    function_return_vla_alignment_callback_t(
        int rows, int columns,
        const struct function_return_vla_alignment_cell
            input[static restrict 1][*]);

function_return_vla_alignment_callback_t
    *function_return_vla_callback_result_alignment_presence_mismatch(void);

int main(void) {
  struct function_return_vla_alignment_cell input[1][1] = {{{42}}};
  function_return_vla_alignment_callback_t *callback =
      function_return_vla_callback_result_alignment_presence_mismatch();
  struct function_return_vla_alignment_result result =
      callback(1, 1, input);
  return result.sum == 42ULL && result.count == 1ULL ? 0 : 1;
}
