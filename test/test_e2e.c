#include "test_common.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <signal.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

typedef enum {
  CASE_INT,
  CASE_FLOAT,
  CASE_DOUBLE,
  // `_FILE` バリアントは `input` をインライン C ソースではなく、
  // `test/fixtures/<...>` 配下のファイルパスとして解釈する。
  // 期待値の解釈 (INT/FLOAT/DOUBLE) は対応する非 _FILE 版と同じ。
  CASE_INT_FILE,
  CASE_FLOAT_FILE,
  CASE_DOUBLE_FILE,
} case_kind_t;

static inline bool case_kind_is_file(case_kind_t k) {
  return k == CASE_INT_FILE || k == CASE_FLOAT_FILE || k == CASE_DOUBLE_FILE;
}

// 比較ロジックに使う「値の種類」を返す。`_FILE` バリアントは対応する非 _FILE 版に正規化する。
static inline case_kind_t case_kind_value_kind(case_kind_t k) {
  switch (k) {
    case CASE_INT_FILE: return CASE_INT;
    case CASE_FLOAT_FILE: return CASE_FLOAT;
    case CASE_DOUBLE_FILE: return CASE_DOUBLE;
    default: return k;
  }
}

typedef struct {
  const char *category;
  const char *name;
  case_kind_t kind;
  const char *input;
  int expected_i;
  double expected_f;
} test_case_t;

typedef struct {
  const char *name;
  const char *input;
  const char *expected_diag;
} compile_fail_case_t;

static int log_file_contains_substr(const char *path, const char *needle);

static void build_artifact_paths(const test_case_t *tc, char *dir, char *s_path, char *bin_path, char *drv_path) {
  snprintf(dir, PATH_MAX, "build/e2e/%s", tc->category);
  snprintf(s_path, PATH_MAX, "%s/%s.s", dir, tc->name);
  snprintf(bin_path, PATH_MAX, "%s/%s", dir, tc->name);
  if (drv_path) {
    snprintf(drv_path, PATH_MAX, "%s/%s_driver.c", dir, tc->name);
  }
}

static void build_source_path(const test_case_t *tc, char *src_path) {
  snprintf(src_path, PATH_MAX, "build/e2e/%s/%s.c", tc->category, tc->name);
}

