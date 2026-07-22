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
  // assert 自己検証 fixture。fixture 内の assert(...) が期待を自己記述し、成功時は
  // main が 0 を返す (失敗時は assert が abort)。期待値は常に 0 なので test_e2e.c に
  // マジックな期待値を書かない (expected_i は 0 固定で無視)。値種別は CASE_INT。
  CASE_ASSERT_FILE,
} case_kind_t;

static inline bool case_kind_is_file(case_kind_t k) {
  return k == CASE_INT_FILE || k == CASE_FLOAT_FILE || k == CASE_DOUBLE_FILE ||
         k == CASE_ASSERT_FILE;
}

// 比較ロジックに使う「値の種類」を返す。`_FILE` バリアントは対応する非 _FILE 版に正規化する。
static inline case_kind_t case_kind_value_kind(case_kind_t k) {
  switch (k) {
    case CASE_INT_FILE: return CASE_INT;
    case CASE_ASSERT_FILE: return CASE_INT;  // 成功 = main が 0 を返す
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
    {"integer", "zero", CASE_ASSERT_FILE, "test/fixtures/integer/zero.c", 0, 0},
    {"integer", "literal", CASE_ASSERT_FILE, "test/fixtures/integer/literal.c", 0, 0},
    {"integer", "hex_literal", CASE_ASSERT_FILE, "test/fixtures/integer/hex_literal.c", 0, 0},
    {"integer", "oct_literal", CASE_ASSERT_FILE, "test/fixtures/integer/oct_literal.c", 0, 0},
    {"integer", "bin_literal", CASE_ASSERT_FILE, "test/fixtures/integer/bin_literal.c", 0, 0},
    {"integer", "suffix_LL_U", CASE_ASSERT_FILE, "test/fixtures/integer/suffix_LL_U.c", 0, 0},

    {"arithmetic", "add_sub", CASE_ASSERT_FILE, "test/fixtures/arithmetic/add_sub.c", 0, 0},
    {"arithmetic", "spaces", CASE_ASSERT_FILE, "test/fixtures/arithmetic/spaces.c", 0, 0},
    {"arithmetic", "mul", CASE_ASSERT_FILE, "test/fixtures/arithmetic/mul.c", 0, 0},
    {"arithmetic", "paren", CASE_ASSERT_FILE, "test/fixtures/arithmetic/paren.c", 0, 0},
    {"arithmetic", "div", CASE_ASSERT_FILE, "test/fixtures/arithmetic/div.c", 0, 0},
    {"arithmetic", "mod", CASE_ASSERT_FILE, "test/fixtures/arithmetic/mod.c", 0, 0},
    {"arithmetic", "mod_prec", CASE_ASSERT_FILE, "test/fixtures/arithmetic/mod_prec.c", 0, 0},
    {"arithmetic", "mod_neg_lhs", CASE_ASSERT_FILE, "test/fixtures/arithmetic/mod_neg_lhs.c", 0, 0},
    {"arithmetic", "mod_neg_rhs", CASE_ASSERT_FILE, "test/fixtures/arithmetic/mod_neg_rhs.c", 0, 0},
    {"arithmetic", "mod_zero_impl_defined", CASE_ASSERT_FILE, "test/fixtures/arithmetic/mod_zero_impl_defined.c", 0, 0},
    {"arithmetic", "unary_plus", CASE_ASSERT_FILE, "test/fixtures/arithmetic/unary_plus.c", 0, 0},
    {"arithmetic", "unary_minus", CASE_ASSERT_FILE, "test/fixtures/arithmetic/unary_minus.c", 0, 0},
    {"arithmetic", "logical_not_true", CASE_ASSERT_FILE, "test/fixtures/arithmetic/logical_not_true.c", 0, 0},
    {"arithmetic", "logical_not_false", CASE_ASSERT_FILE, "test/fixtures/arithmetic/logical_not_false.c", 0, 0},
    {"arithmetic", "bit_not", CASE_ASSERT_FILE, "test/fixtures/arithmetic/bit_not.c", 0, 0},
    {"arithmetic", "pre_inc", CASE_ASSERT_FILE, "test/fixtures/arithmetic/pre_inc.c", 0, 0},
    {"arithmetic", "post_inc", CASE_ASSERT_FILE, "test/fixtures/arithmetic/post_inc.c", 0, 0},
    {"arithmetic", "pre_dec", CASE_ASSERT_FILE, "test/fixtures/arithmetic/pre_dec.c", 0, 0},
    {"arithmetic", "post_dec", CASE_ASSERT_FILE, "test/fixtures/arithmetic/post_dec.c", 0, 0},
    {"arithmetic", "postinc_add", CASE_ASSERT_FILE, "test/fixtures/arithmetic/postinc_add.c", 0, 0},
    {"arithmetic", "postdec_sub", CASE_ASSERT_FILE, "test/fixtures/arithmetic/postdec_sub.c", 0, 0},
    {"arithmetic", "postinc_unary_plus", CASE_ASSERT_FILE, "test/fixtures/arithmetic/postinc_unary_plus.c", 0, 0},
    {"arithmetic", "postdec_unary_minus", CASE_ASSERT_FILE, "test/fixtures/arithmetic/postdec_unary_minus.c", 0, 0},
    {"arithmetic", "postinc_mul", CASE_ASSERT_FILE, "test/fixtures/arithmetic/postinc_mul.c", 0, 0},
    {"arithmetic", "preinc_add", CASE_ASSERT_FILE, "test/fixtures/arithmetic/preinc_add.c", 0, 0},
    {"arithmetic", "postinc_neg", CASE_ASSERT_FILE, "test/fixtures/arithmetic/postinc_neg.c", 0, 0},
    {"arithmetic", "postinc_chain", CASE_ASSERT_FILE, "test/fixtures/arithmetic/postinc_chain.c", 0, 0},
    {"arithmetic", "neg_postinc", CASE_ASSERT_FILE, "test/fixtures/arithmetic/neg_postinc.c", 0, 0},
    {"arithmetic", "postinc_postdec_mix", CASE_ASSERT_FILE, "test/fixtures/arithmetic/postinc_postdec_mix.c", 0, 0},
    {"arithmetic", "add_eq", CASE_ASSERT_FILE, "test/fixtures/arithmetic/add_eq.c", 0, 0},
    {"arithmetic", "sub_eq", CASE_ASSERT_FILE, "test/fixtures/arithmetic/sub_eq.c", 0, 0},
    {"arithmetic", "mul_eq", CASE_ASSERT_FILE, "test/fixtures/arithmetic/mul_eq.c", 0, 0},
    {"arithmetic", "div_eq", CASE_ASSERT_FILE, "test/fixtures/arithmetic/div_eq.c", 0, 0},
    {"arithmetic", "mod_eq", CASE_ASSERT_FILE, "test/fixtures/arithmetic/mod_eq.c", 0, 0},
    {"arithmetic", "shl_eq", CASE_ASSERT_FILE, "test/fixtures/arithmetic/shl_eq.c", 0, 0},
    {"arithmetic", "shr_eq", CASE_ASSERT_FILE, "test/fixtures/arithmetic/shr_eq.c", 0, 0},
    {"arithmetic", "and_eq", CASE_ASSERT_FILE, "test/fixtures/arithmetic/and_eq.c", 0, 0},
    {"arithmetic", "xor_eq", CASE_ASSERT_FILE, "test/fixtures/arithmetic/xor_eq.c", 0, 0},
    {"arithmetic", "or_eq", CASE_ASSERT_FILE, "test/fixtures/arithmetic/or_eq.c", 0, 0},
    {"arithmetic", "comma_basic", CASE_ASSERT_FILE, "test/fixtures/arithmetic/comma_basic.c", 0, 0},
    {"arithmetic", "comma_chain", CASE_ASSERT_FILE, "test/fixtures/arithmetic/comma_chain.c", 0, 0},

    {"comparison", "eq1", CASE_ASSERT_FILE, "test/fixtures/comparison/eq1.c", 0, 0},
    {"comparison", "eq2", CASE_ASSERT_FILE, "test/fixtures/comparison/eq2.c", 0, 0},
    {"comparison", "neq1", CASE_ASSERT_FILE, "test/fixtures/comparison/neq1.c", 0, 0},
    {"comparison", "neq2", CASE_ASSERT_FILE, "test/fixtures/comparison/neq2.c", 0, 0},
    {"comparison", "lt1", CASE_ASSERT_FILE, "test/fixtures/comparison/lt1.c", 0, 0},
    {"comparison", "lt2", CASE_ASSERT_FILE, "test/fixtures/comparison/lt2.c", 0, 0},
    {"comparison", "lt3", CASE_ASSERT_FILE, "test/fixtures/comparison/lt3.c", 0, 0},
    {"comparison", "le1", CASE_ASSERT_FILE, "test/fixtures/comparison/le1.c", 0, 0},
    {"comparison", "le2", CASE_ASSERT_FILE, "test/fixtures/comparison/le2.c", 0, 0},
    {"comparison", "le3", CASE_ASSERT_FILE, "test/fixtures/comparison/le3.c", 0, 0},
    {"comparison", "gt1", CASE_ASSERT_FILE, "test/fixtures/comparison/gt1.c", 0, 0},
    {"comparison", "gt2", CASE_ASSERT_FILE, "test/fixtures/comparison/gt2.c", 0, 0},
    {"comparison", "gt3", CASE_ASSERT_FILE, "test/fixtures/comparison/gt3.c", 0, 0},
    {"comparison", "ge1", CASE_ASSERT_FILE, "test/fixtures/comparison/ge1.c", 0, 0},
    {"comparison", "ge2", CASE_ASSERT_FILE, "test/fixtures/comparison/ge2.c", 0, 0},
    {"comparison", "ge3", CASE_ASSERT_FILE, "test/fixtures/comparison/ge3.c", 0, 0},
    {"comparison", "log_and", CASE_ASSERT_FILE, "test/fixtures/comparison/log_and.c", 0, 0},
    {"comparison", "log_or", CASE_ASSERT_FILE, "test/fixtures/comparison/log_or.c", 0, 0},
    {"comparison", "log_prec", CASE_ASSERT_FILE, "test/fixtures/comparison/log_prec.c", 0, 0},
    {"comparison", "short_and", CASE_ASSERT_FILE, "test/fixtures/comparison/short_and.c", 0, 0},
    {"comparison", "short_or", CASE_ASSERT_FILE, "test/fixtures/comparison/short_or.c", 0, 0},
    {"comparison", "short_and_sideeffect", CASE_ASSERT_FILE, "test/fixtures/comparison/short_and_sideeffect.c", 0, 0},
    {"comparison", "short_or_sideeffect", CASE_ASSERT_FILE, "test/fixtures/comparison/short_or_sideeffect.c", 0, 0},
    {"comparison", "ternary_true", CASE_ASSERT_FILE, "test/fixtures/comparison/ternary_true.c", 0, 0},
    {"comparison", "ternary_false", CASE_ASSERT_FILE, "test/fixtures/comparison/ternary_false.c", 0, 0},
    {"comparison", "ternary_nested", CASE_ASSERT_FILE, "test/fixtures/comparison/ternary_nested.c", 0, 0},
    {"comparison", "ternary_deep_nest", CASE_ASSERT_FILE, "test/fixtures/comparison/ternary_deep_nest.c", 0, 0},
    {"comparison", "ternary_chain", CASE_ASSERT_FILE, "test/fixtures/comparison/ternary_chain.c", 0, 0},
    {"local_variables", "basic", CASE_ASSERT_FILE, "test/fixtures/local_variables/basic.c", 0, 0},
    {"local_variables", "expr", CASE_ASSERT_FILE, "test/fixtures/local_variables/expr.c", 0, 0},
    {"local_variables", "sum3", CASE_ASSERT_FILE, "test/fixtures/local_variables/sum3.c", 0, 0},
    {"local_variables", "mul2", CASE_ASSERT_FILE, "test/fixtures/local_variables/mul2.c", 0, 0},
    {"local_variables", "copy", CASE_ASSERT_FILE, "test/fixtures/local_variables/copy.c", 0, 0},
    {"local_variables", "static_counter", CASE_ASSERT_FILE, "test/fixtures/local_variables/static_counter.c", 0, 0},
    {"local_variables", "static_separate_funcs", CASE_ASSERT_FILE, "test/fixtures/local_variables/static_separate_funcs.c", 0, 0},

    {"if_else", "if_true", CASE_ASSERT_FILE, "test/fixtures/if_else/if_true.c", 0, 0},
    {"if_else", "if_false", CASE_ASSERT_FILE, "test/fixtures/if_else/if_false.c", 0, 0},
    {"if_else", "branch1", CASE_ASSERT_FILE, "test/fixtures/if_else/branch1.c", 0, 0},
    {"if_else", "branch2", CASE_ASSERT_FILE, "test/fixtures/if_else/branch2.c", 0, 0},
    {"if_else", "literal1", CASE_ASSERT_FILE, "test/fixtures/if_else/literal1.c", 0, 0},
    {"if_else", "literal0", CASE_ASSERT_FILE, "test/fixtures/if_else/literal0.c", 0, 0},
    {"if_else", "fallthrough", CASE_ASSERT_FILE, "test/fixtures/if_else/fallthrough.c", 0, 0},

    {"while", "count", CASE_ASSERT_FILE, "test/fixtures/while/count.c", 0, 0},
    {"while", "zero", CASE_ASSERT_FILE, "test/fixtures/while/zero.c", 0, 0},
    {"while", "do_once", CASE_ASSERT_FILE, "test/fixtures/while/do_once.c", 0, 0},
    {"while", "do_loop", CASE_ASSERT_FILE, "test/fixtures/while/do_loop.c", 0, 0},
    {"while", "break", CASE_ASSERT_FILE, "test/fixtures/while/break.c", 0, 0},
    {"while", "continue", CASE_ASSERT_FILE, "test/fixtures/while/continue.c", 0, 0},
    {"while", "for_break_continue", CASE_ASSERT_FILE, "test/fixtures/while/for_break_continue.c", 0, 0},
    {"while", "do_continue", CASE_ASSERT_FILE, "test/fixtures/while/do_continue.c", 0, 0},

    {"for", "sum10", CASE_ASSERT_FILE, "test/fixtures/for/sum10.c", 0, 0},
    {"for", "inc", CASE_ASSERT_FILE, "test/fixtures/for/inc.c", 0, 0},
    {"for", "post_inc_expr", CASE_ASSERT_FILE, "test/fixtures/for/post_inc_expr.c", 0, 0},
    {"for", "empty_for", CASE_ASSERT_FILE, "test/fixtures/for/empty_for.c", 0, 0},
    {"for", "declaration_multiple", CASE_ASSERT_FILE, "test/fixtures/for/declaration_multiple.c", 0, 0},

    {"bitwise", "bit_and", CASE_ASSERT_FILE, "test/fixtures/bitwise/bit_and.c", 0, 0},
    {"bitwise", "bit_xor", CASE_ASSERT_FILE, "test/fixtures/bitwise/bit_xor.c", 0, 0},
    {"bitwise", "bit_or", CASE_ASSERT_FILE, "test/fixtures/bitwise/bit_or.c", 0, 0},
    {"bitwise", "bit_precedence", CASE_ASSERT_FILE, "test/fixtures/bitwise/bit_precedence.c", 0, 0},
    {"bitwise", "bit_vs_logical_prec", CASE_ASSERT_FILE, "test/fixtures/bitwise/bit_vs_logical_prec.c", 0, 0},

    {"shift", "shl", CASE_ASSERT_FILE, "test/fixtures/shift/shl.c", 0, 0},
    {"shift", "shr", CASE_ASSERT_FILE, "test/fixtures/shift/shr.c", 0, 0},
    {"shift", "shift_precedence", CASE_ASSERT_FILE, "test/fixtures/shift/shift_precedence.c", 0, 0},
    {"shift", "shift_neg_right", CASE_ASSERT_FILE, "test/fixtures/shift/shift_neg_right.c", 0, 0},
    {"shift", "shift_by_zero", CASE_ASSERT_FILE, "test/fixtures/shift/shift_by_zero.c", 0, 0},
    {"shift", "shift_large_bit", CASE_ASSERT_FILE, "test/fixtures/shift/shift_large_bit.c", 0, 0},

    {"switch_edge", "match", CASE_ASSERT_FILE, "test/fixtures/switch_edge/match.c", 0, 0},
    {"switch_edge", "default", CASE_ASSERT_FILE, "test/fixtures/switch_edge/default.c", 0, 0},
    {"switch_edge", "fallthrough", CASE_ASSERT_FILE, "test/fixtures/switch_edge/fallthrough.c", 0, 0},
    {"switch_edge", "case_const_expr", CASE_ASSERT_FILE, "test/fixtures/switch_edge/case_const_expr.c", 0, 0},
    {"switch_edge", "case_enum_const_expr", CASE_ASSERT_FILE, "test/fixtures/switch_edge/case_enum_const_expr.c", 0, 0},
    {"switch_edge", "break_in_switch", CASE_ASSERT_FILE, "test/fixtures/switch_edge/break_in_switch.c", 0, 0},
    {"switch_edge", "continue_outer_loop", CASE_ASSERT_FILE, "test/fixtures/switch_edge/continue_outer_loop.c", 0, 0},
    {"switch_edge", "goto_forward", CASE_ASSERT_FILE, "test/fixtures/switch_edge/goto_forward.c", 0, 0},
    {"switch_edge", "goto_backward_loop", CASE_ASSERT_FILE, "test/fixtures/switch_edge/goto_backward_loop.c", 0, 0},
    {"switch_edge", "goto_from_case", CASE_ASSERT_FILE, "test/fixtures/switch_edge/goto_from_case.c", 0, 0},
    {"switch_edge", "goto_loop_switch", CASE_ASSERT_FILE, "test/fixtures/switch_edge/goto_loop_switch.c", 0, 0},
    {"switch_edge", "goto_inside_case", CASE_ASSERT_FILE, "test/fixtures/switch_edge/goto_inside_case.c", 0, 0},
    {"switch_edge", "goto_out_of_loop_switch", CASE_ASSERT_FILE, "test/fixtures/switch_edge/goto_out_of_loop_switch.c", 0, 0},
    {"switch_edge", "fallthrough_multi", CASE_ASSERT_FILE, "test/fixtures/switch_edge/fallthrough_multi.c", 0, 0},
    {"switch_edge", "goto_state_machine", CASE_ASSERT_FILE, "test/fixtures/switch_edge/goto_state_machine.c", 0, 0},
    {"switch_edge", "goto_into_loop", CASE_ASSERT_FILE, "test/fixtures/switch_edge/goto_into_loop.c", 0, 0},
    {"switch_edge", "continue_in_switch_for", CASE_ASSERT_FILE, "test/fixtures/switch_edge/continue_in_switch_for.c", 0, 0},
    {"switch_edge", "nested_switch", CASE_ASSERT_FILE, "test/fixtures/switch_edge/nested_switch.c", 0, 0},
    {"switch_edge", "case_in_block", CASE_ASSERT_FILE, "test/fixtures/switch_edge/case_in_block.c", 0, 0},
    {"switch_edge", "duff_do_while", CASE_ASSERT_FILE, "test/fixtures/switch_edge/duff_do_while.c", 0, 0},
    {"switch_edge", "duff_do_while_case2", CASE_ASSERT_FILE, "test/fixtures/switch_edge/duff_do_while_case2.c", 0, 0},

    {"return", "literal", CASE_ASSERT_FILE, "test/fixtures/return/literal.c", 0, 0},
    {"return", "expr", CASE_ASSERT_FILE, "test/fixtures/return/expr.c", 0, 0},
    {"return", "var", CASE_ASSERT_FILE, "test/fixtures/return/var.c", 0, 0},
    {"return", "sum", CASE_ASSERT_FILE, "test/fixtures/return/sum.c", 0, 0},
    {"return", "if", CASE_ASSERT_FILE, "test/fixtures/return/if.c", 0, 0},
    {"return", "while", CASE_ASSERT_FILE, "test/fixtures/return/while.c", 0, 0},

    {"block", "stmts", CASE_ASSERT_FILE, "test/fixtures/block/stmts.c", 0, 0},
    {"block", "sum", CASE_ASSERT_FILE, "test/fixtures/block/sum.c", 0, 0},
    {"block", "for", CASE_ASSERT_FILE, "test/fixtures/block/for.c", 0, 0},
    {"block", "while", CASE_ASSERT_FILE, "test/fixtures/block/while.c", 0, 0},
    {"block", "if", CASE_ASSERT_FILE, "test/fixtures/block/if.c", 0, 0},

    {"funcall", "noargs", CASE_ASSERT_FILE, "test/fixtures/funcall/noargs.c", 0, 0},
    {"funcall", "add", CASE_ASSERT_FILE, "test/fixtures/funcall/add.c", 0, 0},
    {"funcall", "twice", CASE_ASSERT_FILE, "test/fixtures/funcall/twice.c", 0, 0},
    {"funcall", "multi", CASE_ASSERT_FILE, "test/fixtures/funcall/multi.c", 0, 0},
    {"funcall", "rec", CASE_ASSERT_FILE, "test/fixtures/funcall/rec.c", 0, 0},
    {"funcall", "tail_rec", CASE_ASSERT_FILE, "test/fixtures/funcall/tail_rec.c", 0, 0},
    {"funcall", "comma_arg", CASE_ASSERT_FILE, "test/fixtures/funcall/comma_arg.c", 0, 0},
    {"funcall", "prototype_decl", CASE_ASSERT_FILE, "test/fixtures/funcall/prototype_decl.c", 0, 0},
    {"funcall", "paren_name_funcdef", CASE_ASSERT_FILE, "test/fixtures/funcall/paren_name_funcdef.c", 0, 0},
    {"funcall", "funcdef_ret_funcptr", CASE_ASSERT_FILE, "test/fixtures/funcall/funcdef_ret_funcptr.c", 0, 0},
    {"funcall", "funcdef_ret_funcptr_with_param", CASE_ASSERT_FILE, "test/fixtures/funcall/funcdef_ret_funcptr_with_param.c", 0, 0},
    {"funcall", "funcdef_ret_nested_funcptr_arrayptr", CASE_ASSERT_FILE, "test/fixtures/funcall/funcdef_ret_nested_funcptr_arrayptr.c", 0, 0},
    {"funcall", "param_funcptr_decl", CASE_ASSERT_FILE, "test/fixtures/funcall/param_funcptr_decl.c", 0, 0},
    {"funcall", "param_array_decl", CASE_ASSERT_FILE, "test/fixtures/funcall/param_array_decl.c", 0, 0},
    {"funcall", "param_array_static_restrict", CASE_ASSERT_FILE, "test/fixtures/funcall/param_array_static_restrict.c", 0, 0},
    {"funcall", "funcptr_value_assign_call", CASE_ASSERT_FILE, "test/fixtures/funcall/funcptr_value_assign_call.c", 0, 0},
    {"funcall", "printf_variadic", CASE_ASSERT_FILE, "test/fixtures/funcall/printf_variadic.c", 0, 0},
    {"funcall", "variadic_proto", CASE_ASSERT_FILE, "test/fixtures/funcall/variadic_proto.c", 0, 0},
    {"funcall", "variadic_def", CASE_ASSERT_FILE, "test/fixtures/funcall/variadic_def.c", 0, 0},
    {"funcall", "fib_recursive", CASE_ASSERT_FILE, "test/fixtures/funcall/fib_recursive.c", 0, 0},
    {"funcall", "abs_ternary", CASE_ASSERT_FILE, "test/fixtures/funcall/abs_ternary.c", 0, 0},
    {"funcall", "funcptr_apply_multi", CASE_ASSERT_FILE, "test/fixtures/funcall/funcptr_apply_multi.c", 0, 0},

    {"multichar_var", "foo", CASE_ASSERT_FILE, "test/fixtures/multichar_var/foo.c", 0, 0},
    {"multichar_var", "hello", CASE_ASSERT_FILE, "test/fixtures/multichar_var/hello.c", 0, 0},
    {"multichar_var", "x1x2", CASE_ASSERT_FILE, "test/fixtures/multichar_var/x1x2.c", 0, 0},
    {"multichar_var", "args", CASE_ASSERT_FILE, "test/fixtures/multichar_var/args.c", 0, 0},
    {"multichar_var", "loop", CASE_ASSERT_FILE, "test/fixtures/multichar_var/loop.c", 0, 0},

    {"type_decl", "int_func", CASE_ASSERT_FILE, "test/fixtures/type_decl/int_func.c", 0, 0},
    {"type_decl", "int_var", CASE_ASSERT_FILE, "test/fixtures/type_decl/int_var.c", 0, 0},
    {"type_decl", "int_sum", CASE_ASSERT_FILE, "test/fixtures/type_decl/int_sum.c", 0, 0},
    {"type_decl", "funcdef_ret_inline_struct_tag", CASE_ASSERT_FILE, "test/fixtures/type_decl/funcdef_ret_inline_struct_tag.c", 0, 0},
    {"type_decl", "funcdef_ret_inline_union_tag_parse_only", CASE_ASSERT_FILE, "test/fixtures/type_decl/funcdef_ret_inline_union_tag_parse_only.c", 0, 0},
    {"type_decl", "funcdef_ret_inline_struct_tag_paren_name", CASE_ASSERT_FILE, "test/fixtures/type_decl/funcdef_ret_inline_struct_tag_paren_name.c", 0, 0},
    {"type_decl", "funcdef_ret_inline_union_tag_paren_name_parse_only", CASE_ASSERT_FILE, "test/fixtures/type_decl/funcdef_ret_inline_union_tag_paren_name_parse_only.c", 0, 0},
    {"type_decl", "int_args", CASE_ASSERT_FILE, "test/fixtures/type_decl/int_args.c", 0, 0},
    {"type_decl", "int_init", CASE_ASSERT_FILE, "test/fixtures/type_decl/int_init.c", 0, 0},
    {"type_decl", "multi_decl_one_init", CASE_ASSERT_FILE, "test/fixtures/type_decl/multi_decl_one_init.c", 0, 0},
    {"type_decl", "multi_decl_two_init", CASE_ASSERT_FILE, "test/fixtures/type_decl/multi_decl_two_init.c", 0, 0},
    {"type_decl", "for_decl", CASE_ASSERT_FILE, "test/fixtures/type_decl/for_decl.c", 0, 0},
    {"type_decl", "for_multi_decl_init", CASE_ASSERT_FILE, "test/fixtures/type_decl/for_multi_decl_init.c", 0, 0},
    {"type_decl", "tag_decl_minimal", CASE_ASSERT_FILE, "test/fixtures/type_decl/tag_decl_minimal.c", 0, 0},
    {"type_decl", "tag_decl_ref_ptr", CASE_ASSERT_FILE, "test/fixtures/type_decl/tag_decl_ref_ptr.c", 0, 0},
    {"type_decl", "tag_def_struct", CASE_ASSERT_FILE, "test/fixtures/type_decl/tag_def_struct.c", 0, 0},
    {"type_decl", "tag_def_and_ptr_decl", CASE_ASSERT_FILE, "test/fixtures/type_decl/tag_def_and_ptr_decl.c", 0, 0},
    {"type_decl", "tag_def_union_enum", CASE_ASSERT_FILE, "test/fixtures/type_decl/tag_def_union_enum.c", 0, 0},
    {"type_decl", "enum_const_ref", CASE_ASSERT_FILE, "test/fixtures/type_decl/enum_const_ref.c", 0, 0},
    {"type_decl", "enum_const_expr", CASE_ASSERT_FILE, "test/fixtures/type_decl/enum_const_expr.c", 0, 0},
    {"type_decl", "enum_const_expr_cond", CASE_ASSERT_FILE, "test/fixtures/type_decl/enum_const_expr_cond.c", 0, 0},
    {"type_decl", "enum_const_expr_bitwise", CASE_ASSERT_FILE, "test/fixtures/type_decl/enum_const_expr_bitwise.c", 0, 0},
    {"type_decl", "global_tag_before_main", CASE_ASSERT_FILE, "test/fixtures/type_decl/global_tag_before_main.c", 0, 0},
    {"type_decl", "global_tag_decl_with_var", CASE_ASSERT_FILE, "test/fixtures/type_decl/global_tag_decl_with_var.c", 0, 0},
    {"type_decl", "global_int_var_decl", CASE_ASSERT_FILE, "test/fixtures/type_decl/global_int_var_decl.c", 0, 0},
    {"type_decl", "global_extern_incomplete_array_decl", CASE_ASSERT_FILE, "test/fixtures/type_decl/global_extern_incomplete_array_decl.c", 0, 0},
    {"type_decl", "local_extern_incomplete_array_decl", CASE_ASSERT_FILE, "test/fixtures/type_decl/local_extern_incomplete_array_decl.c", 0, 0},
    {"type_decl", "typedef_incomplete_array_type", CASE_ASSERT_FILE, "test/fixtures/type_decl/typedef_incomplete_array_type.c", 0, 0},
    {"type_decl", "char", CASE_ASSERT_FILE, "test/fixtures/type_decl/char.c", 0, 0},
    {"type_decl", "void", CASE_ASSERT_FILE, "test/fixtures/type_decl/void.c", 0, 0},
    {"type_decl", "short", CASE_ASSERT_FILE, "test/fixtures/type_decl/short.c", 0, 0},
    {"type_decl", "long", CASE_ASSERT_FILE, "test/fixtures/type_decl/long.c", 0, 0},
    {"type_decl", "short_arr", CASE_ASSERT_FILE, "test/fixtures/type_decl/short_arr.c", 0, 0},
    {"type_decl", "short_sum", CASE_ASSERT_FILE, "test/fixtures/type_decl/short_sum.c", 0, 0},
    {"type_decl", "short_one", CASE_ASSERT_FILE, "test/fixtures/type_decl/short_one.c", 0, 0},
    {"type_decl", "unsigned_decl", CASE_ASSERT_FILE, "test/fixtures/type_decl/unsigned_decl.c", 0, 0},
    {"type_decl", "bool_decl", CASE_ASSERT_FILE, "test/fixtures/type_decl/bool_decl.c", 0, 0},
    {"type_decl", "signed_decl", CASE_ASSERT_FILE, "test/fixtures/type_decl/signed_decl.c", 0, 0},
    {"type_decl", "char_add_eq", CASE_ASSERT_FILE, "test/fixtures/type_decl/char_add_eq.c", 0, 0},
    {"type_decl", "short_mul_eq", CASE_ASSERT_FILE, "test/fixtures/type_decl/short_mul_eq.c", 0, 0},
    {"type_decl", "ptr_deref_add_eq", CASE_ASSERT_FILE, "test/fixtures/type_decl/ptr_deref_add_eq.c", 0, 0},
    {"type_decl", "ptr_ptr_deref", CASE_ASSERT_FILE, "test/fixtures/type_decl/ptr_ptr_deref.c", 0, 0},
    {"type_decl", "sizeof_int", CASE_ASSERT_FILE, "test/fixtures/type_decl/sizeof_int.c", 0, 0},
    {"type_decl", "sizeof_bool", CASE_ASSERT_FILE, "test/fixtures/type_decl/sizeof_bool.c", 0, 0},
    {"type_decl", "sizeof_int_ptr", CASE_ASSERT_FILE, "test/fixtures/type_decl/sizeof_int_ptr.c", 0, 0},
    {"type_decl", "sizeof_int_ptr_const", CASE_ASSERT_FILE, "test/fixtures/type_decl/sizeof_int_ptr_const.c", 0, 0},
    {"type_decl", "sizeof_int_ptr_volatile", CASE_ASSERT_FILE, "test/fixtures/type_decl/sizeof_int_ptr_volatile.c", 0, 0},
    {"type_decl", "sizeof_int_ptr_restrict", CASE_ASSERT_FILE, "test/fixtures/type_decl/sizeof_int_ptr_restrict.c", 0, 0},
    {"type_decl", "sizeof_int_array_type", CASE_ASSERT_FILE, "test/fixtures/type_decl/sizeof_int_array_type.c", 0, 0},
    {"type_decl", "sizeof_ptr_to_array_type", CASE_ASSERT_FILE, "test/fixtures/type_decl/sizeof_ptr_to_array_type.c", 0, 0},
    {"type_decl", "sizeof_parenthesized_array_type", CASE_ASSERT_FILE, "test/fixtures/type_decl/sizeof_parenthesized_array_type.c", 0, 0},
    {"type_decl", "sizeof_funcptr_type", CASE_ASSERT_FILE, "test/fixtures/type_decl/sizeof_funcptr_type.c", 0, 0},
    {"type_decl", "alignof_int", CASE_ASSERT_FILE, "test/fixtures/type_decl/alignof_int.c", 0, 0},
    {"type_decl", "alignof_ptr", CASE_ASSERT_FILE, "test/fixtures/type_decl/alignof_ptr.c", 0, 0},
    {"type_decl", "alignof_ptr_const", CASE_ASSERT_FILE, "test/fixtures/type_decl/alignof_ptr_const.c", 0, 0},
    {"type_decl", "alignof_ptr_volatile", CASE_ASSERT_FILE, "test/fixtures/type_decl/alignof_ptr_volatile.c", 0, 0},
    {"type_decl", "alignof_ptr_restrict", CASE_ASSERT_FILE, "test/fixtures/type_decl/alignof_ptr_restrict.c", 0, 0},
    {"type_decl", "alignof_int_array_type", CASE_ASSERT_FILE, "test/fixtures/type_decl/alignof_int_array_type.c", 0, 0},
    {"type_decl", "alignof_ptr_to_array_type", CASE_ASSERT_FILE, "test/fixtures/type_decl/alignof_ptr_to_array_type.c", 0, 0},
    {"type_decl", "alignof_parenthesized_array_type", CASE_ASSERT_FILE, "test/fixtures/type_decl/alignof_parenthesized_array_type.c", 0, 0},
    {"type_decl", "sizeof_expr_var", CASE_ASSERT_FILE, "test/fixtures/type_decl/sizeof_expr_var.c", 0, 0},
    {"type_decl", "sizeof_struct_type", CASE_ASSERT_FILE, "test/fixtures/type_decl/sizeof_struct_type.c", 0, 0},
    {"type_decl", "alignof_struct_type", CASE_ASSERT_FILE, "test/fixtures/type_decl/alignof_struct_type.c", 0, 0},
    {"type_decl", "sizeof_struct_ptr_to_array_type", CASE_ASSERT_FILE, "test/fixtures/type_decl/sizeof_struct_ptr_to_array_type.c", 0, 0},
    {"type_decl", "sizeof_struct_array_type", CASE_ASSERT_FILE, "test/fixtures/type_decl/sizeof_struct_array_type.c", 0, 0},
    {"type_decl", "sizeof_typedef_ptr_to_array_type", CASE_ASSERT_FILE, "test/fixtures/type_decl/sizeof_typedef_ptr_to_array_type.c", 0, 0},
    {"type_decl", "sizeof_typedef_array_type_2d", CASE_ASSERT_FILE, "test/fixtures/type_decl/sizeof_typedef_array_type_2d.c", 0, 0},
    {"type_decl", "cast_chain", CASE_ASSERT_FILE, "test/fixtures/type_decl/cast_chain.c", 0, 0},
    {"type_decl", "cast_double_to_int", CASE_ASSERT_FILE, "test/fixtures/type_decl/cast_double_to_int.c", 0, 0},
    {"type_decl", "cast_func_double_to_int", CASE_ASSERT_FILE, "test/fixtures/type_decl/cast_func_double_to_int.c", 0, 0},
    {"type_decl", "double_param_int_param_mix", CASE_ASSERT_FILE, "test/fixtures/type_decl/double_param_int_param_mix.c", 0, 0},
    {"type_decl", "void_ptr_roundtrip", CASE_ASSERT_FILE, "test/fixtures/type_decl/void_ptr_roundtrip.c", 0, 0},
    {"type_decl", "comma_expr_init", CASE_ASSERT_FILE, "test/fixtures/type_decl/comma_expr_init.c", 0, 0},
    {"type_decl", "comma_sideeffect", CASE_ASSERT_FILE, "test/fixtures/type_decl/comma_sideeffect.c", 0, 0},
    {"type_decl", "comma_assign_chain", CASE_ASSERT_FILE, "test/fixtures/type_decl/comma_assign_chain.c", 0, 0},
    {"type_decl", "unsigned_wrap", CASE_ASSERT_FILE, "test/fixtures/type_decl/unsigned_wrap.c", 0, 0},
    {"type_decl", "signed_char_neg", CASE_ASSERT_FILE, "test/fixtures/type_decl/signed_char_neg.c", 0, 0},
    {"type_decl", "bitwise_swap_nibble", CASE_ASSERT_FILE, "test/fixtures/type_decl/bitwise_swap_nibble.c", 0, 0},
    {"type_decl", "bitwise_mask_or", CASE_ASSERT_FILE, "test/fixtures/type_decl/bitwise_mask_or.c", 0, 0},
    {"type_decl", "struct_copy_independent", CASE_ASSERT_FILE, "test/fixtures/type_decl/struct_copy_independent.c", 0, 0},
    {"type_decl", "struct_return_value", CASE_ASSERT_FILE, "test/fixtures/type_decl/struct_return_value.c", 0, 0},
    {"type_decl", "struct_ptr_arrow", CASE_ASSERT_FILE, "test/fixtures/type_decl/struct_ptr_arrow.c", 0, 0},
    {"type_decl", "global_shadow_local", CASE_ASSERT_FILE, "test/fixtures/type_decl/global_shadow_local.c", 0, 0},
    {"type_decl", "cast_int", CASE_ASSERT_FILE, "test/fixtures/type_decl/cast_int.c", 0, 0},
    {"type_decl", "cast_char_wrap", CASE_ASSERT_FILE, "test/fixtures/type_decl/cast_char_wrap.c", 0, 0},
    {"type_decl", "cast_short_wrap", CASE_ASSERT_FILE, "test/fixtures/type_decl/cast_short_wrap.c", 0, 0},
    {"type_decl", "cast_bool_true", CASE_ASSERT_FILE, "test/fixtures/type_decl/cast_bool_true.c", 0, 0},
    {"type_decl", "cast_bool_false", CASE_ASSERT_FILE, "test/fixtures/type_decl/cast_bool_false.c", 0, 0},
    {"type_decl", "cast_unsigned", CASE_ASSERT_FILE, "test/fixtures/type_decl/cast_unsigned.c", 0, 0},
    {"type_decl", "cast_enum", CASE_ASSERT_FILE, "test/fixtures/type_decl/cast_enum.c", 0, 0},
    {"type_decl", "cast_tag_ptr", CASE_ASSERT_FILE, "test/fixtures/type_decl/cast_tag_ptr.c", 0, 0},
    {"type_decl", "cast_struct_from_scalar", CASE_ASSERT_FILE, "test/fixtures/type_decl/cast_struct_from_scalar.c", 0, 0},
    {"type_decl", "cast_struct_from_pointer_postfix", CASE_ASSERT_FILE, "test/fixtures/type_decl/cast_struct_from_pointer_postfix.c", 0, 0},
    {"type_decl", "cast_struct_same_type", CASE_ASSERT_FILE, "test/fixtures/type_decl/cast_struct_same_type.c", 0, 0},
    {"type_decl", "cast_struct_diff_tag_same_size", CASE_ASSERT_FILE, "test/fixtures/type_decl/cast_struct_diff_tag_same_size.c", 0, 0},
    {"type_decl", "cast_union_same_type", CASE_ASSERT_FILE, "test/fixtures/type_decl/cast_union_same_type.c", 0, 0},
    {"type_decl", "cast_union_diff_tag_same_size", CASE_ASSERT_FILE, "test/fixtures/type_decl/cast_union_diff_tag_same_size.c", 0, 0},
    {"type_decl", "cast_union_from_scalar", CASE_ASSERT_FILE, "test/fixtures/type_decl/cast_union_from_scalar.c", 0, 0},
    {"type_decl", "cast_union_from_pointer_postfix", CASE_ASSERT_FILE, "test/fixtures/type_decl/cast_union_from_pointer_postfix.c", 0, 0},
    {"type_decl", "cast_union_ptr_arrow_chain", CASE_ASSERT_FILE, "test/fixtures/type_decl/cast_union_ptr_arrow_chain.c", 0, 0},
    {"type_decl", "cast_union_ptr_arrow_index", CASE_ASSERT_FILE, "test/fixtures/type_decl/cast_union_ptr_arrow_index.c", 0, 0},
    {"type_decl", "cast_union_ptr_arrow_post_inc", CASE_ASSERT_FILE, "test/fixtures/type_decl/cast_union_ptr_arrow_post_inc.c", 0, 0},
    {"type_decl", "cast_union_ptr_arrow_post_dec", CASE_ASSERT_FILE, "test/fixtures/type_decl/cast_union_ptr_arrow_post_dec.c", 0, 0},
    {"type_decl", "cast_atomic_int", CASE_ASSERT_FILE, "test/fixtures/type_decl/cast_atomic_int.c", 0, 0},
    {"type_decl", "member_dot", CASE_ASSERT_FILE, "test/fixtures/type_decl/member_dot.c", 0, 0},
    {"type_decl", "member_arrow", CASE_ASSERT_FILE, "test/fixtures/type_decl/member_arrow.c", 0, 0},
    {"type_decl", "member_funcptr", CASE_ASSERT_FILE, "test/fixtures/type_decl/member_funcptr.c", 0, 0},
    {"type_decl", "member_union", CASE_ASSERT_FILE, "test/fixtures/type_decl/member_union.c", 0, 0},
    {"type_decl", "union_brace_init_value", CASE_ASSERT_FILE, "test/fixtures/type_decl/union_brace_init_value.c", 0, 0},
    {"type_decl", "union_brace_init_designated", CASE_ASSERT_FILE, "test/fixtures/type_decl/union_brace_init_designated.c", 0, 0},
    {"type_decl", "union_brace_init_multi_designated", CASE_ASSERT_FILE, "test/fixtures/type_decl/union_brace_init_multi_designated.c", 0, 0},
    {"type_decl", "union_array_member_nonbrace_init_values", CASE_ASSERT_FILE, "test/fixtures/type_decl/union_array_member_nonbrace_init_values.c", 0, 0},
    {"type_decl", "struct_bitfield_decl", CASE_ASSERT_FILE, "test/fixtures/type_decl/struct_bitfield_decl.c", 0, 0},
    {"type_decl", "struct_anonymous_struct_member", CASE_ASSERT_FILE, "test/fixtures/type_decl/struct_anonymous_struct_member.c", 0, 0},
    {"type_decl", "struct_anonymous_union_member", CASE_ASSERT_FILE, "test/fixtures/type_decl/struct_anonymous_union_member.c", 0, 0},
    {"type_decl", "struct_brace_init_parse_only", CASE_ASSERT_FILE, "test/fixtures/type_decl/struct_brace_init_parse_only.c", 0, 0},
    {"type_decl", "struct_brace_init_values", CASE_ASSERT_FILE, "test/fixtures/type_decl/struct_brace_init_values.c", 0, 0},
    {"type_decl", "struct_brace_init_designated", CASE_ASSERT_FILE, "test/fixtures/type_decl/struct_brace_init_designated.c", 0, 0},
    {"type_decl", "struct_partial_init", CASE_ASSERT_FILE, "test/fixtures/type_decl/struct_partial_init.c", 0, 0},
    {"type_decl", "struct_designated_gap", CASE_ASSERT_FILE, "test/fixtures/type_decl/struct_designated_gap.c", 0, 0},
    {"type_decl", "sizeof_funcall_int", CASE_ASSERT_FILE, "test/fixtures/type_decl/sizeof_funcall_int.c", 0, 0},
    {"type_decl", "sizeof_funcall_double", CASE_ASSERT_FILE, "test/fixtures/type_decl/sizeof_funcall_double.c", 0, 0},
    {"type_decl", "sizeof_no_side_effect", CASE_ASSERT_FILE, "test/fixtures/type_decl/sizeof_no_side_effect.c", 0, 0},
    {"type_decl", "struct_brace_elision_array_member", CASE_ASSERT_FILE, "test/fixtures/type_decl/struct_brace_elision_array_member.c", 0, 0},
    {"type_decl", "struct_brace_elision_array_member_copy", CASE_ASSERT_FILE, "test/fixtures/type_decl/struct_brace_elision_array_member_copy.c", 0, 0},
    {"type_decl", "struct_brace_elision_array_member_string", CASE_ASSERT_FILE, "test/fixtures/type_decl/struct_brace_elision_array_member_string.c", 0, 0},
    {"type_decl", "struct_nested_desig_single", CASE_ASSERT_FILE, "test/fixtures/type_decl/struct_nested_desig_single.c", 0, 0},
    {"type_decl", "struct_nested_desig_multi", CASE_ASSERT_FILE, "test/fixtures/type_decl/struct_nested_desig_multi.c", 0, 0},
    {"type_decl", "union_nested_desig", CASE_ASSERT_FILE, "test/fixtures/type_decl/union_nested_desig.c", 0, 0},
    {"type_decl", "struct_single_expr_copy_comma", CASE_ASSERT_FILE, "test/fixtures/type_decl/struct_single_expr_copy_comma.c", 0, 0},
    {"type_decl", "struct_single_expr_copy_ternary", CASE_ASSERT_FILE, "test/fixtures/type_decl/struct_single_expr_copy_ternary.c", 0, 0},
    {"type_decl", "union_single_expr_copy_comma", CASE_ASSERT_FILE, "test/fixtures/type_decl/union_single_expr_copy_comma.c", 0, 0},
    {"type_decl", "struct_padding_array", CASE_ASSERT_FILE, "test/fixtures/type_decl/struct_padding_array.c", 0, 0},
    {"type_decl", "typedef_int", CASE_ASSERT_FILE, "test/fixtures/type_decl/typedef_int.c", 0, 0},
    {"type_decl", "typedef_struct_forward_tag", CASE_ASSERT_FILE, "test/fixtures/type_decl/typedef_struct_forward_tag.c", 0, 0},
    {"type_decl", "typedef_struct_anon_top", CASE_ASSERT_FILE, "test/fixtures/type_decl/typedef_struct_anon_top.c", 0, 0},
    {"type_decl", "typedef_union_forward_tag", CASE_ASSERT_FILE, "test/fixtures/type_decl/typedef_union_forward_tag.c", 0, 0},
    {"type_decl", "typedef_union_anon_top", CASE_ASSERT_FILE, "test/fixtures/type_decl/typedef_union_anon_top.c", 0, 0},
    {"type_decl", "typedef_ptr", CASE_ASSERT_FILE, "test/fixtures/type_decl/typedef_ptr.c", 0, 0},
    {"type_decl", "typedef_in_func", CASE_ASSERT_FILE, "test/fixtures/type_decl/typedef_in_func.c", 0, 0},
    {"type_decl", "typedef_in_func_incomplete_array", CASE_ASSERT_FILE, "test/fixtures/type_decl/typedef_in_func_incomplete_array.c", 0, 0},
    {"type_decl", "typedef_local_struct_forward_tag", CASE_ASSERT_FILE, "test/fixtures/type_decl/typedef_local_struct_forward_tag.c", 0, 0},
    {"type_decl", "typedef_local_struct_anon", CASE_ASSERT_FILE, "test/fixtures/type_decl/typedef_local_struct_anon.c", 0, 0},
    {"type_decl", "typedef_local_union_forward_tag", CASE_ASSERT_FILE, "test/fixtures/type_decl/typedef_local_union_forward_tag.c", 0, 0},
    {"type_decl", "typedef_local_union_anon", CASE_ASSERT_FILE, "test/fixtures/type_decl/typedef_local_union_anon.c", 0, 0},
    {"type_decl", "typedef_funcptr", CASE_ASSERT_FILE, "test/fixtures/type_decl/typedef_funcptr.c", 0, 0},
    {"type_decl", "typedef_funcptr_nested", CASE_ASSERT_FILE, "test/fixtures/type_decl/typedef_funcptr_nested.c", 0, 0},
    {"type_decl", "typedef_funcptr_array_nested", CASE_ASSERT_FILE, "test/fixtures/type_decl/typedef_funcptr_array_nested.c", 0, 0},
    {"type_decl", "typedef_local_funcptr_nested", CASE_ASSERT_FILE, "test/fixtures/type_decl/typedef_local_funcptr_nested.c", 0, 0},
    {"type_decl", "local_funcptr_nested_decl", CASE_ASSERT_FILE, "test/fixtures/type_decl/local_funcptr_nested_decl.c", 0, 0},
    {"type_decl", "local_funcptr_array_decl", CASE_ASSERT_FILE, "test/fixtures/type_decl/local_funcptr_array_decl.c", 0, 0},
    {"type_decl", "local_ptr_to_2d_array_sizeof", CASE_ASSERT_FILE, "test/fixtures/type_decl/local_ptr_to_2d_array_sizeof.c", 0, 0},
    {"type_decl", "toplevel_funcptr_array_decl", CASE_ASSERT_FILE, "test/fixtures/type_decl/toplevel_funcptr_array_decl.c", 0, 0},
    {"type_decl", "toplevel_nested_funcptr_array_decl_parse_only", CASE_ASSERT_FILE, "test/fixtures/type_decl/toplevel_nested_funcptr_array_decl_parse_only.c", 0, 0},
    {"type_decl", "struct_member_funcptr_array_decl", CASE_ASSERT_FILE, "test/fixtures/type_decl/struct_member_funcptr_array_decl.c", 0, 0},
    {"type_decl", "struct_member_funcptr_array_size", CASE_ASSERT_FILE, "test/fixtures/type_decl/struct_member_funcptr_array_size.c", 0, 0},
    {"type_decl", "typedef_funcptr_param", CASE_ASSERT_FILE, "test/fixtures/type_decl/typedef_funcptr_param.c", 0, 0},
    {"type_decl", "typedef_ret_funcdef", CASE_ASSERT_FILE, "test/fixtures/type_decl/typedef_ret_funcdef.c", 0, 0},
    {"type_decl", "typedef_ret_proto", CASE_ASSERT_FILE, "test/fixtures/type_decl/typedef_ret_proto.c", 0, 0},
    {"type_decl", "typedef_ptr_ret_proto", CASE_ASSERT_FILE, "test/fixtures/type_decl/typedef_ptr_ret_proto.c", 0, 0},
    {"type_decl", "unnamed_param_prototype", CASE_ASSERT_FILE, "test/fixtures/type_decl/unnamed_param_prototype.c", 0, 0},
    {"type_decl", "unsigned_long_ret_funcdef", CASE_ASSERT_FILE, "test/fixtures/type_decl/unsigned_long_ret_funcdef.c", 0, 0},
    {"type_decl", "unsigned_long_decl", CASE_ASSERT_FILE, "test/fixtures/type_decl/unsigned_long_decl.c", 0, 0},
    {"type_decl", "unsigned_long_long_decl", CASE_ASSERT_FILE, "test/fixtures/type_decl/unsigned_long_long_decl.c", 0, 0},
    {"type_decl", "signed_short_decl", CASE_ASSERT_FILE, "test/fixtures/type_decl/signed_short_decl.c", 0, 0},
    {"type_decl", "signed_char_decl", CASE_ASSERT_FILE, "test/fixtures/type_decl/signed_char_decl.c", 0, 0},
    // integer promotion: signed/unsigned 符号拡張 vs zero拡張
    {"type_decl", "char_sign_extend", CASE_ASSERT_FILE, "test/fixtures/type_decl/char_sign_extend.c", 0, 0},
    {"type_decl", "unsigned_char_zero_extend", CASE_ASSERT_FILE, "test/fixtures/type_decl/unsigned_char_zero_extend.c", 0, 0},
    {"type_decl", "short_sign_extend", CASE_ASSERT_FILE, "test/fixtures/type_decl/short_sign_extend.c", 0, 0},
    {"type_decl", "unsigned_short_zero_extend", CASE_ASSERT_FILE, "test/fixtures/type_decl/unsigned_short_zero_extend.c", 0, 0},
    // unsigned演算セマンティクス
    {"type_decl", "unsigned_div", CASE_ASSERT_FILE, "test/fixtures/type_decl/unsigned_div.c", 0, 0},
    {"type_decl", "unsigned_mod", CASE_ASSERT_FILE, "test/fixtures/type_decl/unsigned_mod.c", 0, 0},
    {"type_decl", "unsigned_shr", CASE_ASSERT_FILE, "test/fixtures/type_decl/unsigned_shr.c", 0, 0},
    {"type_decl", "signed_shr_preserve", CASE_ASSERT_FILE, "test/fixtures/type_decl/signed_shr_preserve.c", 0, 0},
    {"type_decl", "unsigned_cmp_lt", CASE_ASSERT_FILE, "test/fixtures/type_decl/unsigned_cmp_lt.c", 0, 0},
    {"type_decl", "unsigned_cmp_le", CASE_ASSERT_FILE, "test/fixtures/type_decl/unsigned_cmp_le.c", 0, 0},
    {"type_decl", "const_decl", CASE_ASSERT_FILE, "test/fixtures/type_decl/const_decl.c", 0, 0},
    {"type_decl", "volatile_decl", CASE_ASSERT_FILE, "test/fixtures/type_decl/volatile_decl.c", 0, 0},
    {"type_decl", "duplicate_qualifiers_decl", CASE_ASSERT_FILE, "test/fixtures/type_decl/duplicate_qualifiers_decl.c", 0, 0},
    {"type_decl", "duplicate_qualifiers_param", CASE_ASSERT_FILE, "test/fixtures/type_decl/duplicate_qualifiers_param.c", 0, 0},
    {"type_decl", "duplicate_postfix_const_cast", CASE_ASSERT_FILE, "test/fixtures/type_decl/duplicate_postfix_const_cast.c", 0, 0},
    {"type_decl", "storage_specs_local", CASE_ASSERT_FILE, "test/fixtures/type_decl/storage_specs_local.c", 0, 0},
    {"type_decl", "scalar_brace_init", CASE_ASSERT_FILE, "test/fixtures/type_decl/scalar_brace_init.c", 0, 0},
    {"type_decl", "long_double_sizeof", CASE_ASSERT_FILE, "test/fixtures/type_decl/long_double_sizeof.c", 0, 0},
    {"type_decl", "complex_sizeof", CASE_ASSERT_FILE, "test/fixtures/type_decl/complex_sizeof.c", 0, 0},
    {"type_decl", "complex_float_sizeof", CASE_ASSERT_FILE, "test/fixtures/type_decl/complex_float_sizeof.c", 0, 0},
    {"type_decl", "complex_init_copy", CASE_ASSERT_FILE, "test/fixtures/type_decl/complex_init_copy.c", 0, 0},
    {"type_decl", "complex_add", CASE_ASSERT_FILE, "test/fixtures/type_decl/complex_add.c", 0, 0},
    {"type_decl", "complex_sub", CASE_ASSERT_FILE, "test/fixtures/type_decl/complex_sub.c", 0, 0},
    {"type_decl", "complex_mul", CASE_ASSERT_FILE, "test/fixtures/type_decl/complex_mul.c", 0, 0},
    {"type_decl", "complex_div", CASE_ASSERT_FILE, "test/fixtures/type_decl/complex_div.c", 0, 0},
    {"type_decl", "complex_ne", CASE_ASSERT_FILE, "test/fixtures/type_decl/complex_ne.c", 0, 0},
    {"type_decl", "extern_inline_funcspec", CASE_ASSERT_FILE, "test/fixtures/type_decl/extern_inline_funcspec.c", 0, 0},
    {"type_decl", "noreturn_spec_parse", CASE_ASSERT_FILE, "test/fixtures/type_decl/noreturn_spec_parse.c", 0, 0},
    {"type_decl", "static_assert_toplevel", CASE_ASSERT_FILE, "test/fixtures/type_decl/static_assert_toplevel.c", 0, 0},
    {"type_decl", "static_assert_typedef_array_sizeof", CASE_ASSERT_FILE, "test/fixtures/type_decl/static_assert_typedef_array_sizeof.c", 0, 0},
    {"type_decl", "typedef_array_1d_local", CASE_ASSERT_FILE, "test/fixtures/type_decl/typedef_array_1d_local.c", 0, 0},
    {"type_decl", "typedef_array_2d_local", CASE_ASSERT_FILE, "test/fixtures/type_decl/typedef_array_2d_local.c", 0, 0},
    {"type_decl", "typedef_array_3d_local", CASE_ASSERT_FILE, "test/fixtures/type_decl/typedef_array_3d_local.c", 0, 0},
    {"type_decl", "typedef_array_4d_local", CASE_ASSERT_FILE, "test/fixtures/type_decl/typedef_array_4d_local.c", 0, 0},
    {"type_decl", "inline_array_addr_cast", CASE_ASSERT_FILE, "test/fixtures/type_decl/inline_array_addr_cast.c", 0, 0},
    {"type_decl", "typedef_array_addr_cast", CASE_ASSERT_FILE, "test/fixtures/type_decl/typedef_array_addr_cast.c", 0, 0},
    {"type_decl", "typedef_array_addr_func_arg", CASE_ASSERT_FILE, "test/fixtures/type_decl/typedef_array_addr_func_arg.c", 0, 0},
    {"type_decl", "typedef_array_user_suffix", CASE_ASSERT_FILE, "test/fixtures/type_decl/typedef_array_user_suffix.c", 0, 0},
    {"type_decl", "typedef_array_ptr_param_3d", CASE_ASSERT_FILE, "test/fixtures/type_decl/typedef_array_ptr_param_3d.c", 0, 0},
    {"type_decl", "typedef_array_sizeof", CASE_ASSERT_FILE, "test/fixtures/type_decl/typedef_array_sizeof.c", 0, 0},
    {"type_decl", "typedef_array_init", CASE_ASSERT_FILE, "test/fixtures/type_decl/typedef_array_init.c", 0, 0},
    {"type_decl", "typedef_array_5d_local", CASE_ASSERT_FILE, "test/fixtures/type_decl/typedef_array_5d_local.c", 0, 0},
    {"type_decl", "inline_array_1d_addr_cast", CASE_ASSERT_FILE, "test/fixtures/type_decl/inline_array_1d_addr_cast.c", 0, 0},
    {"type_decl", "inline_array_2d_addr_cast", CASE_ASSERT_FILE, "test/fixtures/type_decl/inline_array_2d_addr_cast.c", 0, 0},
    {"type_decl", "global_array_1d_init", CASE_ASSERT_FILE, "test/fixtures/type_decl/global_array_1d_init.c", 0, 0},
    {"type_decl", "global_array_2d_init", CASE_ASSERT_FILE, "test/fixtures/type_decl/global_array_2d_init.c", 0, 0},
    {"type_decl", "global_array_3d_init", CASE_ASSERT_FILE, "test/fixtures/type_decl/global_array_3d_init.c", 0, 0},
    {"type_decl", "global_array_partial_init", CASE_ASSERT_FILE, "test/fixtures/type_decl/global_array_partial_init.c", 0, 0},
    {"type_decl", "global_typedef_array_2d_init", CASE_ASSERT_FILE, "test/fixtures/type_decl/global_typedef_array_2d_init.c", 0, 0},
    {"type_decl", "static_assert_stmt", CASE_ASSERT_FILE, "test/fixtures/type_decl/static_assert_stmt.c", 0, 0},
    {"type_decl", "alignas_atomic_prefix", CASE_ASSERT_FILE, "test/fixtures/type_decl/alignas_atomic_prefix.c", 0, 0},
    {"type_decl", "atomic_type_spec", CASE_ASSERT_FILE, "test/fixtures/type_decl/atomic_type_spec.c", 0, 0},
    {"type_decl", "atomic_type_qual_postfix", CASE_ASSERT_FILE, "test/fixtures/type_decl/atomic_type_qual_postfix.c", 0, 0},
    {"type_decl", "atomic_type_qual_postfix_ptr", CASE_ASSERT_FILE, "test/fixtures/type_decl/atomic_type_qual_postfix_ptr.c", 0, 0},
    {"type_decl", "atomic_load_store", CASE_ASSERT_FILE, "test/fixtures/type_decl/atomic_load_store.c", 0, 0},
    {"type_decl", "thread_local_init", CASE_ASSERT_FILE, "test/fixtures/type_decl/thread_local_init.c", 0, 0},
    {"type_decl", "thread_local_aggregate_init", CASE_ASSERT_FILE, "test/fixtures/type_decl/thread_local_aggregate_init.c", 0, 0},
    {"type_decl", "thread_local_store", CASE_ASSERT_FILE, "test/fixtures/type_decl/thread_local_store.c", 0, 0},
    {"type_decl", "thread_local_arith", CASE_ASSERT_FILE, "test/fixtures/type_decl/thread_local_arith.c", 0, 0},
    {"type_decl", "tl_multi_var_expr", CASE_ASSERT_FILE, "test/fixtures/type_decl/tl_multi_var_expr.c", 0, 0},
    {"type_decl", "tl_cross_func", CASE_ASSERT_FILE, "test/fixtures/type_decl/tl_cross_func.c", 0, 0},
    {"type_decl", "tl_in_loop", CASE_ASSERT_FILE, "test/fixtures/type_decl/tl_in_loop.c", 0, 0},
    {"type_decl", "tl_addr_of", CASE_ASSERT_FILE, "test/fixtures/type_decl/tl_addr_of.c", 0, 0},
    {"type_decl", "tl_uninit", CASE_ASSERT_FILE, "test/fixtures/type_decl/tl_uninit.c", 0, 0},
    {"type_decl", "tl_ternary", CASE_ASSERT_FILE, "test/fixtures/type_decl/tl_ternary.c", 0, 0},
    {"type_decl", "tl_switch", CASE_ASSERT_FILE, "test/fixtures/type_decl/tl_switch.c", 0, 0},
    {"type_decl", "tl_recursive", CASE_ASSERT_FILE, "test/fixtures/type_decl/tl_recursive.c", 0, 0},
    {"type_decl", "tl_rmw_chain", CASE_ASSERT_FILE, "test/fixtures/type_decl/tl_rmw_chain.c", 0, 0},
    {"type_decl", "atomic_global", CASE_ASSERT_FILE, "test/fixtures/type_decl/atomic_global.c", 0, 0},
    {"type_decl", "atomic_in_loop", CASE_ASSERT_FILE, "test/fixtures/type_decl/atomic_in_loop.c", 0, 0},
    {"type_decl", "complex_chain_ops", CASE_ASSERT_FILE, "test/fixtures/type_decl/complex_chain_ops.c", 0, 0},
    {"type_decl", "complex_in_loop", CASE_ASSERT_FILE, "test/fixtures/type_decl/complex_in_loop.c", 0, 0},
    {"type_decl", "generic_int", CASE_ASSERT_FILE, "test/fixtures/type_decl/generic_int.c", 0, 0},
    {"type_decl", "generic_double", CASE_ASSERT_FILE, "test/fixtures/type_decl/generic_double.c", 0, 0},
    {"type_decl", "generic_ptr", CASE_ASSERT_FILE, "test/fixtures/type_decl/generic_ptr.c", 0, 0},
    {"type_decl", "generic_assoc_struct_type_parse", CASE_ASSERT_FILE, "test/fixtures/type_decl/generic_assoc_struct_type_parse.c", 0, 0},
    {"type_decl", "generic_assoc_union_type_parse", CASE_ASSERT_FILE, "test/fixtures/type_decl/generic_assoc_union_type_parse.c", 0, 0},
    {"type_decl", "generic_assoc_struct_type_tag_nomatch", CASE_ASSERT_FILE, "test/fixtures/type_decl/generic_assoc_struct_type_tag_nomatch.c", 0, 0},
    {"type_decl", "generic_assoc_array_type_parse", CASE_ASSERT_FILE, "test/fixtures/type_decl/generic_assoc_array_type_parse.c", 0, 0},
    {"type_decl", "generic_assoc_array_of_funcptr_type", CASE_ASSERT_FILE, "test/fixtures/type_decl/generic_assoc_array_of_funcptr_type.c", 0, 0},
    {"type_decl", "generic_assoc_ptr_to_func_returning_ptr_to_array_type", CASE_ASSERT_FILE, "test/fixtures/type_decl/generic_assoc_ptr_to_func_returning_ptr_to_array_type.c", 0, 0},
    {"type_decl", "generic_funcptr_assoc", CASE_ASSERT_FILE, "test/fixtures/type_decl/generic_funcptr_assoc.c", 0, 0},
    {"type_decl", "generic_deref_double_ptr", CASE_ASSERT_FILE, "test/fixtures/type_decl/generic_deref_double_ptr.c", 0, 0},
    {"type_decl", "generic_deref_float_ptr", CASE_ASSERT_FILE, "test/fixtures/type_decl/generic_deref_float_ptr.c", 0, 0},
    {"type_decl", "generic_subscript_double_ptr", CASE_ASSERT_FILE, "test/fixtures/type_decl/generic_subscript_double_ptr.c", 0, 0},
    {"type_decl", "generic_ptr_kind_match", CASE_ASSERT_FILE, "test/fixtures/type_decl/generic_ptr_kind_match.c", 0, 0},
    {"type_decl", "generic_ptr_fp_match", CASE_ASSERT_FILE, "test/fixtures/type_decl/generic_ptr_fp_match.c", 0, 0},
    {"type_decl", "generic_ptr_struct_tag_match", CASE_ASSERT_FILE, "test/fixtures/type_decl/generic_ptr_struct_tag_match.c", 0, 0},
    {"type_decl", "generic_ptr_const_pointee_match", CASE_ASSERT_FILE, "test/fixtures/type_decl/generic_ptr_const_pointee_match.c", 0, 0},
    {"type_decl", "generic_ptr_typedef_const_pointee_match", CASE_ASSERT_FILE, "test/fixtures/type_decl/generic_ptr_typedef_const_pointee_match.c", 0, 0},
    {"type_decl", "generic_ptr_typedef_volatile_pointee_match", CASE_ASSERT_FILE, "test/fixtures/type_decl/generic_ptr_typedef_volatile_pointee_match.c", 0, 0},
    {"type_decl", "generic_ptr_ptr_kind_match", CASE_ASSERT_FILE, "test/fixtures/type_decl/generic_ptr_ptr_kind_match.c", 0, 0},
    {"type_decl", "generic_ptr_unsigned_match", CASE_ASSERT_FILE, "test/fixtures/type_decl/generic_ptr_unsigned_match.c", 0, 0},
    {"type_decl", "generic_ptr_typedef_unsigned_match", CASE_ASSERT_FILE, "test/fixtures/type_decl/generic_ptr_typedef_unsigned_match.c", 0, 0},
    {"type_decl", "generic_ptr_level_const_match", CASE_ASSERT_FILE, "test/fixtures/type_decl/generic_ptr_level_const_match.c", 0, 0},
    {"type_decl", "generic_ptr_level_volatile_match", CASE_ASSERT_FILE, "test/fixtures/type_decl/generic_ptr_level_volatile_match.c", 0, 0},
    {"type_decl", "generic_scalar_unsigned_long_match", CASE_ASSERT_FILE, "test/fixtures/type_decl/generic_scalar_unsigned_long_match.c", 0, 0},
    {"type_decl", "generic_scalar_long_signedness_match", CASE_ASSERT_FILE, "test/fixtures/type_decl/generic_scalar_long_signedness_match.c", 0, 0},
    {"type_decl", "generic_scalar_post_const_match", CASE_ASSERT_FILE, "test/fixtures/type_decl/generic_scalar_post_const_match.c", 0, 0},
    {"type_decl", "generic_ptr_post_const_match", CASE_ASSERT_FILE, "test/fixtures/type_decl/generic_ptr_post_const_match.c", 0, 0},
    {"type_decl", "const_param", CASE_ASSERT_FILE, "test/fixtures/type_decl/const_param.c", 0, 0},
    {"type_decl", "compound_literal_int", CASE_ASSERT_FILE, "test/fixtures/type_decl/compound_literal_int.c", 0, 0},
    {"type_decl", "compound_literal_struct_stmt", CASE_ASSERT_FILE, "test/fixtures/type_decl/compound_literal_struct_stmt.c", 0, 0},
    {"type_decl", "compound_literal_struct_member", CASE_ASSERT_FILE, "test/fixtures/type_decl/compound_literal_struct_member.c", 0, 0},
    {"type_decl", "compound_literal_struct_member_lvalue_assign", CASE_ASSERT_FILE, "test/fixtures/type_decl/compound_literal_struct_member_lvalue_assign.c", 0, 0},
    {"type_decl", "compound_literal_struct_addr_arrow", CASE_ASSERT_FILE, "test/fixtures/type_decl/compound_literal_struct_addr_arrow.c", 0, 0},
    {"type_decl", "compound_literal_array_inferred_size", CASE_ASSERT_FILE, "test/fixtures/type_decl/compound_literal_array_inferred_size.c", 0, 0},
    {"type_decl", "compound_literal_array_inferred_size_char", CASE_ASSERT_FILE, "test/fixtures/type_decl/compound_literal_array_inferred_size_char.c", 0, 0},
    {"type_decl", "compound_literal_array_inferred_size_designated", CASE_ASSERT_FILE, "test/fixtures/type_decl/compound_literal_array_inferred_size_designated.c", 0, 0},
    {"type_decl", "compound_literal_char_array_brace_string", CASE_ASSERT_FILE, "test/fixtures/type_decl/compound_literal_char_array_brace_string.c", 0, 0},
    {"type_decl", "compound_literal_char_array_brace_string_explicit", CASE_ASSERT_FILE, "test/fixtures/type_decl/compound_literal_char_array_brace_string_explicit.c", 0, 0},
    {"type_decl", "compound_literal_array_subscript", CASE_ASSERT_FILE, "test/fixtures/type_decl/compound_literal_array_subscript.c", 0, 0},
    {"type_decl", "compound_literal_array_subscript0", CASE_ASSERT_FILE, "test/fixtures/type_decl/compound_literal_array_subscript0.c", 0, 0},
    {"type_decl", "compound_literal_array_subscript2", CASE_ASSERT_FILE, "test/fixtures/type_decl/compound_literal_array_subscript2.c", 0, 0},
    // 外側括弧なし: unary() 内で直接 apply_postfix(ref) を呼ぶパス
    {"type_decl", "compound_literal_array_subscript_direct", CASE_ASSERT_FILE, "test/fixtures/type_decl/compound_literal_array_subscript_direct.c", 0, 0},
    {"type_decl", "sizeof_array_of_funcptr_type", CASE_ASSERT_FILE, "test/fixtures/type_decl/sizeof_array_of_funcptr_type.c", 0, 0},
    {"type_decl", "sizeof_array_of_ptr_to_array_type", CASE_ASSERT_FILE, "test/fixtures/type_decl/sizeof_array_of_ptr_to_array_type.c", 0, 0},
    {"type_decl", "sizeof_array_of_ptr_to_array_of_ptr_type", CASE_ASSERT_FILE, "test/fixtures/type_decl/sizeof_array_of_ptr_to_array_of_ptr_type.c", 0, 0},
    {"type_decl", "sizeof_ptr_to_func_returning_ptr_to_array_type", CASE_ASSERT_FILE, "test/fixtures/type_decl/sizeof_ptr_to_func_returning_ptr_to_array_type.c", 0, 0},
    {"type_decl", "sizeof_array_of_ptr_to_func_returning_ptr_to_array_type", CASE_ASSERT_FILE, "test/fixtures/type_decl/sizeof_array_of_ptr_to_func_returning_ptr_to_array_type.c", 0, 0},
    {"type_decl", "sizeof_ptr_to_func_returning_ptr_to_func_type", CASE_ASSERT_FILE, "test/fixtures/type_decl/sizeof_ptr_to_func_returning_ptr_to_func_type.c", 0, 0},
    {"type_decl", "sizeof_ptr_to_func_returning_ptr_to_func_returning_ptr_to_array_type", CASE_ASSERT_FILE, "test/fixtures/type_decl/sizeof_ptr_to_func_returning_ptr_to_func_returning_ptr_to_array_type.c", 0, 0},
    // designator 初期化子との組み合わせ
    {"type_decl", "compound_literal_array_subscript_designator", CASE_ASSERT_FILE, "test/fixtures/type_decl/compound_literal_array_subscript_designator.c", 0, 0},
    // 式中での複数利用
    {"type_decl", "compound_literal_array_subscript_expr", CASE_ASSERT_FILE, "test/fixtures/type_decl/compound_literal_array_subscript_expr.c", 0, 0},
    // ファイルスコープ複合リテラル（静的ストレージ期間）
    {"type_decl", "compound_literal_file_scope", CASE_ASSERT_FILE, "test/fixtures/type_decl/compound_literal_file_scope.c", 0, 0},
    {"type_decl", "float1", CASE_ASSERT_FILE, "test/fixtures/type_decl/float1.c", 0, 0},
    {"type_decl", "float2", CASE_ASSERT_FILE, "test/fixtures/type_decl/float2.c", 0, 0},
    {"type_decl", "float3", CASE_ASSERT_FILE, "test/fixtures/type_decl/float3.c", 0, 0},
    {"type_decl", "float4", CASE_ASSERT_FILE, "test/fixtures/type_decl/float4.c", 0, 0},
    {"type_decl", "float5", CASE_ASSERT_FILE, "test/fixtures/type_decl/float5.c", 0, 0},
    {"type_decl", "double1", CASE_ASSERT_FILE, "test/fixtures/type_decl/double1.c", 0, 0},
    {"type_decl", "double2", CASE_ASSERT_FILE, "test/fixtures/type_decl/double2.c", 0, 0},
    {"type_decl", "double3", CASE_ASSERT_FILE, "test/fixtures/type_decl/double3.c", 0, 0},
    {"type_decl", "double4", CASE_ASSERT_FILE, "test/fixtures/type_decl/double4.c", 0, 0},
    // hex float literals (C11 6.4.4.2)
    {"type_decl", "hex_float_double", CASE_ASSERT_FILE, "test/fixtures/type_decl/hex_float_double.c", 0, 0},
    {"type_decl", "hex_float_no_sign", CASE_ASSERT_FILE, "test/fixtures/type_decl/hex_float_no_sign.c", 0, 0},
    {"type_decl", "hex_float_neg_exp", CASE_ASSERT_FILE, "test/fixtures/type_decl/hex_float_neg_exp.c", 0, 0},
    {"type_decl", "hex_float_suffix_f", CASE_ASSERT_FILE, "test/fixtures/type_decl/hex_float_suffix_f.c", 0, 0},
    {"type_decl", "global_ptr_addr_init", CASE_ASSERT_FILE, "test/fixtures/type_decl/global_ptr_addr_init.c", 0, 0},
    {"type_decl", "global_ptr_addr_write", CASE_ASSERT_FILE, "test/fixtures/type_decl/global_ptr_addr_write.c", 0, 0},

    {"pointer", "deref", CASE_ASSERT_FILE, "test/fixtures/pointer/deref.c", 0, 0},
    {"pointer", "assign", CASE_ASSERT_FILE, "test/fixtures/pointer/assign.c", 0, 0},
    {"pointer", "arith_add", CASE_ASSERT_FILE, "test/fixtures/pointer/arith_add.c", 0, 0},
    {"pointer", "arith_sub", CASE_ASSERT_FILE, "test/fixtures/pointer/arith_sub.c", 0, 0},
    {"pointer", "ptr_subtract", CASE_ASSERT_FILE, "test/fixtures/pointer/ptr_subtract.c", 0, 0},
    {"pointer", "array_decay_diff", CASE_ASSERT_FILE, "test/fixtures/pointer/array_decay_diff.c", 0, 0},
    {"pointer", "comma_postfix", CASE_ASSERT_FILE, "test/fixtures/pointer/comma_postfix.c", 0, 0},
    {"pointer", "global_funcptr_array", CASE_ASSERT_FILE, "test/fixtures/pointer/global_funcptr_array.c", 0, 0},
    {"pointer", "arith_char", CASE_ASSERT_FILE, "test/fixtures/pointer/arith_char.c", 0, 0},
    {"pointer", "triple_deref", CASE_ASSERT_FILE, "test/fixtures/pointer/triple_deref.c", 0, 0},
    {"pointer", "write_via_pp", CASE_ASSERT_FILE, "test/fixtures/pointer/write_via_pp.c", 0, 0},
    {"pointer", "retarget_via_pp", CASE_ASSERT_FILE, "test/fixtures/pointer/retarget_via_pp.c", 0, 0},
    {"pointer", "swap_via_pp", CASE_ASSERT_FILE, "test/fixtures/pointer/swap_via_pp.c", 0, 0},
    {"pointer", "pp_cmp", CASE_ASSERT_FILE, "test/fixtures/pointer/pp_cmp.c", 0, 0},
    {"pointer", "arith_relative", CASE_ASSERT_FILE, "test/fixtures/pointer/arith_relative.c", 0, 0},
    {"pointer", "char_pp_deref", CASE_ASSERT_FILE, "test/fixtures/pointer/char_pp_deref.c", 0, 0},
    {"pointer", "triple_write", CASE_ASSERT_FILE, "test/fixtures/pointer/triple_write.c", 0, 0},
    {"pointer", "pp_inc_deref", CASE_ASSERT_FILE, "test/fixtures/pointer/pp_inc_deref.c", 0, 0},
    {"pointer", "inc_via_pp_func", CASE_ASSERT_FILE, "test/fixtures/pointer/inc_via_pp_func.c", 0, 0},
    {"pointer", "pp_arith_scale", CASE_ASSERT_FILE, "test/fixtures/pointer/pp_arith_scale.c", 0, 0},
    {"pointer", "pp_deref_add", CASE_ASSERT_FILE, "test/fixtures/pointer/pp_deref_add.c", 0, 0},
    {"pointer", "pp_subscript", CASE_ASSERT_FILE, "test/fixtures/pointer/pp_subscript.c", 0, 0},
    {"pointer", "ptr_array", CASE_ASSERT_FILE, "test/fixtures/pointer/ptr_array.c", 0, 0},
    {"pointer", "ptr_array_write", CASE_ASSERT_FILE, "test/fixtures/pointer/ptr_array_write.c", 0, 0},
    {"pointer", "struct_ptr_param_paren", CASE_ASSERT_FILE, "test/fixtures/pointer/struct_ptr_param_paren.c", 0, 0},
    {"pointer", "array_ptr_2d", CASE_ASSERT_FILE, "test/fixtures/pointer/array_ptr_2d.c", 0, 0},
    {"pointer", "array_ptr_2d_first", CASE_ASSERT_FILE, "test/fixtures/pointer/array_ptr_2d_first.c", 0, 0},
    {"pointer", "param_int_ptr_subscript", CASE_ASSERT_FILE, "test/fixtures/pointer/param_int_ptr_subscript.c", 0, 0},
    {"pointer", "param_char_ptr_subscript", CASE_ASSERT_FILE, "test/fixtures/pointer/param_char_ptr_subscript.c", 0, 0},
    {"pointer", "param_short_ptr_subscript", CASE_ASSERT_FILE, "test/fixtures/pointer/param_short_ptr_subscript.c", 0, 0},
    {"pointer", "param_int_pp_double_deref", CASE_ASSERT_FILE, "test/fixtures/pointer/param_int_pp_double_deref.c", 0, 0},
    {"pointer", "funcptr_array_assign_and_call", CASE_ASSERT_FILE, "test/fixtures/pointer/funcptr_array_assign_and_call.c", 0, 0},
    {"pointer", "funcptr_array_brace_init", CASE_ASSERT_FILE, "test/fixtures/pointer/funcptr_array_brace_init.c", 0, 0},
    {"pointer", "funcptr_array_typedef_brace_init", CASE_ASSERT_FILE, "test/fixtures/pointer/funcptr_array_typedef_brace_init.c", 0, 0},
    {"pointer", "funcptr_array_inferred_size", CASE_ASSERT_FILE, "test/fixtures/pointer/funcptr_array_inferred_size.c", 0, 0},

    {"array", "idx", CASE_ASSERT_FILE, "test/fixtures/array/idx.c", 0, 0},
    {"array", "brace_init", CASE_ASSERT_FILE, "test/fixtures/array/brace_init.c", 0, 0},
    {"array", "brace_init_designated", CASE_ASSERT_FILE, "test/fixtures/array/brace_init_designated.c", 0, 0},
    {"array", "brace_init_partial_zeroed", CASE_ASSERT_FILE, "test/fixtures/array/brace_init_partial_zeroed.c", 0, 0},
    {"array", "brace_init_designated_gap", CASE_ASSERT_FILE, "test/fixtures/array/brace_init_designated_gap.c", 0, 0},
    {"array", "sizeof_array_div_elem", CASE_ASSERT_FILE, "test/fixtures/array/sizeof_array_div_elem.c", 0, 0},
    {"array", "struct_array_brace_init", CASE_ASSERT_FILE, "test/fixtures/array/struct_array_brace_init.c", 0, 0},
    {"array", "struct_array_brace_partial", CASE_ASSERT_FILE, "test/fixtures/array/struct_array_brace_partial.c", 0, 0},
    {"array", "char_array_string_init", CASE_ASSERT_FILE, "test/fixtures/array/char_array_string_init.c", 0, 0},
    {"array", "char_3d_string_rows", CASE_ASSERT_FILE, "test/fixtures/array/char_3d_string_rows.c", 0, 0},
    {"array", "inferred_size_brace", CASE_ASSERT_FILE, "test/fixtures/array/inferred_size_brace.c", 0, 0},
    {"array", "inferred_size_brace_trailing_comma", CASE_ASSERT_FILE, "test/fixtures/array/inferred_size_trailing_comma.c", 0, 0},
    {"array", "inferred_size_string", CASE_ASSERT_FILE, "test/fixtures/array/inferred_size_string.c", 0, 0},
    {"array", "inferred_size_char_brace", CASE_ASSERT_FILE, "test/fixtures/array/inferred_size_char_brace.c", 0, 0},
    {"array", "inferred_size_string_concat", CASE_ASSERT_FILE, "test/fixtures/array/inferred_size_string_concat.c", 0, 0},
    {"array", "inferred_size_designated", CASE_ASSERT_FILE, "test/fixtures/array/inferred_size_designated.c", 0, 0},
    {"array", "inferred_size_2d_nested", CASE_ASSERT_FILE, "test/fixtures/array/inferred_size_2d_nested.c", 0, 0},
    {"array", "inferred_size_2d_flat", CASE_ASSERT_FILE, "test/fixtures/array/inferred_size_2d_flat.c", 0, 0},
    {"array", "inferred_size_2d_three_rows", CASE_ASSERT_FILE, "test/fixtures/array/inferred_size_2d_three_rows.c", 0, 0},
    {"array", "brace_wrapped_string_init", CASE_ASSERT_FILE, "test/fixtures/array/brace_wrapped_string_init.c", 0, 0},
    {"array", "brace_wrapped_string_explicit_size", CASE_ASSERT_FILE, "test/fixtures/array/brace_wrapped_string_explicit_size.c", 0, 0},
    {"array", "brace_wrapped_string_concat", CASE_ASSERT_FILE, "test/fixtures/array/brace_wrapped_string_concat.c", 0, 0},
    {"array", "three_dim_assign_read", CASE_ASSERT_FILE, "test/fixtures/array/three_dim_assign_read.c", 0, 0},
    {"array", "three_dim_flat_init", CASE_ASSERT_FILE, "test/fixtures/array/three_dim_flat_init.c", 0, 0},
    {"array", "three_dim_nested_init", CASE_ASSERT_FILE, "test/fixtures/array/three_dim_nested_init.c", 0, 0},
    {"array", "three_dim_inferred_outer", CASE_ASSERT_FILE, "test/fixtures/array/three_dim_inferred_outer.c", 0, 0},
    {"array", "param_2d_array_subscript", CASE_ASSERT_FILE, "test/fixtures/array/param_2d_array_subscript.c", 0, 0},
    {"array", "param_2d_array_explicit_outer", CASE_ASSERT_FILE, "test/fixtures/array/param_2d_array_explicit_outer.c", 0, 0},
    {"array", "param_3d_array_subscript", CASE_ASSERT_FILE, "test/fixtures/array/param_3d_array_subscript.c", 0, 0},
    {"array", "four_dim_assign_read", CASE_ASSERT_FILE, "test/fixtures/array/four_dim_assign_read.c", 0, 0},
    {"array", "four_dim_flat_init", CASE_ASSERT_FILE, "test/fixtures/array/four_dim_flat_init.c", 0, 0},
    {"array", "four_dim_nested_init", CASE_ASSERT_FILE, "test/fixtures/array/four_dim_nested_init.c", 0, 0},
    {"array", "four_dim_inferred_outer", CASE_ASSERT_FILE, "test/fixtures/array/four_dim_inferred_outer.c", 0, 0},
    {"array", "five_dim_assign_read", CASE_ASSERT_FILE, "test/fixtures/array/five_dim_assign_read.c", 0, 0},
    {"array", "param_explicit_ptr_to_2d", CASE_ASSERT_FILE, "test/fixtures/array/param_explicit_ptr_to_2d.c", 0, 0},
    {"array", "param_explicit_ptr_to_3d", CASE_ASSERT_FILE, "test/fixtures/array/param_explicit_ptr_to_3d.c", 0, 0},
    {"array", "param_typedef_array_ptr", CASE_ASSERT_FILE, "test/fixtures/array/param_typedef_array_ptr.c", 0, 0},
    {"array", "param_typedef_array_ptr_sum", CASE_ASSERT_FILE, "test/fixtures/array/param_typedef_array_ptr_sum.c", 0, 0},
    {"array", "param_typedef_2d_array_ptr", CASE_ASSERT_FILE, "test/fixtures/array/param_typedef_2d_array_ptr.c", 0, 0},
    {"array", "sum", CASE_ASSERT_FILE, "test/fixtures/array/sum.c", 0, 0},
    {"array", "const_expr_size", CASE_ASSERT_FILE, "test/fixtures/array/const_expr_size.c", 0, 0},
    {"array", "multi_dim_decl", CASE_ASSERT_FILE, "test/fixtures/array/multi_dim_decl.c", 0, 0},
    {"array", "multi_dim_init", CASE_ASSERT_FILE, "test/fixtures/array/multi_dim_init.c", 0, 0},
    {"array", "multi_dim_init_sum", CASE_ASSERT_FILE, "test/fixtures/array/multi_dim_init_sum.c", 0, 0},
    {"array", "loop", CASE_ASSERT_FILE, "test/fixtures/array/loop.c", 0, 0},

    {"string", "deref", CASE_ASSERT_FILE, "test/fixtures/string/deref.c", 0, 0},
    {"string", "index", CASE_ASSERT_FILE, "test/fixtures/string/index.c", 0, 0},
    {"string", "empty", CASE_ASSERT_FILE, "test/fixtures/string/empty.c", 0, 0},
    {"string", "charlit", CASE_ASSERT_FILE, "test/fixtures/string/charlit.c", 0, 0},
    {"string", "newline", CASE_ASSERT_FILE, "test/fixtures/string/newline.c", 0, 0},
    {"string", "nul", CASE_ASSERT_FILE, "test/fixtures/string/nul_char.c", 0, 0},
    {"string", "buf_idx", CASE_ASSERT_FILE, "test/fixtures/string/buf_idx.c", 0, 0},
    {"string", "buf_sum", CASE_ASSERT_FILE, "test/fixtures/string/buf_sum.c", 0, 0},
    {"string", "char_var", CASE_ASSERT_FILE, "test/fixtures/string/char_var.c", 0, 0},
    // ビットフィールド
    {"bitfield", "read",   CASE_ASSERT_FILE, "test/fixtures/bitfield/read.c", 0, 0},
    {"bitfield", "read_b", CASE_ASSERT_FILE, "test/fixtures/bitfield/read_b.c", 0, 0},
    {"bitfield", "write_masked", CASE_ASSERT_FILE, "test/fixtures/bitfield/write_masked.c", 0, 0},
    {"bitfield", "packing", CASE_ASSERT_FILE, "test/fixtures/bitfield/packing.c", 0, 0},
    {"bitfield", "narrow_storage_preserves_neighbor", CASE_ASSERT_FILE, "test/fixtures/bitfield/narrow_storage_preserves_neighbor.c", 0, 0},
    {"bitfield", "signed_neg", CASE_ASSERT_FILE, "test/fixtures/bitfield/signed_neg.c", 0, 0},
    {"bitfield", "unsigned_wrap", CASE_ASSERT_FILE, "test/fixtures/bitfield/unsigned_wrap.c", 0, 0},
    // _Alignas
    {"alignas", "lvar_value",  CASE_ASSERT_FILE, "test/fixtures/alignas/lvar_value.c", 0, 0},
    {"alignas", "lvar_align",  CASE_ASSERT_FILE, "test/fixtures/alignas/lvar_align.c", 0, 0},
    {"alignas", "struct_member", CASE_ASSERT_FILE, "test/fixtures/alignas/struct_member.c", 0, 0},
    {"alignas", "global_var", CASE_ASSERT_FILE, "test/fixtures/alignas/global_var.c", 0, 0},
    {"alignas", "alignas_alignof", CASE_ASSERT_FILE, "test/fixtures/alignas/alignas_alignof.c", 0, 0},
    {"alignas", "type_name", CASE_ASSERT_FILE, "test/fixtures/alignas/type_name.c", 0, 0},
    // フレキシブル配列メンバー
    {"flex_array", "sizeof_flex", CASE_ASSERT_FILE, "test/fixtures/flex_array/sizeof_flex.c", 0, 0},
    {"flex_array", "parse_ok", CASE_ASSERT_FILE, "test/fixtures/flex_array/parse_ok.c", 0, 0},
    {"flex_array", "alloc_and_use", CASE_ASSERT_FILE, "test/fixtures/flex_array/alloc_and_use.c", 0, 0},
    // tokenizer 拡張機能: 文字列接頭辞、UCN、トライグラフ
    {"tokenizer", "wide_string_L", CASE_ASSERT_FILE, "test/fixtures/tokenizer/wide_string_L.c", 0, 0},
    {"tokenizer", "u8_string", CASE_ASSERT_FILE, "test/fixtures/tokenizer/u8_string.c", 0, 0},
    {"tokenizer", "u_string", CASE_ASSERT_FILE, "test/fixtures/tokenizer/u_string.c", 0, 0},
    {"tokenizer", "u32_string", CASE_ASSERT_FILE, "test/fixtures/tokenizer/u32_string.c", 0, 0},
    {"tokenizer", "charlit_L", CASE_ASSERT_FILE, "test/fixtures/tokenizer/charlit_L.c", 0, 0},
    {"tokenizer", "charlit_u", CASE_ASSERT_FILE, "test/fixtures/tokenizer/charlit_u.c", 0, 0},
    {"tokenizer", "string_concat_prefix", CASE_ASSERT_FILE, "test/fixtures/tokenizer/string_concat_prefix.c", 0, 0},
    {"tokenizer", "ucn_string", CASE_ASSERT_FILE, "test/fixtures/tokenizer/ucn_string.c", 0, 0},
    {"tokenizer", "ucn_string_3byte", CASE_ASSERT_FILE, "test/fixtures/tokenizer/ucn_string_3byte.c", 0, 0},
    {"tokenizer", "ucn_string_u16_surrogate", CASE_ASSERT_FILE, "test/fixtures/tokenizer/ucn_string_u16_surrogate.c", 0, 0},
    {"tokenizer", "ucn_string_u16_bmp", CASE_ASSERT_FILE, "test/fixtures/tokenizer/ucn_string_u16_bmp.c", 0, 0},
    {"tokenizer", "ucn_string_u16_mix", CASE_ASSERT_FILE, "test/fixtures/tokenizer/ucn_string_u16_mix.c", 0, 0},
    {"tokenizer", "ucn_string_u32", CASE_ASSERT_FILE, "test/fixtures/tokenizer/ucn_string_u32.c", 0, 0},
    {"tokenizer", "ucn_ident", CASE_ASSERT_FILE, "test/fixtures/tokenizer/ucn_ident.c", 0, 0},
    {"tokenizer", "trigraph_or", CASE_ASSERT_FILE, "test/fixtures/tokenizer/trigraph_or.c", 0, 0},
    {"tokenizer", "trigraph_xor", CASE_ASSERT_FILE, "test/fixtures/tokenizer/trigraph_xor.c", 0, 0},
    // #pragma pack
    {"pragma_pack", "pack1_sizeof", CASE_ASSERT_FILE, "test/fixtures/pragma_pack/pack1_sizeof.c", 0, 0},
    {"pragma_pack", "pack1_offset", CASE_ASSERT_FILE, "test/fixtures/pragma_pack/pack1_offset.c", 0, 0},
    {"pragma_pack", "pack2_sizeof", CASE_ASSERT_FILE, "test/fixtures/pragma_pack/pack2_sizeof.c", 0, 0},
    {"pragma_pack", "pop_restores", CASE_ASSERT_FILE, "test/fixtures/pragma_pack/pop_restores.c", 0, 0},
    {"pragma_pack", "pack_n_no_push", CASE_ASSERT_FILE, "test/fixtures/pragma_pack/pack_n_no_push.c", 0, 0},
    // 標準ヘッダ
    {"stdheader", "stdint_int32", CASE_ASSERT_FILE, "test/fixtures/stdheader/stdint_int32.c", 0, 0},
    {"stdheader", "stdint_uint8", CASE_ASSERT_FILE, "test/fixtures/stdheader/stdint_uint8.c", 0, 0},
    {"stdheader", "stdbool_true", CASE_ASSERT_FILE, "test/fixtures/stdheader/stdbool_true.c", 0, 0},
    {"stdheader", "stdbool_false", CASE_ASSERT_FILE, "test/fixtures/stdheader/stdbool_false.c", 0, 0},
    {"stdheader", "stddef_size_t", CASE_ASSERT_FILE, "test/fixtures/stdheader/stddef_size_t.c", 0, 0},
    {"stdheader", "stddef_null", CASE_ASSERT_FILE, "test/fixtures/stdheader/stddef_null.c", 0, 0},
    {"stdheader", "stddef_wchar_t", CASE_ASSERT_FILE, "test/fixtures/stdheader/stddef_wchar_t.c", 0, 0},
    {"stdheader", "uchar_multibyte_ops", CASE_ASSERT_FILE, "test/fixtures/stdheader/uchar_multibyte_ops.c", 0, 0},
    {"stdheader", "stddef_max_align_t", CASE_ASSERT_FILE, "test/fixtures/stdheader/stddef_max_align_t.c", 0, 0},
    {"stdheader", "limits_int_max", CASE_ASSERT_FILE, "test/fixtures/stdheader/limits_int_max.c", 0, 0},
    {"stdheader", "limits_int_min", CASE_ASSERT_FILE, "test/fixtures/stdheader/limits_int_min.c", 0, 0},
    {"stdheader", "limits_char_bit", CASE_ASSERT_FILE, "test/fixtures/stdheader/limits_char_bit.c", 0, 0},
    {"stdheader", "float_flt_max", CASE_ASSERT_FILE, "test/fixtures/stdheader/float_flt_max.c", 0, 0},
    {"stdheader", "float_dbl_epsilon", CASE_ASSERT_FILE, "test/fixtures/stdheader/float_dbl_epsilon.c", 0, 0},
    {"stdheader", "float_flt_radix", CASE_ASSERT_FILE, "test/fixtures/stdheader/float_flt_radix.c", 0, 0},
    {"stdheader", "string_strlen", CASE_ASSERT_FILE, "test/fixtures/stdheader/string_strlen.c", 0, 0},
    {"stdheader", "string_strcmp", CASE_ASSERT_FILE, "test/fixtures/stdheader/string_strcmp.c", 0, 0},
    {"stdheader", "string_memmove_overlap", CASE_ASSERT_FILE, "test/fixtures/stdheader/string_memmove_overlap.c", 0, 0},
    {"stdheader", "string_search_concat", CASE_ASSERT_FILE, "test/fixtures/stdheader/string_search_concat.c", 0, 0},
    {"stdheader", "string_strtok_basic", CASE_ASSERT_FILE, "test/fixtures/stdheader/string_strtok_basic.c", 0, 0},
    {"stdheader", "string_strerror", CASE_ASSERT_FILE, "test/fixtures/stdheader/string_strerror.c", 0, 0},
    {"stdheader", "stdio_snprintf_formats", CASE_ASSERT_FILE, "test/fixtures/stdheader/stdio_snprintf_formats.c", 0, 0},
    {"stdheader", "stdio_sprintf_formats", CASE_ASSERT_FILE, "test/fixtures/stdheader/stdio_sprintf_formats.c", 0, 0},
    {"stdheader", "stdio_getline_decl", CASE_ASSERT_FILE, "test/fixtures/stdheader/stdio_getline_decl.c", 0, 0},
    {"stdheader", "stdlib_malloc_free", CASE_ASSERT_FILE, "test/fixtures/stdheader/stdlib_malloc_free.c", 0, 0},
    {"stdheader", "stdlib_realloc", CASE_ASSERT_FILE, "test/fixtures/stdheader/stdlib_realloc.c", 0, 0},
    {"stdheader", "stdlib_atoi", CASE_ASSERT_FILE, "test/fixtures/stdheader/stdlib_atoi.c", 0, 0},
    {"stdheader", "stdlib_abs", CASE_ASSERT_FILE, "test/fixtures/stdheader/stdlib_abs.c", 0, 0},
    {"stdheader", "stdlib_convert_rand", CASE_ASSERT_FILE, "test/fixtures/stdheader/stdlib_convert_rand.c", 0, 0},
    {"stdheader", "stdlib_strto_int", CASE_ASSERT_FILE, "test/fixtures/stdheader/stdlib_strto_int.c", 0, 0},
    {"stdheader", "stdlib_strto_float", CASE_ASSERT_FILE, "test/fixtures/stdheader/stdlib_strto_float.c", 0, 0},
    {"stdheader", "stdlib_env_system", CASE_ASSERT_FILE, "test/fixtures/stdheader/stdlib_env_system.c", 0, 0},
    {"stdheader", "stdlib_realpath", CASE_ASSERT_FILE, "test/fixtures/stdheader/stdlib_realpath.c", 0, 0},
    {"stdheader", "stdlib_qsort_bsearch", CASE_ASSERT_FILE, "test/fixtures/stdheader/stdlib_qsort_bsearch.c", 0, 0},
    {"stdheader", "stdlib_qsort_struct_bsearch_miss", CASE_ASSERT_FILE, "test/fixtures/stdheader/stdlib_qsort_struct_bsearch_miss.c", 0, 0},
    {"stdheader", "string_memset", CASE_ASSERT_FILE, "test/fixtures/stdheader/string_memset.c", 0, 0},
    {"stdheader", "ctype_isdigit", CASE_ASSERT_FILE, "test/fixtures/stdheader/ctype_isdigit.c", 0, 0},
    {"stdheader", "ctype_isalpha", CASE_ASSERT_FILE, "test/fixtures/stdheader/ctype_isalpha.c", 0, 0},
    {"stdheader", "ctype_toupper", CASE_ASSERT_FILE, "test/fixtures/stdheader/ctype_toupper.c", 0, 0},
    {"stdheader", "ctype_classify_more", CASE_ASSERT_FILE, "test/fixtures/stdheader/ctype_classify_more.c", 0, 0},
    {"stdheader", "wchar_runtime_ops", CASE_ASSERT_FILE, "test/fixtures/stdheader/wchar_runtime_ops.c", 0, 0},
    {"stdheader", "wchar_memory_ops", CASE_ASSERT_FILE, "test/fixtures/stdheader/wchar_memory_ops.c", 0, 0},
    {"stdheader", "wchar_search_concat_ops", CASE_ASSERT_FILE, "test/fixtures/stdheader/wchar_search_concat_ops.c", 0, 0},
    {"stdheader", "wchar_multibyte_ops", CASE_ASSERT_FILE, "test/fixtures/stdheader/wchar_multibyte_ops.c", 0, 0},
    {"stdheader", "wchar_convert_ops", CASE_ASSERT_FILE, "test/fixtures/stdheader/wchar_convert_ops.c", 0, 0},
    {"stdheader", "wchar_time_ops", CASE_ASSERT_FILE, "test/fixtures/stdheader/wchar_time_ops.c", 0, 0},
    {"stdheader", "wctype_runtime_ops", CASE_ASSERT_FILE, "test/fixtures/stdheader/wctype_runtime_ops.c", 0, 0},
    {"stdheader", "math_include", CASE_ASSERT_FILE, "test/fixtures/stdheader/math_include.c", 0, 0},
    {"stdheader", "math_dependency_ops", CASE_ASSERT_FILE, "test/fixtures/stdheader/math_dependency_ops.c", 0, 0},
    {"stdheader", "math_runtime_ops", CASE_ASSERT_FILE, "test/fixtures/stdheader/math_runtime_ops.c", 0, 0},
    {"stdheader", "math_wrapper_only_ops", CASE_ASSERT_FILE, "test/fixtures/stdheader/math_wrapper_only_ops.c", 0, 0},
    {"stdheader", "time_runtime_ops", CASE_ASSERT_FILE, "test/fixtures/stdheader/time_runtime_ops.c", 0, 0},
    {"stdheader", "time_gmtime_ops", CASE_ASSERT_FILE, "test/fixtures/stdheader/time_gmtime_ops.c", 0, 0},
    {"stdheader", "time_localtime_ops", CASE_ASSERT_FILE, "test/fixtures/stdheader/time_localtime_ops.c", 0, 0},
    {"stdheader", "time_text_ops", CASE_ASSERT_FILE, "test/fixtures/stdheader/time_text_ops.c", 0, 0},
    {"stdheader", "time_strftime_ops", CASE_ASSERT_FILE, "test/fixtures/stdheader/time_strftime_ops.c", 0, 0},
    {"stdheader", "time_mktime_ops", CASE_ASSERT_FILE, "test/fixtures/stdheader/time_mktime_ops.c", 0, 0},
    {"stdheader", "signal_runtime_ops", CASE_ASSERT_FILE, "test/fixtures/stdheader/signal_runtime_ops.c", 0, 0},
    {"stdheader", "inttypes_strto_ops", CASE_ASSERT_FILE, "test/fixtures/stdheader/inttypes_strto_ops.c", 0, 0},
    {"stdheader", "fenv_runtime_ops", CASE_ASSERT_FILE, "test/fixtures/stdheader/fenv_runtime_ops.c", 0, 0},
    {"stdheader", "locale_runtime_ops", CASE_ASSERT_FILE, "test/fixtures/stdheader/locale_runtime_ops.c", 0, 0},
    {"stdheader", "tgmath_variant_ops", CASE_ASSERT_FILE, "test/fixtures/stdheader/tgmath_variant_ops.c", 0, 0},
    {"stdheader", "assert_include", CASE_ASSERT_FILE, "test/fixtures/stdheader/assert_include.c", 0, 0},
    {"stdheader", "errno_include", CASE_ASSERT_FILE, "test/fixtures/stdheader/errno_include.c", 0, 0},
    {"stdheader", "signal_include", CASE_ASSERT_FILE, "test/fixtures/stdheader/signal_include.c", 0, 0},
    {"stdheader", "time_include", CASE_ASSERT_FILE, "test/fixtures/stdheader/time_include.c", 0, 0},
    {"stdheader", "setjmp_include", CASE_ASSERT_FILE, "test/fixtures/stdheader/setjmp_include.c", 0, 0},
    {"stdheader", "stdatomic_ops", CASE_ASSERT_FILE, "test/fixtures/stdheader/stdatomic_ops.c", 0, 0},
    {"stdheader", "complex_ops", CASE_ASSERT_FILE, "test/fixtures/stdheader/complex_ops.c", 0, 0},
    // stdarg
    {"stdarg", "va_arg_int", CASE_ASSERT_FILE, "test/fixtures/stdarg/va_arg_int.c", 0, 0},
    {"stdarg", "va_arg_double", CASE_ASSERT_FILE, "test/fixtures/stdarg/va_arg_double.c", 0, 0},
    {"stdarg", "va_arg_mix", CASE_ASSERT_FILE, "test/fixtures/stdarg/va_arg_mix.c", 0, 0},
    {"stdarg", "va_arg_many_int", CASE_ASSERT_FILE, "test/fixtures/stdarg/va_arg_many_int.c", 0, 0},
    {"stdarg", "va_copy", CASE_ASSERT_FILE, "test/fixtures/stdarg/va_copy.c", 0, 0},
    {"stdarg", "va_copy_func", CASE_ASSERT_FILE, "test/fixtures/stdarg/va_copy_func.c", 0, 0},
    {"stdarg", "printf_fp_mix", CASE_ASSERT_FILE, "test/fixtures/stdarg/printf_fp_mix.c", 0, 0},

    // VLA (Variable Length Array)
    {"vla", "basic_elem", CASE_ASSERT_FILE, "test/fixtures/vla/basic_elem.c", 0, 0},
    {"vla", "bound_evaluated_once", CASE_ASSERT_FILE, "test/fixtures/vla/bound_evaluated_once.c", 0, 0},
    {"vla", "loop_fill", CASE_ASSERT_FILE, "test/fixtures/vla/loop_fill.c", 0, 0},
    {"vla", "param_size", CASE_ASSERT_FILE, "test/fixtures/vla/param_size.c", 0, 0},
    {"vla", "sizeof_vla", CASE_ASSERT_FILE, "test/fixtures/vla/sizeof_vla.c", 0, 0},
    {"vla", "typedef_capture", CASE_ASSERT_FILE, "test/fixtures/type_decl/vla_typedef_capture.c", 0, 0},
    // 構造体引数渡し (ARM64 ABI)
    {"struct_arg", "small_sum", CASE_ASSERT_FILE, "test/fixtures/struct_arg/small_sum.c", 0, 0},
    {"struct_arg", "small_member", CASE_ASSERT_FILE, "test/fixtures/struct_arg/small_member.c", 0, 0},
    {"struct_arg", "mid_sum", CASE_ASSERT_FILE, "test/fixtures/struct_arg/mid_sum.c", 0, 0},
    {"struct_arg", "large_sum", CASE_ASSERT_FILE, "test/fixtures/struct_arg/large_sum.c", 0, 0},
    // struct return value (≤8B)
    {"struct_ret", "make_and_sum", CASE_ASSERT_FILE, "test/fixtures/struct_ret/make_and_sum.c", 0, 0},
    {"struct_ret", "return_member", CASE_ASSERT_FILE, "test/fixtures/struct_ret/return_member.c", 0, 0},
    {"struct_ret", "chain_call", CASE_ASSERT_FILE, "test/fixtures/struct_ret/chain_call.c", 0, 0},
    // struct return value (9-16B: x0/x1 pair)
    {"struct_ret", "ret_12b_sum", CASE_ASSERT_FILE, "test/fixtures/struct_ret/ret_12b_sum.c", 0, 0},
    {"struct_ret", "ret_16b_sum", CASE_ASSERT_FILE, "test/fixtures/struct_ret/ret_16b_sum.c", 0, 0},
    {"struct_ret", "ret_12b_member_c", CASE_ASSERT_FILE, "test/fixtures/struct_ret/ret_12b_member_c.c", 0, 0},
    // struct return value (>16B: indirect return via x8)
    {"struct_ret", "ret_20b_indirect", CASE_ASSERT_FILE, "test/fixtures/struct_ret/ret_20b_indirect.c", 0, 0},
    {"struct_ret", "ret_24b_member_f", CASE_ASSERT_FILE, "test/fixtures/struct_ret/ret_24b_member_f.c", 0, 0},
    {"struct_ret", "ret_40b_sum", CASE_ASSERT_FILE, "test/fixtures/struct_ret/ret_40b_sum.c", 0, 0},
    // __func__ 定義済み識別子
    {"func_name", "first_char_main", CASE_ASSERT_FILE, "test/fixtures/func_name/first_char_main.c", 0, 0},
    {"func_name", "first_char_helper", CASE_ASSERT_FILE, "test/fixtures/func_name/first_char_helper.c", 0, 0},
    {"func_name", "each_func_distinct", CASE_ASSERT_FILE, "test/fixtures/func_name/each_func_distinct.c", 0, 0},
    {"func_name", "sizeof_and_terminator", CASE_ASSERT_FILE, "test/fixtures/func_name/sizeof_and_terminator.c", 0, 0},
    // 2D VLA: constant inner dimension
    {"vla_2d", "const_inner_read", CASE_ASSERT_FILE, "test/fixtures/vla_2d/const_inner_read.c", 0, 0},
    {"vla_2d", "const_inner_loop", CASE_ASSERT_FILE, "test/fixtures/vla_2d/const_inner_loop.c", 0, 0},
    // 2D VLA: runtime inner dimension
    {"vla_2d", "runtime_inner_read", CASE_ASSERT_FILE, "test/fixtures/vla_2d/runtime_inner_read.c", 0, 0},
    {"vla_2d", "runtime_inner_loop", CASE_ASSERT_FILE, "test/fixtures/vla_2d/runtime_inner_loop.c", 0, 0},
    // 仮引数 VLA 宣言子: int a[n] → int *a (C11 6.7.6.3p7)
    {"vla_param", "basic_access", CASE_ASSERT_FILE, "test/fixtures/vla_param/basic_access.c", 0, 0},
    {"vla_param", "sizeof_is_ptr", CASE_ASSERT_FILE, "test/fixtures/vla_param/sizeof_is_ptr.c", 0, 0},
    {"vla_param", "write_through", CASE_ASSERT_FILE, "test/fixtures/vla_param/write_through.c", 0, 0},
    {"vla_param", "static_restrict_access", CASE_ASSERT_FILE, "test/fixtures/vla_param/static_restrict_access.c", 0, 0},
    // inline 指定子: 単一翻訳単位では通常関数と同様にコード生成 (C11 6.7.4)
    {"inline_func", "basic_inline", CASE_ASSERT_FILE, "test/fixtures/inline_func/basic_inline.c", 0, 0},
    {"inline_func", "static_inline", CASE_ASSERT_FILE, "test/fixtures/inline_func/static_inline.c", 0, 0},
    {"inline_func", "extern_inline", CASE_ASSERT_FILE, "test/fixtures/inline_func/extern_inline.c", 0, 0},
    {"inline_func", "multi_inline", CASE_ASSERT_FILE, "test/fixtures/inline_func/multi_inline.c", 0, 0},
    {"inline_func", "static_inline_pointer", CASE_ASSERT_FILE, "test/fixtures/inline_func/static_inline_pointer.c", 0, 0},
    // グローバル変数: 暫定定義
    {"global_var", "tentative_rw", CASE_ASSERT_FILE, "test/fixtures/global_var/tentative_rw.c", 0, 0},
    {"global_var", "tentative_multi_func", CASE_ASSERT_FILE, "test/fixtures/global_var/tentative_multi_func.c", 0, 0},
    // グローバル変数: 初期化済み定義
    {"global_var", "initialized", CASE_ASSERT_FILE, "test/fixtures/global_var/initialized.c", 0, 0},
    {"global_var", "initialized_modified", CASE_ASSERT_FILE, "test/fixtures/global_var/initialized_modified.c", 0, 0},
    // ローカルスコープのextern宣言
    {"global_var", "local_extern", CASE_ASSERT_FILE, "test/fixtures/global_var/local_extern.c", 0, 0},
    {"global_var", "array_rw", CASE_ASSERT_FILE, "test/fixtures/global_var/array_rw.c", 0, 0},
    {"global_var", "array_sum", CASE_ASSERT_FILE, "test/fixtures/global_var/array_sum.c", 0, 0},
    {"global_var", "global_struct_init", CASE_ASSERT_FILE, "test/fixtures/global_var/global_struct_init.c", 0, 0},
    {"global_var", "global_struct_assign", CASE_ASSERT_FILE, "test/fixtures/global_var/global_struct_assign.c", 0, 0},
    // 意地悪テスト: 各種エッジケース (fixture 化済み)
    {"evil", "dowhile_break", CASE_ASSERT_FILE, "test/fixtures/evil/dowhile_break.c", 0, 0},
    {"evil", "dowhile_continue", CASE_ASSERT_FILE, "test/fixtures/evil/dowhile_continue.c", 0, 0},
    {"evil", "sizeof_no_eval", CASE_ASSERT_FILE, "test/fixtures/evil/sizeof_no_eval.c", 0, 0},
    {"evil", "nested_struct", CASE_ASSERT_FILE, "test/fixtures/evil/nested_struct.c", 0, 0},
    {"evil", "assign_in_cond", CASE_ASSERT_FILE, "test/fixtures/evil/assign_in_cond.c", 0, 0},
    {"evil", "mutual_recursion", CASE_ASSERT_FILE, "test/fixtures/evil/mutual_recursion.c", 0, 0},
    {"evil", "nested_call", CASE_ASSERT_FILE, "test/fixtures/evil/nested_call.c", 0, 0},
    {"evil", "char_subtract", CASE_ASSERT_FILE, "test/fixtures/evil/char_subtract.c", 0, 0},
    {"evil", "char_overflow", CASE_ASSERT_FILE, "test/fixtures/evil/char_overflow.c", 0, 0},
    {"evil", "struct_array_member", CASE_ASSERT_FILE, "test/fixtures/evil/struct_array_member.c", 0, 0},
    {"evil", "collatz_recursion", CASE_ASSERT_FILE, "test/fixtures/evil/collatz_recursion.c", 0, 0},
    {"evil", "complex_expr_8vars", CASE_ASSERT_FILE, "test/fixtures/evil/complex_expr_8vars.c", 0, 0},
    {"evil", "uchar_wrap", CASE_ASSERT_FILE, "test/fixtures/evil/uchar_wrap.c", 0, 0},
    {"evil", "multi_shift", CASE_ASSERT_FILE, "test/fixtures/evil/multi_shift.c", 0, 0},
    {"evil", "global_sideeffect_seq", CASE_ASSERT_FILE, "test/fixtures/evil/global_sideeffect_seq.c", 0, 0},
    {"evil", "deref_dot_vs_arrow", CASE_ASSERT_FILE, "test/fixtures/evil/deref_dot_vs_arrow.c", 0, 0},
    {"evil", "addr_deref_chain", CASE_ASSERT_FILE, "test/fixtures/evil/addr_deref_chain.c", 0, 0},
    {"evil", "logical_not_zero", CASE_ASSERT_FILE, "test/fixtures/evil/logical_not_zero.c", 0, 0},
    {"evil", "logical_not_nonzero", CASE_ASSERT_FILE, "test/fixtures/evil/logical_not_nonzero.c", 0, 0},
    {"evil", "bitwise_not", CASE_ASSERT_FILE, "test/fixtures/evil/bitwise_not.c", 0, 0},
    {"evil", "cast_uchar_neg", CASE_ASSERT_FILE, "test/fixtures/evil/cast_uchar_neg.c", 0, 0},
    {"evil", "struct_padding_sizeof", CASE_ASSERT_FILE, "test/fixtures/evil/struct_padding_sizeof.c", 0, 0},
    {"evil", "struct_ptr_reassign", CASE_ASSERT_FILE, "test/fixtures/evil/struct_ptr_reassign.c", 0, 0},
    {"evil", "ptr_read_then_clear", CASE_ASSERT_FILE, "test/fixtures/evil/ptr_read_then_clear.c", 0, 0},
    {"evil", "max3_nested", CASE_ASSERT_FILE, "test/fixtures/evil/max3_nested.c", 0, 0},
    {"evil", "nested_for_loops", CASE_ASSERT_FILE, "test/fixtures/evil/nested_for_loops.c", 0, 0},
    {"evil", "while1_break", CASE_ASSERT_FILE, "test/fixtures/evil/while1_break.c", 0, 0},
    {"evil", "null_stmt", CASE_ASSERT_FILE, "test/fixtures/evil/null_stmt.c", 0, 0},
    {"evil", "null_stmt_mixed", CASE_ASSERT_FILE, "test/fixtures/evil/null_stmt_mixed.c", 0, 0},
    {"evil", "anon_enum_assign", CASE_ASSERT_FILE, "test/fixtures/evil/anon_enum_assign.c", 0, 0},
    {"evil", "anon_enum_negative", CASE_ASSERT_FILE, "test/fixtures/evil/anon_enum_negative.c", 0, 0},
    {"evil", "post_const_int", CASE_ASSERT_FILE, "test/fixtures/evil/post_const_int.c", 0, 0},
    {"evil", "post_const_char", CASE_ASSERT_FILE, "test/fixtures/evil/post_const_char.c", 0, 0},
    {"evil", "large_imm_mod", CASE_ASSERT_FILE, "test/fixtures/evil/large_imm_mod.c", 0, 0},
    {"evil", "large_imm_var", CASE_ASSERT_FILE, "test/fixtures/evil/large_imm_var.c", 0, 0},
    {"evil", "block_shadow", CASE_ASSERT_FILE, "test/fixtures/evil/block_shadow.c", 0, 0},
    {"evil", "for_scope_shadow", CASE_ASSERT_FILE, "test/fixtures/evil/for_scope_shadow.c", 0, 0},
    {"evil", "nested_shadow", CASE_ASSERT_FILE, "test/fixtures/evil/nested_shadow.c", 0, 0},
    {"evil", "signed_cmp_neg", CASE_ASSERT_FILE, "test/fixtures/evil/signed_cmp_neg.c", 0, 0},
    {"evil", "signed_cmp_lt", CASE_ASSERT_FILE, "test/fixtures/evil/signed_cmp_lt.c", 0, 0},
    {"evil", "self_ref_struct", CASE_ASSERT_FILE, "test/fixtures/evil/self_ref_struct.c", 0, 0},
    {"evil", "static_assert_sizeof", CASE_ASSERT_FILE, "test/fixtures/evil/static_assert_sizeof.c", 0, 0},
    // overflow / sign boundary tests
    {"evil", "int_max_plus1_wraps", CASE_ASSERT_FILE, "test/fixtures/evil/int_max_plus1_wraps.c", 0, 0},
    {"evil", "uint_max_plus1_zero", CASE_ASSERT_FILE, "test/fixtures/evil/uint_max_plus1_zero.c", 0, 0},
    {"evil", "uint_sub_wrap", CASE_ASSERT_FILE, "test/fixtures/evil/uint_sub_wrap.c", 0, 0},
    {"evil", "uint_mul_wrap", CASE_ASSERT_FILE, "test/fixtures/evil/uint_mul_wrap.c", 0, 0},
    {"evil", "uint_shr_no_signext", CASE_ASSERT_FILE, "test/fixtures/evil/uint_shr_no_signext.c", 0, 0},
    {"evil", "char_127_plus1", CASE_ASSERT_FILE, "test/fixtures/evil/char_127_plus1.c", 0, 0},
    {"evil", "char_neg_to_uint", CASE_ASSERT_FILE, "test/fixtures/evil/char_neg_to_uint.c", 0, 0},
    {"evil", "neg_div_truncate", CASE_ASSERT_FILE, "test/fixtures/evil/neg_div_truncate.c", 0, 0},
    {"evil", "uint_div_large", CASE_ASSERT_FILE, "test/fixtures/evil/uint_div_large.c", 0, 0},
    {"evil", "int_max_inc_wraps", CASE_ASSERT_FILE, "test/fixtures/evil/int_max_inc_wraps.c", 0, 0},
    // NaN / Infinity edge cases
    {"evil", "nan_ne_self", CASE_ASSERT_FILE, "test/fixtures/evil/nan_ne_self.c", 0, 0},
    {"evil", "nan_eq_self_false", CASE_ASSERT_FILE, "test/fixtures/evil/nan_eq_self_false.c", 0, 0},
    {"evil", "nan_lt_false", CASE_ASSERT_FILE, "test/fixtures/evil/nan_lt_false.c", 0, 0},
    {"evil", "nan_gt_false", CASE_ASSERT_FILE, "test/fixtures/evil/nan_gt_false.c", 0, 0},
    {"evil", "nan_ge_false", CASE_ASSERT_FILE, "test/fixtures/evil/nan_ge_false.c", 0, 0},
    {"evil", "inf_positive", CASE_ASSERT_FILE, "test/fixtures/evil/inf_positive.c", 0, 0},
    {"evil", "inf_negative", CASE_ASSERT_FILE, "test/fixtures/evil/inf_negative.c", 0, 0},
    {"evil", "inf_plus_neginf_nan", CASE_ASSERT_FILE, "test/fixtures/evil/inf_plus_neginf_nan.c", 0, 0},

    /* 差分テストで発見したバグの fixture (test/fixtures/probes_found_bugs/)。
     * 各 fixture は ag_c と system cc で同じ exit code を返すことを確認する。 */
    {"probes", "anon_union_member", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/anon_union_member.c", 0, 0},
    {"probes", "bool_normalization", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/bool_normalization.c", 0, 0},
    {"probes", "bool_array_element_normalize", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/bool_array_element_normalize.c", 0, 0},
    {"probes", "bool_struct_array_member_normalize", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/bool_struct_array_member_normalize.c", 0, 0},
    {"probes", "bool_2d_array_normalize", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/bool_2d_array_normalize.c", 0, 0},
    {"probes", "bool_func_return_normalize", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/bool_func_return_normalize.c", 0, 0},
    {"probes", "bool_struct_member", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/bool_struct_member.c", 0, 0},
    {"probes", "bitfield_brace_init", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/bitfield_brace_init.c", 0, 0},
    {"probes", "char_ptr_postinc_deref", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/char_ptr_postinc_deref.c", 0, 0},
    {"probes", "const_struct", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/const_struct.c", 0, 0},
    {"probes", "double_array", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/double_array.c", 0, 0},
    {"probes", "func_returning_funcptr", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/func_returning_funcptr.c", 0, 0},
    {"probes", "funcret_subscript", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/funcret_subscript.c", 0, 0},
    {"probes", "integer_indexes_array", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/integer_indexes_array.c", 0, 0},
    {"probes", "int_plus_pointer", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/int_plus_pointer.c", 0, 0},
    {"probes", "scalar_pointer_member_subscript", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/scalar_pointer_member_subscript.c", 0, 0},
    {"probes", "struct_array_param", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_array_param.c", 0, 0},
    {"probes", "static_local_int_array", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/static_local_int_array.c", 0, 0},
    {"probes", "global_scalar_ptr_array_subscript", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_scalar_ptr_array_subscript.c", 0, 0},
    {"probes", "funcptr_array_param", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/funcptr_array_param.c", 0, 0},
    {"probes", "array_designator_with_struct_designator", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/array_designator_with_struct_designator.c", 0, 0},
    {"probes", "cast_to_struct_pointer", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/cast_to_struct_pointer.c", 0, 0},
    {"probes", "global_double_const_expr_init", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_double_const_expr_init.c", 0, 0},
    {"probes", "funcptr_array_compound_literal", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/funcptr_array_compound_literal.c", 0, 0},
    {"probes", "global_struct_with_array_member", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_struct_with_array_member.c", 0, 0},
    {"probes", "ptr_to_funcptr_direct_deref", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/ptr_to_funcptr_direct_deref.c", 0, 0},
    {"probes", "funcptr_ptrptr_global_param", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/funcptr_ptrptr_global_param.c", 0, 0},
    {"probes", "funcptr_retptr_global_param_struct", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/funcptr_retptr_global_param_struct.c", 0, 0},
    {"probes", "typedef_funcptr_retptr_global_local", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/typedef_funcptr_retptr_global_local.c", 0, 0},
    {"probes", "func_return_funcptr_ptrptr", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/func_return_funcptr_ptrptr.c", 0, 0},
    {"probes", "global_char_array_string_size", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_char_array_string_size.c", 0, 0},
    {"probes", "global_designator", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_designator.c", 0, 0},
    {"probes", "global_const_int_expr_init", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_const_int_expr_init.c", 0, 0},
    {"probes", "global_double_scalar", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_double_scalar.c", 0, 0},
    {"probes", "global_double_array", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_double_array.c", 0, 0},
    {"probes", "designator_nested", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/designator_nested.c", 0, 0},
    {"probes", "struct_partial_init_zerofill", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_partial_init_zerofill.c", 0, 0},
    {"probes", "struct_2d_array_nested_brace", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_2d_array_nested_brace.c", 0, 0},
    {"probes", "char_array_string_partial_zerofill", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/char_array_string_partial_zerofill.c", 0, 0},
    {"probes", "const_pointer_reassign", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/const_pointer_reassign.c", 0, 0},
    {"probes", "sizeof_global_array_inferred_size", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/sizeof_global_array_inferred_size.c", 0, 0},
    {"probes", "global_funcptr_call", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_funcptr_call.c", 0, 0},
    {"probes", "global_str_ptr_array", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_str_ptr_array.c", 0, 0},
    {"probes", "global_string_ptr", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_string_ptr.c", 0, 0},
    {"probes", "global_struct_array", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_struct_array.c", 0, 0},
    {"probes", "global_struct_array_flat_elision", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_struct_array_flat_elision.c", 0, 0},
    {"probes", "global_struct_pointer", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_struct_pointer.c", 0, 0},
    {"probes", "global_struct_with_funcptr", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_struct_with_funcptr.c", 0, 0},
    {"probes", "many_double_params", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/many_double_params.c", 0, 0},
    {"probes", "int_arg_to_double_param", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/int_arg_to_double_param.c", 0, 0},
    {"probes", "many_int_params", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/many_int_params.c", 0, 0},
    {"probes", "negative_global", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/negative_global.c", 0, 0},
    {"probes", "nested_compound_literal", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_compound_literal.c", 0, 0},
    {"probes", "pointer_compound_assign", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/pointer_compound_assign.c", 0, 0},
    {"probes", "ptr_to_array_deref", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/ptr_to_array_deref.c", 0, 0},
    {"probes", "ptr_to_array_p_plus_1", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/ptr_to_array_p_plus_1.c", 0, 0},
    {"probes", "short_postinc", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/short_postinc.c", 0, 0},
    {"probes", "sizeof_arith", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/sizeof_arith.c", 0, 0},
    {"probes", "sizeof_postinc", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/sizeof_postinc.c", 0, 0},
    {"probes", "sizeof_string_literal", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/sizeof_string_literal.c", 0, 0},
    {"probes", "string_escape_in_init", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/string_escape_in_init.c", 0, 0},
    {"probes", "struct_funcptr_array", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_funcptr_array.c", 0, 0},
    {"probes", "struct_init_from_deref", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_init_from_deref.c", 0, 0},
    {"probes", "struct_member_array_ptr", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_member_array_ptr.c", 0, 0},
    {"probes", "struct_of_struct_of_array", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_of_struct_of_array.c", 0, 0},
    {"probes", "struct_ptr_plus_arrow", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_ptr_plus_arrow.c", 0, 0},
    {"probes", "struct_ptr_subscript_member", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_ptr_subscript_member.c", 0, 0},
    {"probes", "struct_ternary_member", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_ternary_member.c", 0, 0},
    {"probes", "struct_typedef_forward", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_typedef_forward.c", 0, 0},
    {"probes", "struct_with_double", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_with_double.c", 0, 0},
    {"probes", "typedef_array_param", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/typedef_array_param.c", 0, 0},
    {"probes", "vla_2d_param", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/vla_2d_param.c", 0, 0},
    {"probes", "cmp_wide_signed_vs_unsigned", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/cmp_wide_signed_vs_unsigned.c", 0, 0},
    {"probes", "cmp_narrow_unsigned_promote", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/cmp_narrow_unsigned_promote.c", 0, 0},
    {"probes", "cmp_same_width_unsigned", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/cmp_same_width_unsigned.c", 0, 0},
    {"probes", "array_nested_designator_2d", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/array_nested_designator_2d.c", 0, 0},
    {"probes", "array_nested_designator_3d", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/array_nested_designator_3d.c", 0, 0},
    {"probes", "array_designator_brace_mix", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/array_designator_brace_mix.c", 0, 0},
    {"probes", "div_wide_signed_by_unsigned", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/div_wide_signed_by_unsigned.c", 0, 0},
    {"probes", "mod_wide_signed_by_unsigned", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/mod_wide_signed_by_unsigned.c", 0, 0},
    {"probes", "int_literal_top_bit_set", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/int_literal_top_bit_set.c", 0, 0},
    {"probes", "compound_assign_index_side_effect", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/compound_assign_index_side_effect.c", 0, 0},
    {"probes", "switch_case_long_label", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/switch_case_long_label.c", 0, 0},
    {"probes", "macro_arg_nested_same_name", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/macro_arg_nested_same_name.c", 0, 0},
    {"probes", "variadic_macro_forward", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/variadic_macro_forward.c", 0, 0},
    {"probes", "cast_int_to_double", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/cast_int_to_double.c", 0, 0},
    {"probes", "return_int_to_double", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/return_int_to_double.c", 0, 0},
    {"probes", "float_inc_dec", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/float_inc_dec.c", 0, 0},
    {"probes", "struct_copy_init_array_member", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_copy_init_array_member.c", 0, 0},
    {"probes", "ternary_pointer_subscript", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/ternary_pointer_subscript.c", 0, 0},
    {"probes", "struct_copy_init_from_global", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_copy_init_from_global.c", 0, 0},
    {"probes", "global_pointer_array_offset_init", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_pointer_array_offset_init.c", 0, 0},
    {"probes", "global_array_designated_out_of_order", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_array_designated_out_of_order.c", 0, 0},
    {"probes", "global_struct_string_ptr_member", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_struct_string_ptr_member.c", 0, 0},
    {"probes", "global_struct_designated_and_fp_member", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_struct_designated_and_fp_member.c", 0, 0},
    {"probes", "bool_compound_assign_normalize", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/bool_compound_assign_normalize.c", 0, 0},
    {"probes", "global_bool_normalize", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_bool_normalize.c", 0, 0},
    {"probes", "anon_struct_union_local", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/anon_struct_union_local.c", 0, 0},
    {"probes", "anon_global_array_member_designator", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/anon_global_array_member_designator.c", 0, 0},
    {"probes", "anon_ptr_to_array_member_designator", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/anon_ptr_to_array_member_designator.c", 0, 0},
    {"probes", "anon_union_promoted_array_designator", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/anon_union_promoted_array_designator.c", 0, 0},
    {"probes", "vla_double_element", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/vla_double_element.c", 0, 0},
    {"probes", "funcall_struct_ptr_arrow", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/funcall_struct_ptr_arrow.c", 0, 0},
    {"probes", "struct_ptr_param_subscript", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_ptr_param_subscript.c", 0, 0},
    {"probes", "struct_ptr_incdec_and_typedef_arrow", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_ptr_incdec_and_typedef_arrow.c", 0, 0},
    {"probes", "array_of_struct_pointers_arrow", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/array_of_struct_pointers_arrow.c", 0, 0},
    {"probes", "struct_ptr_compound_assign_and_double_deref", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_ptr_compound_assign_and_double_deref.c", 0, 0},
    {"probes", "ternary_address_pointer_truncation", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/ternary_address_pointer_truncation.c", 0, 0},
    {"probes", "fp_pointer_parameter", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/fp_pointer_parameter.c", 0, 0},
    {"probes", "funcptr_explicit_deref_call", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/funcptr_explicit_deref_call.c", 0, 0},
    {"probes", "funcptr_address_of_init", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/funcptr_address_of_init.c", 0, 0},
    {"probes", "unsigned_int_overflow_wrap", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/unsigned_int_overflow_wrap.c", 0, 0},
    {"probes", "fp_array_parameter", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/fp_array_parameter.c", 0, 0},
    {"probes", "struct_multidim_array_member", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_multidim_array_member.c", 0, 0},
    {"probes", "struct_pointer_var_size", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_pointer_var_size.c", 0, 0},
    {"probes", "ternary_pointer_null_branch", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/ternary_pointer_null_branch.c", 0, 0},
    {"probes", "ternary_long_branch", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/ternary_long_branch.c", 0, 0},
    {"probes", "long_return_value", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/long_return_value.c", 0, 0},
    {"probes", "long_pointer_param_and_call", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/long_pointer_param_and_call.c", 0, 0},
    {"probes", "scalar_init_from_pointer_subscript", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/scalar_init_from_pointer_subscript.c", 0, 0},
    {"probes", "double_pointer_subscript_deref", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/double_pointer_subscript_deref.c", 0, 0},
    {"probes", "double_pointer_double_subscript", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/double_pointer_double_subscript.c", 0, 0},
    {"probes", "long_bitfield", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/long_bitfield.c", 0, 0},
    {"probes", "duplicate_designator_override", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/duplicate_designator_override.c", 0, 0},
    {"probes", "designator_then_positional", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/designator_then_positional.c", 0, 0},
    {"probes", "nested_struct_brace_elision", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_struct_brace_elision.c", 0, 0},
    {"probes", "struct_array_brace_elision", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_array_brace_elision.c", 0, 0},
    {"probes", "global_nested_struct_init", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_nested_struct_init.c", 0, 0},
    {"probes", "global_designator_nested_slot", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_designator_nested_slot.c", 0, 0},
    {"probes", "nested_ternary_long", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_ternary_long.c", 0, 0},
    {"probes", "compound_literal_struct_arg", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/compound_literal_struct_arg.c", 0, 0},
    {"probes", "struct_value_arg_return", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_value_arg_return.c", 0, 0},
    {"probes", "cast_to_signed_comparison", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/cast_to_signed_comparison.c", 0, 0},
    {"probes", "unsigned_member_global_load", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/unsigned_member_global_load.c", 0, 0},
    {"probes", "unsigned_array_pointer_deref", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/unsigned_array_pointer_deref.c", 0, 0},
    {"probes", "typedef_unsigned_global", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/typedef_unsigned_global.c", 0, 0},
    {"probes", "funcptr_array_member_init", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/funcptr_array_member_init.c", 0, 0},
    {"probes", "struct_ptr_array_member_access", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_ptr_array_member_access.c", 0, 0},
    {"probes", "nested_array_designator", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_array_designator.c", 0, 0},
    {"probes", "cast_subint_to_int_signedness", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/cast_subint_to_int_signedness.c", 0, 0},
    {"probes", "multidim_array_explicit_deref", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/multidim_array_explicit_deref.c", 0, 0},
    {"probes", "bool_initializer_normalization", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/bool_initializer_normalization.c", 0, 0},
    {"probes", "bool_array_member_designator", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/bool_array_member_designator.c", 0, 0},
    {"probes", "struct_pointer_arithmetic", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_pointer_arithmetic.c", 0, 0},
    {"probes", "array_of_struct_member_init", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/array_of_struct_member_init.c", 0, 0},
    {"probes", "struct_subint_by_value", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_subint_by_value.c", 0, 0},
    {"probes", "inline_pointer_cast_deref", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/inline_pointer_cast_deref.c", 0, 0},
    {"probes", "int_cast_truncates_long", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/int_cast_truncates_long.c", 0, 0},
    {"probes", "int_cast_truncates_long_return", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/int_cast_truncates_long_return.c", 0, 0},
    {"probes", "long_cast_unsigned_zero_extend", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/long_cast_unsigned_zero_extend.c", 0, 0},
    {"probes", "long_literal_width", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/long_literal_width.c", 0, 0},
    {"probes", "struct_pointer_to_array", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_pointer_to_array.c", 0, 0},
    {"probes", "local_pointer_to_2d_array", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/local_pointer_to_2d_array.c", 0, 0},
    {"probes", "float_array_member", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/float_array_member.c", 0, 0},
    {"probes", "float_array_member_designator", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/float_array_member_designator.c", 0, 0},
    {"probes", "float_truthiness_condition", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/float_truthiness_condition.c", 0, 0},
    {"probes", "float_logical_operand", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/float_logical_operand.c", 0, 0},
    {"probes", "static_local_float_init", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/static_local_float_init.c", 0, 0},
    {"probes", "multidim_float_array_subscript", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/multidim_float_array_subscript.c", 0, 0},
    {"probes", "alignof_aggregate", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/alignof_aggregate.c", 0, 0},
    {"probes", "generic_string_and_long", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/generic_string_and_long.c", 0, 0},
    {"probes", "cast_short_char_sign_extend", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/cast_short_char_sign_extend.c", 0, 0},
    {"probes", "cast_signed_subint_from_unsigned_expr", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/cast_signed_subint_from_unsigned_expr.c", 0, 0},
    {"probes", "array_row_decay_pointer_arith", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/array_row_decay_pointer_arith.c", 0, 0},
    {"probes", "large_stack_frame", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/large_stack_frame.c", 0, 0},
    {"probes", "array_row_decay_3d_pointer_arith", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/array_row_decay_3d_pointer_arith.c", 0, 0},
    {"probes", "funcptr_fp_return", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/funcptr_fp_return.c", 0, 0},
    {"probes", "funcptr_array_fp_return", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/funcptr_array_fp_return.c", 0, 0},
    {"probes", "funcptr_member_fp_return", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/funcptr_member_fp_return.c", 0, 0},
    {"probes", "funcptr_global_fp_return", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/funcptr_global_fp_return.c", 0, 0},
    {"probes", "global_fp_data_pointer_deref", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_fp_data_pointer_deref.c", 0, 0},
    {"probes", "global_ptr_to_array_subscript", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_ptr_to_array_subscript.c", 0, 0},
    {"probes", "ptr_to_array_deref_fp", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/ptr_to_array_deref_fp.c", 0, 0},
    {"probes", "ptr_to_array_struct_member", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/ptr_to_array_struct_member.c", 0, 0},
    {"probes", "typedef_array_chain", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/typedef_array_chain.c", 0, 0},
    {"probes", "vla_3d", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/vla_3d.c", 0, 0},
    {"probes", "vla_mixed_dims", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/vla_mixed_dims.c", 0, 0},
    {"probes", "vla_4d_and_higher", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/vla_4d_and_higher.c", 0, 0},
    {"probes", "vla_3d4d_param", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/vla_3d4d_param.c", 0, 0},
    {"probes", "vla_struct_local", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/vla_struct_local.c", 0, 0},
    {"probes", "extern_global_got", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/extern_global_got.c", 0, 0},
    {"probes", "struct_pp_param_arrow", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_pp_param_arrow.c", 0, 0},
    {"probes", "ptrptr_deref_subscript_member", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/ptrptr_deref_subscript_member.c", 0, 0},
    {"probes", "file_scope_ptr_from_array_compound", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/file_scope_ptr_from_array_compound.c", 0, 0},
    {"probes", "function_redecl_signature", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/function_redecl_signature.c", 0, 0},
    {"probes", "function_duplicate_def", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/function_duplicate_def.c", 0, 0},
    {"probes", "decl_spec_order_and_dup", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/decl_spec_order_and_dup.c", 0, 0},
    {"probes", "name_namespace_collision", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/name_namespace_collision.c", 0, 0},
    {"probes", "identifier_diagnostics", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/identifier_diagnostics.c", 0, 0},
    {"probes", "tag_redef_and_return", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/tag_redef_and_return.c", 0, 0},
    {"probes", "undefined_behavior_warnings", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/undefined_behavior_warnings.c", 0, 0},
    {"probes", "narrowing_and_self_compare", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/narrowing_and_self_compare.c", 0, 0},
    {"probes", "assign_overflow_dangling", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/assign_overflow_dangling.c", 0, 0},
    {"probes", "comma_in_condition", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/comma_in_condition.c", 0, 0},
    {"probes", "switch_fallthrough", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/switch_fallthrough.c", 0, 0},
    {"probes", "sign_compare", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/sign_compare.c", 0, 0},
    {"probes", "comparison_result_is_signed_int", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/comparison_result_is_signed_int.c", 0, 0},
    {"probes", "bitwise_narrow_unsigned_promote", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/bitwise_narrow_unsigned_promote.c", 0, 0},
    {"probes", "float_to_int_narrowing_extended", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/float_to_int_narrowing_extended.c", 0, 0},
    {"probes", "float_to_int_return_narrowing", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/float_to_int_return_narrowing.c", 0, 0},
    {"probes", "unsigned_fp_conversion", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/unsigned_fp_conversion.c", 0, 0},
    {"probes", "tautological_unsigned_zero", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/tautological_unsigned_zero.c", 0, 0},
    {"probes", "identical_logical_operands", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/identical_logical_operands.c", 0, 0},
    {"probes", "logical_not_paren_trap", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/logical_not_paren_trap.c", 0, 0},
    {"probes", "pointer_integer_compare", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/pointer_integer_compare.c", 0, 0},
    {"probes", "integer_const_overflow", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/integer_const_overflow.c", 0, 0},
    {"probes", "bool_bitfield", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/bool_bitfield.c", 0, 0},
    {"probes", "anon_struct_bitfield_promote", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/anon_struct_bitfield_promote.c", 0, 0},
    {"probes", "struct_pointer_typedef_member", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_pointer_typedef_member.c", 0, 0},
    {"probes", "struct_array_typedef_member", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_array_typedef_member.c", 0, 0},
    {"probes", "static_local_array_param_overlap", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/static_local_array_param_overlap.c", 0, 0},
    {"probes", "static_local_string_pointer", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/static_local_string_pointer.c", 0, 0},
    {"probes", "typedef_array_of_pointers", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/typedef_array_of_pointers.c", 0, 0},
    {"probes", "struct_array_typedef_member_2d", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_array_typedef_member_2d.c", 0, 0},
    {"probes", "struct_addr_cast_subtract", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_addr_cast_subtract.c", 0, 0},
    {"probes", "struct_member_alignment", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_member_alignment.c", 0, 0},
    {"probes", "global_string_offset_init", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_string_offset_init.c", 0, 0},
    {"probes", "global_string_offset_in_array_and_struct", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_string_offset_in_array_and_struct.c", 0, 0},
    {"probes", "global_ptrdiff_init", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_ptrdiff_init.c", 0, 0},
    {"probes", "global_int_from_float_cast", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_int_from_float_cast.c", 0, 0},
    {"probes", "struct_ptr_to_array_member", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_ptr_to_array_member.c", 0, 0},
    {"probes", "struct_array_of_ptr_to_array_member", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_array_of_ptr_to_array_member.c", 0, 0},
    {"probes", "struct_ptr_to_2d_array_member", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_ptr_to_2d_array_member.c", 0, 0},
    {"probes", "global_multidim_array_nested_designator_plain", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_multidim_array_nested_designator_plain.c", 0, 0},
    {"probes", "global_struct_member_multidim_struct_array_designator", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_struct_member_multidim_struct_array_designator.c", 0, 0},
    {"probes", "global_struct_member_multidim_nested_designator", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_struct_member_multidim_nested_designator.c", 0, 0},
    {"probes", "local_struct_member_multidim_nested_designator", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/local_struct_member_multidim_nested_designator.c", 0, 0},
    {"probes", "local_function_prototype", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/local_function_prototype.c", 0, 0},
    {"probes", "sizeof_cast_expression", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/sizeof_cast_expression.c", 0, 0},
    {"probes", "global_ptr_to_multidim_array", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_ptr_to_multidim_array.c", 0, 0},
    {"probes", "funcptr_global_array_fp_return", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/funcptr_global_array_fp_return.c", 0, 0},
    {"probes", "global_size1_funcptr_array", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_size1_funcptr_array.c", 0, 0},
    {"probes", "sizeof_vla_subscript", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/sizeof_vla_subscript.c", 0, 0},
    {"probes", "generic_scalar_cast_control", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/generic_scalar_cast_control.c", 0, 0},
    {"probes", "generic_long_long_binary_result", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/generic_long_long_binary_result.c", 0, 0},
    {"probes", "bitfield_pack_after_member", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/bitfield_pack_after_member.c", 0, 0},
    {"probes", "fp_unary_minus_neg_zero", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/fp_unary_minus_neg_zero.c", 0, 0},
    {"probes", "variadic_unnamed_proto_fixed_args", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/variadic_unnamed_proto_fixed_args.c", 0, 0},
    {"probes", "variadic_unnamed_proto_fp_fixed_arg", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/variadic_unnamed_proto_fp_fixed_arg.c", 0, 0},
    {"probes", "line_macro_in_expansion", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/line_macro_in_expansion.c", 0, 0},
    {"probes", "static_local_struct_persist", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/static_local_struct_persist.c", 0, 0},
    {"probes", "int_cmp_width_and_subint_return", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/int_cmp_width_and_subint_return.c", 0, 0},
    {"probes", "anon_member_fp_unsigned_promote", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/anon_member_fp_unsigned_promote.c", 0, 0},
    {"probes", "global_ptr_array_addr_init", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_ptr_array_addr_init.c", 0, 0},
    {"probes", "global_designator_member_index", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_designator_member_index.c", 0, 0},
    {"probes", "local_designator_aggregate_leaf", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/local_designator_aggregate_leaf.c", 0, 0},
    {"probes", "return_struct_funccall", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/return_struct_funccall.c", 0, 0},
    {"probes", "struct_init_from_ternary_funccall", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_init_from_ternary_funccall.c", 0, 0},
    {"probes", "void_ptr_return", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/void_ptr_return.c", 0, 0},
    {"probes", "typedef_unsigned_subint_return", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/typedef_unsigned_subint_return.c", 0, 0},
    {"probes", "static_tag_global", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/static_tag_global.c", 0, 0},
    {"probes", "sizeof_multiword_int", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/sizeof_multiword_int.c", 0, 0},
    {"probes", "shift_left_operand_type", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/shift_left_operand_type.c", 0, 0},
    {"probes", "typedef_unsigned_struct_member", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/typedef_unsigned_struct_member.c", 0, 0},
    {"probes", "unsigned_char_array_member", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/unsigned_char_array_member.c", 0, 0},
    {"probes", "unsigned_subint_return_promote", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/unsigned_subint_return_promote.c", 0, 0},
    {"probes", "chained_assign_narrow_lvalue", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/chained_assign_narrow_lvalue.c", 0, 0},
    {"probes", "addr_of_array_compound_literal", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/addr_of_array_compound_literal.c", 0, 0},
    {"probes", "struct_array_partial_init", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_array_partial_init.c", 0, 0},
    {"probes", "typedef_array_pointer_stride", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/typedef_array_pointer_stride.c", 0, 0},
    {"probes", "typedef_ptr_to_array", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/typedef_ptr_to_array.c", 0, 0},
    {"probes", "global_pointer_typedef", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_pointer_typedef.c", 0, 0},
    {"probes", "multilevel_pointer_typedef", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/multilevel_pointer_typedef.c", 0, 0},
    {"probes", "global_multilevel_pointer", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_multilevel_pointer.c", 0, 0},
    {"probes", "ptr_array_arith_subscript_deref", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/ptr_array_arith_subscript_deref.c", 0, 0},
    {"probes", "generic_long_vs_longlong", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/generic_long_vs_longlong.c", 0, 0},
    {"probes", "negative_fp_global_init", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/negative_fp_global_init.c", 0, 0},
    {"probes", "pp_if_operators", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/pp_if_operators.c", 0, 0},
    {"probes", "pp_if_short_circuit", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/pp_if_short_circuit.c", 0, 0},
    {"probes", "pp_line_macro_arg", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/pp_line_macro_arg.c", 0, 0},
    {"probes", "pp_predefined_lp64", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/pp_predefined_lp64.c", 0, 0},
    {"probes", "mixed_decl_func_proto_and_var", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/mixed_decl_func_proto_and_var.c", 0, 0},
    {"probes", "func_returning_funcptr_call", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/func_returning_funcptr_call.c", 0, 0},
    {"probes", "func_returning_funcptr_chain", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/func_returning_funcptr_chain.c", 0, 0},
    {"probes", "typedef_label_shadow", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/typedef_label_shadow.c", 0, 0},
    {"probes", "global_incomplete_outer_array_dim", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_incomplete_outer_array_dim.c", 0, 0},
    {"probes", "sizeof_int_literal", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/sizeof_int_literal.c", 0, 0},
    {"probes", "variadic_macro_empty_va", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/variadic_macro_empty_va.c", 0, 0},
    {"probes", "ternary_subint_branch", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/ternary_subint_branch.c", 0, 0},
    {"probes", "ternary_usual_arith_size_signedness", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/ternary_usual_arith_size_signedness.c", 0, 0},
    {"probes", "string_concat_stringize", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/string_concat_stringize.c", 0, 0},
    {"probes", "complex_brace_init", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/complex_brace_init.c", 0, 0},
    {"probes", "cast_voidptr_subscript", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/cast_voidptr_subscript.c", 0, 0},
    {"probes", "int_expr_pointer_cast_deref", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/int_expr_pointer_cast_deref.c", 0, 0},
    {"probes", "void_cast_wrapper_side_effect", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/void_cast_wrapper_side_effect.c", 0, 0},
    {"probes", "pointer_constant_cast_wrapper", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/pointer_constant_cast_wrapper.c", 0, 0},
    {"probes", "fp_pointer_cast_deref", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/fp_pointer_cast_deref.c", 0, 0},
    {"probes", "fp_cast_subscript", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/fp_cast_subscript.c", 0, 0},
    {"probes", "real_imag_operators", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/real_imag_operators.c", 0, 0},
    {"probes", "complex_float_double_convert", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/complex_float_double_convert.c", 0, 0},
    {"probes", "complex_by_value_abi", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/complex_by_value_abi.c", 0, 0},
    {"probes", "bitfield_enum_and_static_init", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/bitfield_enum_and_static_init.c", 0, 0},
    {"probes", "sizeof_enum_type", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/sizeof_enum_type.c", 0, 0},
    {"probes", "sizeof_cast_subint_constant", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/sizeof_cast_subint_constant.c", 0, 0},
    {"probes", "compound_literal_struct_assign", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/compound_literal_struct_assign.c", 0, 0},
    {"probes", "file_scope_compound_literal_init", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/file_scope_compound_literal_init.c", 0, 0},
    {"probes", "file_scope_addr_of_compound_literal", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/file_scope_addr_of_compound_literal.c", 0, 0},
    {"probes", "struct_funcptr_zero_init", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_funcptr_zero_init.c", 0, 0},
    {"probes", "variadic_via_func_pointer", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/variadic_via_func_pointer.c", 0, 0},
    {"probes", "global_variadic_funcptr_call", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_variadic_funcptr_call.c", 0, 0},
    {"probes", "macro_nested_paste_call", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/macro_nested_paste_call.c", 0, 0},
    {"probes", "macro_nested_paste_call_arg", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/macro_nested_paste_call_arg.c", 0, 0},
    {"probes", "macro_paste_empty_operand", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/macro_paste_empty_operand.c", 0, 0},
    {"probes", "incomplete_tag_and_nested_func_param", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/incomplete_tag_and_nested_func_param.c", 0, 0},
    {"probes", "builtin_expect_fold", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/builtin_expect_fold.c", 0, 0},
    {"probes", "sizeof_string_and_addr_of_array", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/sizeof_string_and_addr_of_array.c", 0, 0},
    {"probes", "stringize_string_literal", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/stringize_string_literal.c", 0, 0},
    {"probes", "char_2d_array_string_init", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/char_2d_array_string_init.c", 0, 0},
    {"probes", "empty_macro_argument", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/empty_macro_argument.c", 0, 0},
    {"probes", "generic_struct_vs_scalar", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/generic_struct_vs_scalar.c", 0, 0},
    {"probes", "generic_array_assoc_and_func_designator", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/generic_array_assoc_and_func_designator.c", 0, 0},
    {"probes", "generic_char_and_longlong_identity", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/generic_char_and_longlong_identity.c", 0, 0},
    {"probes", "alignas_overaligned_local", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/alignas_overaligned_local.c", 0, 0},
    {"probes", "vla_2d_param_and_row_sizeof", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/vla_2d_param_and_row_sizeof.c", 0, 0},
    {"probes", "static_internal_linkage", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/static_internal_linkage.c", 0, 0},
    {"probes", "generic_complex_derived_type", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/generic_complex_derived_type.c", 0, 0},
    {"probes", "generic_complex_derived_type_global", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/generic_complex_derived_type_global.c", 0, 0},
    {"probes", "generic_streaming_lookahead", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/generic_streaming_lookahead.c", 0, 0},
    {"probes", "fp_arg_to_int_param", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/fp_arg_to_int_param.c", 0, 0},
    {"probes", "static_local_array_sizeof", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/static_local_array_sizeof.c", 0, 0},
    {"probes", "static_local_multidim_array", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/static_local_multidim_array.c", 0, 0},
    {"probes", "static_local_typedef_multidim_array", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/static_local_typedef_multidim_array.c", 0, 0},
    {"probes", "unsigned_long_return_signedness", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/unsigned_long_return_signedness.c", 0, 0},
    {"probes", "mixed_width_comparison", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/mixed_width_comparison.c", 0, 0},
    {"probes", "funcptr_int_to_fp_arg", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/funcptr_int_to_fp_arg.c", 0, 0},
    {"probes", "func_returning_funcptr_int_to_fp_arg", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/func_returning_funcptr_int_to_fp_arg.c", 0, 0},
    {"probes", "typedef_funcptr_int_to_fp_arg", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/typedef_funcptr_int_to_fp_arg.c", 0, 0},
    {"probes", "funcptr_member_int_to_fp_arg", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/funcptr_member_int_to_fp_arg.c", 0, 0},
    {"probes", "funcptr_fp_to_int_arg", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/funcptr_fp_to_int_arg.c", 0, 0},
    {"probes", "union_array_brace_init", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/union_array_brace_init.c", 0, 0},
    {"probes", "multilevel_pointer_fp_pointee", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/multilevel_pointer_fp_pointee.c", 0, 0},
    {"probes", "file_scope_aggregate_compound_literal_addr", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/file_scope_aggregate_compound_literal_addr.c", 0, 0},
    {"probes", "global_nested_brace_designator", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_nested_brace_designator.c", 0, 0},
    {"probes", "global_multidim_member_funcptr_designator", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_multidim_member_funcptr_designator.c", 0, 0},
    {"probes", "if0_skip_non_c_tokens", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/if0_skip_non_c_tokens.c", 0, 0},
    {"probes", "pointer_to_vla", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/pointer_to_vla.c", 0, 0},
    {"probes", "func_pointer_return_subscript", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/func_pointer_return_subscript.c", 0, 0},
    {"probes", "static_tag_return_function", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/static_tag_return_function.c", 0, 0},
    {"probes", "func_return_pointer_to_array", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/func_return_pointer_to_array.c", 0, 0},
    {"probes", "func_return_pointer_to_2d_array", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/func_return_pointer_to_2d_array.c", 0, 0},
    {"probes", "funcptr_return_pointer_to_array", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/funcptr_return_pointer_to_array.c", 0, 0},
    {"probes", "funcptr_return_pointer_to_2d_array", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/funcptr_return_pointer_to_2d_array.c", 0, 0},
    {"probes", "static_typedef_name_global", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/static_typedef_name_global.c", 0, 0},
    {"probes", "qualified_pointer_return", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/qualified_pointer_return.c", 0, 0},
    {"probes", "tag_return_complex_declarator", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/tag_return_complex_declarator.c", 0, 0},
    {"probes", "funcptr_return_struct_member", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/funcptr_return_struct_member.c", 0, 0},
    {"probes", "funcptr_return_large_struct", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/funcptr_return_large_struct.c", 0, 0},
    {"probes", "indirect_aggregate_return", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/indirect_aggregate_return.c", 0, 0},
    {"probes", "arm64_aggregate_varargs", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/arm64_aggregate_varargs.c", 0, 0},
    {"probes", "multilevel_pointer_return", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/multilevel_pointer_return.c", 0, 0},
    {"probes", "extern_then_def_same_tu", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/extern_then_def_same_tu.c", 0, 0},
    {"probes", "local_extern_tag_decl", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/local_extern_tag_decl.c", 0, 0},
    {"probes", "pointer_typedef_param_subscript", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/pointer_typedef_param_subscript.c", 0, 0},
    {"probes", "unsigned_char_pointer_zero_extend", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/unsigned_char_pointer_zero_extend.c", 0, 0},
    {"probes", "cast_ptr_to_array_leaf_flags", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/cast_ptr_to_array_leaf_flags.c", 0, 0},
    {"probes", "global_2d_pointer_array", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_2d_pointer_array.c", 0, 0},
    {"probes", "local_array_of_ptr_to_array", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/local_array_of_ptr_to_array.c", 0, 0},
    {"probes", "local_2d_pointer_array", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/local_2d_pointer_array.c", 0, 0},
    {"probes", "local_2d_funcptr_array", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/local_2d_funcptr_array.c", 0, 0},
    {"probes", "wide_string_literal_init", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/wide_string_literal_init.c", 0, 0},
    {"probes", "c11_standard_headers", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/c11_standard_headers.c", 0, 0},
    {"probes", "generic_long_double", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/generic_long_double.c", 0, 0},
    {"probes", "global_struct_char_array_member", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_struct_char_array_member.c", 0, 0},
    {"probes", "global_struct_member_after_fp_decl", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_struct_member_after_fp_decl.c", 0, 0},
    {"probes", "global_struct_2d_char_array_member", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_struct_2d_char_array_member.c", 0, 0},
    {"probes", "global_struct_array_char_member", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_struct_array_char_member.c", 0, 0},
    {"probes", "global_struct_3d_char_array_member", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_struct_3d_char_array_member.c", 0, 0},
    {"probes", "local_struct_2d_char_array_member", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/local_struct_2d_char_array_member.c", 0, 0},
    {"probes", "local_struct_3d_char_array_member", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/local_struct_3d_char_array_member.c", 0, 0},
    {"probes", "multidim_char_member_brace_elision", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/multidim_char_member_brace_elision.c", 0, 0},
    {"probes", "global_multidim_array_nested_designator", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_multidim_array_nested_designator.c", 0, 0},
    {"probes", "global_struct_fp_array_member", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_struct_fp_array_member.c", 0, 0},
    {"probes", "tag_shadowing_block_scope", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/tag_shadowing_block_scope.c", 0, 0},
    {"probes", "tag_shadowing_advanced", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/tag_shadowing_advanced.c", 0, 0},
    {"probes", "ptr_to_array_of_funcptrs", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/ptr_to_array_of_funcptrs.c", 0, 0},
    {"probes", "global_struct_nested_union_fp", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_struct_nested_union_fp.c", 0, 0},
    {"probes", "typedef_pointer_element_array_sizeof", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/typedef_pointer_element_array_sizeof.c", 0, 0},
    {"probes", "nested_union_designator_ordinal", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_union_designator_ordinal.c", 0, 0},
    {"probes", "typedef_pointer_element_array_decl", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/typedef_pointer_element_array_decl.c", 0, 0},
    {"probes", "static_assert_in_struct", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/static_assert_in_struct.c", 0, 0},
    {"probes", "global_struct_ptr_array_subscript", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_struct_ptr_array_subscript.c", 0, 0},
    {"probes", "vla_sizeof_direct", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/vla_sizeof_direct.c", 0, 0},
    {"probes", "struct_fp_pointer_member_subscript", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_fp_pointer_member_subscript.c", 0, 0},
    {"probes", "struct_double_ptr_deref_arrow", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_double_ptr_deref_arrow.c", 0, 0},
    {"probes", "funcptr_return_const_pointee", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/funcptr_return_const_pointee.c", 0, 0},
    {"probes", "file_scope_array_compound_literal_decay", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/file_scope_array_compound_literal_decay.c", 0, 0},
    {"probes", "global_multidim_struct_pointer_designator", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_multidim_struct_pointer_designator.c", 0, 0},
    {"probes", "static_local_pointer_array_init", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/static_local_pointer_array_init.c", 0, 0},
    {"probes", "global_nested_union_pointer_init", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_nested_union_pointer_init.c", 0, 0},
    {"probes", "static_local_struct_pointer_member_init", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/static_local_struct_pointer_member_init.c", 0, 0},
    {"probes", "compound_literal_array_size_and_decay", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/compound_literal_array_size_and_decay.c", 0, 0},
    {"probes", "compound_literal_inferred_array_sizeof", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/compound_literal_inferred_array_sizeof.c", 0, 0},
    {"probes", "compound_literal_array_addr_sizeof", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/compound_literal_array_addr_sizeof.c", 0, 0},
    {"probes", "struct_funcptr_designated_zero_init", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_funcptr_designated_zero_init.c", 0, 0},
    {"probes", "nested_struct_funcptr_designated_zero_init", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_struct_funcptr_designated_zero_init.c", 0, 0},
    {"probes", "wasm_nonvoid_indirect_unused_result", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/wasm_nonvoid_indirect_unused_result.c", 0, 0},
    {"probes", "indirect_struct_return_funcptr", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/indirect_struct_return_funcptr.c", 0, 0},
    {"probes", "typedef_void_funcptr_param", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/typedef_void_funcptr_param.c", 0, 0},
    {"probes", "scope_graph_namespace_lifetime", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/scope_graph_namespace_lifetime.c", 0, 0},
    {"probes", "target_layout_pointer_record", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/target_layout_pointer_record.c", 0, 0},
    {"probes", "vla_typedef_bound_identity", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/vla_typedef_bound_identity.c", 0, 0},
    {"probes", "abi_dynamic_mixed_params", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/abi_dynamic_mixed_params.c", 0, 0},
    {"probes", "pp_active_macro_redefinition", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/pp_active_macro_redefinition.c", 0, 0},
    {"probes", "qualified_pointer_array_function", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/qualified_pointer_array_function.c", 0, 0},
    {"probes", "prototype_typedef_array_qualifiers", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/prototype_typedef_array_qualifiers.c", 0, 0},
    {"probes", "local_typedef_object_shadow_restore", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/local_typedef_object_shadow_restore.c", 0, 0},
    {"probes", "address_of_parameter_subarray", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/address_of_parameter_subarray.c", 0, 0},
    {"probes", "address_of_vla_subarray", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/address_of_vla_subarray.c", 0, 0},
    {"probes", "address_of_struct_member_subarray", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/address_of_struct_member_subarray.c", 0, 0},
    {"probes", "conditional_qualified_array_pointer", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/conditional_qualified_array_pointer.c", 0, 0},
    {"probes", "global_subarray_address_initializer", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_subarray_address_initializer.c", 0, 0},
    {"probes", "static_local_subarray_address_initializer", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/static_local_subarray_address_initializer.c", 0, 0},
    {"probes", "compound_literal_subarray_address", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/compound_literal_subarray_address.c", 0, 0},
    {"probes", "nested_parameter_subarray_address", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_parameter_subarray_address.c", 0, 0},
    {"probes", "tentative_incomplete_array_completion", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/tentative_incomplete_array_completion.c", 0, 0},
    {"probes", "tentative_definition_with_initializer", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/tentative_definition_with_initializer.c", 0, 0},
    {"probes", "function_parameter_adjustment_redeclaration", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/function_parameter_adjustment_redeclaration.c", 0, 0},
    {"probes", "block_scope_extern_binding", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/block_scope_extern_binding.c", 0, 0},
    {"probes", "tentative_incomplete_record_completion", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/tentative_incomplete_record_completion.c", 0, 0},
    {"probes", "tentative_incomplete_record_address", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/tentative_incomplete_record_address.c", 0, 0},
    {"probes", "extern_incomplete_record_declaration", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/extern_incomplete_record_declaration.c", 0, 0},
    {"probes", "tentative_incomplete_union_completion", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/tentative_incomplete_union_completion.c", 0, 0},
    {"probes", "deferred_type_name_binding", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/deferred_type_name_binding.c", 0, 0},
    {"probes", "generic_semantic_selection", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/generic_semantic_selection.c", 0, 0},
    {"probes", "local_typedef_funcptr_array_call", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/local_typedef_funcptr_array_call.c", 0, 0},
    {"probes", "sizeof_vla_type_name", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/sizeof_vla_type_name.c", 0, 0},
    {"probes", "type_name_shadowing_delayed", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/type_name_shadowing_delayed.c", 0, 0},
    {"probes", "unary_semantic_resolution", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/unary_semantic_resolution.c", 0, 0},
    {"probes", "function_designator_operator_call", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/function_designator_operator_call.c", 0, 0},
    {"probes", "array_parameter_const_adjustment", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/array_parameter_const_adjustment.c", 0, 0},
    {"probes", "block_scope_extern_array_binding", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/block_scope_extern_array_binding.c", 0, 0},
    {"probes", "prototype_void_oldstyle_definition", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/prototype_void_oldstyle_definition.c", 0, 0},
    {"probes", "parameter_shadows_function_name", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/parameter_shadows_function_name.c", 0, 0},
    {"probes", "vla_prototype_star_adjustment", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/vla_prototype_star_adjustment.c", 0, 0},
    {"probes", "conditional_void_pointer_null", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/conditional_void_pointer_null.c", 0, 0},
    {"probes", "oldstyle_prototype_default_promotion", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/oldstyle_prototype_default_promotion.c", 0, 0},
    {"probes", "declaration_qualifier_constraints", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/declaration_qualifier_constraints.c", 0, 0},
    {"probes", "alignas_global_static_storage", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/alignas_global_static_storage.c", 0, 0},
};

/* クロス TU (複数 translation unit) テスト。2 つの .c を ag_c で別々に .s 化し、
 * 同じ名前空間接頭辞で記号を namespace してから一緒に category binary へリンクする。
 * 単一ファイル fixture では再現できない「別 TU の同名シンボル衝突」を検査できる
 * (例: 両 TU が同名 file-scope static を持つとき、内部リンケージが壊れていると
 *  namespace 後に .global が重複し category binary のリンクが失敗する)。
 * file_main が main を含む TU、file_other がもう一方の TU。expected_i は main の戻り値。
 * test_cases[] に 2 つ目のファイル列を足すと既存約 1000 エントリが
 * -Wmissing-field-initializers 警告を出すため、別テーブルにしている。 */
typedef struct {
  const char *category;
  const char *name;
  const char *file_main;   // main を含む TU
  const char *file_other;  // もう一方の TU
  int expected_i;          // main の戻り値 (exit code mod 256)
} link2_case_t;

static const link2_case_t link2_cases[] = {
    {"probes", "static_internal_linkage_xtu",
     "test/fixtures/probes_found_bugs/static_internal_linkage_xtu_main.c",
     "test/fixtures/probes_found_bugs/static_internal_linkage_xtu_other.c", 42},
    {"probes", "extern_funcptr_xtu",
     "test/fixtures/probes_found_bugs/extern_funcptr_xtu_main.c",
     "test/fixtures/probes_found_bugs/extern_funcptr_xtu_other.c", 42},
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
    {"const_struct_member_assign_rejected",
     "int main() { const struct S { int x; } s = {1}; s.x = 2; return s.x; }",
     "const修飾された変数への代入はできません"},
    {"const_struct_array_member_assign_rejected",
     "int main() { const struct S { int x; } a[1] = {{1}}; a[0].x = 2; return a[0].x; }",
     "const修飾された変数への代入はできません"},
    {"global_const_struct_array_member_assign_rejected",
     "struct S { int x; }; const struct S a[1] = {{1}}; int main() { a[0].x = 2; return a[0].x; }",
     "const修飾された変数への代入はできません"},
    {"const_struct_pointer_to_array_member_assign_rejected",
     "struct S { int x; }; int main() { const struct S a[1] = {{1}}; const struct S (*p)[1] = &a; (*p)[0].x = 2; return (*p)[0].x; }",
     "const修飾された変数への代入はできません"},
    {"const_struct_pointer_member_assign_rejected",
     "int main() { struct S { int x; }; const struct S *p = 0; p->x = 2; return 0; }",
     "const修飾された変数への代入はできません"},
    {"const_struct_func_ret_pointer_member_assign_rejected",
     "struct S { int x; }; const struct S g = {1}; const struct S *get(void); int main() { get()->x = 2; return get()->x; } const struct S *get(void) { return &g; }",
     "const修飾された変数への代入はできません"},
    {"const_struct_func_ret_pointer_to_array_member_assign_rejected",
     "struct S { int x; }; const struct S g[1] = {{1}}; const struct S (*get(void))[1] { return &g; } int main() { (*get())[0].x = 2; return (*get())[0].x; }",
     "const修飾された変数への代入はできません"},
    {"const_struct_funcptr_ret_pointer_member_assign_rejected",
     "struct S { int x; }; const struct S g = {1}; const struct S *get(void) { return &g; } int main() { const struct S *(*fp)(void) = get; fp()->x = 2; return fp()->x; }",
     "const修飾された変数への代入はできません"},
    {"const_struct_funcptr_ret_pointer_to_array_member_assign_rejected",
     "struct S { int x; }; const struct S g[1] = {{1}}; const struct S (*get(void))[1] { return &g; } int main() { const struct S (*(*fp)(void))[1] = get; (*fp())[0].x = 2; return (*fp())[0].x; }",
     "const修飾された変数への代入はできません"},
    {"const_qual_discard_init_rejected",
     "int main() { const int x = 5; const int *cp = &x; int *p = cp; return 0; }",
     "const修飾されたポインタからconst無しポインタへの暗黙変換はできません"},
    {"const_qual_discard_assign_rejected",
     "int main() { const int x = 5; const int *cp = &x; int *p; p = cp; return 0; }",
     "const修飾されたポインタからconst無しポインタへの暗黙変換はできません"},
    {"incompatible_prototype_oldstyle_definition_rejected",
     "int f(char); int f() { return 0; } int main(void) { return f(1); }",
     "E3064"},
    {"oldstyle_float_prototype_rejected",
     "int f(); int f(float); int main(void) { return 0; }",
     "E3064"},
    {"upgraded_prototype_argument_count_rejected",
     "int f(); int f(int); int main(void) { return f(); } int f(int value) { return value; }",
     "E3103"},
    {"vla_star_outside_parameter_rejected",
     "int values[*]; int main(void) { return 0; }",
     "E3064"},
    {"vla_static_star_rejected",
     "int f(int values[static *]); int main(void) { return 0; }",
     "E3064"},
    {"const_array_parameter_reassignment_rejected",
     "int f(int values[const]) { values = 0; return 0; } int main(void) { return 0; }",
     "E3077"},
    {"restrict_nonpointer_prefix_rejected",
     "restrict int value; int main(void) { return value; }",
     "E3064"},
    {"restrict_nonpointer_parameter_rejected",
     "int f(int restrict value) { return value; } int main(void) { return f(0); }",
     "E3064"},
    {"array_qualifier_outside_parameter_rejected",
     "int values[const 3]; int main(void) { return 0; }",
     "E3064"},
    {"array_static_outside_parameter_rejected",
     "int values[static 3]; int main(void) { return 0; }",
     "E3064"},
    {"array_parameter_static_without_bound_rejected",
     "int f(int values[static]); int main(void) { return 0; }",
     "E3064"},
    {"variadic_without_named_parameter_rejected",
     "int f(...); int main(void) { return 0; }",
     "E3064"},
    {"function_returning_array_rejected",
     "int f(void)[3]; int main(void) { return 0; }",
     "E3064"},
    {"function_returning_function_rejected",
     "int f(void)(void); int main(void) { return 0; }",
     "E3064"},
    {"alignas_weaker_than_natural_rejected",
     "_Alignas(1) int value; int main(void) { return value; }",
     "E3064"},
    {"alignas_non_power_of_two_rejected",
     "_Alignas(3) int value; int main(void) { return value; }",
     "E3064"},
    {"alignas_typedef_rejected",
     "typedef _Alignas(16) int aligned_int; int main(void) { return 0; }",
     "E3064"},
    {"alignas_function_declaration_rejected",
     "_Alignas(16) int f(void); int main(void) { return 0; }",
     "E3064"},
    {"alignas_function_definition_rejected",
     "_Alignas(16) int f(void) { return 0; } int main(void) { return f(); }",
     "E3064"},
    {"alignas_parameter_rejected",
     "int f(_Alignas(16) int value) { return value; } int main(void) { return f(0); }",
     "E3064"},
    {"alignas_bitfield_rejected",
     "struct S { _Alignas(8) unsigned int bit : 1; }; int main(void) { return 0; }",
     "E3064"},
    {"alignas_register_object_rejected",
     "int main(void) { register _Alignas(8) int value = 0; return value; }",
     "E3064"},
    {"inline_object_rejected",
     "inline int value; int main(void) { return value; }",
     "E3064"},
    {"noreturn_object_rejected",
     "_Noreturn int value; int main(void) { return value; }",
     "E3064"},
    {"inline_function_typedef_rejected",
     "typedef inline int function_type(void); int main(void) { return 0; }",
     "E3064"},
    {"static_tentative_incomplete_array_rejected",
     "static int values[]; int main(void) { return 0; }",
     "不完全型のオブジェクトは宣言できません"},
    {"static_tentative_incomplete_record_rejected",
     "struct Value; static struct Value value; struct Value { int member; }; int main(void) { return 0; }",
     "不完全型のオブジェクトは宣言できません"},
    {"unresolved_tentative_incomplete_record_rejected",
     "struct Value; struct Value value; int main(void) { return 0; }",
     "E3037"},
    {"funcdef_unnamed_param_rejected",
     "int bad(int) { return 0; }",
     "必要な項目がありません: 仮引数"},
    {"c11_implicit_int_objects_rejected",
     "aaa;\nbb;\n",
     "E3088"},
    {"c11_implicit_return_type_rejected",
     "implicit_return(void) { return 0; }",
     "E3088"},
    {"c11_old_style_parameter_rejected",
     "int old_style(value) { return value; }",
     "E3088"},
    {"c11_block_implicit_int_rejected",
     "int block_scope(void) { static local; return 0; }",
     "E3088"},
    {"gnu_statement_expression_rejected",
     "int main(void) { return ({ int value = 1; value; }); }",
     "E3096"},
    {"gnu_attribute_rejected",
     "int value __attribute__((unused)); int main(void) { return 0; }",
     "E3096"},
    {"gnu_pragma_rejected",
     "#define VALUE 1\n#pragma push_macro(\"VALUE\")\nint main(void) { return VALUE; }\n",
     "E3096"},
    {"gnu_array_range_designator_rejected",
     "int values[3] = {[0 ... 2] = 1}; int main(void) { return 0; }",
     "E3096"},
    {"multiple_function_syntax_errors_reported",
     "int first(void) { int a = ; return a; }\n"
     "int second(void) { int b = ; return b; }\n",
     "E3045"},
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
            strcmp(sym, "_strrchr") == 0 || strcmp(sym, "_strspn") == 0 ||
            strcmp(sym, "_strcspn") == 0 || strcmp(sym, "_strpbrk") == 0 ||
            strcmp(sym, "_strstr") == 0 ||
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
            strcmp(sym, "_system") == 0 || strcmp(sym, "_atof") == 0 ||
            strcmp(sym, "_strtol") == 0 || strcmp(sym, "_strtoul") == 0 ||
            strcmp(sym, "_strtoll") == 0 || strcmp(sym, "_strtoull") == 0 ||
            strcmp(sym, "_strtof") == 0 || strcmp(sym, "_strtod") == 0 ||
            strcmp(sym, "_strtold") == 0 || strcmp(sym, "_atoll") == 0 ||
            strcmp(sym, "_realpath") == 0 ||
            strcmp(sym, "_time") == 0 || strcmp(sym, "_clock") == 0 ||
            strcmp(sym, "_difftime") == 0 ||
            strcmp(sym, "_gmtime") == 0 || strcmp(sym, "_localtime") == 0 ||
            strcmp(sym, "_asctime") == 0 || strcmp(sym, "_ctime") == 0 ||
            strcmp(sym, "_strftime") == 0 || strcmp(sym, "_mktime") == 0 ||
            strcmp(sym, "_timespec_get") == 0 ||
            strcmp(sym, "_signal") == 0 || strcmp(sym, "_raise") == 0 ||
            strcmp(sym, "_perror") == 0 || strcmp(sym, "_fopen") == 0 ||
            strcmp(sym, "_fclose") == 0 || strcmp(sym, "_fflush") == 0 ||
            strcmp(sym, "_fread") == 0 || strcmp(sym, "_fwrite") == 0 ||
            strcmp(sym, "_fputs") == 0 || strcmp(sym, "_fputc") == 0 ||
            strcmp(sym, "_fgetc") == 0 || strcmp(sym, "_fgets") == 0 ||
            strcmp(sym, "_fseek") == 0 || strcmp(sym, "_ftell") == 0 ||
            strcmp(sym, "_rewind") == 0 || strcmp(sym, "_feof") == 0 ||
            strcmp(sym, "_ferror") == 0 || strcmp(sym, "_clearerr") == 0 ||
            strcmp(sym, "_getchar") == 0 || strcmp(sym, "_putchar") == 0 ||
            strcmp(sym, "_isalnum") == 0 || strcmp(sym, "_isalpha") == 0 ||
            strcmp(sym, "_isblank") == 0 || strcmp(sym, "_iscntrl") == 0 ||
            strcmp(sym, "_isdigit") == 0 || strcmp(sym, "_isgraph") == 0 ||
            strcmp(sym, "_islower") == 0 || strcmp(sym, "_isprint") == 0 ||
            strcmp(sym, "_ispunct") == 0 || strcmp(sym, "_isspace") == 0 ||
            strcmp(sym, "_isupper") == 0 || strcmp(sym, "_isxdigit") == 0 ||
            strcmp(sym, "_tolower") == 0 || strcmp(sym, "_toupper") == 0 ||
            /* <math.h> の実数関数 (complex.h の cabs/carg 等が呼ぶ)。外部 libc
             * シンボルなので名前空間化してはならない。 */
            strcmp(sym, "_acos") == 0 || strcmp(sym, "_asin") == 0 ||
            strcmp(sym, "_atan") == 0 || strcmp(sym, "_atan2") == 0 ||
            strcmp(sym, "_cos") == 0 || strcmp(sym, "_sin") == 0 ||
            strcmp(sym, "_tan") == 0 || strcmp(sym, "_cosh") == 0 ||
            strcmp(sym, "_sinh") == 0 || strcmp(sym, "_tanh") == 0 ||
            strcmp(sym, "_exp") == 0 || strcmp(sym, "_log") == 0 ||
            strcmp(sym, "_log10") == 0 || strcmp(sym, "_log2") == 0 ||
            strcmp(sym, "_pow") == 0 || strcmp(sym, "_sqrt") == 0 ||
            strcmp(sym, "_cbrt") == 0 || strcmp(sym, "_ceil") == 0 ||
            strcmp(sym, "_floor") == 0 || strcmp(sym, "_round") == 0 ||
            strcmp(sym, "_trunc") == 0 || strcmp(sym, "_fabs") == 0 ||
            strcmp(sym, "_fmod") == 0 || strcmp(sym, "_fabsf") == 0 ||
            strcmp(sym, "_fmodf") == 0 || strcmp(sym, "_fmodl") == 0 ||
            strcmp(sym, "_sqrtf") == 0 || strcmp(sym, "_ceilf") == 0 ||
            strcmp(sym, "_floorf") == 0 || strcmp(sym, "_roundf") == 0 ||
            strcmp(sym, "_cbrtf") == 0 || strcmp(sym, "_cbrtl") == 0 ||
            strcmp(sym, "_sinf") == 0 || strcmp(sym, "_sinl") == 0 ||
            strcmp(sym, "_cosf") == 0 || strcmp(sym, "_cosl") == 0 ||
            strcmp(sym, "_tanf") == 0 || strcmp(sym, "_tanl") == 0 ||
            strcmp(sym, "_sinhf") == 0 || strcmp(sym, "_sinhl") == 0 ||
            strcmp(sym, "_coshf") == 0 || strcmp(sym, "_coshl") == 0 ||
            strcmp(sym, "_tanhf") == 0 || strcmp(sym, "_tanhl") == 0 ||
            strcmp(sym, "_asinh") == 0 || strcmp(sym, "_asinhf") == 0 ||
            strcmp(sym, "_asinhl") == 0 ||
            strcmp(sym, "_acosh") == 0 || strcmp(sym, "_acoshf") == 0 ||
            strcmp(sym, "_acoshl") == 0 ||
            strcmp(sym, "_atanh") == 0 || strcmp(sym, "_atanhf") == 0 ||
            strcmp(sym, "_atanhl") == 0 ||
            strcmp(sym, "_asinf") == 0 || strcmp(sym, "_asinl") == 0 ||
            strcmp(sym, "_acosf") == 0 || strcmp(sym, "_acosl") == 0 ||
            strcmp(sym, "_atanf") == 0 || strcmp(sym, "_atanl") == 0 ||
            strcmp(sym, "_atan2f") == 0 || strcmp(sym, "_atan2l") == 0 ||
            strcmp(sym, "_exp2") == 0 || strcmp(sym, "_exp2f") == 0 ||
            strcmp(sym, "_exp2l") == 0 ||
            strcmp(sym, "_expm1") == 0 || strcmp(sym, "_expm1f") == 0 ||
            strcmp(sym, "_expm1l") == 0 ||
            strcmp(sym, "_expf") == 0 || strcmp(sym, "_expl") == 0 ||
            strcmp(sym, "_erf") == 0 || strcmp(sym, "_erff") == 0 ||
            strcmp(sym, "_erfl") == 0 ||
            strcmp(sym, "_erfc") == 0 || strcmp(sym, "_erfcf") == 0 ||
            strcmp(sym, "_erfcl") == 0 ||
            strcmp(sym, "_logf") == 0 || strcmp(sym, "_logl") == 0 ||
            strcmp(sym, "_log1p") == 0 || strcmp(sym, "_log1pf") == 0 ||
            strcmp(sym, "_log1pl") == 0 ||
            strcmp(sym, "_log10f") == 0 || strcmp(sym, "_log10l") == 0 ||
            strcmp(sym, "_log2f") == 0 || strcmp(sym, "_log2l") == 0 ||
            strcmp(sym, "_floorl") == 0 || strcmp(sym, "_ceill") == 0 ||
            strcmp(sym, "_roundl") == 0 || strcmp(sym, "_truncf") == 0 ||
            strcmp(sym, "_truncl") == 0 || strcmp(sym, "_hypot") == 0 ||
            strcmp(sym, "_hypotf") == 0 || strcmp(sym, "_hypotl") == 0 ||
            strcmp(sym, "_nearbyint") == 0 || strcmp(sym, "_nearbyintf") == 0 ||
            strcmp(sym, "_nearbyintl") == 0 ||
            strcmp(sym, "_rint") == 0 || strcmp(sym, "_rintf") == 0 ||
            strcmp(sym, "_rintl") == 0 ||
            strcmp(sym, "_lrint") == 0 || strcmp(sym, "_lrintf") == 0 ||
            strcmp(sym, "_lrintl") == 0 ||
            strcmp(sym, "_llrint") == 0 || strcmp(sym, "_llrintf") == 0 ||
            strcmp(sym, "_llrintl") == 0 ||
            strcmp(sym, "_lround") == 0 || strcmp(sym, "_lroundf") == 0 ||
            strcmp(sym, "_lroundl") == 0 ||
            strcmp(sym, "_llround") == 0 || strcmp(sym, "_llroundf") == 0 ||
            strcmp(sym, "_llroundl") == 0 ||
            strcmp(sym, "_remainder") == 0 || strcmp(sym, "_remainderf") == 0 ||
            strcmp(sym, "_remainderl") == 0 ||
            strcmp(sym, "_remquo") == 0 || strcmp(sym, "_remquof") == 0 ||
            strcmp(sym, "_remquol") == 0 ||
            strcmp(sym, "_fdim") == 0 || strcmp(sym, "_fdimf") == 0 ||
            strcmp(sym, "_fdiml") == 0 ||
            strcmp(sym, "_fma") == 0 || strcmp(sym, "_fmaf") == 0 ||
            strcmp(sym, "_fmal") == 0 ||
            strcmp(sym, "_frexp") == 0 || strcmp(sym, "_frexpf") == 0 ||
            strcmp(sym, "_frexpl") == 0 ||
            strcmp(sym, "_ldexp") == 0 || strcmp(sym, "_ldexpf") == 0 ||
            strcmp(sym, "_ldexpl") == 0 ||
            strcmp(sym, "_scalbn") == 0 || strcmp(sym, "_scalbnf") == 0 ||
            strcmp(sym, "_scalbnl") == 0 ||
            strcmp(sym, "_scalbln") == 0 || strcmp(sym, "_scalblnf") == 0 ||
            strcmp(sym, "_scalblnl") == 0 ||
            strcmp(sym, "_ilogb") == 0 || strcmp(sym, "_ilogbf") == 0 ||
            strcmp(sym, "_ilogbl") == 0 ||
            strcmp(sym, "_logb") == 0 || strcmp(sym, "_logbf") == 0 ||
            strcmp(sym, "_logbl") == 0 ||
            strcmp(sym, "_modf") == 0 || strcmp(sym, "_modff") == 0 ||
            strcmp(sym, "_modfl") == 0 ||
            strcmp(sym, "_copysign") == 0 || strcmp(sym, "_copysignf") == 0 ||
            strcmp(sym, "_copysignl") == 0 ||
            strcmp(sym, "_nan") == 0 || strcmp(sym, "_nanf") == 0 ||
            strcmp(sym, "_nanl") == 0 ||
            strcmp(sym, "_fmin") == 0 || strcmp(sym, "_fminf") == 0 ||
            strcmp(sym, "_fminl") == 0 || strcmp(sym, "_fmax") == 0 ||
            strcmp(sym, "_fmaxf") == 0 || strcmp(sym, "_fmaxl") == 0 ||
            /* <wctype.h> / <wchar.h> / <fenv.h> / <locale.h> / <inttypes.h> の libc 関数。
             * 外部シンボルなので名前空間化しない (c11_standard_headers fixture が使用)。 */
            strcmp(sym, "_iswalnum") == 0 || strcmp(sym, "_iswalpha") == 0 ||
            strcmp(sym, "_iswblank") == 0 || strcmp(sym, "_iswcntrl") == 0 ||
            strcmp(sym, "_iswdigit") == 0 || strcmp(sym, "_iswgraph") == 0 ||
            strcmp(sym, "_iswlower") == 0 || strcmp(sym, "_iswprint") == 0 ||
            strcmp(sym, "_iswpunct") == 0 || strcmp(sym, "_iswspace") == 0 ||
            strcmp(sym, "_iswupper") == 0 || strcmp(sym, "_iswxdigit") == 0 ||
            strcmp(sym, "_towlower") == 0 || strcmp(sym, "_towupper") == 0 ||
            strcmp(sym, "_wctype") == 0 || strcmp(sym, "_iswctype") == 0 ||
            strcmp(sym, "_wctrans") == 0 || strcmp(sym, "_towctrans") == 0 ||
            strcmp(sym, "_wcslen") == 0 || strcmp(sym, "_wcscpy") == 0 ||
            strcmp(sym, "_wcsncpy") == 0 || strcmp(sym, "_wcscat") == 0 ||
            strcmp(sym, "_wcsncat") == 0 || strcmp(sym, "_wcsstr") == 0 ||
            strcmp(sym, "_wcscmp") == 0 || strcmp(sym, "_wcsncmp") == 0 ||
            strcmp(sym, "_wcscoll") == 0 || strcmp(sym, "_wcsxfrm") == 0 ||
            strcmp(sym, "_wcschr") == 0 || strcmp(sym, "_wcsrchr") == 0 ||
            strcmp(sym, "_wcsspn") == 0 || strcmp(sym, "_wcscspn") == 0 ||
            strcmp(sym, "_wcspbrk") == 0 || strcmp(sym, "_wcstok") == 0 ||
            strcmp(sym, "_wmemcpy") == 0 || strcmp(sym, "_wmemset") == 0 ||
            strcmp(sym, "_wmemmove") == 0 || strcmp(sym, "_wmemcmp") == 0 ||
            strcmp(sym, "_wmemchr") == 0 ||
            strcmp(sym, "_mbrtowc") == 0 || strcmp(sym, "_wcrtomb") == 0 ||
            strcmp(sym, "_mbsrtowcs") == 0 || strcmp(sym, "_wcsrtombs") == 0 ||
            strcmp(sym, "_mbrlen") == 0 || strcmp(sym, "_mbsinit") == 0 ||
            strcmp(sym, "_btowc") == 0 || strcmp(sym, "_wctob") == 0 ||
            strcmp(sym, "_wcstol") == 0 || strcmp(sym, "_wcstoul") == 0 ||
            strcmp(sym, "_wcstod") == 0 ||
            strcmp(sym, "_wcsftime") == 0 ||
            strcmp(sym, "_mbrtoc16") == 0 || strcmp(sym, "_c16rtomb") == 0 ||
            strcmp(sym, "_mbrtoc32") == 0 || strcmp(sym, "_c32rtomb") == 0 ||
            strcmp(sym, "_feclearexcept") == 0 || strcmp(sym, "_fetestexcept") == 0 ||
            strcmp(sym, "_feraiseexcept") == 0 || strcmp(sym, "_fegetround") == 0 ||
            strcmp(sym, "_fesetround") == 0 ||
            strcmp(sym, "_fegetexceptflag") == 0 || strcmp(sym, "_fesetexceptflag") == 0 ||
            strcmp(sym, "_fegetenv") == 0 || strcmp(sym, "_feholdexcept") == 0 ||
            strcmp(sym, "_fesetenv") == 0 || strcmp(sym, "_feupdateenv") == 0 ||
            strcmp(sym, "_setlocale") == 0 || strcmp(sym, "_localeconv") == 0 ||
            strcmp(sym, "_imaxabs") == 0 ||
            strcmp(sym, "_strtoimax") == 0 || strcmp(sym, "_strtoumax") == 0 ||
            strcmp(sym, "_powf") == 0 ||
            strcmp(sym, "_powl") == 0 || strcmp(sym, "_sqrtl") == 0 ||
            strcmp(sym, "_fabsl") == 0 || strcmp(sym, "_fmodf") == 0) {
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
  const size_t n_link2 = sizeof(link2_cases) / sizeof(link2_cases[0]);
  /* link2 ケースは 1 件につき 2 つの .s を追加するため余分に確保する (+ driver 用の 1)。 */
  const char **clang_inputs = calloc(max_cases + 2 * n_link2 + 1, sizeof(char *));
  char **owned_paths = calloc(max_cases + 2 * n_link2 + 1, sizeof(char *));
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
  fprintf(drv, "int main(void) {\n  setvbuf(stdout, NULL, _IONBF, 0);\n  int failed = 0;\n");

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

  /* クロス TU ケース: 2 つの TU を同じ名前空間接頭辞で namespace し、両方とも
   * category binary にリンクする。両 TU の同名 static が内部リンケージを失っていると
   * namespace 後に .global が重複してリンクが失敗する (= 回帰検出)。 */
  for (size_t i = 0; i < n_link2; i++) {
    const link2_case_t *lc = &link2_cases[i];
    if (strcmp(lc->category, category) != 0) continue;
    char cat_sym[128], name_sym[128], fn_sym[320];
    sanitize_symbol(category, cat_sym, sizeof(cat_sym));
    sanitize_symbol(lc->name, name_sym, sizeof(name_sym));
    snprintf(fn_sym, sizeof(fn_sym), "agc_%s_%s", cat_sym, name_sym);
    const char *files[2] = {lc->file_main, lc->file_other};
    const char *tags[2] = {"main", "other"};
    for (int k = 0; k < 2; k++) {
      char src2[PATH_MAX], s2[PATH_MAX], rs2[PATH_MAX];
      snprintf(src2, sizeof(src2), "%s/%s__%s.c", category_dir, lc->name, tags[k]);
      snprintf(s2, sizeof(s2), "%s/%s__%s.s", category_dir, lc->name, tags[k]);
      snprintf(rs2, sizeof(rs2), "%s/%s__%s.renamed.s", category_dir, lc->name, tags[k]);
      if (copy_source_file(files[k], src2) != 0 || run_ag_c_to_s(src2, s2) != 0 ||
          copy_and_namespace_symbols(s2, rs2, fn_sym) != 0) {
        fprintf(log, "  FAIL: build link2 %s (%s)\n", lc->name, tags[k]);
        fclose(drv);
        fclose(log);
        free(clang_inputs);
        free(owned_paths);
        return 1;
      }
      owned_paths[input_count] = strdup(rs2);
      clang_inputs[input_count] = owned_paths[input_count];
      if (!owned_paths[input_count]) {
        fclose(drv);
        fclose(log);
        free(clang_inputs);
        free(owned_paths);
        return 1;
      }
      input_count++;
    }
    fprintf(drv, "  extern int %s_main(void);\n", fn_sym);
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
  for (size_t i = 0; i < n_link2; i++) {
    const link2_case_t *lc = &link2_cases[i];
    if (strcmp(lc->category, category) != 0) continue;
    char cat_sym[128], name_sym[128], fn_sym[320];
    sanitize_symbol(category, cat_sym, sizeof(cat_sym));
    sanitize_symbol(lc->name, name_sym, sizeof(name_sym));
    snprintf(fn_sym, sizeof(fn_sym), "agc_%s_%s", cat_sym, name_sym);
    fprintf(drv, "  { int actual = (%s_main() & 255); if (actual != %d) { failed = 1; printf(\"FAIL %s expected %d got %%d\\n\", actual); } else { printf(\"OK %s => %%d\\n\", actual); } }\n",
            fn_sym, lc->expected_i, lc->name, lc->expected_i, lc->name);
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
    /* stderr もカテゴリログへ捕捉する。CASE_ASSERT_FILE の assert 失敗時、__assert_rtn が
     * stderr に "Assertion failed: (expr), function f, file ..., line N." を書いて abort する。
     * これを拾えないとログが "Summary: FAILED" だけになり、どの fixture が落ちたか分からない。
     * (stderr は無バッファなので abort 前に確実にパイプへ出る。) */
    dup2(pipefd[1], STDERR_FILENO);
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
    if ((strcmp(tc->name, "c11_implicit_int_objects_rejected") == 0 ||
         strcmp(tc->name, "multiple_function_syntax_errors_reported") == 0)) {
      FILE *log = fopen(log_path, "r");
      char diagnostics[8192] = {0};
      size_t length = log ? fread(diagnostics, 1, sizeof(diagnostics) - 1, log) : 0;
      if (log) fclose(log);
      diagnostics[length] = '\0';
      int count = 0;
      for (char *match = diagnostics;
           (match = strstr(match, tc->expected_diag)) != NULL;
           match += strlen(tc->expected_diag))
        count++;
      if (count != 2) {
        fprintf(stderr, "Compile-fail case did not emit two diagnostics: %s (see %s)\n",
                tc->name, log_path);
        return 1;
      }
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
    const char *pp_limit_path = "build/e2e/compile_fail/macro_expansion_limit.c";
    const char *log_path = "build/e2e/logs/compile_fail_macro_expansion_limit.log";
    if (mkdir_p("build/e2e/compile_fail") != 0 ||
        write_macro_expansion_limit_source(pp_limit_path, 19) != 0 ||
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
