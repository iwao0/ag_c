// Incompatible definition: count has an explicit natural alignment specifier.
// Size, alignment, member offsets, and the lowered hidden-pointer ABI remain
// identical to the declaration-side result type.
struct function_return_vla_alignment_cell {
  _Alignas(64) unsigned long long value;
};

struct function_return_vla_alignment_result {
  _Alignas(64) unsigned long long sum;
  _Alignas(8) unsigned long long count;
};

_Static_assert(
    _Alignof(struct function_return_vla_alignment_result) == 64,
    "factory callback definition result alignment");
_Static_assert(
    sizeof(struct function_return_vla_alignment_result) == 64,
    "factory callback definition result size");

typedef struct function_return_vla_alignment_result
    function_return_vla_alignment_callback_t(
        int rows, int columns,
        const struct function_return_vla_alignment_cell
            input[static restrict 1][*]);

static struct function_return_vla_alignment_result
read_function_return_vla_alignment_callback(
    int rows, int columns,
    const struct function_return_vla_alignment_cell
        (*restrict input)[columns]) {
  struct function_return_vla_alignment_result result = {0, 0};
  for (int row = 0; row < rows; row++) {
    for (int column = 0; column < columns; column++) {
      result.sum += input[row][column].value;
      result.count++;
    }
  }
  return result;
}

function_return_vla_alignment_callback_t
    *function_return_vla_callback_result_alignment_presence_mismatch(void) {
  return read_function_return_vla_alignment_callback;
}