static const test_case_t test_cases[] = {
    {"integer", "zero", CASE_INT_FILE, "test/fixtures/integer/zero.c", 0, 0},
    {"integer", "literal", CASE_INT_FILE, "test/fixtures/integer/literal.c", 42, 0},
    {"integer", "hex_literal", CASE_INT_FILE, "test/fixtures/integer/hex_literal.c", 255, 0},
    {"integer", "oct_literal", CASE_INT_FILE, "test/fixtures/integer/oct_literal.c", 255, 0},
    {"integer", "bin_literal", CASE_INT_FILE, "test/fixtures/integer/bin_literal.c", 42, 0},
    {"integer", "suffix_LL_U", CASE_INT_FILE, "test/fixtures/integer/suffix_LL_U.c", 6, 0},

    {"arithmetic", "add_sub", CASE_INT_FILE, "test/fixtures/arithmetic/add_sub.c", 21, 0},
    {"arithmetic", "spaces", CASE_INT_FILE, "test/fixtures/arithmetic/spaces.c", 41, 0},
    {"arithmetic", "mul", CASE_INT_FILE, "test/fixtures/arithmetic/mul.c", 47, 0},
    {"arithmetic", "paren", CASE_INT_FILE, "test/fixtures/arithmetic/paren.c", 15, 0},
    {"arithmetic", "div", CASE_INT_FILE, "test/fixtures/arithmetic/div.c", 4, 0},
    {"arithmetic", "mod", CASE_INT_FILE, "test/fixtures/arithmetic/mod.c", 1, 0},
    {"arithmetic", "mod_prec", CASE_INT_FILE, "test/fixtures/arithmetic/mod_prec.c", 16, 0},
    {"arithmetic", "mod_neg_lhs", CASE_INT_FILE, "test/fixtures/arithmetic/mod_neg_lhs.c", 1, 0},
    {"arithmetic", "mod_neg_rhs", CASE_INT_FILE, "test/fixtures/arithmetic/mod_neg_rhs.c", 1, 0},
    {"arithmetic", "mod_zero_impl_defined", CASE_INT_FILE, "test/fixtures/arithmetic/mod_zero_impl_defined.c", 10, 0},
    {"arithmetic", "unary_plus", CASE_INT_FILE, "test/fixtures/arithmetic/unary_plus.c", 42, 0},
    {"arithmetic", "unary_minus", CASE_INT_FILE, "test/fixtures/arithmetic/unary_minus.c", 3, 0},
    {"arithmetic", "logical_not_true", CASE_INT_FILE, "test/fixtures/arithmetic/logical_not_true.c", 1, 0},
    {"arithmetic", "logical_not_false", CASE_INT_FILE, "test/fixtures/arithmetic/logical_not_false.c", 0, 0},
    {"arithmetic", "bit_not", CASE_INT_FILE, "test/fixtures/arithmetic/bit_not.c", 250, 0},
    {"arithmetic", "pre_inc", CASE_INT_FILE, "test/fixtures/arithmetic/pre_inc.c", 2, 0},
    {"arithmetic", "post_inc", CASE_INT_FILE, "test/fixtures/arithmetic/post_inc.c", 21, 0},
    {"arithmetic", "pre_dec", CASE_INT_FILE, "test/fixtures/arithmetic/pre_dec.c", 2, 0},
    {"arithmetic", "post_dec", CASE_INT_FILE, "test/fixtures/arithmetic/post_dec.c", 23, 0},
    {"arithmetic", "postinc_add", CASE_INT_FILE, "test/fixtures/arithmetic/postinc_add.c", 3, 0},
    {"arithmetic", "postdec_sub", CASE_INT_FILE, "test/fixtures/arithmetic/postdec_sub.c", 1, 0},
    {"arithmetic", "postinc_unary_plus", CASE_INT_FILE, "test/fixtures/arithmetic/postinc_unary_plus.c", 2, 0},
    {"arithmetic", "postdec_unary_minus", CASE_INT_FILE, "test/fixtures/arithmetic/postdec_unary_minus.c", 8, 0},
    {"arithmetic", "postinc_mul", CASE_INT_FILE, "test/fixtures/arithmetic/postinc_mul.c", 6, 0},
    {"arithmetic", "preinc_add", CASE_INT_FILE, "test/fixtures/arithmetic/preinc_add.c", 6, 0},
    {"arithmetic", "postinc_neg", CASE_INT_FILE, "test/fixtures/arithmetic/postinc_neg.c", 255, 0},
    {"arithmetic", "postinc_chain", CASE_INT_FILE, "test/fixtures/arithmetic/postinc_chain.c", 9, 0},
    {"arithmetic", "neg_postinc", CASE_INT_FILE, "test/fixtures/arithmetic/neg_postinc.c", 254, 0},
    {"arithmetic", "postinc_postdec_mix", CASE_INT_FILE, "test/fixtures/arithmetic/postinc_postdec_mix.c", 1, 0},
    {"arithmetic", "add_eq", CASE_INT_FILE, "test/fixtures/arithmetic/add_eq.c", 8, 0},
    {"arithmetic", "sub_eq", CASE_INT_FILE, "test/fixtures/arithmetic/sub_eq.c", 2, 0},
    {"arithmetic", "mul_eq", CASE_INT_FILE, "test/fixtures/arithmetic/mul_eq.c", 15, 0},
    {"arithmetic", "div_eq", CASE_INT_FILE, "test/fixtures/arithmetic/div_eq.c", 4, 0},
    {"arithmetic", "mod_eq", CASE_INT_FILE, "test/fixtures/arithmetic/mod_eq.c", 2, 0},
    {"arithmetic", "shl_eq", CASE_INT_FILE, "test/fixtures/arithmetic/shl_eq.c", 12, 0},
    {"arithmetic", "shr_eq", CASE_INT_FILE, "test/fixtures/arithmetic/shr_eq.c", 4, 0},
    {"arithmetic", "and_eq", CASE_INT_FILE, "test/fixtures/arithmetic/and_eq.c", 2, 0},
    {"arithmetic", "xor_eq", CASE_INT_FILE, "test/fixtures/arithmetic/xor_eq.c", 13, 0},
    {"arithmetic", "or_eq", CASE_INT_FILE, "test/fixtures/arithmetic/or_eq.c", 11, 0},
    {"arithmetic", "comma_basic", CASE_INT_FILE, "test/fixtures/arithmetic/comma_basic.c", 3, 0},
    {"arithmetic", "comma_chain", CASE_INT_FILE, "test/fixtures/arithmetic/comma_chain.c", 3, 0},

    {"comparison", "eq1", CASE_INT_FILE, "test/fixtures/comparison/eq1.c", 1, 0},
    {"comparison", "eq2", CASE_INT_FILE, "test/fixtures/comparison/eq2.c", 0, 0},
    {"comparison", "neq1", CASE_INT_FILE, "test/fixtures/comparison/neq1.c", 1, 0},
    {"comparison", "neq2", CASE_INT_FILE, "test/fixtures/comparison/neq2.c", 0, 0},
    {"comparison", "lt1", CASE_INT_FILE, "test/fixtures/comparison/lt1.c", 1, 0},
    {"comparison", "lt2", CASE_INT_FILE, "test/fixtures/comparison/lt2.c", 0, 0},
    {"comparison", "lt3", CASE_INT_FILE, "test/fixtures/comparison/lt3.c", 0, 0},
    {"comparison", "le1", CASE_INT_FILE, "test/fixtures/comparison/le1.c", 1, 0},
    {"comparison", "le2", CASE_INT_FILE, "test/fixtures/comparison/le2.c", 1, 0},
    {"comparison", "le3", CASE_INT_FILE, "test/fixtures/comparison/le3.c", 0, 0},
    {"comparison", "gt1", CASE_INT_FILE, "test/fixtures/comparison/gt1.c", 1, 0},
    {"comparison", "gt2", CASE_INT_FILE, "test/fixtures/comparison/gt2.c", 0, 0},
    {"comparison", "gt3", CASE_INT_FILE, "test/fixtures/comparison/gt3.c", 0, 0},
    {"comparison", "ge1", CASE_INT_FILE, "test/fixtures/comparison/ge1.c", 1, 0},
    {"comparison", "ge2", CASE_INT_FILE, "test/fixtures/comparison/ge2.c", 1, 0},
    {"comparison", "ge3", CASE_INT_FILE, "test/fixtures/comparison/ge3.c", 0, 0},
    {"comparison", "log_and", CASE_INT_FILE, "test/fixtures/comparison/log_and.c", 1, 0},
    {"comparison", "log_or", CASE_INT_FILE, "test/fixtures/comparison/log_or.c", 1, 0},
    {"comparison", "log_prec", CASE_INT_FILE, "test/fixtures/comparison/log_prec.c", 1, 0},
    {"comparison", "short_and", CASE_INT_FILE, "test/fixtures/comparison/short_and.c", 0, 0},
    {"comparison", "short_or", CASE_INT_FILE, "test/fixtures/comparison/short_or.c", 2, 0},
    {"comparison", "short_and_sideeffect", CASE_INT_FILE, "test/fixtures/comparison/short_and_sideeffect.c", 100, 0},
    {"comparison", "short_or_sideeffect", CASE_INT_FILE, "test/fixtures/comparison/short_or_sideeffect.c", 11, 0},
    {"comparison", "ternary_true", CASE_INT_FILE, "test/fixtures/comparison/ternary_true.c", 10, 0},
    {"comparison", "ternary_false", CASE_INT_FILE, "test/fixtures/comparison/ternary_false.c", 20, 0},
    {"comparison", "ternary_nested", CASE_INT_FILE, "test/fixtures/comparison/ternary_nested.c", 2, 0},
    {"comparison", "ternary_deep_nest", CASE_INT_FILE, "test/fixtures/comparison/ternary_deep_nest.c", 3, 0},
    {"comparison", "ternary_chain", CASE_INT_FILE, "test/fixtures/comparison/ternary_chain.c", 3, 0},
    {"local_variables", "basic", CASE_INT_FILE, "test/fixtures/local_variables/basic.c", 3, 0},
    {"local_variables", "expr", CASE_INT_FILE, "test/fixtures/local_variables/expr.c", 14, 0},
    {"local_variables", "sum3", CASE_INT_FILE, "test/fixtures/local_variables/sum3.c", 6, 0},
    {"local_variables", "mul2", CASE_INT_FILE, "test/fixtures/local_variables/mul2.c", 10, 0},
    {"local_variables", "copy", CASE_INT_FILE, "test/fixtures/local_variables/copy.c", 1, 0},
    {"local_variables", "static_counter", CASE_INT_FILE, "test/fixtures/local_variables/static_counter.c", 3, 0},
    {"local_variables", "static_separate_funcs", CASE_INT_FILE, "test/fixtures/local_variables/static_separate_funcs.c", 204, 0},

    {"if_else", "if_true", CASE_INT_FILE, "test/fixtures/if_else/if_true.c", 3, 0},
    {"if_else", "if_false", CASE_INT_FILE, "test/fixtures/if_else/if_false.c", 0, 0},
    {"if_else", "branch1", CASE_INT_FILE, "test/fixtures/if_else/branch1.c", 5, 0},
    {"if_else", "branch2", CASE_INT_FILE, "test/fixtures/if_else/branch2.c", 10, 0},
    {"if_else", "literal1", CASE_INT_FILE, "test/fixtures/if_else/literal1.c", 2, 0},
    {"if_else", "literal0", CASE_INT_FILE, "test/fixtures/if_else/literal0.c", 3, 0},
    {"if_else", "fallthrough", CASE_INT_FILE, "test/fixtures/if_else/fallthrough.c", 42, 0},

    {"while", "count", CASE_INT_FILE, "test/fixtures/while/count.c", 10, 0},
    {"while", "zero", CASE_INT_FILE, "test/fixtures/while/zero.c", 0, 0},
    {"while", "do_once", CASE_INT_FILE, "test/fixtures/while/do_once.c", 1, 0},
    {"while", "do_loop", CASE_INT_FILE, "test/fixtures/while/do_loop.c", 5, 0},
    {"while", "break", CASE_INT_FILE, "test/fixtures/while/break.c", 1, 0},
    {"while", "continue", CASE_INT_FILE, "test/fixtures/while/continue.c", 12, 0},
    {"while", "for_break_continue", CASE_INT_FILE, "test/fixtures/while/for_break_continue.c", 8, 0},
    {"while", "do_continue", CASE_INT_FILE, "test/fixtures/while/do_continue.c", 7, 0},

    {"for", "sum10", CASE_INT_FILE, "test/fixtures/for/sum10.c", 55, 0},
    {"for", "inc", CASE_INT_FILE, "test/fixtures/for/inc.c", 10, 0},
    {"for", "post_inc_expr", CASE_INT_FILE, "test/fixtures/for/post_inc_expr.c", 5, 0},
    {"for", "empty_for", CASE_INT_FILE, "test/fixtures/for/empty_for.c", 5, 0},

    {"bitwise", "bit_and", CASE_INT_FILE, "test/fixtures/bitwise/bit_and.c", 2, 0},
    {"bitwise", "bit_xor", CASE_INT_FILE, "test/fixtures/bitwise/bit_xor.c", 5, 0},
    {"bitwise", "bit_or", CASE_INT_FILE, "test/fixtures/bitwise/bit_or.c", 7, 0},
    {"bitwise", "bit_precedence", CASE_INT_FILE, "test/fixtures/bitwise/bit_precedence.c", 3, 0},
    {"bitwise", "bit_vs_logical_prec", CASE_INT_FILE, "test/fixtures/bitwise/bit_vs_logical_prec.c", 1, 0},

    {"shift", "shl", CASE_INT_FILE, "test/fixtures/shift/shl.c", 12, 0},
    {"shift", "shr", CASE_INT_FILE, "test/fixtures/shift/shr.c", 4, 0},
    {"shift", "shift_precedence", CASE_INT_FILE, "test/fixtures/shift/shift_precedence.c", 24, 0},
    {"shift", "shift_neg_right", CASE_INT_FILE, "test/fixtures/shift/shift_neg_right.c", 1, 0},
    {"shift", "shift_by_zero", CASE_INT_FILE, "test/fixtures/shift/shift_by_zero.c", 1, 0},
    {"shift", "shift_large_bit", CASE_INT_FILE, "test/fixtures/shift/shift_large_bit.c", 1, 0},

    {"switch_edge", "match", CASE_INT_FILE, "test/fixtures/switch_edge/match.c", 20, 0},
    {"switch_edge", "default", CASE_INT_FILE, "test/fixtures/switch_edge/default.c", 30, 0},
    {"switch_edge", "fallthrough", CASE_INT_FILE, "test/fixtures/switch_edge/fallthrough.c", 3, 0},
    {"switch_edge", "case_const_expr", CASE_INT_FILE, "test/fixtures/switch_edge/case_const_expr.c", 33, 0},
    {"switch_edge", "case_enum_const_expr", CASE_INT_FILE, "test/fixtures/switch_edge/case_enum_const_expr.c", 44, 0},
    {"switch_edge", "break_in_switch", CASE_INT_FILE, "test/fixtures/switch_edge/break_in_switch.c", 7, 0},
    {"switch_edge", "continue_outer_loop", CASE_INT_FILE, "test/fixtures/switch_edge/continue_outer_loop.c", 8, 0},
    {"switch_edge", "goto_forward", CASE_INT_FILE, "test/fixtures/switch_edge/goto_forward.c", 42, 0},
    {"switch_edge", "goto_backward_loop", CASE_INT_FILE, "test/fixtures/switch_edge/goto_backward_loop.c", 3, 0},
    {"switch_edge", "goto_from_case", CASE_INT_FILE, "test/fixtures/switch_edge/goto_from_case.c", 42, 0},
    {"switch_edge", "goto_loop_switch", CASE_INT_FILE, "test/fixtures/switch_edge/goto_loop_switch.c", 111, 0},
    {"switch_edge", "goto_inside_case", CASE_INT_FILE, "test/fixtures/switch_edge/goto_inside_case.c", 30, 0},
    {"switch_edge", "goto_out_of_loop_switch", CASE_INT_FILE, "test/fixtures/switch_edge/goto_out_of_loop_switch.c", 3, 0},
    {"switch_edge", "fallthrough_multi", CASE_INT_FILE, "test/fixtures/switch_edge/fallthrough_multi.c", 31, 0},
    {"switch_edge", "goto_state_machine", CASE_INT_FILE, "test/fixtures/switch_edge/goto_state_machine.c", 111, 0},
    {"switch_edge", "goto_into_loop", CASE_INT_FILE, "test/fixtures/switch_edge/goto_into_loop.c", 101, 0},
    {"switch_edge", "continue_in_switch_for", CASE_INT_FILE, "test/fixtures/switch_edge/continue_in_switch_for.c", 6, 0},
    {"switch_edge", "nested_switch", CASE_INT_FILE, "test/fixtures/switch_edge/nested_switch.c", 22, 0},
    {"switch_edge", "case_in_block", CASE_INT_FILE, "test/fixtures/switch_edge/case_in_block.c", 20, 0},
    {"switch_edge", "duff_do_while", CASE_INT_FILE, "test/fixtures/switch_edge/duff_do_while.c", 20, 0},
    {"switch_edge", "duff_do_while_case2", CASE_INT_FILE, "test/fixtures/switch_edge/duff_do_while_case2.c", 20, 0},

    {"return", "literal", CASE_INT_FILE, "test/fixtures/return/literal.c", 42, 0},
    {"return", "expr", CASE_INT_FILE, "test/fixtures/return/expr.c", 5, 0},
    {"return", "var", CASE_INT_FILE, "test/fixtures/return/var.c", 10, 0},
    {"return", "sum", CASE_INT_FILE, "test/fixtures/return/sum.c", 3, 0},
    {"return", "if", CASE_INT_FILE, "test/fixtures/return/if.c", 1, 0},
    {"return", "while", CASE_INT_FILE, "test/fixtures/return/while.c", 10, 0},

    {"block", "stmts", CASE_INT_FILE, "test/fixtures/block/stmts.c", 3, 0},
    {"block", "sum", CASE_INT_FILE, "test/fixtures/block/sum.c", 6, 0},
    {"block", "for", CASE_INT_FILE, "test/fixtures/block/for.c", 55, 0},
    {"block", "while", CASE_INT_FILE, "test/fixtures/block/while.c", 10, 0},
    {"block", "if", CASE_INT_FILE, "test/fixtures/block/if.c", 5, 0},

    {"funcall", "noargs", CASE_INT_FILE, "test/fixtures/funcall/noargs.c", 42, 0},
    {"funcall", "add", CASE_INT_FILE, "test/fixtures/funcall/add.c", 7, 0},
    {"funcall", "twice", CASE_INT_FILE, "test/fixtures/funcall/twice.c", 10, 0},
    {"funcall", "multi", CASE_INT_FILE, "test/fixtures/funcall/multi.c", 21, 0},
    {"funcall", "rec", CASE_INT_FILE, "test/fixtures/funcall/rec.c", 120, 0},
    {"funcall", "tail_rec", CASE_INT_FILE, "test/fixtures/funcall/tail_rec.c", 55, 0},
    {"funcall", "comma_arg", CASE_INT_FILE, "test/fixtures/funcall/comma_arg.c", 23, 0},
    {"funcall", "prototype_decl", CASE_INT_FILE, "test/fixtures/funcall/prototype_decl.c", 42, 0},
    {"funcall", "paren_name_funcdef", CASE_INT_FILE, "test/fixtures/funcall/paren_name_funcdef.c", 42, 0},
    {"funcall", "funcdef_ret_funcptr", CASE_INT_FILE, "test/fixtures/funcall/funcdef_ret_funcptr.c", 0, 0},
    {"funcall", "funcdef_ret_funcptr_with_param", CASE_INT_FILE, "test/fixtures/funcall/funcdef_ret_funcptr_with_param.c", 0, 0},
    {"funcall", "funcdef_ret_nested_funcptr_arrayptr", CASE_INT_FILE, "test/fixtures/funcall/funcdef_ret_nested_funcptr_arrayptr.c", 0, 0},
    {"funcall", "param_funcptr_decl", CASE_INT_FILE, "test/fixtures/funcall/param_funcptr_decl.c", 7, 0},
    {"funcall", "param_array_decl", CASE_INT_FILE, "test/fixtures/funcall/param_array_decl.c", 5, 0},
    {"funcall", "param_array_static_restrict", CASE_INT_FILE, "test/fixtures/funcall/param_array_static_restrict.c", 7, 0},
    {"funcall", "funcptr_value_assign_call", CASE_INT_FILE, "test/fixtures/funcall/funcptr_value_assign_call.c", 42, 0},
    {"funcall", "printf_variadic", CASE_INT_FILE, "test/fixtures/funcall/printf_variadic.c", 0, 0},
    {"funcall", "variadic_proto", CASE_INT_FILE, "test/fixtures/funcall/variadic_proto.c", 7, 0},
    {"funcall", "variadic_def", CASE_INT_FILE, "test/fixtures/funcall/variadic_def.c", 9, 0},
    {"funcall", "fib_recursive", CASE_INT_FILE, "test/fixtures/funcall/fib_recursive.c", 55, 0},
    {"funcall", "abs_ternary", CASE_INT_FILE, "test/fixtures/funcall/abs_ternary.c", 49, 0},
    {"funcall", "funcptr_apply_multi", CASE_INT_FILE, "test/fixtures/funcall/funcptr_apply_multi.c", 80, 0},

    {"multichar_var", "foo", CASE_INT_FILE, "test/fixtures/multichar_var/foo.c", 3, 0},
    {"multichar_var", "hello", CASE_INT_FILE, "test/fixtures/multichar_var/hello.c", 5, 0},
    {"multichar_var", "x1x2", CASE_INT_FILE, "test/fixtures/multichar_var/x1x2.c", 15, 0},
    {"multichar_var", "args", CASE_INT_FILE, "test/fixtures/multichar_var/args.c", 10, 0},
    {"multichar_var", "loop", CASE_INT_FILE, "test/fixtures/multichar_var/loop.c", 6, 0},

    {"type_decl", "int_func", CASE_INT_FILE, "test/fixtures/type_decl/int_func.c", 42, 0},
    {"type_decl", "int_var", CASE_INT_FILE, "test/fixtures/type_decl/int_var.c", 3, 0},
    {"type_decl", "int_sum", CASE_INT_FILE, "test/fixtures/type_decl/int_sum.c", 7, 0},
    {"type_decl", "funcdef_ret_inline_struct_tag", CASE_INT_FILE, "test/fixtures/type_decl/funcdef_ret_inline_struct_tag.c", 3, 0},
    {"type_decl", "funcdef_ret_inline_union_tag_parse_only", CASE_INT_FILE, "test/fixtures/type_decl/funcdef_ret_inline_union_tag_parse_only.c", 0, 0},
    {"type_decl", "funcdef_ret_inline_struct_tag_paren_name", CASE_INT_FILE, "test/fixtures/type_decl/funcdef_ret_inline_struct_tag_paren_name.c", 3, 0},
    {"type_decl", "funcdef_ret_inline_union_tag_paren_name_parse_only", CASE_INT_FILE, "test/fixtures/type_decl/funcdef_ret_inline_union_tag_paren_name_parse_only.c", 0, 0},
    {"type_decl", "int_args", CASE_INT_FILE, "test/fixtures/type_decl/int_args.c", 10, 0},
    {"type_decl", "int_init", CASE_INT_FILE, "test/fixtures/type_decl/int_init.c", 5, 0},
    {"type_decl", "multi_decl_one_init", CASE_INT_FILE, "test/fixtures/type_decl/multi_decl_one_init.c", 7, 0},
    {"type_decl", "multi_decl_two_init", CASE_INT_FILE, "test/fixtures/type_decl/multi_decl_two_init.c", 7, 0},
    {"type_decl", "for_decl", CASE_INT_FILE, "test/fixtures/type_decl/for_decl.c", 55, 0},
    {"type_decl", "for_multi_decl_init", CASE_INT_FILE, "test/fixtures/type_decl/for_multi_decl_init.c", 3, 0},
    {"type_decl", "tag_decl_minimal", CASE_INT_FILE, "test/fixtures/type_decl/tag_decl_minimal.c", 7, 0},
    {"type_decl", "tag_decl_ref_ptr", CASE_INT_FILE, "test/fixtures/type_decl/tag_decl_ref_ptr.c", 1, 0},
    {"type_decl", "tag_def_struct", CASE_INT_FILE, "test/fixtures/type_decl/tag_def_struct.c", 7, 0},
    {"type_decl", "tag_def_and_ptr_decl", CASE_INT_FILE, "test/fixtures/type_decl/tag_def_and_ptr_decl.c", 1, 0},
    {"type_decl", "tag_def_union_enum", CASE_INT_FILE, "test/fixtures/type_decl/tag_def_union_enum.c", 7, 0},
    {"type_decl", "enum_const_ref", CASE_INT_FILE, "test/fixtures/type_decl/enum_const_ref.c", 13, 0},
    {"type_decl", "enum_const_expr", CASE_INT_FILE, "test/fixtures/type_decl/enum_const_expr.c", 5, 0},
    {"type_decl", "enum_const_expr_cond", CASE_INT_FILE, "test/fixtures/type_decl/enum_const_expr_cond.c", 7, 0},
    {"type_decl", "enum_const_expr_bitwise", CASE_INT_FILE, "test/fixtures/type_decl/enum_const_expr_bitwise.c", 11, 0},
    {"type_decl", "global_tag_before_main", CASE_INT_FILE, "test/fixtures/type_decl/global_tag_before_main.c", 7, 0},
    {"type_decl", "global_tag_decl_with_var", CASE_INT_FILE, "test/fixtures/type_decl/global_tag_decl_with_var.c", 7, 0},
    {"type_decl", "global_int_var_decl", CASE_INT_FILE, "test/fixtures/type_decl/global_int_var_decl.c", 7, 0},
    {"type_decl", "global_extern_incomplete_array_decl", CASE_INT_FILE, "test/fixtures/type_decl/global_extern_incomplete_array_decl.c", 7, 0},
    {"type_decl", "local_extern_incomplete_array_decl", CASE_INT_FILE, "test/fixtures/type_decl/local_extern_incomplete_array_decl.c", 7, 0},
    {"type_decl", "typedef_incomplete_array_type", CASE_INT_FILE, "test/fixtures/type_decl/typedef_incomplete_array_type.c", 1, 0},
    {"type_decl", "char", CASE_INT_FILE, "test/fixtures/type_decl/char.c", 65, 0},
    {"type_decl", "void", CASE_INT_FILE, "test/fixtures/type_decl/void.c", 42, 0},
    {"type_decl", "short", CASE_INT_FILE, "test/fixtures/type_decl/short.c", 10, 0},
    {"type_decl", "long", CASE_INT_FILE, "test/fixtures/type_decl/long.c", 99, 0},
    {"type_decl", "short_arr", CASE_INT_FILE, "test/fixtures/type_decl/short_arr.c", 30, 0},
    {"type_decl", "short_sum", CASE_INT_FILE, "test/fixtures/type_decl/short_sum.c", 60, 0},
    {"type_decl", "short_one", CASE_INT_FILE, "test/fixtures/type_decl/short_one.c", 42, 0},
    {"type_decl", "unsigned_decl", CASE_INT_FILE, "test/fixtures/type_decl/unsigned_decl.c", 42, 0},
    {"type_decl", "bool_decl", CASE_INT_FILE, "test/fixtures/type_decl/bool_decl.c", 1, 0},
    {"type_decl", "signed_decl", CASE_INT_FILE, "test/fixtures/type_decl/signed_decl.c", 1, 0},
    {"type_decl", "char_add_eq", CASE_INT_FILE, "test/fixtures/type_decl/char_add_eq.c", 3, 0},
    {"type_decl", "short_mul_eq", CASE_INT_FILE, "test/fixtures/type_decl/short_mul_eq.c", 30, 0},
    {"type_decl", "ptr_deref_add_eq", CASE_INT_FILE, "test/fixtures/type_decl/ptr_deref_add_eq.c", 7, 0},
    {"type_decl", "ptr_ptr_deref", CASE_INT_FILE, "test/fixtures/type_decl/ptr_ptr_deref.c", 42, 0},
    {"type_decl", "sizeof_int", CASE_INT_FILE, "test/fixtures/type_decl/sizeof_int.c", 4, 0},
    {"type_decl", "sizeof_bool", CASE_INT_FILE, "test/fixtures/type_decl/sizeof_bool.c", 1, 0},
    {"type_decl", "sizeof_int_ptr", CASE_INT_FILE, "test/fixtures/type_decl/sizeof_int_ptr.c", 8, 0},
    {"type_decl", "sizeof_int_ptr_const", CASE_INT_FILE, "test/fixtures/type_decl/sizeof_int_ptr_const.c", 8, 0},
    {"type_decl", "sizeof_int_ptr_volatile", CASE_INT_FILE, "test/fixtures/type_decl/sizeof_int_ptr_volatile.c", 8, 0},
    {"type_decl", "sizeof_int_ptr_restrict", CASE_INT_FILE, "test/fixtures/type_decl/sizeof_int_ptr_restrict.c", 8, 0},
    {"type_decl", "sizeof_int_array_type", CASE_INT_FILE, "test/fixtures/type_decl/sizeof_int_array_type.c", 40, 0},
    {"type_decl", "sizeof_ptr_to_array_type", CASE_INT_FILE, "test/fixtures/type_decl/sizeof_ptr_to_array_type.c", 8, 0},
    {"type_decl", "sizeof_parenthesized_array_type", CASE_INT_FILE, "test/fixtures/type_decl/sizeof_parenthesized_array_type.c", 12, 0},
    {"type_decl", "sizeof_funcptr_type", CASE_INT_FILE, "test/fixtures/type_decl/sizeof_funcptr_type.c", 8, 0},
    {"type_decl", "alignof_int", CASE_INT_FILE, "test/fixtures/type_decl/alignof_int.c", 4, 0},
    {"type_decl", "alignof_ptr", CASE_INT_FILE, "test/fixtures/type_decl/alignof_ptr.c", 8, 0},
    {"type_decl", "alignof_ptr_const", CASE_INT_FILE, "test/fixtures/type_decl/alignof_ptr_const.c", 8, 0},
    {"type_decl", "alignof_ptr_volatile", CASE_INT_FILE, "test/fixtures/type_decl/alignof_ptr_volatile.c", 8, 0},
    {"type_decl", "alignof_ptr_restrict", CASE_INT_FILE, "test/fixtures/type_decl/alignof_ptr_restrict.c", 8, 0},
    {"type_decl", "alignof_int_array_type", CASE_INT_FILE, "test/fixtures/type_decl/alignof_int_array_type.c", 4, 0},
    {"type_decl", "alignof_ptr_to_array_type", CASE_INT_FILE, "test/fixtures/type_decl/alignof_ptr_to_array_type.c", 8, 0},
    {"type_decl", "alignof_parenthesized_array_type", CASE_INT_FILE, "test/fixtures/type_decl/alignof_parenthesized_array_type.c", 4, 0},
    {"type_decl", "sizeof_expr_var", CASE_INT_FILE, "test/fixtures/type_decl/sizeof_expr_var.c", 4, 0},
    {"type_decl", "sizeof_struct_type", CASE_INT_FILE, "test/fixtures/type_decl/sizeof_struct_type.c", 4, 0},
    {"type_decl", "alignof_struct_type", CASE_INT_FILE, "test/fixtures/type_decl/alignof_struct_type.c", 4, 0},
    {"type_decl", "sizeof_struct_ptr_to_array_type", CASE_INT_FILE, "test/fixtures/type_decl/sizeof_struct_ptr_to_array_type.c", 8, 0},
    {"type_decl", "sizeof_struct_array_type", CASE_INT_FILE, "test/fixtures/type_decl/sizeof_struct_array_type.c", 12, 0},
    {"type_decl", "sizeof_typedef_ptr_to_array_type", CASE_INT_FILE, "test/fixtures/type_decl/sizeof_typedef_ptr_to_array_type.c", 8, 0},
    {"type_decl", "sizeof_typedef_array_type_2d", CASE_INT_FILE, "test/fixtures/type_decl/sizeof_typedef_array_type_2d.c", 24, 0},
    {"type_decl", "cast_chain", CASE_INT_FILE, "test/fixtures/type_decl/cast_chain.c", 42, 0},
    {"type_decl", "cast_double_to_int", CASE_INT_FILE, "test/fixtures/type_decl/cast_double_to_int.c", 7, 0},
    {"type_decl", "cast_func_double_to_int", CASE_INT_FILE, "test/fixtures/type_decl/cast_func_double_to_int.c", 7, 0},
    {"type_decl", "double_param_int_param_mix", CASE_INT_FILE, "test/fixtures/type_decl/double_param_int_param_mix.c", 11, 0},
    {"type_decl", "void_ptr_roundtrip", CASE_INT_FILE, "test/fixtures/type_decl/void_ptr_roundtrip.c", 5, 0},
    {"type_decl", "comma_expr_init", CASE_INT_FILE, "test/fixtures/type_decl/comma_expr_init.c", 5, 0},
    {"type_decl", "comma_sideeffect", CASE_INT_FILE, "test/fixtures/type_decl/comma_sideeffect.c", 47, 0},
    {"type_decl", "comma_assign_chain", CASE_INT_FILE, "test/fixtures/type_decl/comma_assign_chain.c", 16, 0},
    {"type_decl", "unsigned_wrap", CASE_INT_FILE, "test/fixtures/type_decl/unsigned_wrap.c", 2, 0},
    {"type_decl", "signed_char_neg", CASE_INT_FILE, "test/fixtures/type_decl/signed_char_neg.c", 1, 0},
    {"type_decl", "bitwise_swap_nibble", CASE_INT_FILE, "test/fixtures/type_decl/bitwise_swap_nibble.c", 186, 0},
    {"type_decl", "bitwise_mask_or", CASE_INT_FILE, "test/fixtures/type_decl/bitwise_mask_or.c", 15, 0},
    {"type_decl", "struct_copy_independent", CASE_INT_FILE, "test/fixtures/type_decl/struct_copy_independent.c", 199, 0},
    {"type_decl", "struct_return_value", CASE_INT_FILE, "test/fixtures/type_decl/struct_return_value.c", 25, 0},
    {"type_decl", "struct_ptr_arrow", CASE_INT_FILE, "test/fixtures/type_decl/struct_ptr_arrow.c", 65, 0},
    {"type_decl", "global_shadow_local", CASE_INT_FILE, "test/fixtures/type_decl/global_shadow_local.c", 10, 0},
    {"type_decl", "cast_int", CASE_INT_FILE, "test/fixtures/type_decl/cast_int.c", 42, 0},
    {"type_decl", "cast_char_wrap", CASE_INT_FILE, "test/fixtures/type_decl/cast_char_wrap.c", 44, 0},
    {"type_decl", "cast_short_wrap", CASE_INT_FILE, "test/fixtures/type_decl/cast_short_wrap.c", 112, 0},
    {"type_decl", "cast_bool_true", CASE_INT_FILE, "test/fixtures/type_decl/cast_bool_true.c", 1, 0},
    {"type_decl", "cast_bool_false", CASE_INT_FILE, "test/fixtures/type_decl/cast_bool_false.c", 0, 0},
    {"type_decl", "cast_unsigned", CASE_INT_FILE, "test/fixtures/type_decl/cast_unsigned.c", 42, 0},
    {"type_decl", "cast_enum", CASE_INT_FILE, "test/fixtures/type_decl/cast_enum.c", 42, 0},
    {"type_decl", "cast_tag_ptr", CASE_INT_FILE, "test/fixtures/type_decl/cast_tag_ptr.c", 1, 0},
    {"type_decl", "cast_struct_from_scalar", CASE_INT_FILE, "test/fixtures/type_decl/cast_struct_from_scalar.c", 7, 0},
    {"type_decl", "cast_struct_from_pointer_postfix", CASE_INT_FILE, "test/fixtures/type_decl/cast_struct_from_pointer_postfix.c", 3, 0},
    {"type_decl", "cast_struct_same_type", CASE_INT_FILE, "test/fixtures/type_decl/cast_struct_same_type.c", 7, 0},
    {"type_decl", "cast_struct_diff_tag_same_size", CASE_INT_FILE, "test/fixtures/type_decl/cast_struct_diff_tag_same_size.c", 7, 0},
    {"type_decl", "cast_union_same_type", CASE_INT_FILE, "test/fixtures/type_decl/cast_union_same_type.c", 9, 0},
    {"type_decl", "cast_union_diff_tag_same_size", CASE_INT_FILE, "test/fixtures/type_decl/cast_union_diff_tag_same_size.c", 9, 0},
    {"type_decl", "cast_union_from_scalar", CASE_INT_FILE, "test/fixtures/type_decl/cast_union_from_scalar.c", 7, 0},
    {"type_decl", "cast_union_from_pointer_postfix", CASE_INT_FILE, "test/fixtures/type_decl/cast_union_from_pointer_postfix.c", 1, 0},
    {"type_decl", "cast_union_ptr_arrow_chain", CASE_INT_FILE, "test/fixtures/type_decl/cast_union_ptr_arrow_chain.c", 2, 0},
    {"type_decl", "cast_union_ptr_arrow_index", CASE_INT_FILE, "test/fixtures/type_decl/cast_union_ptr_arrow_index.c", 1, 0},
    {"type_decl", "cast_union_ptr_arrow_post_inc", CASE_INT_FILE, "test/fixtures/type_decl/cast_union_ptr_arrow_post_inc.c", 4, 0},
    {"type_decl", "cast_union_ptr_arrow_post_dec", CASE_INT_FILE, "test/fixtures/type_decl/cast_union_ptr_arrow_post_dec.c", 2, 0},
    {"type_decl", "cast_atomic_int", CASE_INT_FILE, "test/fixtures/type_decl/cast_atomic_int.c", 42, 0},
    {"type_decl", "member_dot", CASE_INT_FILE, "test/fixtures/type_decl/member_dot.c", 7, 0},
    {"type_decl", "member_arrow", CASE_INT_FILE, "test/fixtures/type_decl/member_arrow.c", 7, 0},
    {"type_decl", "member_funcptr", CASE_INT_FILE, "test/fixtures/type_decl/member_funcptr.c", 42, 0},
    {"type_decl", "member_union", CASE_INT_FILE, "test/fixtures/type_decl/member_union.c", 7, 0},
    {"type_decl", "union_brace_init_value", CASE_INT_FILE, "test/fixtures/type_decl/union_brace_init_value.c", 7, 0},
    {"type_decl", "union_brace_init_designated", CASE_INT_FILE, "test/fixtures/type_decl/union_brace_init_designated.c", 7, 0},
    {"type_decl", "union_brace_init_multi_designated", CASE_INT_FILE, "test/fixtures/type_decl/union_brace_init_multi_designated.c", 2, 0},
    {"type_decl", "union_array_member_nonbrace_init_values", CASE_INT_FILE, "test/fixtures/type_decl/union_array_member_nonbrace_init_values.c", 3, 0},
    {"type_decl", "struct_bitfield_decl", CASE_INT_FILE, "test/fixtures/type_decl/struct_bitfield_decl.c", 7, 0},
    {"type_decl", "struct_anonymous_struct_member", CASE_INT_FILE, "test/fixtures/type_decl/struct_anonymous_struct_member.c", 7, 0},
    {"type_decl", "struct_anonymous_union_member", CASE_INT_FILE, "test/fixtures/type_decl/struct_anonymous_union_member.c", 7, 0},
    {"type_decl", "struct_brace_init_parse_only", CASE_INT_FILE, "test/fixtures/type_decl/struct_brace_init_parse_only.c", 7, 0},
    {"type_decl", "struct_brace_init_values", CASE_INT_FILE, "test/fixtures/type_decl/struct_brace_init_values.c", 3, 0},
    {"type_decl", "struct_brace_init_designated", CASE_INT_FILE, "test/fixtures/type_decl/struct_brace_init_designated.c", 3, 0},
    {"type_decl", "struct_partial_init", CASE_INT_FILE, "test/fixtures/type_decl/struct_partial_init.c", 30, 0},
    {"type_decl", "struct_designated_gap", CASE_INT_FILE, "test/fixtures/type_decl/struct_designated_gap.c", 40, 0},
    {"type_decl", "sizeof_funcall_int", CASE_INT_FILE, "test/fixtures/type_decl/sizeof_funcall_int.c", 4, 0},
    {"type_decl", "sizeof_funcall_double", CASE_INT_FILE, "test/fixtures/type_decl/sizeof_funcall_double.c", 8, 0},
    {"type_decl", "sizeof_no_side_effect", CASE_INT_FILE, "test/fixtures/type_decl/sizeof_no_side_effect.c", 1, 0},
    {"type_decl", "struct_brace_elision_array_member", CASE_INT_FILE, "test/fixtures/type_decl/struct_brace_elision_array_member.c", 3, 0},
    {"type_decl", "struct_brace_elision_array_member_copy", CASE_INT_FILE, "test/fixtures/type_decl/struct_brace_elision_array_member_copy.c", 18, 0},
    {"type_decl", "struct_brace_elision_array_member_string", CASE_INT_FILE, "test/fixtures/type_decl/struct_brace_elision_array_member_string.c", 202, 0},
    {"type_decl", "struct_nested_desig_single", CASE_INT_FILE, "test/fixtures/type_decl/struct_nested_desig_single.c", 3, 0},
    {"type_decl", "struct_nested_desig_multi", CASE_INT_FILE, "test/fixtures/type_decl/struct_nested_desig_multi.c", 12, 0},
    {"type_decl", "union_nested_desig", CASE_INT_FILE, "test/fixtures/type_decl/union_nested_desig.c", 3, 0},
    {"type_decl", "struct_single_expr_copy_comma", CASE_INT_FILE, "test/fixtures/type_decl/struct_single_expr_copy_comma.c", 13, 0},
    {"type_decl", "struct_single_expr_copy_ternary", CASE_INT_FILE, "test/fixtures/type_decl/struct_single_expr_copy_ternary.c", 7, 0},
    {"type_decl", "union_single_expr_copy_comma", CASE_INT_FILE, "test/fixtures/type_decl/union_single_expr_copy_comma.c", 9, 0},
    {"type_decl", "struct_padding_array", CASE_INT_FILE, "test/fixtures/type_decl/struct_padding_array.c", 3, 0},
    {"type_decl", "typedef_int", CASE_INT_FILE, "test/fixtures/type_decl/typedef_int.c", 9, 0},
    {"type_decl", "typedef_struct_forward_tag", CASE_INT_FILE, "test/fixtures/type_decl/typedef_struct_forward_tag.c", 7, 0},
    {"type_decl", "typedef_struct_anon_top", CASE_INT_FILE, "test/fixtures/type_decl/typedef_struct_anon_top.c", 5, 0},
    {"type_decl", "typedef_union_forward_tag", CASE_INT_FILE, "test/fixtures/type_decl/typedef_union_forward_tag.c", 8, 0},
    {"type_decl", "typedef_union_anon_top", CASE_INT_FILE, "test/fixtures/type_decl/typedef_union_anon_top.c", 6, 0},
    {"type_decl", "typedef_ptr", CASE_INT_FILE, "test/fixtures/type_decl/typedef_ptr.c", 11, 0},
    {"type_decl", "typedef_in_func", CASE_INT_FILE, "test/fixtures/type_decl/typedef_in_func.c", 6, 0},
    {"type_decl", "typedef_in_func_incomplete_array", CASE_INT_FILE, "test/fixtures/type_decl/typedef_in_func_incomplete_array.c", 1, 0},
    {"type_decl", "typedef_local_struct_forward_tag", CASE_INT_FILE, "test/fixtures/type_decl/typedef_local_struct_forward_tag.c", 0, 0},
    {"type_decl", "typedef_local_struct_anon", CASE_INT_FILE, "test/fixtures/type_decl/typedef_local_struct_anon.c", 9, 0},
    {"type_decl", "typedef_local_union_forward_tag", CASE_INT_FILE, "test/fixtures/type_decl/typedef_local_union_forward_tag.c", 0, 0},
    {"type_decl", "typedef_local_union_anon", CASE_INT_FILE, "test/fixtures/type_decl/typedef_local_union_anon.c", 4, 0},
    {"type_decl", "typedef_funcptr", CASE_INT_FILE, "test/fixtures/type_decl/typedef_funcptr.c", 0, 0},
    {"type_decl", "typedef_funcptr_nested", CASE_INT_FILE, "test/fixtures/type_decl/typedef_funcptr_nested.c", 0, 0},
    {"type_decl", "typedef_funcptr_array_nested", CASE_INT_FILE, "test/fixtures/type_decl/typedef_funcptr_array_nested.c", 0, 0},
    {"type_decl", "typedef_local_funcptr_nested", CASE_INT_FILE, "test/fixtures/type_decl/typedef_local_funcptr_nested.c", 0, 0},
    {"type_decl", "local_funcptr_nested_decl", CASE_INT_FILE, "test/fixtures/type_decl/local_funcptr_nested_decl.c", 0, 0},
    {"type_decl", "local_funcptr_array_decl", CASE_INT_FILE, "test/fixtures/type_decl/local_funcptr_array_decl.c", 0, 0},
    {"type_decl", "local_ptr_to_2d_array_sizeof", CASE_INT_FILE, "test/fixtures/type_decl/local_ptr_to_2d_array_sizeof.c", 48, 0},
    {"type_decl", "toplevel_funcptr_array_decl", CASE_INT_FILE, "test/fixtures/type_decl/toplevel_funcptr_array_decl.c", 0, 0},
    {"type_decl", "toplevel_nested_funcptr_array_decl_parse_only", CASE_INT_FILE, "test/fixtures/type_decl/toplevel_nested_funcptr_array_decl_parse_only.c", 0, 0},
    {"type_decl", "struct_member_funcptr_array_decl", CASE_INT_FILE, "test/fixtures/type_decl/struct_member_funcptr_array_decl.c", 0, 0},
    {"type_decl", "struct_member_funcptr_array_size", CASE_INT_FILE, "test/fixtures/type_decl/struct_member_funcptr_array_size.c", 16, 0},
    {"type_decl", "typedef_funcptr_param", CASE_INT_FILE, "test/fixtures/type_decl/typedef_funcptr_param.c", 14, 0},
    {"type_decl", "typedef_ret_funcdef", CASE_INT_FILE, "test/fixtures/type_decl/typedef_ret_funcdef.c", 7, 0},
    {"type_decl", "typedef_ret_proto", CASE_INT_FILE, "test/fixtures/type_decl/typedef_ret_proto.c", 5, 0},
    {"type_decl", "typedef_ptr_ret_proto", CASE_INT_FILE, "test/fixtures/type_decl/typedef_ptr_ret_proto.c", 0, 0},
    {"type_decl", "unnamed_param_prototype", CASE_INT_FILE, "test/fixtures/type_decl/unnamed_param_prototype.c", 0, 0},
    {"type_decl", "unsigned_long_ret_funcdef", CASE_INT_FILE, "test/fixtures/type_decl/unsigned_long_ret_funcdef.c", 42, 0},
    {"type_decl", "unsigned_long_decl", CASE_INT_FILE, "test/fixtures/type_decl/unsigned_long_decl.c", 12, 0},
    {"type_decl", "unsigned_long_long_decl", CASE_INT_FILE, "test/fixtures/type_decl/unsigned_long_long_decl.c", 12, 0},
    {"type_decl", "signed_short_decl", CASE_INT_FILE, "test/fixtures/type_decl/signed_short_decl.c", 13, 0},
    {"type_decl", "signed_char_decl", CASE_INT_FILE, "test/fixtures/type_decl/signed_char_decl.c", 13, 0},
    // integer promotion: signed/unsigned 符号拡張 vs zero拡張
    {"type_decl", "char_sign_extend", CASE_INT_FILE, "test/fixtures/type_decl/char_sign_extend.c", 1, 0},
    {"type_decl", "unsigned_char_zero_extend", CASE_INT_FILE, "test/fixtures/type_decl/unsigned_char_zero_extend.c", 255, 0},
    {"type_decl", "short_sign_extend", CASE_INT_FILE, "test/fixtures/type_decl/short_sign_extend.c", 1, 0},
    {"type_decl", "unsigned_short_zero_extend", CASE_INT_FILE, "test/fixtures/type_decl/unsigned_short_zero_extend.c", 200, 0},
    // unsigned演算セマンティクス
    {"type_decl", "unsigned_div", CASE_INT_FILE, "test/fixtures/type_decl/unsigned_div.c", 14, 0},
    {"type_decl", "unsigned_mod", CASE_INT_FILE, "test/fixtures/type_decl/unsigned_mod.c", 2, 0},
    {"type_decl", "unsigned_shr", CASE_INT_FILE, "test/fixtures/type_decl/unsigned_shr.c", 1, 0},
    {"type_decl", "signed_shr_preserve", CASE_INT_FILE, "test/fixtures/type_decl/signed_shr_preserve.c", 255, 0},
    {"type_decl", "unsigned_cmp_lt", CASE_INT_FILE, "test/fixtures/type_decl/unsigned_cmp_lt.c", 1, 0},
    {"type_decl", "unsigned_cmp_le", CASE_INT_FILE, "test/fixtures/type_decl/unsigned_cmp_le.c", 1, 0},
    {"type_decl", "const_decl", CASE_INT_FILE, "test/fixtures/type_decl/const_decl.c", 8, 0},
    {"type_decl", "volatile_decl", CASE_INT_FILE, "test/fixtures/type_decl/volatile_decl.c", 9, 0},
    {"type_decl", "duplicate_qualifiers_decl", CASE_INT_FILE, "test/fixtures/type_decl/duplicate_qualifiers_decl.c", 18, 0},
    {"type_decl", "duplicate_qualifiers_param", CASE_INT_FILE, "test/fixtures/type_decl/duplicate_qualifiers_param.c", 8, 0},
    {"type_decl", "duplicate_postfix_const_cast", CASE_INT_FILE, "test/fixtures/type_decl/duplicate_postfix_const_cast.c", 21, 0},
    {"type_decl", "storage_specs_local", CASE_INT_FILE, "test/fixtures/type_decl/storage_specs_local.c", 12, 0},
    {"type_decl", "scalar_brace_init", CASE_INT_FILE, "test/fixtures/type_decl/scalar_brace_init.c", 3, 0},
    {"type_decl", "long_double_sizeof", CASE_INT_FILE, "test/fixtures/type_decl/long_double_sizeof.c", 8, 0},
    {"type_decl", "complex_sizeof", CASE_INT_FILE, "test/fixtures/type_decl/complex_sizeof.c", 16, 0},
    {"type_decl", "complex_float_sizeof", CASE_INT_FILE, "test/fixtures/type_decl/complex_float_sizeof.c", 8, 0},
    {"type_decl", "complex_init_copy", CASE_INT_FILE, "test/fixtures/type_decl/complex_init_copy.c", 1, 0},
    {"type_decl", "complex_add", CASE_INT_FILE, "test/fixtures/type_decl/complex_add.c", 1, 0},
    {"type_decl", "complex_sub", CASE_INT_FILE, "test/fixtures/type_decl/complex_sub.c", 1, 0},
    {"type_decl", "complex_mul", CASE_INT_FILE, "test/fixtures/type_decl/complex_mul.c", 1, 0},
    {"type_decl", "complex_div", CASE_INT_FILE, "test/fixtures/type_decl/complex_div.c", 1, 0},
    {"type_decl", "complex_ne", CASE_INT_FILE, "test/fixtures/type_decl/complex_ne.c", 1, 0},
    {"type_decl", "extern_inline_funcspec", CASE_INT_FILE, "test/fixtures/type_decl/extern_inline_funcspec.c", 7, 0},
    {"type_decl", "noreturn_spec_parse", CASE_INT_FILE, "test/fixtures/type_decl/noreturn_spec_parse.c", 7, 0},
    {"type_decl", "static_assert_toplevel", CASE_INT_FILE, "test/fixtures/type_decl/static_assert_toplevel.c", 7, 0},
    {"type_decl", "static_assert_typedef_array_sizeof", CASE_INT_FILE, "test/fixtures/type_decl/static_assert_typedef_array_sizeof.c", 7, 0},
    {"type_decl", "typedef_array_1d_local", CASE_INT_FILE, "test/fixtures/type_decl/typedef_array_1d_local.c", 30, 0},
    {"type_decl", "typedef_array_2d_local", CASE_INT_FILE, "test/fixtures/type_decl/typedef_array_2d_local.c", 23, 0},
    {"type_decl", "typedef_array_3d_local", CASE_INT_FILE, "test/fixtures/type_decl/typedef_array_3d_local.c", 123, 0},
    {"type_decl", "typedef_array_4d_local", CASE_INT_FILE, "test/fixtures/type_decl/typedef_array_4d_local.c", 210, 0},
    {"type_decl", "inline_array_addr_cast", CASE_INT_FILE, "test/fixtures/type_decl/inline_array_addr_cast.c", 99, 0},
    {"type_decl", "typedef_array_addr_cast", CASE_INT_FILE, "test/fixtures/type_decl/typedef_array_addr_cast.c", 99, 0},
    {"type_decl", "typedef_array_addr_func_arg", CASE_INT_FILE, "test/fixtures/type_decl/typedef_array_addr_func_arg.c", 123, 0},
    {"type_decl", "typedef_array_user_suffix", CASE_INT_FILE, "test/fixtures/type_decl/typedef_array_user_suffix.c", 123, 0},
    {"type_decl", "typedef_array_ptr_param_3d", CASE_INT_FILE, "test/fixtures/type_decl/typedef_array_ptr_param_3d.c", 123, 0},
    {"type_decl", "typedef_array_sizeof", CASE_INT_FILE, "test/fixtures/type_decl/typedef_array_sizeof.c", 96, 0},
    {"type_decl", "typedef_array_init", CASE_INT_FILE, "test/fixtures/type_decl/typedef_array_init.c", 6, 0},
    {"type_decl", "typedef_array_5d_local", CASE_INT_FILE, "test/fixtures/type_decl/typedef_array_5d_local.c", 30, 0},
    {"type_decl", "inline_array_1d_addr_cast", CASE_INT_FILE, "test/fixtures/type_decl/inline_array_1d_addr_cast.c", 33, 0},
    {"type_decl", "inline_array_2d_addr_cast", CASE_INT_FILE, "test/fixtures/type_decl/inline_array_2d_addr_cast.c", 77, 0},
    {"type_decl", "global_array_1d_init", CASE_INT_FILE, "test/fixtures/type_decl/global_array_1d_init.c", 30, 0},
    {"type_decl", "global_array_2d_init", CASE_INT_FILE, "test/fixtures/type_decl/global_array_2d_init.c", 60, 0},
    {"type_decl", "global_array_3d_init", CASE_INT_FILE, "test/fixtures/type_decl/global_array_3d_init.c", 24, 0},
    {"type_decl", "global_array_partial_init", CASE_INT_FILE, "test/fixtures/type_decl/global_array_partial_init.c", 4, 0},
    {"type_decl", "global_typedef_array_2d_init", CASE_INT_FILE, "test/fixtures/type_decl/global_typedef_array_2d_init.c", 60, 0},
    {"type_decl", "static_assert_stmt", CASE_INT_FILE, "test/fixtures/type_decl/static_assert_stmt.c", 7, 0},
    {"type_decl", "alignas_atomic_prefix", CASE_INT_FILE, "test/fixtures/type_decl/alignas_atomic_prefix.c", 7, 0},
    {"type_decl", "atomic_type_spec", CASE_INT_FILE, "test/fixtures/type_decl/atomic_type_spec.c", 5, 0},
    {"type_decl", "atomic_type_qual_postfix", CASE_INT_FILE, "test/fixtures/type_decl/atomic_type_qual_postfix.c", 6, 0},
    {"type_decl", "atomic_type_qual_postfix_ptr", CASE_INT_FILE, "test/fixtures/type_decl/atomic_type_qual_postfix_ptr.c", 7, 0},
    {"type_decl", "atomic_load_store", CASE_INT_FILE, "test/fixtures/type_decl/atomic_load_store.c", 42, 0},
    {"type_decl", "thread_local_init", CASE_INT_FILE, "test/fixtures/type_decl/thread_local_init.c", 7, 0},
    {"type_decl", "thread_local_store", CASE_INT_FILE, "test/fixtures/type_decl/thread_local_store.c", 99, 0},
    {"type_decl", "thread_local_arith", CASE_INT_FILE, "test/fixtures/type_decl/thread_local_arith.c", 15, 0},
    {"type_decl", "tl_multi_var_expr", CASE_INT_FILE, "test/fixtures/type_decl/tl_multi_var_expr.c", 35, 0},
    {"type_decl", "tl_cross_func", CASE_INT_FILE, "test/fixtures/type_decl/tl_cross_func.c", 3, 0},
    {"type_decl", "tl_in_loop", CASE_INT_FILE, "test/fixtures/type_decl/tl_in_loop.c", 10, 0},
    {"type_decl", "tl_addr_of", CASE_INT_FILE, "test/fixtures/type_decl/tl_addr_of.c", 5, 0},
    {"type_decl", "tl_uninit", CASE_INT_FILE, "test/fixtures/type_decl/tl_uninit.c", 0, 0},
    {"type_decl", "tl_ternary", CASE_INT_FILE, "test/fixtures/type_decl/tl_ternary.c", 9, 0},
    {"type_decl", "tl_switch", CASE_INT_FILE, "test/fixtures/type_decl/tl_switch.c", 20, 0},
    {"type_decl", "tl_recursive", CASE_INT_FILE, "test/fixtures/type_decl/tl_recursive.c", 15, 0},
    {"type_decl", "tl_rmw_chain", CASE_INT_FILE, "test/fixtures/type_decl/tl_rmw_chain.c", 5, 0},
    {"type_decl", "atomic_global", CASE_INT_FILE, "test/fixtures/type_decl/atomic_global.c", 42, 0},
    {"type_decl", "atomic_in_loop", CASE_INT_FILE, "test/fixtures/type_decl/atomic_in_loop.c", 10, 0},
    {"type_decl", "complex_chain_ops", CASE_INT_FILE, "test/fixtures/type_decl/complex_chain_ops.c", 1, 0},
    {"type_decl", "complex_in_loop", CASE_INT_FILE, "test/fixtures/type_decl/complex_in_loop.c", 1, 0},
    {"type_decl", "generic_int", CASE_INT_FILE, "test/fixtures/type_decl/generic_int.c", 11, 0},
    {"type_decl", "generic_double", CASE_INT_FILE, "test/fixtures/type_decl/generic_double.c", 33, 0},
    {"type_decl", "generic_ptr", CASE_INT_FILE, "test/fixtures/type_decl/generic_ptr.c", 3, 0},
    {"type_decl", "generic_assoc_struct_type_parse", CASE_INT_FILE, "test/fixtures/type_decl/generic_assoc_struct_type_parse.c", 1, 0},
    {"type_decl", "generic_assoc_union_type_parse", CASE_INT_FILE, "test/fixtures/type_decl/generic_assoc_union_type_parse.c", 1, 0},
    {"type_decl", "generic_assoc_struct_type_tag_nomatch", CASE_INT_FILE, "test/fixtures/type_decl/generic_assoc_struct_type_tag_nomatch.c", 2, 0},
    {"type_decl", "generic_assoc_array_type_parse", CASE_INT_FILE, "test/fixtures/type_decl/generic_assoc_array_type_parse.c", 2, 0},
    {"type_decl", "generic_assoc_array_of_funcptr_type", CASE_INT_FILE, "test/fixtures/type_decl/generic_assoc_array_of_funcptr_type.c", 2, 0},
    {"type_decl", "generic_assoc_ptr_to_func_returning_ptr_to_array_type", CASE_INT_FILE, "test/fixtures/type_decl/generic_assoc_ptr_to_func_returning_ptr_to_array_type.c", 2, 0},
    {"type_decl", "generic_funcptr_assoc", CASE_INT_FILE, "test/fixtures/type_decl/generic_funcptr_assoc.c", 13, 0},
    {"type_decl", "generic_deref_double_ptr", CASE_INT_FILE, "test/fixtures/type_decl/generic_deref_double_ptr.c", 42, 0},
    {"type_decl", "generic_deref_float_ptr", CASE_INT_FILE, "test/fixtures/type_decl/generic_deref_float_ptr.c", 11, 0},
    {"type_decl", "generic_subscript_double_ptr", CASE_INT_FILE, "test/fixtures/type_decl/generic_subscript_double_ptr.c", 42, 0},
    {"type_decl", "generic_ptr_kind_match", CASE_INT_FILE, "test/fixtures/type_decl/generic_ptr_kind_match.c", 2, 0},
    {"type_decl", "generic_ptr_fp_match", CASE_INT_FILE, "test/fixtures/type_decl/generic_ptr_fp_match.c", 2, 0},
    {"type_decl", "generic_ptr_struct_tag_match", CASE_INT_FILE, "test/fixtures/type_decl/generic_ptr_struct_tag_match.c", 2, 0},
    {"type_decl", "generic_ptr_const_pointee_match", CASE_INT_FILE, "test/fixtures/type_decl/generic_ptr_const_pointee_match.c", 2, 0},
    {"type_decl", "generic_ptr_typedef_const_pointee_match", CASE_INT_FILE, "test/fixtures/type_decl/generic_ptr_typedef_const_pointee_match.c", 2, 0},
    {"type_decl", "generic_ptr_typedef_volatile_pointee_match", CASE_INT_FILE, "test/fixtures/type_decl/generic_ptr_typedef_volatile_pointee_match.c", 2, 0},
    {"type_decl", "generic_ptr_ptr_kind_match", CASE_INT_FILE, "test/fixtures/type_decl/generic_ptr_ptr_kind_match.c", 2, 0},
    {"type_decl", "generic_ptr_unsigned_match", CASE_INT_FILE, "test/fixtures/type_decl/generic_ptr_unsigned_match.c", 2, 0},
    {"type_decl", "generic_ptr_typedef_unsigned_match", CASE_INT_FILE, "test/fixtures/type_decl/generic_ptr_typedef_unsigned_match.c", 2, 0},
    {"type_decl", "generic_ptr_level_const_match", CASE_INT_FILE, "test/fixtures/type_decl/generic_ptr_level_const_match.c", 2, 0},
    {"type_decl", "generic_ptr_level_volatile_match", CASE_INT_FILE, "test/fixtures/type_decl/generic_ptr_level_volatile_match.c", 2, 0},
    {"type_decl", "generic_scalar_unsigned_long_match", CASE_INT_FILE, "test/fixtures/type_decl/generic_scalar_unsigned_long_match.c", 2, 0},
    {"type_decl", "generic_scalar_long_signedness_match", CASE_INT_FILE, "test/fixtures/type_decl/generic_scalar_long_signedness_match.c", 2, 0},
    {"type_decl", "generic_scalar_post_const_match", CASE_INT_FILE, "test/fixtures/type_decl/generic_scalar_post_const_match.c", 2, 0},
    {"type_decl", "generic_ptr_post_const_match", CASE_INT_FILE, "test/fixtures/type_decl/generic_ptr_post_const_match.c", 2, 0},
    {"type_decl", "const_param", CASE_INT_FILE, "test/fixtures/type_decl/const_param.c", 7, 0},
    {"type_decl", "compound_literal_int", CASE_INT_FILE, "test/fixtures/type_decl/compound_literal_int.c", 3, 0},
    {"type_decl", "compound_literal_struct_stmt", CASE_INT_FILE, "test/fixtures/type_decl/compound_literal_struct_stmt.c", 7, 0},
    {"type_decl", "compound_literal_struct_member", CASE_INT_FILE, "test/fixtures/type_decl/compound_literal_struct_member.c", 2, 0},
    {"type_decl", "compound_literal_struct_member_lvalue_assign", CASE_INT_FILE, "test/fixtures/type_decl/compound_literal_struct_member_lvalue_assign.c", 5, 0},
    {"type_decl", "compound_literal_struct_addr_arrow", CASE_INT_FILE, "test/fixtures/type_decl/compound_literal_struct_addr_arrow.c", 3, 0},
    {"type_decl", "compound_literal_array_inferred_size", CASE_INT_FILE, "test/fixtures/type_decl/compound_literal_array_inferred_size.c", 10, 0},
    {"type_decl", "compound_literal_array_inferred_size_char", CASE_INT_FILE, "test/fixtures/type_decl/compound_literal_array_inferred_size_char.c", 'a'+'b', 0},
    {"type_decl", "compound_literal_array_inferred_size_designated", CASE_INT_FILE, "test/fixtures/type_decl/compound_literal_array_inferred_size_designated.c", 111, 0},
    {"type_decl", "compound_literal_char_array_brace_string", CASE_INT_FILE, "test/fixtures/type_decl/compound_literal_char_array_brace_string.c", 'h'+'o', 0},
    {"type_decl", "compound_literal_char_array_brace_string_explicit", CASE_INT_FILE, "test/fixtures/type_decl/compound_literal_char_array_brace_string_explicit.c", 'h'+'i', 0},
    {"type_decl", "compound_literal_array_subscript", CASE_INT_FILE, "test/fixtures/type_decl/compound_literal_array_subscript.c", 2, 0},
    {"type_decl", "compound_literal_array_subscript0", CASE_INT_FILE, "test/fixtures/type_decl/compound_literal_array_subscript0.c", 10, 0},
    {"type_decl", "compound_literal_array_subscript2", CASE_INT_FILE, "test/fixtures/type_decl/compound_literal_array_subscript2.c", 30, 0},
    // 外側括弧なし: unary() 内で直接 apply_postfix(ref) を呼ぶパス
    {"type_decl", "compound_literal_array_subscript_direct", CASE_INT_FILE, "test/fixtures/type_decl/compound_literal_array_subscript_direct.c", 9, 0},
    {"type_decl", "sizeof_array_of_funcptr_type", CASE_INT_FILE, "test/fixtures/type_decl/sizeof_array_of_funcptr_type.c", 24, 0},
    {"type_decl", "sizeof_array_of_ptr_to_array_type", CASE_INT_FILE, "test/fixtures/type_decl/sizeof_array_of_ptr_to_array_type.c", 16, 0},
    {"type_decl", "sizeof_array_of_ptr_to_array_of_ptr_type", CASE_INT_FILE, "test/fixtures/type_decl/sizeof_array_of_ptr_to_array_of_ptr_type.c", 16, 0},
    {"type_decl", "sizeof_ptr_to_func_returning_ptr_to_array_type", CASE_INT_FILE, "test/fixtures/type_decl/sizeof_ptr_to_func_returning_ptr_to_array_type.c", 8, 0},
    {"type_decl", "sizeof_array_of_ptr_to_func_returning_ptr_to_array_type", CASE_INT_FILE, "test/fixtures/type_decl/sizeof_array_of_ptr_to_func_returning_ptr_to_array_type.c", 16, 0},
    {"type_decl", "sizeof_ptr_to_func_returning_ptr_to_func_type", CASE_INT_FILE, "test/fixtures/type_decl/sizeof_ptr_to_func_returning_ptr_to_func_type.c", 8, 0},
    {"type_decl", "sizeof_ptr_to_func_returning_ptr_to_func_returning_ptr_to_array_type", CASE_INT_FILE, "test/fixtures/type_decl/sizeof_ptr_to_func_returning_ptr_to_func_returning_ptr_to_array_type.c", 8, 0},
    // designator 初期化子との組み合わせ
    {"type_decl", "compound_literal_array_subscript_designator", CASE_INT_FILE, "test/fixtures/type_decl/compound_literal_array_subscript_designator.c", 99, 0},
    // 式中での複数利用
    {"type_decl", "compound_literal_array_subscript_expr", CASE_INT_FILE, "test/fixtures/type_decl/compound_literal_array_subscript_expr.c", 7, 0},
    // ファイルスコープ複合リテラル（静的ストレージ期間）
    {"type_decl", "compound_literal_file_scope", CASE_INT_FILE, "test/fixtures/type_decl/compound_literal_file_scope.c", 42, 0},
    {"type_decl", "float1", CASE_FLOAT_FILE, "test/fixtures/type_decl/float1.c", 0, 7.0},
    {"type_decl", "float2", CASE_FLOAT_FILE, "test/fixtures/type_decl/float2.c", 0, 7.34},
    {"type_decl", "float3", CASE_FLOAT_FILE, "test/fixtures/type_decl/float3.c", 0, 2.3},
    {"type_decl", "float4", CASE_FLOAT_FILE, "test/fixtures/type_decl/float4.c", 0, 15.0},
    {"type_decl", "float5", CASE_FLOAT_FILE, "test/fixtures/type_decl/float5.c", 0, 3.5},
    {"type_decl", "double1", CASE_DOUBLE_FILE, "test/fixtures/type_decl/double1.c", 0, 3.99},
    {"type_decl", "double2", CASE_DOUBLE_FILE, "test/fixtures/type_decl/double2.c", 0, 7.3},
    {"type_decl", "double3", CASE_DOUBLE_FILE, "test/fixtures/type_decl/double3.c", 0, 15.0},
    {"type_decl", "double4", CASE_DOUBLE_FILE, "test/fixtures/type_decl/double4.c", 0, 5.0},
    // hex float literals (C11 6.4.4.2)
    {"type_decl", "hex_float_double", CASE_DOUBLE_FILE, "test/fixtures/type_decl/hex_float_double.c", 0, 12.0},
    {"type_decl", "hex_float_no_sign", CASE_DOUBLE_FILE, "test/fixtures/type_decl/hex_float_no_sign.c", 0, 16.0},
    {"type_decl", "hex_float_neg_exp", CASE_DOUBLE_FILE, "test/fixtures/type_decl/hex_float_neg_exp.c", 0, 0.25},
    {"type_decl", "hex_float_suffix_f", CASE_FLOAT_FILE, "test/fixtures/type_decl/hex_float_suffix_f.c", 0, 12.0},
    {"type_decl", "global_ptr_addr_init", CASE_INT_FILE, "test/fixtures/type_decl/global_ptr_addr_init.c", 99, 0},
    {"type_decl", "global_ptr_addr_write", CASE_INT_FILE, "test/fixtures/type_decl/global_ptr_addr_write.c", 55, 0},

    {"pointer", "deref", CASE_INT_FILE, "test/fixtures/pointer/deref.c", 5, 0},
    {"pointer", "assign", CASE_INT_FILE, "test/fixtures/pointer/assign.c", 10, 0},
    {"pointer", "arith_add", CASE_INT_FILE, "test/fixtures/pointer/arith_add.c", 30, 0},
    {"pointer", "arith_sub", CASE_INT_FILE, "test/fixtures/pointer/arith_sub.c", 30, 0},
    {"pointer", "ptr_subtract", CASE_INT_FILE, "test/fixtures/pointer/ptr_subtract.c", 3, 0},
    {"pointer", "array_decay_diff", CASE_INT_FILE, "test/fixtures/pointer/array_decay_diff.c", 9, 0},
    {"pointer", "global_funcptr_array", CASE_INT_FILE, "test/fixtures/pointer/global_funcptr_array.c", 25, 0},
    {"pointer", "arith_char", CASE_INT_FILE, "test/fixtures/pointer/arith_char.c", 3, 0},
    {"pointer", "triple_deref", CASE_INT_FILE, "test/fixtures/pointer/triple_deref.c", 42, 0},
    {"pointer", "write_via_pp", CASE_INT_FILE, "test/fixtures/pointer/write_via_pp.c", 77, 0},
    {"pointer", "retarget_via_pp", CASE_INT_FILE, "test/fixtures/pointer/retarget_via_pp.c", 2, 0},
    {"pointer", "swap_via_pp", CASE_INT_FILE, "test/fixtures/pointer/swap_via_pp.c", 120, 0},
    {"pointer", "pp_cmp", CASE_INT_FILE, "test/fixtures/pointer/pp_cmp.c", 1, 0},
    {"pointer", "arith_relative", CASE_INT_FILE, "test/fixtures/pointer/arith_relative.c", 88, 0},
    {"pointer", "char_pp_deref", CASE_INT_FILE, "test/fixtures/pointer/char_pp_deref.c", 65, 0},
    {"pointer", "triple_write", CASE_INT_FILE, "test/fixtures/pointer/triple_write.c", 99, 0},
    {"pointer", "pp_inc_deref", CASE_INT_FILE, "test/fixtures/pointer/pp_inc_deref.c", 8, 0},
    {"pointer", "inc_via_pp_func", CASE_INT_FILE, "test/fixtures/pointer/inc_via_pp_func.c", 12, 0},
    {"pointer", "pp_arith_scale", CASE_INT_FILE, "test/fixtures/pointer/pp_arith_scale.c", 30, 0},
    {"pointer", "pp_deref_add", CASE_INT_FILE, "test/fixtures/pointer/pp_deref_add.c", 30, 0},
    {"pointer", "pp_subscript", CASE_INT_FILE, "test/fixtures/pointer/pp_subscript.c", 40, 0},
    {"pointer", "ptr_array", CASE_INT_FILE, "test/fixtures/pointer/ptr_array.c", 6, 0},
    {"pointer", "ptr_array_write", CASE_INT_FILE, "test/fixtures/pointer/ptr_array_write.c", 15, 0},
    {"pointer", "struct_ptr_param_paren", CASE_INT_FILE, "test/fixtures/pointer/struct_ptr_param_paren.c", 42, 0},
    {"pointer", "array_ptr_2d", CASE_INT_FILE, "test/fixtures/pointer/array_ptr_2d.c", 6, 0},
    {"pointer", "array_ptr_2d_first", CASE_INT_FILE, "test/fixtures/pointer/array_ptr_2d_first.c", 2, 0},
    {"pointer", "param_int_ptr_subscript", CASE_INT_FILE, "test/fixtures/pointer/param_int_ptr_subscript.c", 15, 0},
    {"pointer", "param_char_ptr_subscript", CASE_INT_FILE, "test/fixtures/pointer/param_char_ptr_subscript.c", 6, 0},
    {"pointer", "param_short_ptr_subscript", CASE_INT_FILE, "test/fixtures/pointer/param_short_ptr_subscript.c", 15, 0},
    {"pointer", "param_int_pp_double_deref", CASE_INT_FILE, "test/fixtures/pointer/param_int_pp_double_deref.c", 12, 0},
    {"pointer", "funcptr_array_assign_and_call", CASE_INT_FILE, "test/fixtures/pointer/funcptr_array_assign_and_call.c", 20, 0},
    {"pointer", "funcptr_array_brace_init", CASE_INT_FILE, "test/fixtures/pointer/funcptr_array_brace_init.c", 20, 0},
    {"pointer", "funcptr_array_typedef_brace_init", CASE_INT_FILE, "test/fixtures/pointer/funcptr_array_typedef_brace_init.c", 17, 0},
    {"pointer", "funcptr_array_inferred_size", CASE_INT_FILE, "test/fixtures/pointer/funcptr_array_inferred_size.c", 32, 0},

    {"array", "idx", CASE_INT_FILE, "test/fixtures/array/idx.c", 3, 0},
    {"array", "brace_init", CASE_INT_FILE, "test/fixtures/array/brace_init.c", 3, 0},
    {"array", "brace_init_designated", CASE_INT_FILE, "test/fixtures/array/brace_init_designated.c", 8, 0},
    {"array", "brace_init_partial_zeroed", CASE_INT_FILE, "test/fixtures/array/brace_init_partial_zeroed.c", 3, 0},
    {"array", "brace_init_designated_gap", CASE_INT_FILE, "test/fixtures/array/brace_init_designated_gap.c", 101, 0},
    {"array", "sizeof_array_div_elem", CASE_INT_FILE, "test/fixtures/array/sizeof_array_div_elem.c", 10, 0},
    {"array", "struct_array_brace_init", CASE_INT_FILE, "test/fixtures/array/struct_array_brace_init.c", 11, 0},
    {"array", "struct_array_brace_partial", CASE_INT_FILE, "test/fixtures/array/struct_array_brace_partial.c", 60, 0},
    {"array", "char_array_string_init", CASE_INT_FILE, "test/fixtures/array/char_array_string_init.c", 99, 0},
    {"array", "inferred_size_brace", CASE_INT_FILE, "test/fixtures/array/inferred_size_brace.c", 100, 0},
    {"array", "inferred_size_brace_trailing_comma", CASE_INT_FILE, "test/fixtures/array/inferred_size_trailing_comma.c", 15, 0},
    {"array", "inferred_size_string", CASE_INT_FILE, "test/fixtures/array/inferred_size_string.c", 215, 0},
    {"array", "inferred_size_char_brace", CASE_INT_FILE, "test/fixtures/array/inferred_size_char_brace.c", 209, 0},
    {"array", "inferred_size_string_concat", CASE_INT_FILE, "test/fixtures/array/inferred_size_string_concat.c", 199, 0},
    {"array", "inferred_size_designated", CASE_INT_FILE, "test/fixtures/array/inferred_size_designated.c", 111, 0},
    {"array", "inferred_size_2d_nested", CASE_INT_FILE, "test/fixtures/array/inferred_size_2d_nested.c", 7, 0},
    {"array", "inferred_size_2d_flat", CASE_INT_FILE, "test/fixtures/array/inferred_size_2d_flat.c", 7, 0},
    {"array", "inferred_size_2d_three_rows", CASE_INT_FILE, "test/fixtures/array/inferred_size_2d_three_rows.c", 9, 0},
    {"array", "brace_wrapped_string_init", CASE_INT_FILE, "test/fixtures/array/brace_wrapped_string_init.c", 215, 0},
    {"array", "brace_wrapped_string_explicit_size", CASE_INT_FILE, "test/fixtures/array/brace_wrapped_string_explicit_size.c", 'h'+'i'+1, 0},
    {"array", "brace_wrapped_string_concat", CASE_INT_FILE, "test/fixtures/array/brace_wrapped_string_concat.c", 'a'+'e'+1, 0},
    {"array", "three_dim_assign_read", CASE_INT_FILE, "test/fixtures/array/three_dim_assign_read.c", 12, 0},
    {"array", "three_dim_flat_init", CASE_INT_FILE, "test/fixtures/array/three_dim_flat_init.c", 12, 0},
    {"array", "three_dim_nested_init", CASE_INT_FILE, "test/fixtures/array/three_dim_nested_init.c", 12, 0},
    {"array", "three_dim_inferred_outer", CASE_INT_FILE, "test/fixtures/array/three_dim_inferred_outer.c", 8, 0},
    {"array", "param_2d_array_subscript", CASE_INT_FILE, "test/fixtures/array/param_2d_array_subscript.c", 6, 0},
    {"array", "param_2d_array_explicit_outer", CASE_INT_FILE, "test/fixtures/array/param_2d_array_explicit_outer.c", 7, 0},
    {"array", "param_3d_array_subscript", CASE_INT_FILE, "test/fixtures/array/param_3d_array_subscript.c", 9, 0},
    {"array", "four_dim_assign_read", CASE_INT_FILE, "test/fixtures/array/four_dim_assign_read.c", 99, 0},
    {"array", "four_dim_flat_init", CASE_INT_FILE, "test/fixtures/array/four_dim_flat_init.c", 24, 0},
    {"array", "four_dim_nested_init", CASE_INT_FILE, "test/fixtures/array/four_dim_nested_init.c", 24, 0},
    {"array", "four_dim_inferred_outer", CASE_INT_FILE, "test/fixtures/array/four_dim_inferred_outer.c", 18, 0},
    {"array", "five_dim_assign_read", CASE_INT_FILE, "test/fixtures/array/five_dim_assign_read.c", 77, 0},
    {"array", "param_explicit_ptr_to_2d", CASE_INT_FILE, "test/fixtures/array/param_explicit_ptr_to_2d.c", 6, 0},
    {"array", "param_explicit_ptr_to_3d", CASE_INT_FILE, "test/fixtures/array/param_explicit_ptr_to_3d.c", 9, 0},
    {"array", "param_typedef_array_ptr", CASE_INT_FILE, "test/fixtures/array/param_typedef_array_ptr.c", 6, 0},
    {"array", "param_typedef_array_ptr_sum", CASE_INT_FILE, "test/fixtures/array/param_typedef_array_ptr_sum.c", 45, 0},
    {"array", "param_typedef_2d_array_ptr", CASE_INT_FILE, "test/fixtures/array/param_typedef_2d_array_ptr.c", 23, 0},
    {"array", "sum", CASE_INT_FILE, "test/fixtures/array/sum.c", 6, 0},
    {"array", "const_expr_size", CASE_INT_FILE, "test/fixtures/array/const_expr_size.c", 3, 0},
    {"array", "multi_dim_decl", CASE_INT_FILE, "test/fixtures/array/multi_dim_decl.c", 7, 0},
    {"array", "multi_dim_init", CASE_INT_FILE, "test/fixtures/array/multi_dim_init.c", 6, 0},
    {"array", "multi_dim_init_sum", CASE_INT_FILE, "test/fixtures/array/multi_dim_init_sum.c", 7, 0},
    {"array", "loop", CASE_INT_FILE, "test/fixtures/array/loop.c", 55, 0},

    {"string", "deref", CASE_INT_FILE, "test/fixtures/string/deref.c", 65, 0},
    {"string", "index", CASE_INT_FILE, "test/fixtures/string/index.c", 66, 0},
    {"string", "empty", CASE_INT_FILE, "test/fixtures/string/empty.c", 0, 0},
    {"string", "charlit", CASE_INT_FILE, "test/fixtures/string/charlit.c", 65, 0},
    {"string", "newline", CASE_INT_FILE, "test/fixtures/string/newline.c", 10, 0},
    {"string", "nul", CASE_INT_FILE, "test/fixtures/string/nul.c", 0, 0},
    {"string", "buf_idx", CASE_INT_FILE, "test/fixtures/string/buf_idx.c", 3, 0},
    {"string", "buf_sum", CASE_INT_FILE, "test/fixtures/string/buf_sum.c", 6, 0},
    {"string", "char_var", CASE_INT_FILE, "test/fixtures/string/char_var.c", 42, 0},
    // ビットフィールド
    {"bitfield", "read",   CASE_INT_FILE, "test/fixtures/bitfield/read.c", 5, 0},
    {"bitfield", "read_b", CASE_INT_FILE, "test/fixtures/bitfield/read_b.c", 10, 0},
    {"bitfield", "write_masked", CASE_INT_FILE, "test/fixtures/bitfield/write_masked.c", 15, 0},
    {"bitfield", "packing", CASE_INT_FILE, "test/fixtures/bitfield/packing.c", 8, 0},
    {"bitfield", "signed_neg", CASE_INT_FILE, "test/fixtures/bitfield/signed_neg.c", 42, 0},
    {"bitfield", "unsigned_wrap", CASE_INT_FILE, "test/fixtures/bitfield/unsigned_wrap.c", 1, 0},
    // _Alignas
    {"alignas", "lvar_value",  CASE_INT_FILE, "test/fixtures/alignas/lvar_value.c", 42, 0},
    {"alignas", "lvar_align",  CASE_INT_FILE, "test/fixtures/alignas/lvar_align.c", 42, 0},
    {"alignas", "struct_member", CASE_INT_FILE, "test/fixtures/alignas/struct_member.c", 42, 0},
    {"alignas", "global_var", CASE_INT_FILE, "test/fixtures/alignas/global_var.c", 7, 0},
    {"alignas", "alignas_alignof", CASE_INT_FILE, "test/fixtures/alignas/alignas_alignof.c", 42, 0},
    // フレキシブル配列メンバー
    {"flex_array", "sizeof_flex", CASE_INT_FILE, "test/fixtures/flex_array/sizeof_flex.c", 4, 0},
    {"flex_array", "parse_ok", CASE_INT_FILE, "test/fixtures/flex_array/parse_ok.c", 0, 0},
    {"flex_array", "alloc_and_use", CASE_INT_FILE, "test/fixtures/flex_array/alloc_and_use.c", 42, 0},
    // tokenizer 拡張機能: 文字列接頭辞、UCN、トライグラフ
    {"tokenizer", "wide_string_L", CASE_INT_FILE, "test/fixtures/tokenizer/wide_string_L.c", 65, 0},
    {"tokenizer", "u8_string", CASE_INT_FILE, "test/fixtures/tokenizer/u8_string.c", 132, 0},
    {"tokenizer", "u_string", CASE_INT_FILE, "test/fixtures/tokenizer/u_string.c", 65, 0},
    {"tokenizer", "u32_string", CASE_INT_FILE, "test/fixtures/tokenizer/u32_string.c", 65, 0},
    {"tokenizer", "charlit_L", CASE_INT_FILE, "test/fixtures/tokenizer/charlit_L.c", 65, 0},
    {"tokenizer", "charlit_u", CASE_INT_FILE, "test/fixtures/tokenizer/charlit_u.c", 65, 0},
    {"tokenizer", "string_concat_prefix", CASE_INT_FILE, "test/fixtures/tokenizer/string_concat_prefix.c", 133, 0},
    {"tokenizer", "ucn_string", CASE_INT_FILE, "test/fixtures/tokenizer/ucn_string.c", 108, 0},
    {"tokenizer", "ucn_string_3byte", CASE_INT_FILE, "test/fixtures/tokenizer/ucn_string_3byte.c", 230, 0},
    {"tokenizer", "ucn_string_u16_surrogate", CASE_INT_FILE, "test/fixtures/tokenizer/ucn_string_u16_surrogate.c", 222, 0},
    {"tokenizer", "ucn_string_u16_bmp", CASE_INT_FILE, "test/fixtures/tokenizer/ucn_string_u16_bmp.c", 255, 0},
    {"tokenizer", "ucn_string_u16_mix", CASE_INT_FILE, "test/fixtures/tokenizer/ucn_string_u16_mix.c", 99, 0},
    {"tokenizer", "ucn_string_u32", CASE_INT_FILE, "test/fixtures/tokenizer/ucn_string_u32.c", 246, 0},
    {"tokenizer", "ucn_ident", CASE_INT_FILE, "test/fixtures/tokenizer/ucn_ident.c", 7, 0},
    {"tokenizer", "trigraph_or", CASE_INT_FILE, "test/fixtures/tokenizer/trigraph_or.c", 7, 0},
    {"tokenizer", "trigraph_xor", CASE_INT_FILE, "test/fixtures/tokenizer/trigraph_xor.c", 6, 0},
    // #pragma pack
    {"pragma_pack", "pack1_sizeof", CASE_INT_FILE, "test/fixtures/pragma_pack/pack1_sizeof.c", 5, 0},
    {"pragma_pack", "pack1_offset", CASE_INT_FILE, "test/fixtures/pragma_pack/pack1_offset.c", 42, 0},
    {"pragma_pack", "pack2_sizeof", CASE_INT_FILE, "test/fixtures/pragma_pack/pack2_sizeof.c", 6, 0},
    {"pragma_pack", "pop_restores", CASE_INT_FILE, "test/fixtures/pragma_pack/pop_restores.c", 8, 0},
    {"pragma_pack", "pack_n_no_push", CASE_INT_FILE, "test/fixtures/pragma_pack/pack_n_no_push.c", 5, 0},
    // 標準ヘッダ
    {"stdheader", "stdint_int32", CASE_INT_FILE, "test/fixtures/stdheader/stdint_int32.c", 42, 0},
    {"stdheader", "stdint_uint8", CASE_INT_FILE, "test/fixtures/stdheader/stdint_uint8.c", 200, 0},
    {"stdheader", "stdbool_true", CASE_INT_FILE, "test/fixtures/stdheader/stdbool_true.c", 42, 0},
    {"stdheader", "stdbool_false", CASE_INT_FILE, "test/fixtures/stdheader/stdbool_false.c", 0, 0},
    {"stdheader", "stddef_size_t", CASE_INT_FILE, "test/fixtures/stdheader/stddef_size_t.c", 10, 0},
    {"stdheader", "stddef_null", CASE_INT_FILE, "test/fixtures/stdheader/stddef_null.c", 42, 0},
    {"stdheader", "stddef_wchar_t", CASE_INT_FILE, "test/fixtures/stdheader/stddef_wchar_t.c", 42, 0},
    {"stdheader", "stddef_max_align_t", CASE_INT_FILE, "test/fixtures/stdheader/stddef_max_align_t.c", 42, 0},
    {"stdheader", "limits_int_max", CASE_INT_FILE, "test/fixtures/stdheader/limits_int_max.c", 42, 0},
    {"stdheader", "limits_int_min", CASE_INT_FILE, "test/fixtures/stdheader/limits_int_min.c", 42, 0},
    {"stdheader", "limits_char_bit", CASE_INT_FILE, "test/fixtures/stdheader/limits_char_bit.c", 42, 0},
    {"stdheader", "float_flt_max", CASE_INT_FILE, "test/fixtures/stdheader/float_flt_max.c", 42, 0},
    {"stdheader", "float_dbl_epsilon", CASE_INT_FILE, "test/fixtures/stdheader/float_dbl_epsilon.c", 42, 0},
    {"stdheader", "float_flt_radix", CASE_INT_FILE, "test/fixtures/stdheader/float_flt_radix.c", 42, 0},
    {"stdheader", "string_strlen", CASE_INT_FILE, "test/fixtures/stdheader/string_strlen.c", 5, 0},
    {"stdheader", "string_strcmp", CASE_INT_FILE, "test/fixtures/stdheader/string_strcmp.c", 42, 0},
    {"stdheader", "stdlib_malloc_free", CASE_INT_FILE, "test/fixtures/stdheader/stdlib_malloc_free.c", 42, 0},
    {"stdheader", "stdlib_atoi", CASE_INT_FILE, "test/fixtures/stdheader/stdlib_atoi.c", 42, 0},
    {"stdheader", "stdlib_abs", CASE_INT_FILE, "test/fixtures/stdheader/stdlib_abs.c", 42, 0},
    {"stdheader", "string_memset", CASE_INT_FILE, "test/fixtures/stdheader/string_memset.c", 42, 0},
    {"stdheader", "ctype_isdigit", CASE_INT_FILE, "test/fixtures/stdheader/ctype_isdigit.c", 42, 0},
    {"stdheader", "ctype_isalpha", CASE_INT_FILE, "test/fixtures/stdheader/ctype_isalpha.c", 42, 0},
    {"stdheader", "ctype_toupper", CASE_INT_FILE, "test/fixtures/stdheader/ctype_toupper.c", 65, 0},
    {"stdheader", "math_include", CASE_INT_FILE, "test/fixtures/stdheader/math_include.c", 42, 0},
    {"stdheader", "assert_include", CASE_INT_FILE, "test/fixtures/stdheader/assert_include.c", 42, 0},
    {"stdheader", "errno_include", CASE_INT_FILE, "test/fixtures/stdheader/errno_include.c", 42, 0},
    {"stdheader", "signal_include", CASE_INT_FILE, "test/fixtures/stdheader/signal_include.c", 42, 0},
    {"stdheader", "time_include", CASE_INT_FILE, "test/fixtures/stdheader/time_include.c", 42, 0},
    {"stdheader", "setjmp_include", CASE_INT_FILE, "test/fixtures/stdheader/setjmp_include.c", 42, 0},
    // stdarg
    {"stdarg", "va_arg_int", CASE_INT_FILE, "test/fixtures/stdarg/va_arg_int.c", 42, 0},
    {"stdarg", "va_arg_double", CASE_INT_FILE, "test/fixtures/stdarg/va_arg_double.c", 3, 0},
    {"stdarg", "va_arg_mix", CASE_INT_FILE, "test/fixtures/stdarg/va_arg_mix.c", 19, 0},
    {"stdarg", "va_arg_many_int", CASE_INT_FILE, "test/fixtures/stdarg/va_arg_many_int.c", 136, 0},
    {"stdarg", "va_copy", CASE_INT_FILE, "test/fixtures/stdarg/va_copy.c", 12, 0},
    {"stdarg", "va_copy_func", CASE_INT_FILE, "test/fixtures/stdarg/va_copy_func.c", 120, 0},
    {"stdarg", "printf_fp_mix", CASE_INT_FILE, "test/fixtures/stdarg/printf_fp_mix.c", 0, 0},

    // VLA (Variable Length Array)
    {"vla", "basic_elem", CASE_INT_FILE, "test/fixtures/vla/basic_elem.c", 42, 0},
    {"vla", "loop_fill", CASE_INT_FILE, "test/fixtures/vla/loop_fill.c", 10, 0},
    {"vla", "param_size", CASE_INT_FILE, "test/fixtures/vla/param_size.c", 10, 0},
    {"vla", "sizeof_vla", CASE_INT_FILE, "test/fixtures/vla/sizeof_vla.c", 12, 0},
    // 構造体引数渡し (ARM64 ABI)
    {"struct_arg", "small_sum", CASE_INT_FILE, "test/fixtures/struct_arg/small_sum.c", 7, 0},
    {"struct_arg", "small_member", CASE_INT_FILE, "test/fixtures/struct_arg/small_member.c", 42, 0},
    {"struct_arg", "mid_sum", CASE_INT_FILE, "test/fixtures/struct_arg/mid_sum.c", 42, 0},
    {"struct_arg", "large_sum", CASE_INT_FILE, "test/fixtures/struct_arg/large_sum.c", 15, 0},
    // struct return value (≤8B)
    {"struct_ret", "make_and_sum", CASE_INT_FILE, "test/fixtures/struct_ret/make_and_sum.c", 42, 0},
    {"struct_ret", "return_member", CASE_INT_FILE, "test/fixtures/struct_ret/return_member.c", 42, 0},
    {"struct_ret", "chain_call", CASE_INT_FILE, "test/fixtures/struct_ret/chain_call.c", 42, 0},
    // struct return value (9-16B: x0/x1 pair)
    {"struct_ret", "ret_12b_sum", CASE_INT_FILE, "test/fixtures/struct_ret/ret_12b_sum.c", 42, 0},
    {"struct_ret", "ret_16b_sum", CASE_INT_FILE, "test/fixtures/struct_ret/ret_16b_sum.c", 42, 0},
    {"struct_ret", "ret_12b_member_c", CASE_INT_FILE, "test/fixtures/struct_ret/ret_12b_member_c.c", 12, 0},
    // struct return value (>16B: indirect return via x8)
    {"struct_ret", "ret_20b_indirect", CASE_INT_FILE, "test/fixtures/struct_ret/ret_20b_indirect.c", 35, 0},
    {"struct_ret", "ret_24b_member_f", CASE_INT_FILE, "test/fixtures/struct_ret/ret_24b_member_f.c", 6, 0},
    {"struct_ret", "ret_40b_sum", CASE_INT_FILE, "test/fixtures/struct_ret/ret_40b_sum.c", 55, 0},
    // __func__ 定義済み識別子
    {"func_name", "first_char_main", CASE_INT_FILE, "test/fixtures/func_name/first_char_main.c", 109, 0},
    {"func_name", "first_char_helper", CASE_INT_FILE, "test/fixtures/func_name/first_char_helper.c", 104, 0},
    {"func_name", "each_func_distinct", CASE_INT_FILE, "test/fixtures/func_name/each_func_distinct.c", 42, 0},
    // 2D VLA: constant inner dimension
    {"vla_2d", "const_inner_read", CASE_INT_FILE, "test/fixtures/vla_2d/const_inner_read.c", 113, 0},
    {"vla_2d", "const_inner_loop", CASE_INT_FILE, "test/fixtures/vla_2d/const_inner_loop.c", 15, 0},
    // 2D VLA: runtime inner dimension
    {"vla_2d", "runtime_inner_read", CASE_INT_FILE, "test/fixtures/vla_2d/runtime_inner_read.c", 42, 0},
    {"vla_2d", "runtime_inner_loop", CASE_INT_FILE, "test/fixtures/vla_2d/runtime_inner_loop.c", 66, 0},
    // 仮引数 VLA 宣言子: int a[n] → int *a (C11 6.7.6.3p7)
    {"vla_param", "basic_access", CASE_INT_FILE, "test/fixtures/vla_param/basic_access.c", 15, 0},
    {"vla_param", "sizeof_is_ptr", CASE_INT_FILE, "test/fixtures/vla_param/sizeof_is_ptr.c", 8, 0},
    {"vla_param", "write_through", CASE_INT_FILE, "test/fixtures/vla_param/write_through.c", 42, 0},
    // inline 指定子: 単一翻訳単位では通常関数と同様にコード生成 (C11 6.7.4)
    {"inline_func", "basic_inline", CASE_INT_FILE, "test/fixtures/inline_func/basic_inline.c", 42, 0},
    {"inline_func", "static_inline", CASE_INT_FILE, "test/fixtures/inline_func/static_inline.c", 42, 0},
    {"inline_func", "extern_inline", CASE_INT_FILE, "test/fixtures/inline_func/extern_inline.c", 42, 0},
    {"inline_func", "multi_inline", CASE_INT_FILE, "test/fixtures/inline_func/multi_inline.c", 17, 0},
    // グローバル変数: 暫定定義
    {"global_var", "tentative_rw", CASE_INT_FILE, "test/fixtures/global_var/tentative_rw.c", 42, 0},
    {"global_var", "tentative_multi_func", CASE_INT_FILE, "test/fixtures/global_var/tentative_multi_func.c", 42, 0},
    // グローバル変数: 初期化済み定義
    {"global_var", "initialized", CASE_INT_FILE, "test/fixtures/global_var/initialized.c", 42, 0},
    {"global_var", "initialized_modified", CASE_INT_FILE, "test/fixtures/global_var/initialized_modified.c", 42, 0},
    // ローカルスコープのextern宣言
    {"global_var", "local_extern", CASE_INT_FILE, "test/fixtures/global_var/local_extern.c", 42, 0},
    {"global_var", "array_rw", CASE_INT_FILE, "test/fixtures/global_var/array_rw.c", 20, 0},
    {"global_var", "array_sum", CASE_INT_FILE, "test/fixtures/global_var/array_sum.c", 6, 0},
    {"global_var", "global_struct_init", CASE_INT_FILE, "test/fixtures/global_var/global_struct_init.c", 42, 0},
    {"global_var", "global_struct_assign", CASE_INT_FILE, "test/fixtures/global_var/global_struct_assign.c", 42, 0},
    // 意地悪テスト: 各種エッジケース (fixture 化済み)
    {"evil", "dowhile_break", CASE_INT_FILE, "test/fixtures/evil/dowhile_break.c", 3, 0},
    {"evil", "dowhile_continue", CASE_INT_FILE, "test/fixtures/evil/dowhile_continue.c", 9, 0},
    {"evil", "sizeof_no_eval", CASE_INT_FILE, "test/fixtures/evil/sizeof_no_eval.c", 0, 0},
    {"evil", "nested_struct", CASE_INT_FILE, "test/fixtures/evil/nested_struct.c", 65, 0},
    {"evil", "assign_in_cond", CASE_INT_FILE, "test/fixtures/evil/assign_in_cond.c", 5, 0},
    {"evil", "mutual_recursion", CASE_INT_FILE, "test/fixtures/evil/mutual_recursion.c", 11, 0},
    {"evil", "nested_call", CASE_INT_FILE, "test/fixtures/evil/nested_call.c", 15, 0},
    {"evil", "char_subtract", CASE_INT_FILE, "test/fixtures/evil/char_subtract.c", 9, 0},
    {"evil", "char_overflow", CASE_INT_FILE, "test/fixtures/evil/char_overflow.c", 1, 0},
    {"evil", "struct_array_member", CASE_INT_FILE, "test/fixtures/evil/struct_array_member.c", 100, 0},
    {"evil", "collatz_recursion", CASE_INT_FILE, "test/fixtures/evil/collatz_recursion.c", 9, 0},
    {"evil", "complex_expr_8vars", CASE_INT_FILE, "test/fixtures/evil/complex_expr_8vars.c", 90, 0},
    {"evil", "uchar_wrap", CASE_INT_FILE, "test/fixtures/evil/uchar_wrap.c", 44, 0},
    {"evil", "multi_shift", CASE_INT_FILE, "test/fixtures/evil/multi_shift.c", 30, 0},
    {"evil", "global_sideeffect_seq", CASE_INT_FILE, "test/fixtures/evil/global_sideeffect_seq.c", 123, 0},
    {"evil", "deref_dot_vs_arrow", CASE_INT_FILE, "test/fixtures/evil/deref_dot_vs_arrow.c", 10, 0},
    {"evil", "addr_deref_chain", CASE_INT_FILE, "test/fixtures/evil/addr_deref_chain.c", 42, 0},
    {"evil", "logical_not_zero", CASE_INT_FILE, "test/fixtures/evil/logical_not_zero.c", 1, 0},
    {"evil", "logical_not_nonzero", CASE_INT_FILE, "test/fixtures/evil/logical_not_nonzero.c", 0, 0},
    {"evil", "bitwise_not", CASE_INT_FILE, "test/fixtures/evil/bitwise_not.c", 0, 0},
    {"evil", "cast_uchar_neg", CASE_INT_FILE, "test/fixtures/evil/cast_uchar_neg.c", 255, 0},
    {"evil", "struct_padding_sizeof", CASE_INT_FILE, "test/fixtures/evil/struct_padding_sizeof.c", 12, 0},
    {"evil", "struct_ptr_reassign", CASE_INT_FILE, "test/fixtures/evil/struct_ptr_reassign.c", 30, 0},
    {"evil", "ptr_read_then_clear", CASE_INT_FILE, "test/fixtures/evil/ptr_read_then_clear.c", 42, 0},
    {"evil", "max3_nested", CASE_INT_FILE, "test/fixtures/evil/max3_nested.c", 42, 0},
    {"evil", "nested_for_loops", CASE_INT_FILE, "test/fixtures/evil/nested_for_loops.c", 12, 0},
    {"evil", "while1_break", CASE_INT_FILE, "test/fixtures/evil/while1_break.c", 64, 0},
    {"evil", "null_stmt", CASE_INT_FILE, "test/fixtures/evil/null_stmt.c", 0, 0},
    {"evil", "null_stmt_mixed", CASE_INT_FILE, "test/fixtures/evil/null_stmt_mixed.c", 3, 0},
    {"evil", "anon_enum_assign", CASE_INT_FILE, "test/fixtures/evil/anon_enum_assign.c", 20, 0},
    {"evil", "anon_enum_negative", CASE_INT_FILE, "test/fixtures/evil/anon_enum_negative.c", 0, 0},
    {"evil", "post_const_int", CASE_INT_FILE, "test/fixtures/evil/post_const_int.c", 42, 0},
    {"evil", "post_const_char", CASE_INT_FILE, "test/fixtures/evil/post_const_char.c", 65, 0},
    {"evil", "large_imm_mod", CASE_INT_FILE, "test/fixtures/evil/large_imm_mod.c", 160, 0},
    {"evil", "large_imm_var", CASE_INT_FILE, "test/fixtures/evil/large_imm_var.c", 64, 0},
    {"evil", "block_shadow", CASE_INT_FILE, "test/fixtures/evil/block_shadow.c", 10, 0},
    {"evil", "for_scope_shadow", CASE_INT_FILE, "test/fixtures/evil/for_scope_shadow.c", 99, 0},
    {"evil", "nested_shadow", CASE_INT_FILE, "test/fixtures/evil/nested_shadow.c", 1, 0},
    {"evil", "signed_cmp_neg", CASE_INT_FILE, "test/fixtures/evil/signed_cmp_neg.c", 0, 0},
    {"evil", "signed_cmp_lt", CASE_INT_FILE, "test/fixtures/evil/signed_cmp_lt.c", 1, 0},
    {"evil", "self_ref_struct", CASE_INT_FILE, "test/fixtures/evil/self_ref_struct.c", 42, 0},
    {"evil", "static_assert_sizeof", CASE_INT_FILE, "test/fixtures/evil/static_assert_sizeof.c", 0, 0},
    // overflow / sign boundary tests
    {"evil", "int_max_plus1_wraps", CASE_INT_FILE, "test/fixtures/evil/int_max_plus1_wraps.c", 1, 0},
    {"evil", "uint_max_plus1_zero", CASE_INT_FILE, "test/fixtures/evil/uint_max_plus1_zero.c", 1, 0},
    {"evil", "uint_sub_wrap", CASE_INT_FILE, "test/fixtures/evil/uint_sub_wrap.c", 1, 0},
    {"evil", "uint_mul_wrap", CASE_INT_FILE, "test/fixtures/evil/uint_mul_wrap.c", 1, 0},
    {"evil", "uint_shr_no_signext", CASE_INT_FILE, "test/fixtures/evil/uint_shr_no_signext.c", 1, 0},
    {"evil", "char_127_plus1", CASE_INT_FILE, "test/fixtures/evil/char_127_plus1.c", 1, 0},
    {"evil", "char_neg_to_uint", CASE_INT_FILE, "test/fixtures/evil/char_neg_to_uint.c", 1, 0},
    {"evil", "neg_div_truncate", CASE_INT_FILE, "test/fixtures/evil/neg_div_truncate.c", 2, 0},
    {"evil", "uint_div_large", CASE_INT_FILE, "test/fixtures/evil/uint_div_large.c", 1, 0},
    {"evil", "int_max_inc_wraps", CASE_INT_FILE, "test/fixtures/evil/int_max_inc_wraps.c", 1, 0},
    // NaN / Infinity edge cases
    {"evil", "nan_ne_self", CASE_INT_FILE, "test/fixtures/evil/nan_ne_self.c", 1, 0},
    {"evil", "nan_eq_self_false", CASE_INT_FILE, "test/fixtures/evil/nan_eq_self_false.c", 1, 0},
    {"evil", "nan_lt_false", CASE_INT_FILE, "test/fixtures/evil/nan_lt_false.c", 1, 0},
    {"evil", "nan_gt_false", CASE_INT_FILE, "test/fixtures/evil/nan_gt_false.c", 1, 0},
    {"evil", "nan_ge_false", CASE_INT_FILE, "test/fixtures/evil/nan_ge_false.c", 1, 0},
    {"evil", "inf_positive", CASE_INT_FILE, "test/fixtures/evil/inf_positive.c", 1, 0},
    {"evil", "inf_negative", CASE_INT_FILE, "test/fixtures/evil/inf_negative.c", 1, 0},
    {"evil", "inf_plus_neginf_nan", CASE_INT_FILE, "test/fixtures/evil/inf_plus_neginf_nan.c", 1, 0},

    /* 差分テストで発見したバグの fixture (test/fixtures/probes_found_bugs/)。
     * 各 fixture は ag_c と system cc で同じ exit code を返すことを確認する。 */
    {"probes", "anon_union_member", CASE_INT_FILE, "test/fixtures/probes_found_bugs/anon_union_member.c", 52, 0},
    {"probes", "bool_normalization", CASE_INT_FILE, "test/fixtures/probes_found_bugs/bool_normalization.c", 2, 0},
    {"probes", "bool_array_element_normalize", CASE_INT_FILE, "test/fixtures/probes_found_bugs/bool_array_element_normalize.c", 13, 0},
    {"probes", "bool_struct_array_member_normalize", CASE_INT_FILE, "test/fixtures/probes_found_bugs/bool_struct_array_member_normalize.c", 35, 0},
    {"probes", "bool_2d_array_normalize", CASE_INT_FILE, "test/fixtures/probes_found_bugs/bool_2d_array_normalize.c", 4, 0},
    {"probes", "bool_func_return_normalize", CASE_INT_FILE, "test/fixtures/probes_found_bugs/bool_func_return_normalize.c", 17, 0},
    {"probes", "bool_struct_member", CASE_INT_FILE, "test/fixtures/probes_found_bugs/bool_struct_member.c", 41, 0},
    {"probes", "bitfield_brace_init", CASE_INT_FILE, "test/fixtures/probes_found_bugs/bitfield_brace_init.c", 89, 0},
    {"probes", "char_ptr_postinc_deref", CASE_INT_FILE, "test/fixtures/probes_found_bugs/char_ptr_postinc_deref.c", 6, 0},
    {"probes", "const_struct", CASE_INT_FILE, "test/fixtures/probes_found_bugs/const_struct.c", 42, 0},
    {"probes", "double_array", CASE_INT_FILE, "test/fixtures/probes_found_bugs/double_array.c", 7, 0},
    {"probes", "func_returning_funcptr", CASE_INT_FILE, "test/fixtures/probes_found_bugs/func_returning_funcptr.c", 25, 0},
    {"probes", "funcret_subscript", CASE_INT_FILE, "test/fixtures/probes_found_bugs/funcret_subscript.c", 80, 0},
    {"probes", "integer_indexes_array", CASE_INT_FILE, "test/fixtures/probes_found_bugs/integer_indexes_array.c", 8, 0},
    {"probes", "int_plus_pointer", CASE_INT_FILE, "test/fixtures/probes_found_bugs/int_plus_pointer.c", 0, 0},
    {"probes", "scalar_pointer_member_subscript", CASE_INT_FILE, "test/fixtures/probes_found_bugs/scalar_pointer_member_subscript.c", 144, 0},
    {"probes", "struct_array_param", CASE_INT_FILE, "test/fixtures/probes_found_bugs/struct_array_param.c", 100, 0},
    {"probes", "static_local_int_array", CASE_INT_FILE, "test/fixtures/probes_found_bugs/static_local_int_array.c", 147, 0},
    {"probes", "global_scalar_ptr_array_subscript", CASE_INT_FILE, "test/fixtures/probes_found_bugs/global_scalar_ptr_array_subscript.c", 47, 0},
    {"probes", "funcptr_array_param", CASE_INT_FILE, "test/fixtures/probes_found_bugs/funcptr_array_param.c", 84, 0},
    {"probes", "array_designator_with_struct_designator", CASE_INT_FILE, "test/fixtures/probes_found_bugs/array_designator_with_struct_designator.c", 33, 0},
    {"probes", "cast_to_struct_pointer", CASE_INT_FILE, "test/fixtures/probes_found_bugs/cast_to_struct_pointer.c", 20, 0},
    {"probes", "global_double_const_expr_init", CASE_INT_FILE, "test/fixtures/probes_found_bugs/global_double_const_expr_init.c", 40, 0},
    {"probes", "funcptr_array_compound_literal", CASE_INT_FILE, "test/fixtures/probes_found_bugs/funcptr_array_compound_literal.c", 19, 0},
    {"probes", "global_struct_with_array_member", CASE_INT_FILE, "test/fixtures/probes_found_bugs/global_struct_with_array_member.c", 120, 0},
    {"probes", "ptr_to_funcptr_direct_deref", CASE_INT_FILE, "test/fixtures/probes_found_bugs/ptr_to_funcptr_direct_deref.c", 21, 0},
    {"probes", "global_char_array_string_size", CASE_INT_FILE, "test/fixtures/probes_found_bugs/global_char_array_string_size.c", 209, 0},
    {"probes", "global_designator", CASE_INT_FILE, "test/fixtures/probes_found_bugs/global_designator.c", 30, 0},
    {"probes", "global_const_int_expr_init", CASE_INT_FILE, "test/fixtures/probes_found_bugs/global_const_int_expr_init.c", 35, 0},
    {"probes", "global_double_scalar", CASE_INT_FILE, "test/fixtures/probes_found_bugs/global_double_scalar.c", 14, 0},
    {"probes", "global_double_array", CASE_INT_FILE, "test/fixtures/probes_found_bugs/global_double_array.c", 17, 0},
    {"probes", "designator_nested", CASE_INT_FILE, "test/fixtures/probes_found_bugs/designator_nested.c", 21, 0},
    {"probes", "struct_partial_init_zerofill", CASE_INT_FILE, "test/fixtures/probes_found_bugs/struct_partial_init_zerofill.c", 0, 0},
    {"probes", "struct_2d_array_nested_brace", CASE_INT_FILE, "test/fixtures/probes_found_bugs/struct_2d_array_nested_brace.c", 9, 0},
    {"probes", "char_array_string_partial_zerofill", CASE_INT_FILE, "test/fixtures/probes_found_bugs/char_array_string_partial_zerofill.c", 209, 0},
    {"probes", "const_pointer_reassign", CASE_INT_FILE, "test/fixtures/probes_found_bugs/const_pointer_reassign.c", 13, 0},
    {"probes", "sizeof_global_array_inferred_size", CASE_INT_FILE, "test/fixtures/probes_found_bugs/sizeof_global_array_inferred_size.c", 3, 0},
    {"probes", "global_funcptr_call", CASE_INT_FILE, "test/fixtures/probes_found_bugs/global_funcptr_call.c", 42, 0},
    {"probes", "global_str_ptr_array", CASE_INT_FILE, "test/fixtures/probes_found_bugs/global_str_ptr_array.c", 14, 0},
    {"probes", "global_string_ptr", CASE_INT_FILE, "test/fixtures/probes_found_bugs/global_string_ptr.c", 11, 0},
    {"probes", "global_struct_array", CASE_INT_FILE, "test/fixtures/probes_found_bugs/global_struct_array.c", 21, 0},
    {"probes", "global_struct_pointer", CASE_INT_FILE, "test/fixtures/probes_found_bugs/global_struct_pointer.c", 42, 0},
    {"probes", "global_struct_with_funcptr", CASE_INT_FILE, "test/fixtures/probes_found_bugs/global_struct_with_funcptr.c", 49, 0},
    {"probes", "many_double_params", CASE_INT_FILE, "test/fixtures/probes_found_bugs/many_double_params.c", 55, 0},
    {"probes", "int_arg_to_double_param", CASE_INT_FILE, "test/fixtures/probes_found_bugs/int_arg_to_double_param.c", 55, 0},
    {"probes", "many_int_params", CASE_INT_FILE, "test/fixtures/probes_found_bugs/many_int_params.c", 55, 0},
    {"probes", "negative_global", CASE_INT_FILE, "test/fixtures/probes_found_bugs/negative_global.c", 42, 0},
    {"probes", "nested_compound_literal", CASE_INT_FILE, "test/fixtures/probes_found_bugs/nested_compound_literal.c", 50, 0},
    {"probes", "pointer_compound_assign", CASE_INT_FILE, "test/fixtures/probes_found_bugs/pointer_compound_assign.c", 3, 0},
    {"probes", "ptr_to_array_deref", CASE_INT_FILE, "test/fixtures/probes_found_bugs/ptr_to_array_deref.c", 1, 0},
    {"probes", "ptr_to_array_p_plus_1", CASE_INT_FILE, "test/fixtures/probes_found_bugs/ptr_to_array_p_plus_1.c", 4, 0},
    {"probes", "short_postinc", CASE_INT_FILE, "test/fixtures/probes_found_bugs/short_postinc.c", 60, 0},
    {"probes", "sizeof_arith", CASE_INT_FILE, "test/fixtures/probes_found_bugs/sizeof_arith.c", 4, 0},
    {"probes", "sizeof_postinc", CASE_INT_FILE, "test/fixtures/probes_found_bugs/sizeof_postinc.c", 4, 0},
    {"probes", "sizeof_string_literal", CASE_INT_FILE, "test/fixtures/probes_found_bugs/sizeof_string_literal.c", 6, 0},
    {"probes", "string_escape_in_init", CASE_INT_FILE, "test/fixtures/probes_found_bugs/string_escape_in_init.c", 111, 0},
    {"probes", "struct_funcptr_array", CASE_INT_FILE, "test/fixtures/probes_found_bugs/struct_funcptr_array.c", 32, 0},
    {"probes", "struct_init_from_deref", CASE_INT_FILE, "test/fixtures/probes_found_bugs/struct_init_from_deref.c", 23, 0},
    {"probes", "struct_member_array_ptr", CASE_INT_FILE, "test/fixtures/probes_found_bugs/struct_member_array_ptr.c", 60, 0},
    {"probes", "struct_of_struct_of_array", CASE_INT_FILE, "test/fixtures/probes_found_bugs/struct_of_struct_of_array.c", 42, 0},
    {"probes", "struct_ptr_plus_arrow", CASE_INT_FILE, "test/fixtures/probes_found_bugs/struct_ptr_plus_arrow.c", 9, 0},
    {"probes", "struct_ptr_subscript_member", CASE_INT_FILE, "test/fixtures/probes_found_bugs/struct_ptr_subscript_member.c", 9, 0},
    {"probes", "struct_ternary_member", CASE_INT_FILE, "test/fixtures/probes_found_bugs/struct_ternary_member.c", 10, 0},
    {"probes", "struct_typedef_forward", CASE_INT_FILE, "test/fixtures/probes_found_bugs/struct_typedef_forward.c", 3, 0},
    {"probes", "struct_with_double", CASE_INT_FILE, "test/fixtures/probes_found_bugs/struct_with_double.c", 4, 0},
    {"probes", "typedef_array_param", CASE_INT_FILE, "test/fixtures/probes_found_bugs/typedef_array_param.c", 6, 0},
    {"probes", "vla_2d_param", CASE_INT_FILE, "test/fixtures/probes_found_bugs/vla_2d_param.c", 21, 0},
    {"probes", "cmp_wide_signed_vs_unsigned", CASE_INT_FILE, "test/fixtures/probes_found_bugs/cmp_wide_signed_vs_unsigned.c", 15, 0},
    {"probes", "cmp_narrow_unsigned_promote", CASE_INT_FILE, "test/fixtures/probes_found_bugs/cmp_narrow_unsigned_promote.c", 53, 0},
    {"probes", "cmp_same_width_unsigned", CASE_INT_FILE, "test/fixtures/probes_found_bugs/cmp_same_width_unsigned.c", 62, 0},
    {"probes", "array_nested_designator_2d", CASE_INT_FILE, "test/fixtures/probes_found_bugs/array_nested_designator_2d.c", 20, 0},
    {"probes", "array_nested_designator_3d", CASE_INT_FILE, "test/fixtures/probes_found_bugs/array_nested_designator_3d.c", 43, 0},
    {"probes", "array_designator_brace_mix", CASE_INT_FILE, "test/fixtures/probes_found_bugs/array_designator_brace_mix.c", 44, 0},
    {"probes", "div_wide_signed_by_unsigned", CASE_INT_FILE, "test/fixtures/probes_found_bugs/div_wide_signed_by_unsigned.c", 48, 0},
    {"probes", "mod_wide_signed_by_unsigned", CASE_INT_FILE, "test/fixtures/probes_found_bugs/mod_wide_signed_by_unsigned.c", 99, 0},
    {"probes", "int_literal_top_bit_set", CASE_INT_FILE, "test/fixtures/probes_found_bugs/int_literal_top_bit_set.c", 15, 0},
    {"probes", "compound_assign_index_side_effect", CASE_INT_FILE, "test/fixtures/probes_found_bugs/compound_assign_index_side_effect.c", 15, 0},
    {"probes", "switch_case_long_label", CASE_INT_FILE, "test/fixtures/probes_found_bugs/switch_case_long_label.c", 42, 0},
    {"probes", "macro_arg_nested_same_name", CASE_INT_FILE, "test/fixtures/probes_found_bugs/macro_arg_nested_same_name.c", 10, 0},
    {"probes", "variadic_macro_forward", CASE_INT_FILE, "test/fixtures/probes_found_bugs/variadic_macro_forward.c", 42, 0},
    {"probes", "cast_int_to_double", CASE_INT_FILE, "test/fixtures/probes_found_bugs/cast_int_to_double.c", 35, 0},
    {"probes", "return_int_to_double", CASE_INT_FILE, "test/fixtures/probes_found_bugs/return_int_to_double.c", 42, 0},
    {"probes", "float_inc_dec", CASE_INT_FILE, "test/fixtures/probes_found_bugs/float_inc_dec.c", 42, 0},
    {"probes", "struct_copy_init_array_member", CASE_INT_FILE, "test/fixtures/probes_found_bugs/struct_copy_init_array_member.c", 42, 0},
    {"probes", "ternary_pointer_subscript", CASE_INT_FILE, "test/fixtures/probes_found_bugs/ternary_pointer_subscript.c", 42, 0},
    {"probes", "struct_copy_init_from_global", CASE_INT_FILE, "test/fixtures/probes_found_bugs/struct_copy_init_from_global.c", 42, 0},
    {"probes", "global_pointer_array_offset_init", CASE_INT_FILE, "test/fixtures/probes_found_bugs/global_pointer_array_offset_init.c", 42, 0},
    {"probes", "global_array_designated_out_of_order", CASE_INT_FILE, "test/fixtures/probes_found_bugs/global_array_designated_out_of_order.c", 42, 0},
    {"probes", "global_struct_string_ptr_member", CASE_INT_FILE, "test/fixtures/probes_found_bugs/global_struct_string_ptr_member.c", 42, 0},
    {"probes", "global_struct_designated_and_fp_member", CASE_INT_FILE, "test/fixtures/probes_found_bugs/global_struct_designated_and_fp_member.c", 42, 0},
    {"probes", "bool_compound_assign_normalize", CASE_INT_FILE, "test/fixtures/probes_found_bugs/bool_compound_assign_normalize.c", 42, 0},
    {"probes", "global_bool_normalize", CASE_INT_FILE, "test/fixtures/probes_found_bugs/global_bool_normalize.c", 42, 0},
    {"probes", "anon_struct_union_local", CASE_INT_FILE, "test/fixtures/probes_found_bugs/anon_struct_union_local.c", 42, 0},
    {"probes", "vla_double_element", CASE_INT_FILE, "test/fixtures/probes_found_bugs/vla_double_element.c", 42, 0},
    {"probes", "funcall_struct_ptr_arrow", CASE_INT_FILE, "test/fixtures/probes_found_bugs/funcall_struct_ptr_arrow.c", 42, 0},
    {"probes", "struct_ptr_param_subscript", CASE_INT_FILE, "test/fixtures/probes_found_bugs/struct_ptr_param_subscript.c", 42, 0},
    {"probes", "struct_ptr_incdec_and_typedef_arrow", CASE_INT_FILE, "test/fixtures/probes_found_bugs/struct_ptr_incdec_and_typedef_arrow.c", 42, 0},
    {"probes", "array_of_struct_pointers_arrow", CASE_INT_FILE, "test/fixtures/probes_found_bugs/array_of_struct_pointers_arrow.c", 42, 0},
    {"probes", "struct_ptr_compound_assign_and_double_deref", CASE_INT_FILE, "test/fixtures/probes_found_bugs/struct_ptr_compound_assign_and_double_deref.c", 42, 0},
    {"probes", "ternary_address_pointer_truncation", CASE_INT_FILE, "test/fixtures/probes_found_bugs/ternary_address_pointer_truncation.c", 42, 0},
    {"probes", "fp_pointer_parameter", CASE_INT_FILE, "test/fixtures/probes_found_bugs/fp_pointer_parameter.c", 42, 0},
    {"probes", "funcptr_explicit_deref_call", CASE_INT_FILE, "test/fixtures/probes_found_bugs/funcptr_explicit_deref_call.c", 42, 0},
    {"probes", "funcptr_address_of_init", CASE_INT_FILE, "test/fixtures/probes_found_bugs/funcptr_address_of_init.c", 42, 0},
    {"probes", "unsigned_int_overflow_wrap", CASE_INT_FILE, "test/fixtures/probes_found_bugs/unsigned_int_overflow_wrap.c", 42, 0},
    {"probes", "fp_array_parameter", CASE_INT_FILE, "test/fixtures/probes_found_bugs/fp_array_parameter.c", 42, 0},
    {"probes", "struct_multidim_array_member", CASE_INT_FILE, "test/fixtures/probes_found_bugs/struct_multidim_array_member.c", 42, 0},
    {"probes", "struct_pointer_var_size", CASE_INT_FILE, "test/fixtures/probes_found_bugs/struct_pointer_var_size.c", 42, 0},
    {"probes", "ternary_pointer_null_branch", CASE_INT_FILE, "test/fixtures/probes_found_bugs/ternary_pointer_null_branch.c", 42, 0},
    {"probes", "ternary_long_branch", CASE_INT_FILE, "test/fixtures/probes_found_bugs/ternary_long_branch.c", 42, 0},
    {"probes", "long_return_value", CASE_INT_FILE, "test/fixtures/probes_found_bugs/long_return_value.c", 42, 0},
    {"probes", "long_pointer_param_and_call", CASE_INT_FILE, "test/fixtures/probes_found_bugs/long_pointer_param_and_call.c", 42, 0},
    {"probes", "scalar_init_from_pointer_subscript", CASE_INT_FILE, "test/fixtures/probes_found_bugs/scalar_init_from_pointer_subscript.c", 42, 0},
    {"probes", "double_pointer_subscript_deref", CASE_INT_FILE, "test/fixtures/probes_found_bugs/double_pointer_subscript_deref.c", 42, 0},
    {"probes", "double_pointer_double_subscript", CASE_INT_FILE, "test/fixtures/probes_found_bugs/double_pointer_double_subscript.c", 42, 0},
    {"probes", "long_bitfield", CASE_INT_FILE, "test/fixtures/probes_found_bugs/long_bitfield.c", 42, 0},
    {"probes", "duplicate_designator_override", CASE_INT_FILE, "test/fixtures/probes_found_bugs/duplicate_designator_override.c", 42, 0},
    {"probes", "designator_then_positional", CASE_INT_FILE, "test/fixtures/probes_found_bugs/designator_then_positional.c", 42, 0},
    {"probes", "nested_struct_brace_elision", CASE_INT_FILE, "test/fixtures/probes_found_bugs/nested_struct_brace_elision.c", 42, 0},
    {"probes", "struct_array_brace_elision", CASE_INT_FILE, "test/fixtures/probes_found_bugs/struct_array_brace_elision.c", 42, 0},
    {"probes", "global_nested_struct_init", CASE_INT_FILE, "test/fixtures/probes_found_bugs/global_nested_struct_init.c", 42, 0},
    {"probes", "global_designator_nested_slot", CASE_INT_FILE, "test/fixtures/probes_found_bugs/global_designator_nested_slot.c", 42, 0},
    {"probes", "nested_ternary_long", CASE_INT_FILE, "test/fixtures/probes_found_bugs/nested_ternary_long.c", 42, 0},
    {"probes", "compound_literal_struct_arg", CASE_INT_FILE, "test/fixtures/probes_found_bugs/compound_literal_struct_arg.c", 42, 0},
    {"probes", "struct_value_arg_return", CASE_INT_FILE, "test/fixtures/probes_found_bugs/struct_value_arg_return.c", 42, 0},
    {"probes", "cast_to_signed_comparison", CASE_INT_FILE, "test/fixtures/probes_found_bugs/cast_to_signed_comparison.c", 42, 0},
    {"probes", "unsigned_member_global_load", CASE_INT_FILE, "test/fixtures/probes_found_bugs/unsigned_member_global_load.c", 42, 0},
    {"probes", "unsigned_array_pointer_deref", CASE_INT_FILE, "test/fixtures/probes_found_bugs/unsigned_array_pointer_deref.c", 42, 0},
    {"probes", "typedef_unsigned_global", CASE_INT_FILE, "test/fixtures/probes_found_bugs/typedef_unsigned_global.c", 42, 0},
    {"probes", "funcptr_array_member_init", CASE_INT_FILE, "test/fixtures/probes_found_bugs/funcptr_array_member_init.c", 42, 0},
    {"probes", "struct_ptr_array_member_access", CASE_INT_FILE, "test/fixtures/probes_found_bugs/struct_ptr_array_member_access.c", 42, 0},
    {"probes", "nested_array_designator", CASE_INT_FILE, "test/fixtures/probes_found_bugs/nested_array_designator.c", 42, 0},
    {"probes", "cast_subint_to_int_signedness", CASE_INT_FILE, "test/fixtures/probes_found_bugs/cast_subint_to_int_signedness.c", 42, 0},
    {"probes", "multidim_array_explicit_deref", CASE_INT_FILE, "test/fixtures/probes_found_bugs/multidim_array_explicit_deref.c", 42, 0},
    {"probes", "bool_initializer_normalization", CASE_INT_FILE, "test/fixtures/probes_found_bugs/bool_initializer_normalization.c", 42, 0},
    {"probes", "struct_pointer_arithmetic", CASE_INT_FILE, "test/fixtures/probes_found_bugs/struct_pointer_arithmetic.c", 42, 0},
    {"probes", "array_of_struct_member_init", CASE_INT_FILE, "test/fixtures/probes_found_bugs/array_of_struct_member_init.c", 42, 0},
    {"probes", "struct_subint_by_value", CASE_INT_FILE, "test/fixtures/probes_found_bugs/struct_subint_by_value.c", 42, 0},
    {"probes", "inline_pointer_cast_deref", CASE_INT_FILE, "test/fixtures/probes_found_bugs/inline_pointer_cast_deref.c", 42, 0},
    {"probes", "int_cast_truncates_long", CASE_INT_FILE, "test/fixtures/probes_found_bugs/int_cast_truncates_long.c", 42, 0},
    {"probes", "int_cast_truncates_long_return", CASE_INT_FILE, "test/fixtures/probes_found_bugs/int_cast_truncates_long_return.c", 42, 0},
    {"probes", "long_cast_unsigned_zero_extend", CASE_INT_FILE, "test/fixtures/probes_found_bugs/long_cast_unsigned_zero_extend.c", 42, 0},
    {"probes", "long_literal_width", CASE_INT_FILE, "test/fixtures/probes_found_bugs/long_literal_width.c", 42, 0},
    {"probes", "struct_pointer_to_array", CASE_INT_FILE, "test/fixtures/probes_found_bugs/struct_pointer_to_array.c", 42, 0},
    {"probes", "local_pointer_to_2d_array", CASE_INT_FILE, "test/fixtures/probes_found_bugs/local_pointer_to_2d_array.c", 42, 0},
    {"probes", "float_array_member", CASE_INT_FILE, "test/fixtures/probes_found_bugs/float_array_member.c", 42, 0},
    {"probes", "float_truthiness_condition", CASE_INT_FILE, "test/fixtures/probes_found_bugs/float_truthiness_condition.c", 42, 0},
    {"probes", "float_logical_operand", CASE_INT_FILE, "test/fixtures/probes_found_bugs/float_logical_operand.c", 42, 0},
    {"probes", "static_local_float_init", CASE_INT_FILE, "test/fixtures/probes_found_bugs/static_local_float_init.c", 42, 0},
    {"probes", "multidim_float_array_subscript", CASE_INT_FILE, "test/fixtures/probes_found_bugs/multidim_float_array_subscript.c", 42, 0},
    {"probes", "alignof_aggregate", CASE_INT_FILE, "test/fixtures/probes_found_bugs/alignof_aggregate.c", 42, 0},
    {"probes", "generic_string_and_long", CASE_INT_FILE, "test/fixtures/probes_found_bugs/generic_string_and_long.c", 42, 0},
    {"probes", "cast_short_char_sign_extend", CASE_INT_FILE, "test/fixtures/probes_found_bugs/cast_short_char_sign_extend.c", 42, 0},
    {"probes", "array_row_decay_pointer_arith", CASE_INT_FILE, "test/fixtures/probes_found_bugs/array_row_decay_pointer_arith.c", 42, 0},
    {"probes", "array_row_decay_3d_pointer_arith", CASE_INT_FILE, "test/fixtures/probes_found_bugs/array_row_decay_3d_pointer_arith.c", 42, 0},
    {"probes", "funcptr_fp_return", CASE_INT_FILE, "test/fixtures/probes_found_bugs/funcptr_fp_return.c", 42, 0},
    {"probes", "funcptr_array_fp_return", CASE_INT_FILE, "test/fixtures/probes_found_bugs/funcptr_array_fp_return.c", 42, 0},
    {"probes", "funcptr_member_fp_return", CASE_INT_FILE, "test/fixtures/probes_found_bugs/funcptr_member_fp_return.c", 42, 0},
    {"probes", "funcptr_global_fp_return", CASE_INT_FILE, "test/fixtures/probes_found_bugs/funcptr_global_fp_return.c", 42, 0},
    {"probes", "global_fp_data_pointer_deref", CASE_INT_FILE, "test/fixtures/probes_found_bugs/global_fp_data_pointer_deref.c", 42, 0},
    {"probes", "global_ptr_to_array_subscript", CASE_INT_FILE, "test/fixtures/probes_found_bugs/global_ptr_to_array_subscript.c", 42, 0},
    {"probes", "ptr_to_array_deref_fp", CASE_INT_FILE, "test/fixtures/probes_found_bugs/ptr_to_array_deref_fp.c", 42, 0},
    {"probes", "global_ptr_to_multidim_array", CASE_INT_FILE, "test/fixtures/probes_found_bugs/global_ptr_to_multidim_array.c", 42, 0},
    {"probes", "funcptr_global_array_fp_return", CASE_INT_FILE, "test/fixtures/probes_found_bugs/funcptr_global_array_fp_return.c", 42, 0},
    {"probes", "global_size1_funcptr_array", CASE_INT_FILE, "test/fixtures/probes_found_bugs/global_size1_funcptr_array.c", 42, 0},
    {"probes", "sizeof_vla_subscript", CASE_INT_FILE, "test/fixtures/probes_found_bugs/sizeof_vla_subscript.c", 42, 0},
    {"probes", "static_local_struct_persist", CASE_INT_FILE, "test/fixtures/probes_found_bugs/static_local_struct_persist.c", 42, 0},
    {"probes", "int_cmp_width_and_subint_return", CASE_INT_FILE, "test/fixtures/probes_found_bugs/int_cmp_width_and_subint_return.c", 42, 0},
    {"probes", "anon_member_fp_unsigned_promote", CASE_INT_FILE, "test/fixtures/probes_found_bugs/anon_member_fp_unsigned_promote.c", 42, 0},
    {"probes", "global_ptr_array_addr_init", CASE_INT_FILE, "test/fixtures/probes_found_bugs/global_ptr_array_addr_init.c", 42, 0},
    {"probes", "global_designator_member_index", CASE_INT_FILE, "test/fixtures/probes_found_bugs/global_designator_member_index.c", 42, 0},
    {"probes", "local_designator_aggregate_leaf", CASE_INT_FILE, "test/fixtures/probes_found_bugs/local_designator_aggregate_leaf.c", 42, 0},
    {"probes", "return_struct_funccall", CASE_INT_FILE, "test/fixtures/probes_found_bugs/return_struct_funccall.c", 42, 0},
    {"probes", "struct_init_from_ternary_funccall", CASE_INT_FILE, "test/fixtures/probes_found_bugs/struct_init_from_ternary_funccall.c", 42, 0},
};

