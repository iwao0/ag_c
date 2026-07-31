/*
 * Keep preprocessing, typedef-name classification, recursive declarators,
 * parameter adjustment, VLA bounds, and Atomic function-pointer types on one
 * execution path.  Each declaration is emitted by a macro rather than merely
 * using a macro-expanded initializer.
 */
#define CAT_IMPL(left, right) left##right
#define CAT(left, right) CAT_IMPL(left, right)

#define DECL_FACTORY(suffix, result_type, argument_type) \
  result_type (*CAT(make_, suffix)(void))(argument_type)
#define DECL_CALLBACK_TABLE(name, result_type, argument_type, count) \
  result_type (*(*CAT(make_, name)(void))[count])(argument_type)
#define DECL_SUM(name, element_type, count) \
  int name(element_type values[static count])
#define DECL_MATRIX_SUM(name, element_type) \
  int name(int rows, int width, \
           element_type matrix[static rows][width], \
           element_type (*selected)[width])
#define ATOMIC(type) _Atomic(type)
#define DECL_ATOMIC_CONSUMER(name, result_type, parameter_type) \
  int name(result_type (*callback)(ATOMIC(parameter_type)), \
           ATOMIC(parameter_type) value)
#define FUNCTION_TYPE(result_type, argument_type) \
  result_type (*)(argument_type)
#define FUNCTION_DECL(result_type, name, argument_type) \
  result_type (*name)(argument_type)
#define ATOMIC_FUNCTION(result_type, argument_type) \
  _Atomic(FUNCTION_TYPE(result_type, argument_type))
#define TYPE(index) CAT(callback_type_, index)
#define DECL_CALLBACK_TYPE(index, result_type, argument_type) \
  typedef result_type (*TYPE(index))(argument_type)

static int add_one(int value) { return value + 1; }
static int add_ten(int value) { return value + 10; }
static int add_twelve(int value) { return value + 12; }
static int add_two_atomic(ATOMIC(int) value) { return value + 2; }
static int add_three(int value) { return value + 3; }
static long widen(short value) { return (long)value + 40; }

DECL_FACTORY(unary, int, int) { return add_one; }

static int (*callback_table[2])(int) = {add_ten, add_twelve};
DECL_CALLBACK_TABLE(callbacks, int, int, 2) { return &callback_table; }

DECL_SUM(sum_three, const int, 3) {
  return values[0] + values[1] + values[2];
}

DECL_MATRIX_SUM(sum_selected_row, int) {
  int total = 0;
  for (int column = 0; column < width; ++column) {
    total += (*selected)[column];
  }
  return total + (selected == matrix + rows - 1 ? 0 : 1);
}

DECL_ATOMIC_CONSUMER(apply_atomic, int, int) { return callback(value); }

static ATOMIC_FUNCTION(int, int) atomic_callback = add_three;

DECL_CALLBACK_TYPE(7, long, short);

_Static_assert(_Generic(&make_unary,
                        int (*(*)(void))(int): 1,
                        default: 0),
               "factory declarator type");
_Static_assert(_Generic(&make_callbacks,
                        int (*(*(*)(void))[2])(int): 1,
                        default: 0),
               "callback array factory declarator type");
_Static_assert(_Generic(&sum_three,
                        int (*)(const int *): 1,
                        default: 0),
               "array parameter adjustment");
_Static_assert(_Generic(&apply_atomic,
                        int (*)(int (*)(_Atomic(int)), _Atomic(int)): 1,
                        default: 0),
               "nested Atomic callback type");
_Static_assert(_Generic(atomic_callback,
                        int (*)(int): 1,
                        default: 0),
               "Atomic function-pointer lvalue conversion");
_Static_assert(_Generic((TYPE(7))0,
                        long (*)(short): 1,
                        default: 0),
               "pasted typedef declarator type");

int main(void) {
  int (*(*table)[2])(int) = make_callbacks();
  int values[3] = {10, 14, 18};
  int matrix[2][3] = {{1, 2, 3}, {11, 13, 18}};
  FUNCTION_DECL(int, loaded, int) = atomic_callback;
  TYPE(7) widened = widen;

  if (make_unary()(41) != 42) return 1;
  if ((*table)[0](10) + (*table)[1](10) != 42) return 2;
  if (sum_three(values) != 42) return 3;
  if (sum_selected_row(2, 3, matrix, &matrix[1]) != 42) return 4;
  if (apply_atomic(add_two_atomic, 40) != 42) return 5;
  if (loaded(39) != 42) return 6;
  if (widened(2) != 42) return 7;
  return 0;
}