static const compile_fail_case_t compile_fail_cases[] = {
    {"cast_struct_from_nonscalar_rejected",
     "int main() { struct S { int x; }; union U { int y; }; union U u={1}; return (struct S)u; }",
     "[cast] struct 値へのキャストは未対応です（型不整合）"},
    {"const_assign_rejected",
     "int main() { const int x = 5; x = 10; return 0; }",
     "E3077"},
    {"const_compound_assign_rejected",
     "int main() { const int x = 5; x += 1; return 0; }",
     "const修飾された変数への代入はできません"},
    {"const_increment_rejected",
     "int main() { const int x = 5; x++; return 0; }",
     "const修飾された変数への代入はできません"},
    {"const_qual_discard_init_rejected",
     "int main() { const int x = 5; const int *cp = &x; int *p = cp; return 0; }",
     "const修飾されたポインタからconst無しポインタへの暗黙変換はできません"},
    {"const_qual_discard_assign_rejected",
     "int main() { const int x = 5; const int *cp = &x; int *p; p = cp; return 0; }",
     "const修飾されたポインタからconst無しポインタへの暗黙変換はできません"},
    {"funcdef_unnamed_param_rejected",
     "int bad(int) { return 0; }",
     "必要な項目がありません: 仮引数"},
};

static int test_count = 0;
static int pass_count = 0;

static int mkdir_p(const char *path) {
  char tmp[PATH_MAX];
  snprintf(tmp, sizeof(tmp), "%s", path);
  size_t len = strlen(tmp);
  if (len == 0) return 0;
  if (tmp[len - 1] == '/') tmp[len - 1] = '\0';
  for (char *p = tmp + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';
      if (mkdir(tmp, 0777) != 0 && errno != EEXIST) return -1;
      *p = '/';
    }
  }
  if (mkdir(tmp, 0777) != 0 && errno != EEXIST) return -1;
  return 0;
}

static int run_ag_c_to_s(const char *input, const char *s_path) {
  pid_t pid = fork();
  if (pid == 0) {
    freopen(s_path, "w", stdout);
    execl("./build/ag_c", "./build/ag_c", input, (char *)NULL);
    _exit(1);
  }
  int status;
  waitpid(pid, &status, 0);
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return -1;
  return 0;
}

static int run_ag_c_expect_fail_with_diag(const char *input, const char *expected_diag,
                                          const char *log_path) {
  int pipefd[2];
  if (pipe(pipefd) != 0) return -1;

  pid_t pid = fork();
  if (pid == 0) {
    close(pipefd[0]);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);
    freopen("/dev/null", "w", stdout);
    execl("./build/ag_c", "./build/ag_c", input, (char *)NULL);
    _exit(1);
  }
  close(pipefd[1]);

  char diag_buf[8192];
  size_t used = 0;
  for (;;) {
    char sink[512];
    char *dst = diag_buf + used;
    size_t room = sizeof(diag_buf) - 1 - used;
    if (room == 0) {
      dst = sink;
      room = sizeof(sink);
    }
    ssize_t nread = read(pipefd[0], dst, room);
    if (nread <= 0) break;
    if (used < sizeof(diag_buf) - 1) {
      size_t keep = (size_t)nread;
      if (keep > sizeof(diag_buf) - 1 - used) keep = sizeof(diag_buf) - 1 - used;
      used += keep;
    }
  }
  close(pipefd[0]);
  diag_buf[used] = '\0';

  int status;
  waitpid(pid, &status, 0);

  FILE *log = fopen(log_path, "w");
  if (log) {
    fputs(diag_buf, log);
    fclose(log);
  }

  if (!WIFEXITED(status) || WEXITSTATUS(status) == 0) return -1;
  if (expected_diag && expected_diag[0] != '\0' && !strstr(diag_buf, expected_diag)) return -1;
  return 0;
}

static int diag_has_error_code_prefix(const char *diag) {
  if (!diag) return 0;
  for (size_t i = 0; diag[i] != '\0'; i++) {
    if (diag[i] != 'E') continue;
    if (diag[i + 1] < '0' || diag[i + 1] > '9') continue;
    if (diag[i + 2] < '0' || diag[i + 2] > '9') continue;
    if (diag[i + 3] < '0' || diag[i + 3] > '9') continue;
    if (diag[i + 4] < '0' || diag[i + 4] > '9') continue;
    if (diag[i + 5] == ':') return 1;
  }
  return 0;
}

static int run_ag_c_expect_fail_profiled(const char *input, const char *expected_diag,
                                         const char *log_path, size_t max_log_len) {
  int pipefd[2];
  if (pipe(pipefd) != 0) return -1;

  pid_t pid = fork();
  if (pid == 0) {
    close(pipefd[0]);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);
    freopen("/dev/null", "w", stdout);
    execl("./build/ag_c", "./build/ag_c", input, (char *)NULL);
    _exit(1);
  }
  close(pipefd[1]);

  char diag_buf[8192];
  size_t used = 0;
  for (;;) {
    char sink[512];
    char *dst = diag_buf + used;
    size_t room = sizeof(diag_buf) - 1 - used;
    if (room == 0) {
      dst = sink;
      room = sizeof(sink);
    }
    ssize_t nread = read(pipefd[0], dst, room);
    if (nread <= 0) break;
    if (used < sizeof(diag_buf) - 1) {
      size_t keep = (size_t)nread;
      if (keep > sizeof(diag_buf) - 1 - used) keep = sizeof(diag_buf) - 1 - used;
      used += keep;
    }
  }
  close(pipefd[0]);
  diag_buf[used] = '\0';

  int status;
  waitpid(pid, &status, 0);

  FILE *log = fopen(log_path, "w");
  if (log) {
    fputs(diag_buf, log);
    fclose(log);
  }

  if (!WIFEXITED(status) || WEXITSTATUS(status) != 1) return -1;
  if (!diag_has_error_code_prefix(diag_buf)) return -1;
  if (expected_diag && expected_diag[0] != '\0' && !strstr(diag_buf, expected_diag)) return -1;
  if (used > max_log_len) return -1;
  return 0;
}

static int run_ag_c_expect_fail_profiled_with_limits_logfile(const char *input, const char *expected_diag,
                                                             const char *log_path, size_t max_log_len,
                                                             rlim_t nofile_limit, rlim_t as_limit_bytes,
                                                             int consume_fd_count) {
  pid_t pid = fork();
  if (pid == 0) {
    freopen("/dev/null", "w", stdout);
    freopen(log_path, "w", stderr);

    if (nofile_limit > 0) {
      struct rlimit rl = {nofile_limit, nofile_limit};
      (void)setrlimit(RLIMIT_NOFILE, &rl);
    }
    int consumed[64];
    int consumed_n = 0;
    if (consume_fd_count > 0) {
      if (consume_fd_count > (int)(sizeof(consumed) / sizeof(consumed[0]))) {
        consume_fd_count = (int)(sizeof(consumed) / sizeof(consumed[0]));
      }
      for (int i = 0; i < consume_fd_count; i++) {
        int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
        if (fd < 0) break;
        consumed[consumed_n++] = fd;
      }
    }
    if (as_limit_bytes > 0) {
#ifdef RLIMIT_AS
      struct rlimit rl_as = {as_limit_bytes, as_limit_bytes};
      (void)setrlimit(RLIMIT_AS, &rl_as);
#elif defined(RLIMIT_DATA)
      struct rlimit rl_data = {as_limit_bytes, as_limit_bytes};
      (void)setrlimit(RLIMIT_DATA, &rl_data);
#endif
    }

    execl("./build/ag_c", "./build/ag_c", input, (char *)NULL);
    for (int i = 0; i < consumed_n; i++) close(consumed[i]);
    _exit(1);
  }
  if (pid < 0) return -1;

  int status;
  waitpid(pid, &status, 0);
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 1) return -1;
  if (expected_diag && expected_diag[0] != '\0') {
    if (!log_file_contains_substr(log_path, "E")) return -1;
    if (!log_file_contains_substr(log_path, expected_diag)) return -1;
  }

  FILE *fp = fopen(log_path, "r");
  if (!fp) return -1;
  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return -1;
  }
  long sz = ftell(fp);
  fclose(fp);
  if (sz < 0) return -1;
  if ((size_t)sz > max_log_len) return -1;
  return 0;
}

static int log_file_contains_substr(const char *path, const char *needle) {
  if (!needle || !*needle) return 1;
  FILE *fp = fopen(path, "r");
  if (!fp) return 0;
  char buf[8192];
  size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
  fclose(fp);
  buf[n] = '\0';
  return strstr(buf, needle) != NULL;
}

static int run_ag_c_expect_fail_with_diag_timeout(const char *input, const char *expected_diag,
                                                  const char *log_path, int timeout_sec,
                                                  const char *reason_tag) {
  pid_t pid = fork();
  if (pid == 0) {
    freopen("/dev/null", "w", stdout);
    freopen(log_path, "w", stderr);
    execl("./build/ag_c", "./build/ag_c", input, (char *)NULL);
    _exit(1);
  }
  if (pid < 0) return -1;

  int status = 0;
  int waited_ms = 0;
  const int poll_ms = 10;
  const int timeout_ms = timeout_sec * 1000;
  while (waited_ms < timeout_ms) {
    pid_t w = waitpid(pid, &status, WNOHANG);
    if (w == pid) break;
    if (w < 0) return -1;
    usleep((useconds_t)poll_ms * 1000);
    waited_ms += poll_ms;
  }
  if (waited_ms >= timeout_ms) {
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
    FILE *log = fopen(log_path, "a");
    if (log) {
      fprintf(log, "\n[timeout] case=%s timeout_sec=%d\n",
              reason_tag ? reason_tag : "unknown", timeout_sec);
      fclose(log);
    }
    return -1;
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) == 0) {
    FILE *log = fopen(log_path, "a");
    if (log) {
      fprintf(log, "\n[unexpected] case=%s status=%d\n",
              reason_tag ? reason_tag : "unknown", status);
      fclose(log);
    }
    return -1;
  }
  if (expected_diag && !log_file_contains_substr(log_path, expected_diag)) return -1;
  return 0;
}

static int run_ag_c_expect_fail_with_prog_args_and_diag(const char *prog_path, const char *arg1,
                                                        const char *arg2, const char *expected_diag,
                                                        const char *log_path) {
  const char *prog = prog_path ? prog_path : "./build/ag_c";
  int pipefd[2];
  if (pipe(pipefd) != 0) return -1;

  pid_t pid = fork();
  if (pid == 0) {
    close(pipefd[0]);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);
    freopen("/dev/null", "w", stdout);
    if (arg1 && arg2) {
      execl(prog, prog, arg1, arg2, (char *)NULL);
    } else if (arg1) {
      execl(prog, prog, arg1, (char *)NULL);
    } else {
      execl(prog, prog, (char *)NULL);
    }
    _exit(1);
  }
  close(pipefd[1]);

  char diag_buf[8192];
  size_t used = 0;
  for (;;) {
    char sink[512];
    char *dst = diag_buf + used;
    size_t room = sizeof(diag_buf) - 1 - used;
    if (room == 0) {
      dst = sink;
      room = sizeof(sink);
    }
    ssize_t nread = read(pipefd[0], dst, room);
    if (nread <= 0) break;
    if (used < sizeof(diag_buf) - 1) {
      size_t keep = (size_t)nread;
      if (keep > sizeof(diag_buf) - 1 - used) keep = sizeof(diag_buf) - 1 - used;
      used += keep;
    }
  }
  close(pipefd[0]);
  diag_buf[used] = '\0';

  int status;
  waitpid(pid, &status, 0);

  FILE *log = fopen(log_path, "w");
  if (log) {
    fputs(diag_buf, log);
    fclose(log);
  }

  if (!WIFEXITED(status) || WEXITSTATUS(status) == 0) return -1;
  if (!strstr(diag_buf, expected_diag)) return -1;
  return 0;
}

static int run_ag_c_expect_fail_with_args_and_diag(const char *arg1, const char *arg2,
                                                   const char *expected_diag, const char *log_path) {
  return run_ag_c_expect_fail_with_prog_args_and_diag(NULL, arg1, arg2, expected_diag, log_path);
}

static int count_open_fds_self(void) {
  DIR *d = opendir("/dev/fd");
  if (!d) return -1;
  int count = 0;
  struct dirent *ent;
  while ((ent = readdir(d)) != NULL) {
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
    count++;
  }
  closedir(d);
  return count;
}

static int count_tmp_files_with_prefix(const char *prefix) {
  DIR *d = opendir("/tmp");
  if (!d) return -1;
  int count = 0;
  size_t n = strlen(prefix);
  struct dirent *ent;
  while ((ent = readdir(d)) != NULL) {
    if (strncmp(ent->d_name, prefix, n) == 0) count++;
  }
  closedir(d);
  return count;
}

static int run_clang_build_many(const char *bin_path, const char **inputs, size_t ninputs) {
  pid_t pid = fork();
  if (pid == 0) {
    char **argv = calloc(ninputs + 4, sizeof(char *));
    if (!argv) _exit(1);
    argv[0] = "clang";
    argv[1] = "-o";
    argv[2] = (char *)bin_path;
    for (size_t i = 0; i < ninputs; i++) {
      argv[3 + i] = (char *)inputs[i];
    }
    argv[3 + ninputs] = NULL;
    execvp("clang", argv);
    _exit(1);
  }
  if (pid < 0) return -1;
  int status;
  waitpid(pid, &status, 0);
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return -1;
  return 0;
}

static int write_source_file(const char *path, const char *source);
static int write_source_file_bytes(const char *path, const unsigned char *data, size_t len);

static int run_ag_c_parallel_smoke(void) {
  const int jobs = 8;
  if (mkdir_p("build/e2e/concurrency") != 0) return -1;

  pid_t pids[jobs];
  for (int i = 0; i < jobs; i++) {
    char src_path[PATH_MAX];
    snprintf(src_path, sizeof(src_path), "build/e2e/concurrency/job_%d.c", i);
    char src[128];
    snprintf(src, sizeof(src), "int main(){ return %d; }\n", i);
    if (write_source_file(src_path, src) != 0) return -1;

    char s_path[PATH_MAX];
    snprintf(s_path, sizeof(s_path), "build/e2e/concurrency/job_%d.s", i);
    pid_t pid = fork();
    if (pid == 0) {
      freopen(s_path, "w", stdout);
      execl("./build/ag_c", "./build/ag_c", src_path, (char *)NULL);
      _exit(1);
    }
    if (pid < 0) return -1;
    pids[i] = pid;
  }

  for (int i = 0; i < jobs; i++) {
    int status;
    waitpid(pids[i], &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return -1;
  }
  return 0;
}

static int write_source_file(const char *path, const char *source) {
  FILE *fp = fopen(path, "w");
  if (!fp) return -1;
  fputs(source, fp);
  fclose(fp);
  return 0;
}

// `tc->input` がファイルパス (`_FILE` バリアント) のときに、その内容を
// 既存パイプラインが期待する `build/e2e/<cat>/<name>.c` へコピーする。
static int copy_source_file(const char *src_path, const char *dst_path) {
  FILE *in = fopen(src_path, "rb");
  if (!in) return -1;
  FILE *out = fopen(dst_path, "wb");
  if (!out) { fclose(in); return -1; }
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
    if (fwrite(buf, 1, n, out) != n) {
      fclose(in); fclose(out);
      return -1;
    }
  }
  fclose(in);
  fclose(out);
  return 0;
}

// 入力を準備する。インラインなら write、ファイルなら copy。
static int prepare_test_source(const test_case_t *tc, const char *dst_path) {
  if (case_kind_is_file(tc->kind)) {
    return copy_source_file(tc->input, dst_path);
  }
  return write_source_file(dst_path, tc->input);
}

static int write_source_file_bytes(const char *path, const unsigned char *data, size_t len) {
  FILE *fp = fopen(path, "wb");
  if (!fp) return -1;
  if (len > 0 && fwrite(data, 1, len, fp) != len) {
    fclose(fp);
    return -1;
  }
  fclose(fp);
  return 0;
}

static int write_large_single_line_unterminated_string(const char *path, size_t body_len) {
  FILE *fp = fopen(path, "w");
  if (!fp) return -1;
  if (fputc('"', fp) == EOF) {
    fclose(fp);
    return -1;
  }
  for (size_t i = 0; i < body_len; i++) {
    if (fputc('a', fp) == EOF) {
      fclose(fp);
      return -1;
    }
  }
  // Intentionally do not close with '"' to force tokenizer error on a huge single line.
  if (fputc('\n', fp) == EOF) {
    fclose(fp);
    return -1;
  }
  fclose(fp);
  return 0;
}

static int write_macro_expansion_limit_source(const char *path, int levels) {
  if (levels < 1) return -1;
  FILE *fp = fopen(path, "w");
  if (!fp) return -1;
  if (fprintf(fp, "#define X0 1\n") < 0) {
    fclose(fp);
    return -1;
  }
  for (int i = 1; i <= levels; i++) {
    if (fprintf(fp, "#define X%d (X%d + X%d)\n", i, i - 1, i - 1) < 0) {
      fclose(fp);
      return -1;
    }
  }
  if (fprintf(fp, "int main() { return X%d; }\n", levels) < 0) {
    fclose(fp);
    return -1;
  }
  fclose(fp);
  return 0;
}

static int write_pp_if_token_limit_source(const char *path, int terms) {
  if (terms < 1) return -1;
  FILE *fp = fopen(path, "w");
  if (!fp) return -1;
  if (fprintf(fp, "#if ") < 0) {
    fclose(fp);
    return -1;
  }
  for (int i = 0; i < terms; i++) {
    if (fprintf(fp, "%s1", i == 0 ? "" : " + ") < 0) {
      fclose(fp);
      return -1;
    }
  }
  if (fprintf(fp, "\nint main(){return 0;}\n#endif\n") < 0) {
    fclose(fp);
    return -1;
  }
  fclose(fp);
  return 0;
}

static int write_parser_decl_width_limit_source(const char *path, int ndecls) {
  if (ndecls < 1) return -1;
  FILE *fp = fopen(path, "w");
  if (!fp) return -1;
  if (fprintf(fp, "int main(){ int ") < 0) {
    fclose(fp);
    return -1;
  }
  for (int i = 0; i < ndecls; i++) {
    if (fprintf(fp, "%sv%d", i == 0 ? "" : ",", i) < 0) {
      fclose(fp);
      return -1;
    }
  }
  if (fprintf(fp, "; return 0; }\n") < 0) {
    fclose(fp);
    return -1;
  }
  fclose(fp);
  return 0;
}

static void build_category_bin_path(const char *category, char *bin_path) {
  snprintf(bin_path, PATH_MAX, "build/e2e/%s/%s_category_runner", category, category);
}

static void sanitize_symbol(const char *in, char *out, size_t out_size) {
  size_t j = 0;
  for (size_t i = 0; in[i] && j + 1 < out_size; i++) {
    unsigned char c = (unsigned char)in[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
      out[j++] = (char)c;
    } else {
      out[j++] = '_';
    }
  }
  out[j] = '\0';
}

static int copy_and_namespace_symbols(const char *src_path, const char *dst_path, const char *prefix) {
  FILE *in = fopen(src_path, "r");
  if (!in) return -1;
  FILE *out = fopen(dst_path, "w");
  if (!out) {
    fclose(in);
    return -1;
  }

  char *line = NULL;
  size_t cap = 0;
  while (getline(&line, &cap, in) != -1) {
    // .section ディレクティブはリネーム対象外（セクション名にシンボルが混在するため）
    if (strncmp(line, ".section ", 9) == 0) {
      fputs(line, out);
      continue;
    }
    size_t len = strlen(line);
    for (size_t i = 0; i < len; ) {
      if (line[i] == '_' && (i == 0 || line[i - 1] != '_') && i + 1 < len &&
          ((line[i + 1] >= 'A' && line[i + 1] <= 'Z') || (line[i + 1] >= 'a' && line[i + 1] <= 'z') ||
           line[i + 1] == '_')) {
        size_t j = i + 1;
        while ((line[j] >= 'A' && line[j] <= 'Z') || (line[j] >= 'a' && line[j] <= 'z') ||
               (line[j] >= '0' && line[j] <= '9') || line[j] == '_') {
          j++;
        }
        char sym[256];
        size_t sym_len = j - i;
        if (sym_len >= sizeof(sym)) sym_len = sizeof(sym) - 1;
        memcpy(sym, line + i, sym_len);
        sym[sym_len] = '\0';
        if (strcmp(sym, "_printf") == 0 || (sym[1] == '_') ||
            strcmp(sym, "_TEXT") == 0 || strcmp(sym, "_cstring") == 0 ||
            strcmp(sym, "_literal4") == 0 || strcmp(sym, "_literal8") == 0 ||
            strcmp(sym, "_literal16") == 0 || strcmp(sym, "_const") == 0 ||
            strcmp(sym, "_strcmp") == 0 || strcmp(sym, "_strncmp") == 0 ||
            strcmp(sym, "_strlen") == 0 || strcmp(sym, "_strcpy") == 0 ||
            strcmp(sym, "_strncpy") == 0 || strcmp(sym, "_strcat") == 0 ||
            strcmp(sym, "_strncat") == 0 || strcmp(sym, "_strchr") == 0 ||
            strcmp(sym, "_strrchr") == 0 || strcmp(sym, "_strstr") == 0 ||
            strcmp(sym, "_strtok") == 0 || strcmp(sym, "_strerror") == 0 ||
            strcmp(sym, "_memcpy") == 0 || strcmp(sym, "_memmove") == 0 ||
            strcmp(sym, "_memcmp") == 0 || strcmp(sym, "_memchr") == 0 ||
            strcmp(sym, "_memset") == 0 ||
            strcmp(sym, "_malloc") == 0 || strcmp(sym, "_calloc") == 0 ||
            strcmp(sym, "_realloc") == 0 || strcmp(sym, "_free") == 0 ||
            strcmp(sym, "_exit") == 0 || strcmp(sym, "_abort") == 0 ||
            strcmp(sym, "_atoi") == 0 || strcmp(sym, "_atol") == 0 ||
            strcmp(sym, "_puts") == 0 || strcmp(sym, "_fprintf") == 0 ||
            strcmp(sym, "_sprintf") == 0 || strcmp(sym, "_snprintf") == 0 ||
            strcmp(sym, "_va_start") == 0 || strcmp(sym, "_va_end") == 0 ||
            strcmp(sym, "_abs") == 0 || strcmp(sym, "_labs") == 0 ||
            strcmp(sym, "_rand") == 0 || strcmp(sym, "_srand") == 0 ||
            strcmp(sym, "_qsort") == 0 || strcmp(sym, "_bsearch") == 0 ||
            strcmp(sym, "_atexit") == 0 || strcmp(sym, "_getenv") == 0 ||
            strcmp(sym, "_system") == 0 || strcmp(sym, "_strtol") == 0 ||
            strcmp(sym, "_perror") == 0 || strcmp(sym, "_fopen") == 0 ||
            strcmp(sym, "_fclose") == 0 || strcmp(sym, "_fflush") == 0 ||
            strcmp(sym, "_fread") == 0 || strcmp(sym, "_fwrite") == 0 ||
            strcmp(sym, "_fputs") == 0 || strcmp(sym, "_fputc") == 0 ||
            strcmp(sym, "_fgetc") == 0 || strcmp(sym, "_fgets") == 0 ||
            strcmp(sym, "_getchar") == 0 || strcmp(sym, "_putchar") == 0 ||
            strcmp(sym, "_isalnum") == 0 || strcmp(sym, "_isalpha") == 0 ||
            strcmp(sym, "_isblank") == 0 || strcmp(sym, "_iscntrl") == 0 ||
            strcmp(sym, "_isdigit") == 0 || strcmp(sym, "_isgraph") == 0 ||
            strcmp(sym, "_islower") == 0 || strcmp(sym, "_isprint") == 0 ||
            strcmp(sym, "_ispunct") == 0 || strcmp(sym, "_isspace") == 0 ||
            strcmp(sym, "_isupper") == 0 || strcmp(sym, "_isxdigit") == 0 ||
            strcmp(sym, "_tolower") == 0 || strcmp(sym, "_toupper") == 0) {
          fputs(sym, out);
        } else {
          fprintf(out, "_%s_%s", prefix, sym + 1);
        }
        i = j;
      } else {
        fputc(line[i], out);
        i++;
      }
    }
  }
  free(line);
  fclose(in);
  fclose(out);
  return 0;
}

static int build_category(const char *category) {
  char log_path[PATH_MAX];
  char driver_path[PATH_MAX];
  char bin_path[PATH_MAX];
  char category_dir[PATH_MAX];
  snprintf(log_path, sizeof(log_path), "build/e2e/logs/%s.build.log", category);
  snprintf(driver_path, sizeof(driver_path), "build/e2e/%s/%s_category_driver.c", category, category);
  snprintf(category_dir, sizeof(category_dir), "build/e2e/%s", category);
  build_category_bin_path(category, bin_path);

  FILE *log = fopen(log_path, "w");
  if (!log) return -1;
  fprintf(log, "Category: %s\n", category);

  const size_t max_cases = sizeof(test_cases) / sizeof(test_cases[0]);
  const char **clang_inputs = calloc(max_cases + 1, sizeof(char *));
  char **owned_paths = calloc(max_cases + 1, sizeof(char *));
  if (!clang_inputs || !owned_paths) {
    fclose(log);
    free(clang_inputs);
    free(owned_paths);
    return 1;
  }
  if (mkdir_p(category_dir) != 0) {
    fclose(log);
    free(clang_inputs);
    free(owned_paths);
    return 1;
  }

  FILE *drv = fopen(driver_path, "w");
  if (!drv) {
    fclose(log);
    free(clang_inputs);
    free(owned_paths);
    return 1;
  }

  fprintf(drv, "#include <math.h>\n#include <stdio.h>\n\n");
  fprintf(drv, "static int agc_nearly(double a, double b) { return fabs(a - b) < 0.001; }\n\n");
  fprintf(drv, "int main(void) {\n  int failed = 0;\n");

  size_t input_count = 0;
  for (size_t i = 0; i < max_cases; i++) {
    const test_case_t *tc = &test_cases[i];
    if (strcmp(tc->category, category) != 0) continue;

    char dir[PATH_MAX], s_path[PATH_MAX], bin_unused[PATH_MAX], drv_unused[PATH_MAX], src_path[PATH_MAX], rs_path[PATH_MAX];
    build_artifact_paths(tc, dir, s_path, bin_unused, drv_unused);
    build_source_path(tc, src_path);
    snprintf(rs_path, sizeof(rs_path), "%s/%s.renamed.s", dir, tc->name);

    if (mkdir_p(dir) != 0 || prepare_test_source(tc, src_path) != 0 || run_ag_c_to_s(src_path, s_path) != 0) {
      fprintf(log, "  FAIL: build %s\n  input: %s\n  artifacts: s=%s\n", tc->name, tc->input, s_path);
      fclose(drv);
      fclose(log);
      free(clang_inputs);
      free(owned_paths);
      return 1;
    }

    char cat_sym[128], name_sym[128], fn_sym[320];
    sanitize_symbol(category, cat_sym, sizeof(cat_sym));
    sanitize_symbol(tc->name, name_sym, sizeof(name_sym));
    snprintf(fn_sym, sizeof(fn_sym), "agc_%s_%s", cat_sym, name_sym);
    if (copy_and_namespace_symbols(s_path, rs_path, fn_sym) != 0) {
      fprintf(log, "  FAIL: rewrite %s\n", tc->name);
      fclose(drv);
      fclose(log);
      free(clang_inputs);
      free(owned_paths);
      return 1;
    }

    owned_paths[input_count] = strdup(rs_path);
    clang_inputs[input_count] = owned_paths[input_count];
    if (!owned_paths[input_count]) {
      fclose(drv);
      fclose(log);
      free(clang_inputs);
      free(owned_paths);
      return 1;
    }
    input_count++;

    case_kind_t vk = case_kind_value_kind(tc->kind);
    if (vk == CASE_INT) {
      fprintf(drv, "  extern int %s_main(void);\n", fn_sym);
    } else if (vk == CASE_DOUBLE) {
      fprintf(drv, "  extern double %s_ag_m(void);\n", fn_sym);
    } else {
      fprintf(drv, "  extern float %s_ag_m(void);\n", fn_sym);
    }
  }

  fprintf(drv, "\n");
  for (size_t i = 0; i < max_cases; i++) {
    const test_case_t *tc = &test_cases[i];
    if (strcmp(tc->category, category) != 0) continue;
    char cat_sym[128], name_sym[128], fn_sym[320];
    sanitize_symbol(category, cat_sym, sizeof(cat_sym));
    sanitize_symbol(tc->name, name_sym, sizeof(name_sym));
    snprintf(fn_sym, sizeof(fn_sym), "agc_%s_%s", cat_sym, name_sym);

    if (case_kind_value_kind(tc->kind) == CASE_INT) {
      fprintf(drv, "  { int actual = (%s_main() & 255); if (actual != %d) { failed = 1; printf(\"FAIL %s expected %d got %%d\\n\", actual); } else { printf(\"OK %s => %%d\\n\", actual); } }\n",
              fn_sym, tc->expected_i, tc->name, tc->expected_i, tc->name);
    } else {
      fprintf(drv, "  { double actual = (double)%s_ag_m(); if (!agc_nearly(actual, %.6f)) { failed = 1; printf(\"FAIL %s expected %.2f got %%.2f\\n\", actual); } else { printf(\"OK %s => %%.2f\\n\", actual); } }\n",
              fn_sym, tc->expected_f, tc->name, tc->expected_f, tc->name);
    }
  }
  fprintf(drv, "  return failed;\n}\n");
  fclose(drv);

  owned_paths[input_count] = strdup(driver_path);
  clang_inputs[input_count] = owned_paths[input_count];
  if (!owned_paths[input_count]) {
    fclose(log);
    free(clang_inputs);
    free(owned_paths);
    return 1;
  }
  input_count++;

  if (run_clang_build_many(bin_path, clang_inputs, input_count) != 0) {
    fprintf(log, "  FAIL: clang link category binary\n");
    for (size_t i = 0; i < input_count; i++) free(owned_paths[i]);
    free(clang_inputs);
    free(owned_paths);
    fclose(log);
    return 1;
  }

  for (size_t i = 0; i < input_count; i++) free(owned_paths[i]);
  free(clang_inputs);
  free(owned_paths);
  fprintf(log, "Summary: build OK\n");
  fclose(log);
  return 0;
}

static int run_category(const char *category) {
  char log_path[PATH_MAX];
  char bin_path[PATH_MAX];
  snprintf(log_path, sizeof(log_path), "build/e2e/logs/%s.log", category);
  build_category_bin_path(category, bin_path);
  FILE *log = fopen(log_path, "w");
  if (!log) return -1;
  fprintf(log, "Category: %s\n", category);

  int pipefd[2];
  if (pipe(pipefd) != 0) {
    fclose(log);
    return -1;
  }
  pid_t pid = fork();
  if (pid == 0) {
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    close(pipefd[1]);
    execl(bin_path, bin_path, (char *)NULL);
    _exit(1);
  }
  close(pipefd[1]);
  if (pid < 0) {
    close(pipefd[0]);
    fclose(log);
    return -1;
  }

  char buf[1024];
  ssize_t nread = 0;
  while ((nread = read(pipefd[0], buf, sizeof(buf))) > 0) {
    fwrite(buf, 1, (size_t)nread, log);
  }
  close(pipefd[0]);
  int status;
  waitpid(pid, &status, 0);
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    fprintf(log, "Summary: FAILED\n");
    fclose(log);
    return 1;
  }
  fprintf(log, "Summary: PASS\n");
  fclose(log);
  return 0;
}

int main() {
  printf("Running E2E tests...\n");
  fflush(stdout);

  if (mkdir_p("build/e2e/logs") != 0) {
    fprintf(stderr, "Failed to create log directory\n");
    return 1;
  }
  int fd_count_baseline = count_open_fds_self();
  if (fd_count_baseline < 0) {
    fprintf(stderr, "Failed to read fd baseline\n");
    return 1;
  }
  int tmp_include_prefix_baseline = count_tmp_files_with_prefix("ag_c_e2e_include_");
  if (tmp_include_prefix_baseline < 0) {
    fprintf(stderr, "Failed to read /tmp include-prefix baseline\n");
    return 1;
  }

  for (size_t i = 0; i < sizeof(compile_fail_cases) / sizeof(compile_fail_cases[0]); i++) {
    const compile_fail_case_t *tc = &compile_fail_cases[i];
    char src_path[PATH_MAX];
    char log_path[PATH_MAX];
    snprintf(src_path, sizeof(src_path), "build/e2e/compile_fail/%s.c", tc->name);
    snprintf(log_path, sizeof(log_path), "build/e2e/logs/compile_fail_%s.log", tc->name);
    if (mkdir_p("build/e2e/compile_fail") != 0 || write_source_file(src_path, tc->input) != 0 ||
        run_ag_c_expect_fail_with_diag(src_path, tc->expected_diag, log_path) != 0) {
      fprintf(stderr, "Compile-fail case failed: %s (see %s)\n", tc->name, log_path);
      return 1;
    }
  }
  {
    const char *missing_path = "build/e2e/compile_fail/__missing_input__.c";
    const char *log_path = "build/e2e/logs/compile_fail_missing_input.log";
    if (run_ag_c_expect_fail_with_diag(missing_path, "入力ファイルを読み込めませんでした", log_path) != 0) {
      fprintf(stderr, "Compile-fail case failed: missing_input (see %s)\n", log_path);
      return 1;
    }
  }
  {
    const char *missing_abs_path = "/tmp/__ag_c_missing_input_abs__.c";
    const char *log_path = "build/e2e/logs/compile_fail_missing_input_abs.log";
    if (run_ag_c_expect_fail_with_diag(missing_abs_path, "入力ファイルを読み込めませんでした", log_path) != 0) {
      fprintf(stderr, "Compile-fail case failed: missing_input_abs (see %s)\n", log_path);
      return 1;
    }
    if (log_file_contains_substr(log_path, "/tmp/")) {
      fprintf(stderr, "Compile-fail case failed: missing_input_abs path leak (see %s)\n", log_path);
      return 1;
    }
  }
  {
    const char *dir_path = ".";
    const char *log_path = "build/e2e/logs/compile_fail_directory_input.log";
    if (run_ag_c_expect_fail_with_diag(dir_path, "入力ファイルを読み込めませんでした", log_path) != 0) {
      fprintf(stderr, "Compile-fail case failed: directory_input (see %s)\n", log_path);
      return 1;
    }
  }
  {
    const char *blocked_path = "build/e2e/compile_fail/no_read_input.c";
    const char *log_path = "build/e2e/logs/compile_fail_no_read_input.log";
    if (mkdir_p("build/e2e/compile_fail") != 0 || write_source_file(blocked_path, "int main(){return 0;}") != 0) {
      fprintf(stderr, "Compile-fail setup failed: no_read_input\n");
      return 1;
    }
    if (chmod(blocked_path, 0000) != 0) {
      fprintf(stderr, "Compile-fail setup failed: cannot chmod 000 (%s)\n", blocked_path);
      return 1;
    }
    int rc = run_ag_c_expect_fail_with_diag(blocked_path, "入力ファイルを読み込めませんでした", log_path);
    chmod(blocked_path, 0644);
    if (rc != 0) {
      fprintf(stderr, "Compile-fail case failed: no_read_input (see %s)\n", log_path);
      return 1;
    }
  }
  {
    const char *long_path = "build/e2e/compile_fail/huge_single_line.c";
    const char *log_path = "build/e2e/logs/compile_fail_huge_single_line.log";
    if (mkdir_p("build/e2e/compile_fail") != 0 ||
        write_large_single_line_unterminated_string(long_path, 4096) != 0 ||
        run_ag_c_expect_fail_with_diag_timeout(long_path, NULL, log_path, 3,
                                               "huge_single_line") != 0) {
      fprintf(stderr, "Compile-fail case failed: huge_single_line (see %s)\n", log_path);
      return 1;
    }
  }
  {
    const char *tok_limit_path = "build/e2e/compile_fail/tokenizer_int_too_large.c";
    const char *log_path = "build/e2e/logs/compile_fail_tokenizer_int_too_large.log";
    if (mkdir_p("build/e2e/compile_fail") != 0 ||
        write_source_file(tok_limit_path, "int main() { return 18446744073709551616; }\n") != 0 ||
        run_ag_c_expect_fail_profiled(tok_limit_path, "E2015", log_path, 1024) != 0) {
      fprintf(stderr, "Compile-fail case failed: tokenizer_int_too_large (see %s)\n", log_path);
      return 1;
    }
  }
  {
    const char *pp_if_limit_path = "build/e2e/compile_fail/preprocess_if_token_limit.c";
    const char *log_path = "build/e2e/logs/compile_fail_preprocess_if_token_limit.log";
    if (mkdir_p("build/e2e/compile_fail") != 0 ||
        write_pp_if_token_limit_source(pp_if_limit_path, 4200) != 0 ||
        run_ag_c_expect_fail_profiled(pp_if_limit_path, "E1037", log_path, 1024) != 0) {
      fprintf(stderr, "Compile-fail case failed: preprocess_if_token_limit (see %s)\n", log_path);
      return 1;
    }
  }
  {
    const char *parser_width_path = "build/e2e/compile_fail/parser_decl_width_limit.c";
    const char *log_path = "build/e2e/logs/compile_fail_parser_decl_width_limit.log";
    if (mkdir_p("build/e2e/compile_fail") != 0 ||
        write_parser_decl_width_limit_source(parser_width_path, 1300) != 0 ||
        run_ag_c_expect_fail_profiled(parser_width_path, "E3064", log_path, 1024) != 0) {
      fprintf(stderr, "Compile-fail case failed: parser_decl_width_limit (see %s)\n", log_path);
      return 1;
    }
  }
  {
    const char *fd_limit_path = "build/e2e/compile_fail/fd_limit_input.c";
    const char *log_path = "build/e2e/logs/compile_fail_fd_limit.log";
    if (mkdir_p("build/e2e/compile_fail") != 0 ||
        write_source_file(fd_limit_path, "int main() { const int x = 5; x = 10; return 0; }\n") != 0 ||
        run_ag_c_expect_fail_profiled_with_limits_logfile(fd_limit_path, "E3077", log_path, 1024,
                                                          32, 0, 0) != 0) {
      fprintf(stderr, "Compile-fail case failed: fd_limit_input (see %s)\n", log_path);
      return 1;
    }
  }
  {
    const char *mem_limit_path = "build/e2e/compile_fail/mem_limit_input.c";
    const char *log_path = "build/e2e/logs/compile_fail_mem_limit.log";
    if (mkdir_p("build/e2e/compile_fail") != 0 ||
        write_source_file(mem_limit_path, "int main() { const int x = 5; x = 10; return 0; }\n") != 0 ||
        run_ag_c_expect_fail_profiled_with_limits_logfile(mem_limit_path, "E3077", log_path, 1024,
                                                          0, 64 * 1024 * 1024, 0) != 0) {
      fprintf(stderr, "Compile-fail case failed: mem_limit_input (see %s)\n", log_path);
      return 1;
    }
  }
  {
    const char *pp_limit_path = "build/e2e/compile_fail/macro_expansion_limit.c";
    const char *log_path = "build/e2e/logs/compile_fail_macro_expansion_limit.log";
    if (mkdir_p("build/e2e/compile_fail") != 0 ||
        write_macro_expansion_limit_source(pp_limit_path, 16) != 0 ||
        run_ag_c_expect_fail_with_diag(pp_limit_path, "E1029", log_path) != 0) {
      fprintf(stderr, "Compile-fail case failed: macro_expansion_limit (see %s)\n", log_path);
      return 1;
    }
  }
  {
    const char *nul_path = "build/e2e/compile_fail/nul_input.c";
    const char *log_path = "build/e2e/logs/compile_fail_nul_input.log";
    static const unsigned char nul_input[] = {
        'i', 'n', 't', ' ', 'm', 'a', 'i', 'n', '(', ' ', '{', ' ',
        'r', 'e', 't', 'u', 'r', 'n', ' ', '0', ';', ' ', '}', '\n',
        0x00, 'x', '\n',
    };
    if (mkdir_p("build/e2e/compile_fail") != 0 ||
        write_source_file_bytes(nul_path, nul_input, sizeof(nul_input)) != 0 ||
        run_ag_c_expect_fail_with_diag(nul_path, NULL, log_path) != 0) {
      fprintf(stderr, "Compile-fail case failed: nul_input (see %s)\n", log_path);
      return 1;
    }
  }
  {
    const char *path = "build/e2e/compile_fail/control_char_line_filename.c";
    const char *log_path = "build/e2e/logs/compile_fail_control_char_line_filename.log";
    static const unsigned char src[] = {
        '#', 'l', 'i', 'n', 'e', ' ', '1', ' ', '"',
        'b', 'a', 'd', 0x1F, '.', 'c', '"', '\n',
        'i', 'n', 't', ' ', 'm', 'a', 'i', 'n', '(', ')', '{',
        'r', 'e', 't', 'u', 'r', 'n', ' ', '0', ';', '}', '\n',
    };
    if (mkdir_p("build/e2e/compile_fail") != 0 ||
        write_source_file_bytes(path, src, sizeof(src)) != 0 ||
        run_ag_c_expect_fail_with_diag(path, NULL, log_path) != 0) {
      fprintf(stderr, "Compile-fail case failed: control_char_line_filename (see %s)\n", log_path);
      return 1;
    }
  }
  {
    const char *path = "build/e2e/compile_fail/invalid_utf8_include_filename.c";
    const char *log_path = "build/e2e/logs/compile_fail_invalid_utf8_include_filename.log";
    static const unsigned char src[] = {
        '#', 'i', 'n', 'c', 'l', 'u', 'd', 'e', ' ', '"',
        'b', 'u', 'i', 'l', 'd', '/', 0xC0, 0xAF, '.', 'h', '"', '\n',
        'i', 'n', 't', ' ', 'm', 'a', 'i', 'n', '(', ')', '{',
        'r', 'e', 't', 'u', 'r', 'n', ' ', '0', ';', '}', '\n',
    };
    if (mkdir_p("build/e2e/compile_fail") != 0 ||
        write_source_file_bytes(path, src, sizeof(src)) != 0 ||
        run_ag_c_expect_fail_with_diag(path, NULL, log_path) != 0) {
      fprintf(stderr, "Compile-fail case failed: invalid_utf8_include_filename (see %s)\n", log_path);
      return 1;
    }
  }
  {
    const char *path = "build/e2e/compile_fail/invalid_utf8_macro_arg.c";
    const char *log_path = "build/e2e/logs/compile_fail_invalid_utf8_macro_arg.log";
    static const unsigned char src[] = {
        '#', 'd', 'e', 'f', 'i', 'n', 'e', ' ', 'I', 'D', '(', 'x', ')', ' ', 'x', '\n',
        'i', 'n', 't', ' ', 'm', 'a', 'i', 'n', '(', ')', '{',
        'r', 'e', 't', 'u', 'r', 'n', ' ', 'I', 'D', '(', 0xC0, 0xAF, ')', ';', '}', '\n',
    };
    if (mkdir_p("build/e2e/compile_fail") != 0 ||
        write_source_file_bytes(path, src, sizeof(src)) != 0 ||
        run_ag_c_expect_fail_with_diag(path, NULL, log_path) != 0) {
      fprintf(stderr, "Compile-fail case failed: invalid_utf8_macro_arg (see %s)\n", log_path);
      return 1;
    }
  }
  {
    char cwd[PATH_MAX];
    int tmp_prefix_before = count_tmp_files_with_prefix("ag_c_e2e_include_");
    if (tmp_prefix_before < 0) {
      fprintf(stderr, "Compile-fail setup failed: cannot count /tmp include-prefix files\n");
      return 1;
    }
    if (!getcwd(cwd, sizeof(cwd))) {
      fprintf(stderr, "Compile-fail setup failed: cannot get cwd for include leak check\n");
      return 1;
    }
    char tmp_header[] = "/tmp/ag_c_e2e_include_XXXXXX";
    int tmp_fd = mkstemp(tmp_header);
    if (tmp_fd < 0) {
      fprintf(stderr, "Compile-fail setup failed: cannot create temp include header\n");
      return 1;
    }
    FILE *tmp_fp = fdopen(tmp_fd, "w");
    if (!tmp_fp) {
      close(tmp_fd);
      unlink(tmp_header);
      fprintf(stderr, "Compile-fail setup failed: cannot open temp include header\n");
      return 1;
    }
    fprintf(tmp_fp, "int leaked_tmp_header(void) { return 0; }\n");
    fclose(tmp_fp);

    const char *link_path = "build/e2e/compile_fail/include_tmp_leak.h";
    unlink(link_path);
    if (symlink(tmp_header, link_path) != 0) {
      unlink(tmp_header);
      fprintf(stderr, "Compile-fail setup failed: cannot create include leak symlink\n");
      return 1;
    }

    const char *src_path = "build/e2e/compile_fail/include_tmp_leak.c";
    const char *log_path = "build/e2e/logs/compile_fail_include_tmp_leak.log";
    if (write_source_file(src_path, "#include \"build/e2e/compile_fail/include_tmp_leak.h\"\nint main(){return 0;}\n") != 0 ||
        run_ag_c_expect_fail_with_diag(src_path, "E1002", log_path) != 0) {
      unlink(link_path);
      unlink(tmp_header);
      fprintf(stderr, "Compile-fail case failed: include_tmp_leak (see %s)\n", log_path);
      return 1;
    }
    if (log_file_contains_substr(log_path, tmp_header) || log_file_contains_substr(log_path, cwd) ||
        log_file_contains_substr(log_path, "/tmp/ag_c_e2e_include_")) {
      unlink(link_path);
      unlink(tmp_header);
      fprintf(stderr, "Compile-fail case failed: include_tmp_leak path leak (see %s)\n", log_path);
      return 1;
    }
    unlink(link_path);
    unlink(tmp_header);
    int tmp_prefix_after = count_tmp_files_with_prefix("ag_c_e2e_include_");
    if (tmp_prefix_after < 0 || tmp_prefix_after != tmp_prefix_before) {
      fprintf(stderr, "Compile-fail case failed: include_tmp_leak tmp artifact leak\n");
      return 1;
    }
  }
  {
    const char *log_path = "build/e2e/logs/compile_fail_usage_no_args.log";
    if (run_ag_c_expect_fail_with_args_and_diag(NULL, NULL, "使い方:", log_path) != 0) {
      fprintf(stderr, "Compile-fail case failed: usage_no_args (see %s)\n", log_path);
      return 1;
    }
  }
  {
    const char *log_path = "build/e2e/logs/compile_fail_usage_too_many_args.log";
    if (run_ag_c_expect_fail_with_args_and_diag("a.c", "b.c", "使い方:", log_path) != 0) {
      fprintf(stderr, "Compile-fail case failed: usage_too_many_args (see %s)\n", log_path);
      return 1;
    }
  }
  {
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) {
      fprintf(stderr, "Compile-fail setup failed: cannot get cwd for leak check\n");
      return 1;
    }
    char abs_prog[PATH_MAX];
    snprintf(abs_prog, sizeof(abs_prog), "%s/build/ag_c", cwd);
    const char *log_path = "build/e2e/logs/compile_fail_usage_abs_prog_path.log";
    if (run_ag_c_expect_fail_with_prog_args_and_diag(abs_prog, NULL, NULL, "使い方:", log_path) != 0) {
      fprintf(stderr, "Compile-fail case failed: usage_abs_prog_path (see %s)\n", log_path);
      return 1;
    }
    if (log_file_contains_substr(log_path, cwd) || log_file_contains_substr(log_path, "/tmp/") ||
        log_file_contains_substr(log_path, abs_prog)) {
      fprintf(stderr, "Compile-fail case failed: usage_abs_prog_path leak (see %s)\n", log_path);
      return 1;
    }
    const char *user = getenv("USER");
    if (user && *user && log_file_contains_substr(log_path, user)) {
      fprintf(stderr, "Compile-fail case failed: usage_abs_prog_path user leak (see %s)\n", log_path);
      return 1;
    }
  }
  if (run_ag_c_parallel_smoke() != 0) {
    fprintf(stderr, "Concurrency smoke case failed: parallel ag_c invocation\n");
    return 1;
  }
  int fd_count_after = count_open_fds_self();
  if (fd_count_after < 0 || fd_count_after != fd_count_baseline) {
    fprintf(stderr, "Resource leak check failed: fd count changed (before=%d after=%d)\n",
            fd_count_baseline, fd_count_after);
    return 1;
  }
  int tmp_include_prefix_after = count_tmp_files_with_prefix("ag_c_e2e_include_");
  if (tmp_include_prefix_after < 0 || tmp_include_prefix_after != tmp_include_prefix_baseline) {
    fprintf(stderr, "Resource leak check failed: /tmp include-prefix count changed (before=%d after=%d)\n",
            tmp_include_prefix_baseline, tmp_include_prefix_after);
    return 1;
  }

  size_t max_cases = sizeof(test_cases) / sizeof(test_cases[0]);
  const char **categories = calloc(max_cases, sizeof(const char *));
  pid_t *build_pids = calloc(max_cases, sizeof(pid_t));
  pid_t *pids = calloc(max_cases, sizeof(pid_t));
  if (!categories || !build_pids || !pids) {
    fprintf(stderr, "Failed to allocate category buffers\n");
    free(categories);
    free(build_pids);
    free(pids);
    return 1;
  }

  size_t ncat = 0;
  for (size_t i = 0; i < max_cases; i++) {
    const char *cat = test_cases[i].category;
    bool exists = false;
    for (size_t j = 0; j < ncat; j++) {
      if (strcmp(categories[j], cat) == 0) { exists = true; break; }
    }
    if (!exists) categories[ncat++] = cat;
  }

  for (size_t i = 0; i < ncat; i++) {
    pid_t pid = fork();
    if (pid == 0) {
      int rc = build_category(categories[i]);
      _exit(rc);
    }
    build_pids[i] = pid;
  }

  for (size_t i = 0; i < ncat; i++) {
    int status;
    waitpid(build_pids[i], &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      fprintf(stderr, "Build failed: %s (see build/e2e/logs/%s.build.log)\n", categories[i], categories[i]);
      free(categories);
      free(build_pids);
      free(pids);
      return 1;
    }
  }

  for (size_t i = 0; i < ncat; i++) {
    pid_t pid = fork();
    if (pid == 0) {
      int rc = run_category(categories[i]);
      _exit(rc);
    }
    pids[i] = pid;
  }

  int failed = 0;
  for (size_t i = 0; i < ncat; i++) {
    int status;
    waitpid(pids[i], &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      failed = 1;
      fprintf(stderr, "Category failed: %s (see build/e2e/logs/%s.log)\n", categories[i], categories[i]);
    }
  }

  test_count = (int)((sizeof(test_cases) / sizeof(test_cases[0])) +
                     (sizeof(compile_fail_cases) / sizeof(compile_fail_cases[0])) + 14);
  pass_count = failed ? 0 : test_count;

  free(categories);
  free(build_pids);
  free(pids);
  if (failed) return 1;
  printf("OK: All %d E2E tests passed! (%d/%d)\n", test_count, pass_count, test_count);
  return 0;
}
