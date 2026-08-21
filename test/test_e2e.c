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
    {"type_decl", "scalar_subobject_brace_init", CASE_ASSERT_FILE, "test/fixtures/type_decl/scalar_subobject_brace_init.c", 0, 0},
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
    {"probes", "thread_local_alignment_initializer_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/thread_local_alignment_initializer_boundaries.c", 0, 0},
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
    {"flex_array", "assignment_copies_fixed_prefix", CASE_ASSERT_FILE, "test/fixtures/flex_array/assignment_copies_fixed_prefix.c", 0, 0},
    {"flex_array", "parse_ok", CASE_ASSERT_FILE, "test/fixtures/flex_array/parse_ok.c", 0, 0},
    {"flex_array", "alloc_and_use", CASE_ASSERT_FILE, "test/fixtures/flex_array/alloc_and_use.c", 0, 0},
    {"flex_array", "union_contains_flex", CASE_ASSERT_FILE, "test/fixtures/flex_array/union_contains_flex.c", 0, 0},
    // tokenizer 拡張機能: 文字列接頭辞、UCN、トライグラフ
    {"tokenizer", "wide_string_L", CASE_ASSERT_FILE, "test/fixtures/tokenizer/wide_string_L.c", 0, 0},
    {"tokenizer", "u8_string", CASE_ASSERT_FILE, "test/fixtures/tokenizer/u8_string.c", 0, 0},
    {"tokenizer", "u_string", CASE_ASSERT_FILE, "test/fixtures/tokenizer/u_string.c", 0, 0},
    {"tokenizer", "u32_string", CASE_ASSERT_FILE, "test/fixtures/tokenizer/u32_string.c", 0, 0},
    {"tokenizer", "charlit_L", CASE_ASSERT_FILE, "test/fixtures/tokenizer/charlit_L.c", 0, 0},
    {"tokenizer", "charlit_u", CASE_ASSERT_FILE, "test/fixtures/tokenizer/charlit_u.c", 0, 0},
    {"tokenizer", "string_concat_prefix", CASE_ASSERT_FILE, "test/fixtures/tokenizer/string_concat_prefix.c", 0, 0},
    {"probes", "adjacent_encoded_string_literal_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/adjacent_encoded_string_literal_boundaries.c", 0, 0},
    {"probes", "encoded_character_constant_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/encoded_character_constant_boundaries.c", 0, 0},
    {"probes", "encoded_character_promotion_varargs_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/encoded_character_promotion_varargs_boundaries.c", 0, 0},
    {"probes", "macro_paste_literal_prefix_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/macro_paste_literal_prefix_boundaries.c", 0, 0},
    {"probes", "narrow_ucn_string_literal_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/narrow_ucn_string_literal_boundaries.c", 0, 0},
    {"tokenizer", "ucn_string", CASE_ASSERT_FILE, "test/fixtures/tokenizer/ucn_string.c", 0, 0},
    {"tokenizer", "ucn_string_3byte", CASE_ASSERT_FILE, "test/fixtures/tokenizer/ucn_string_3byte.c", 0, 0},
    {"tokenizer", "ucn_string_u16_surrogate", CASE_ASSERT_FILE, "test/fixtures/tokenizer/ucn_string_u16_surrogate.c", 0, 0},
    {"tokenizer", "ucn_string_u16_bmp", CASE_ASSERT_FILE, "test/fixtures/tokenizer/ucn_string_u16_bmp.c", 0, 0},
    {"tokenizer", "ucn_string_u16_mix", CASE_ASSERT_FILE, "test/fixtures/tokenizer/ucn_string_u16_mix.c", 0, 0},
    {"tokenizer", "ucn_string_u32", CASE_ASSERT_FILE, "test/fixtures/tokenizer/ucn_string_u32.c", 0, 0},
    {"tokenizer", "ucn_ident", CASE_ASSERT_FILE, "test/fixtures/tokenizer/ucn_ident.c", 0, 0},
    {"probes", "universal_character_identifier_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/universal_character_identifier_boundaries.c", 0, 0},
    {"tokenizer", "trigraph_or", CASE_ASSERT_FILE, "test/fixtures/tokenizer/trigraph_or.c", 0, 0},
    {"tokenizer", "trigraph_xor", CASE_ASSERT_FILE, "test/fixtures/tokenizer/trigraph_xor.c", 0, 0},
    {"probes", "digraph_preprocessing_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/digraph_preprocessing_boundaries.c", 0, 0},
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
    {"stdheader", "stdio_snprintf_float_rounding", CASE_ASSERT_FILE, "test/fixtures/stdheader/stdio_snprintf_float_rounding.c", 0, 0},
    {"stdheader", "stdio_snprintf_binary_halfway_rounding", CASE_ASSERT_FILE, "test/fixtures/stdheader/stdio_snprintf_binary_halfway_rounding.c", 0, 0},
    {"stdheader", "stdio_snprintf_float_integer_digits", CASE_ASSERT_FILE, "test/fixtures/stdheader/stdio_snprintf_float_integer_digits.c", 0, 0},
    {"stdheader", "stdio_snprintf_significant_rounding", CASE_ASSERT_FILE, "test/fixtures/stdheader/stdio_snprintf_significant_rounding.c", 0, 0},
    {"stdheader", "stdio_snprintf_hex_default_precision", CASE_ASSERT_FILE, "test/fixtures/stdheader/stdio_snprintf_hex_default_precision.c", 0, 0},
    {"stdheader", "stdio_snprintf_size_boundaries", CASE_ASSERT_FILE, "test/fixtures/stdheader/stdio_snprintf_size_boundaries.c", 0, 0},
    {"stdheader", "stdio_sprintf_formats", CASE_ASSERT_FILE, "test/fixtures/stdheader/stdio_sprintf_formats.c", 0, 0},
    {"stdheader", "stdio_getline_decl", CASE_ASSERT_FILE, "test/fixtures/stdheader/stdio_getline_decl.c", 0, 0},
    {"stdheader", "stdlib_malloc_free", CASE_ASSERT_FILE, "test/fixtures/stdheader/stdlib_malloc_free.c", 0, 0},
    {"stdheader", "stdlib_realloc", CASE_ASSERT_FILE, "test/fixtures/stdheader/stdlib_realloc.c", 0, 0},
    {"stdheader", "stdlib_atoi", CASE_ASSERT_FILE, "test/fixtures/stdheader/stdlib_atoi.c", 0, 0},
    {"stdheader", "stdlib_abs", CASE_ASSERT_FILE, "test/fixtures/stdheader/stdlib_abs.c", 0, 0},
    {"stdheader", "stdlib_convert_rand", CASE_ASSERT_FILE, "test/fixtures/stdheader/stdlib_convert_rand.c", 0, 0},
    {"stdheader", "stdlib_strto_int", CASE_ASSERT_FILE, "test/fixtures/stdheader/stdlib_strto_int.c", 0, 0},
    {"probes", "strto_integer_base_detection_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/strto_integer_base_detection_boundaries.c", 0, 0},
    {"probes", "stdlib_const_input_qualifier_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/stdlib_const_input_qualifier_boundaries.c", 0, 0},
    {"probes", "stdlib_multibyte_const_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/stdlib_multibyte_const_boundaries.c", 0, 0},
    {"probes", "multibyte_large_count_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/multibyte_large_count_boundaries.c", 0, 0},
    {"probes", "stdlib_management_signature_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/stdlib_management_signature_boundaries.c", 0, 0},
    {"probes", "stdlib_noreturn_cfg_termination_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/stdlib_noreturn_cfg_termination_boundaries.c", 0, 0},
    {"probes", "stdlib_noreturn_cfg_warning_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/stdlib_noreturn_cfg_warning_boundaries.c", 0, 0},
    {"probes", "allocation_failure_size_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/allocation_failure_size_boundaries.c", 0, 0},
    {"probes", "nonvoid_aggregate_fallthrough_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nonvoid_aggregate_fallthrough_boundaries.c", 0, 0},
    {"probes", "noreturn_cfg_termination_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/noreturn_cfg_termination_boundaries.c", 0, 0},
    {"probes", "noreturn_fallthrough_warning_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/noreturn_fallthrough_warning_boundaries.c", 0, 0},
    {"probes", "constant_condition_cfg_termination_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/constant_condition_cfg_termination_boundaries.c", 0, 0},
    {"probes", "standard_constant_expression_cfg_termination_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/standard_constant_expression_cfg_termination_boundaries.c", 0, 0},
    {"probes", "standard_constant_expression_cfg_warning_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/standard_constant_expression_cfg_warning_boundaries.c", 0, 0},
    {"probes", "string_function_pointer_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/string_function_pointer_boundaries.c", 0, 0},
    {"probes", "string_search_char_conversion_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/string_search_char_conversion_boundaries.c", 0, 0},
    {"probes", "string_large_count_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/string_large_count_boundaries.c", 0, 0},
    {"probes", "character_classification_function_pointer_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/character_classification_function_pointer_boundaries.c", 0, 0},
    {"probes", "wctype_descriptor_target_abi_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/wctype_descriptor_target_abi_boundaries.c", 0, 0},
    {"probes", "time_locale_function_pointer_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/time_locale_function_pointer_boundaries.c", 0, 0},
    {"probes", "time_format_large_count_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/time_format_large_count_boundaries.c", 0, 0},
    {"probes", "time_target_type_abi_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/time_target_type_abi_boundaries.c", 0, 0},
    {"probes", "time_timespec_target_abi_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/time_timespec_target_abi_boundaries.c", 0, 0},
    {"probes", "time_struct_tm_target_abi_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/time_struct_tm_target_abi_boundaries.c", 0, 0},
    {"probes", "locale_category_macro_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/locale_category_macro_boundaries.c", 0, 0},
    {"probes", "localeconv_full_layout_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/localeconv_full_layout_boundaries.c", 0, 0},
    {"probes", "resource_usage_function_pointer_abi_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/resource_usage_function_pointer_abi_boundaries.c", 0, 0},
    {"probes", "setjmp_native_control_flow_abi_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/setjmp_native_control_flow_abi_boundaries.c", 0, 0},
    {"probes", "setjmp_noreturn_cfg_termination_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/setjmp_noreturn_cfg_termination_boundaries.c", 0, 0},
    {"probes", "setjmp_noreturn_cfg_warning_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/setjmp_noreturn_cfg_warning_boundaries.c", 0, 0},
    {"probes", "wchar_macro_type_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/wchar_macro_type_boundaries.c", 0, 0},
    {"probes", "wide_string_function_pointer_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/wide_string_function_pointer_boundaries.c", 0, 0},
    {"probes", "wide_conversion_function_pointer_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/wide_conversion_function_pointer_boundaries.c", 0, 0},
    {"probes", "wide_floating_exponent_conversion_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/wide_floating_exponent_conversion_boundaries.c", 0, 0},
    {"probes", "floating_string_special_range_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/floating_string_special_range_boundaries.c", 0, 0},
    {"probes", "floating_string_float_range_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/floating_string_float_range_boundaries.c", 0, 0},
    {"probes", "floating_string_nan_payload_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/floating_string_nan_payload_boundaries.c", 0, 0},
    {"probes", "floating_string_hex_subject_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/floating_string_hex_subject_boundaries.c", 0, 0},
    {"probes", "wchar_stream_function_pointer_state_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/wchar_stream_function_pointer_state_boundaries.c", 0, 0},
    {"probes", "uchar_function_pointer_state_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/uchar_function_pointer_state_boundaries.c", 0, 0},
    {"probes", "mbstate_target_abi_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/mbstate_target_abi_boundaries.c", 0, 0},
    {"probes", "standard_header_shared_definition_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/standard_header_shared_definition_boundaries.c", 0, 0},
    {"probes", "public_header_composition_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/public_header_composition_boundaries.c", 0, 0},
    {"probes", "inttypes_function_pointer_abi_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/inttypes_function_pointer_abi_boundaries.c", 0, 0},
    {"probes", "inttypes_wide_conversion_function_pointer_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/inttypes_wide_conversion_function_pointer_boundaries.c", 0, 0},
    {"probes", "aggregate_return_register_width_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/aggregate_return_register_width_boundaries.c", 0, 0},
    {"probes", "fenv_function_pointer_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/fenv_function_pointer_boundaries.c", 0, 0},
    {"probes", "signal_function_pointer_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/signal_function_pointer_boundaries.c", 0, 0},
    {"probes", "signal_macro_type_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/signal_macro_type_boundaries.c", 0, 0},
    {"probes", "integer_pointer_cast_aggregate_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/integer_pointer_cast_aggregate_boundaries.c", 0, 0},
    {"probes", "errno_macro_contract_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/errno_macro_contract_boundaries.c", 0, 0},
    {"probes", "aggregate_array_member_usage_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/aggregate_array_member_usage_boundaries.c", 0, 0},
    {"probes", "out_parameter_initialization_usage_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/out_parameter_initialization_usage_boundaries.c", 0, 0},
    {"probes", "read_modify_write_usage_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/read_modify_write_usage_boundaries.c", 0, 0},
    {"probes", "initialization_event_order_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/initialization_event_order_boundaries.c", 0, 0},
    {"probes", "definite_initialization_branch_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/definite_initialization_branch_boundaries.c", 0, 0},
    {"probes", "definite_initialization_switch_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/definite_initialization_switch_boundaries.c", 0, 0},
    {"probes", "definite_initialization_goto_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/definite_initialization_goto_boundaries.c", 0, 0},
    {"probes", "predefined_environment_feature_macros", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/predefined_environment_feature_macros.c", 0, 0},
    {"probes", "predefined_integer_macro_spelling_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/predefined_integer_macro_spelling_boundaries.c", 0, 0},
    {"probes", "predefined_stdc_version_type_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/predefined_stdc_version_type_boundaries.c", 0, 0},
    {"probes", "type_query_result_type_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/type_query_result_type_boundaries.c", 0, 0},
    {"probes", "vla_bound_assignment_expression", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/vla_bound_assignment_expression.c", 0, 0},
    {"probes", "array_parameter_static_bound_evaluation", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/array_parameter_static_bound_evaluation.c", 0, 0},
    {"probes", "array_parameter_inner_bound_evaluation", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/array_parameter_inner_bound_evaluation.c", 0, 0},
    {"probes", "vla_wide_runtime_bound", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/vla_wide_runtime_bound.c", 0, 0},
    {"probes", "stdlib_algorithm_function_pointer_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/stdlib_algorithm_function_pointer_boundaries.c", 0, 0},
    {"probes", "stdlib_integer_function_pointer_abi_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/stdlib_integer_function_pointer_abi_boundaries.c", 0, 0},
    {"probes", "stdlib_conversion_function_pointer_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/stdlib_conversion_function_pointer_boundaries.c", 0, 0},
    {"probes", "math_output_pointer_function_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/math_output_pointer_function_boundaries.c", 0, 0},
    {"probes", "math_unary_function_pointer_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/math_unary_function_pointer_boundaries.c", 0, 0},
    {"probes", "math_mixed_function_pointer_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/math_mixed_function_pointer_boundaries.c", 0, 0},
    {"probes", "math_classification_macro_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/math_classification_macro_boundaries.c", 0, 0},
    {"probes", "math_special_macro_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/math_special_macro_boundaries.c", 0, 0},
    {"probes", "math_evaluation_type_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/math_evaluation_type_boundaries.c", 0, 0},
    {"probes", "wide_integer_control_flow_truth_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/wide_integer_control_flow_truth_boundaries.c", 0, 0},
    {"probes", "complex_function_pointer_abi_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/complex_function_pointer_abi_boundaries.c", 0, 0},
    {"probes", "complex_construction_macro_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/complex_construction_macro_boundaries.c", 0, 0},
    {"probes", "complex_value_function_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/complex_value_function_boundaries.c", 0, 0},
    {"probes", "complex_width_variant_function_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/complex_width_variant_function_boundaries.c", 0, 0},
    {"probes", "complex_conditional_width_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/complex_conditional_width_boundaries.c", 0, 0},
    {"probes", "complex_control_flow_truth_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/complex_control_flow_truth_boundaries.c", 0, 0},
    {"probes", "pointer_complex_constant_cfg_termination_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/pointer_complex_constant_cfg_termination_boundaries.c", 0, 0},
    {"probes", "pointer_complex_constant_cfg_warning_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/pointer_complex_constant_cfg_warning_boundaries.c", 0, 0},
    {"probes", "complex_after_required_cfg_termination_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/complex_after_required_cfg_termination_boundaries.c", 0, 0},
    {"probes", "complex_after_required_cfg_warning_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/complex_after_required_cfg_warning_boundaries.c", 0, 0},
    {"probes", "known_lvalue_address_cfg_termination_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/known_lvalue_address_cfg_termination_boundaries.c", 0, 0},
    {"probes", "known_lvalue_address_cfg_warning_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/known_lvalue_address_cfg_warning_boundaries.c", 0, 0},
    {"probes", "complex_scalar_cfg_termination_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/complex_scalar_cfg_termination_boundaries.c", 0, 0},
    {"probes", "complex_scalar_cfg_warning_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/complex_scalar_cfg_warning_boundaries.c", 0, 0},
    {"probes", "complex_component_function_unit_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/complex_component_function_unit_boundaries.c", 0, 0},
    {"probes", "complex_static_initializer_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/complex_static_initializer_boundaries.c", 0, 0},
    {"probes", "complex_static_scalar_conversion_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/complex_static_scalar_conversion_boundaries.c", 0, 0},
    {"probes", "complex_runtime_scalar_conversion_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/complex_runtime_scalar_conversion_boundaries.c", 0, 0},
    {"probes", "complex_scalar_conversion_context_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/complex_scalar_conversion_context_boundaries.c", 0, 0},
    {"probes", "scalar_to_complex_call_conversion_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/scalar_to_complex_call_conversion_boundaries.c", 0, 0},
    {"probes", "scalar_to_complex_return_conversion_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/scalar_to_complex_return_conversion_boundaries.c", 0, 0},
    {"probes", "scalar_to_complex_storage_conversion_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/scalar_to_complex_storage_conversion_boundaries.c", 0, 0},
    {"probes", "complex_aggregate_conversion_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/complex_aggregate_conversion_boundaries.c", 0, 0},
    {"probes", "qualified_complex_aggregate_initializer_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/qualified_complex_aggregate_initializer_boundaries.c", 0, 0},
    {"probes", "complex_special_scalar_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/complex_special_scalar_boundaries.c", 0, 0},
    {"probes", "stdio_function_pointer_state_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/stdio_function_pointer_state_boundaries.c", 0, 0},
    {"probes", "stdio_position_type_abi_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/stdio_position_type_abi_boundaries.c", 0, 0},
    {"probes", "stdio_opaque_file_type_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/stdio_opaque_file_type_boundaries.c", 0, 0},
    {"probes", "posix_file_descriptor_function_pointer_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/posix_file_descriptor_function_pointer_boundaries.c", 0, 0},
    {"probes", "posix_offset_type_abi_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/posix_offset_type_abi_boundaries.c", 0, 0},
    {"probes", "sys_types_stat_member_identity_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/sys_types_stat_member_identity_boundaries.c", 0, 0},
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
    {"stdheader", "time_timespec_get_utc_boundaries", CASE_ASSERT_FILE, "test/fixtures/stdheader/time_timespec_get_utc_boundaries.c", 0, 0},
    {"probes", "time_macro_type_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/time_macro_type_boundaries.c", 0, 0},
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
    {"probes", "tgmath_mixed_dispatch_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/tgmath_mixed_dispatch_boundaries.c", 0, 0},
    {"probes", "tgmath_complex_dispatch_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/tgmath_complex_dispatch_boundaries.c", 0, 0},
    {"probes", "tgmath_extended_real_family_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/tgmath_extended_real_family_boundaries.c", 0, 0},
    {"probes", "complex_inverse_function_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/complex_inverse_function_boundaries.c", 0, 0},
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
    {"vla", "static_pointer_to_vla", CASE_ASSERT_FILE, "test/fixtures/vla/static_pointer_to_vla.c", 0, 0},
    {"vla", "pointer_to_multidim_vla", CASE_ASSERT_FILE, "test/fixtures/vla/pointer_to_multidim_vla.c", 0, 0},
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
    {"probes", "predefined_function_name_const_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/predefined_function_name_const_boundaries.c", 0, 0},
    {"probes", "predefined_function_name_static_initializer_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/predefined_function_name_static_initializer_boundaries.c", 0, 0},
    {"probes", "predefined_function_name_macro_unicode_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/predefined_function_name_macro_unicode_boundaries.c", 0, 0},
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
    {"probes", "aggregate_const_subobject_conversion", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/aggregate_const_subobject_conversion.c", 0, 0},
    {"probes", "address_of_string_literal_array", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/address_of_string_literal_array.c", 0, 0},
    {"probes", "update_expression_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/update_expression_boundaries.c", 0, 0},
    {"probes", "indirection_subscript_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/indirection_subscript_boundaries.c", 0, 0},
    {"probes", "explicit_cast_category_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/explicit_cast_category_boundaries.c", 0, 0},
    {"probes", "pointer_arithmetic_comparison_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/pointer_arithmetic_comparison_boundaries.c", 0, 0},
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
    {"probes", "static_character_array_string_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/static_character_array_string_boundaries.c", 0, 0},
    {"probes", "ordinary_character_array_exact_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/ordinary_character_array_exact_boundaries.c", 0, 0},
    {"probes", "global_designator", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_designator.c", 0, 0},
    {"probes", "global_const_int_expr_init", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_const_int_expr_init.c", 0, 0},
    {"probes", "global_double_scalar", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_double_scalar.c", 0, 0},
    {"probes", "global_double_array", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/global_double_array.c", 0, 0},
    {"probes", "designator_nested", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/designator_nested.c", 0, 0},
    {"probes", "struct_partial_init_zerofill", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_partial_init_zerofill.c", 0, 0},
    {"probes", "struct_pointer_array_partial_initializer_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/struct_pointer_array_partial_initializer_boundaries.c", 0, 0},
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
    {"probes", "switch_case_promoted_values", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/switch_case_promoted_values.c", 0, 0},
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
    {"probes", "generic_encoded_string_literal_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/generic_encoded_string_literal_boundaries.c", 0, 0},
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
    {"probes", "same_object_array_signature_refinement", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/same_object_array_signature_refinement.c", 0, 0},
    {"probes", "nested_incomplete_array_signature_refinement", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_incomplete_array_signature_refinement.c", 0, 0},
    {"probes", "old_style_function_definition_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/old_style_function_definition_boundaries.c", 0, 0},
    {"probes", "function_parameter_tag_scope_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/function_parameter_tag_scope_boundaries.c", 0, 0},
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
    {"probes", "integer_to_floating_conversion_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/integer_to_floating_conversion_boundaries.c", 0, 0},
    {"probes", "floating_to_integer_conversion_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/floating_to_integer_conversion_boundaries.c", 0, 0},
    {"probes", "tautological_unsigned_zero", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/tautological_unsigned_zero.c", 0, 0},
    {"probes", "identical_logical_operands", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/identical_logical_operands.c", 0, 0},
    {"probes", "logical_not_paren_trap", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/logical_not_paren_trap.c", 0, 0},
    {"probes", "pointer_integer_compare", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/pointer_integer_compare.c", 0, 0},
    {"probes", "integer_const_overflow", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/integer_const_overflow.c", 0, 0},
    {"probes", "bool_bitfield", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/bool_bitfield.c", 0, 0},
    {"probes", "anon_struct_bitfield_promote", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/anon_struct_bitfield_promote.c", 0, 0},
    {"probes", "bitfield_conditional_promotion_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/bitfield_conditional_promotion_boundaries.c", 0, 0},
    {"probes", "bitfield_derived_expression_promotion", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/bitfield_derived_expression_promotion.c", 0, 0},
    {"probes", "bitfield_derived_sizeof_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/bitfield_derived_sizeof_boundaries.c", 0, 0},
    {"probes", "bitfield_integer_promotion_operators", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/bitfield_integer_promotion_operators.c", 0, 0},
    {"probes", "bitfield_update_expression_normalization", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/bitfield_update_expression_normalization.c", 0, 0},
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
    {"probes", "shift_integer_promotion_runtime_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/shift_integer_promotion_runtime_boundaries.c", 0, 0},
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
    {"probes", "pp_if_integer_type_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/pp_if_integer_type_boundaries.c", 0, 0},
    {"probes", "pp_line_macro_arg", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/pp_line_macro_arg.c", 0, 0},
    {"probes", "include_macro_expansion_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/include_macro_expansion_boundaries.c", 0, 0},
    {"probes", "pragma_operator_macro_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/pragma_operator_macro_boundaries.c", 0, 0},
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
    {"probes", "enum_bitfield_extension_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/enum_bitfield_extension_boundaries.c", 0, 0},
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
    {"probes", "object_macro_operator_token_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/object_macro_operator_token_boundaries.c", 0, 0},
    {"probes", "macro_generated_declarator_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/macro_generated_declarator_boundaries.c", 0, 0},
    {"probes", "macro_generated_initializer_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/macro_generated_initializer_boundaries.c", 0, 0},
    {"probes", "incomplete_tag_and_nested_func_param", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/incomplete_tag_and_nested_func_param.c", 0, 0},
    {"probes", "builtin_expect_fold", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/builtin_expect_fold.c", 0, 0},
    {"probes", "sizeof_string_and_addr_of_array", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/sizeof_string_and_addr_of_array.c", 0, 0},
    {"probes", "stringize_string_literal", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/stringize_string_literal.c", 0, 0},
    {"probes", "macro_stringize_whitespace_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/macro_stringize_whitespace_boundaries.c", 0, 0},
    {"probes", "macro_stringize_character_constant_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/macro_stringize_character_constant_boundaries.c", 0, 0},
    {"probes", "line_splicing_translation_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/line_splicing_translation_boundaries.c", 0, 0},
    {"probes", "macro_redefinition_identity_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/macro_redefinition_identity_boundaries.c", 0, 0},
    {"probes", "macro_parameter_list_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/macro_parameter_list_boundaries.c", 0, 0},
    {"probes", "macro_replacement_operator_definition_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/macro_replacement_operator_definition_boundaries.c", 0, 0},
    {"probes", "macro_undef_directive_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/macro_undef_directive_boundaries.c", 0, 0},
    {"probes", "conditional_directive_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/conditional_directive_boundaries.c", 0, 0},
    {"probes", "line_directive_syntax_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/line_directive_syntax_boundaries.c", 0, 0},
    {"probes", "line_directive_maximum_value", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/line_directive_maximum_value.c", 0, 0},
    {"probes", "line_directive_include_location_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/line_directive_include_location_boundaries.c", 0, 0},
    {"probes", "predefined_date_time_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/predefined_date_time_boundaries.c", 0, 0},
    {"probes", "macro_invocation_argument_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/macro_invocation_argument_boundaries.c", 0, 0},
    {"probes", "macro_argument_prescan_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/macro_argument_prescan_boundaries.c", 0, 0},
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
    {"probes", "mixed_width_negative_runtime_conversion", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/mixed_width_negative_runtime_conversion.c", 0, 0},
    {"probes", "mixed_rank_runtime_arithmetic_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/mixed_rank_runtime_arithmetic_boundaries.c", 0, 0},
    {"probes", "cast_lvalue_mixed_rank_compound_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/cast_lvalue_mixed_rank_compound_boundaries.c", 0, 0},
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
    {"probes", "wide_string_pointer_iteration_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/wide_string_pointer_iteration_boundaries.c", 0, 0},
    {"probes", "string_array_element_type_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/string_array_element_type_boundaries.c", 0, 0},
    {"probes", "string_array_typedef_element_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/string_array_typedef_element_boundaries.c", 0, 0},
    {"probes", "string_array_typedef_unicode_inference_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/string_array_typedef_unicode_inference_boundaries.c", 0, 0},
    {"probes", "encoded_string_unicode_exact_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/encoded_string_unicode_exact_boundaries.c", 0, 0},
    {"probes", "encoded_string_embedded_null_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/encoded_string_embedded_null_boundaries.c", 0, 0},
    {"probes", "encoded_string_union_member_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/encoded_string_union_member_boundaries.c", 0, 0},
    {"probes", "string_array_compound_literal_type_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/string_array_compound_literal_type_boundaries.c", 0, 0},
    {"probes", "encoded_multidimensional_string_array_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/encoded_multidimensional_string_array_boundaries.c", 0, 0},
    {"probes", "encoded_string_row_designator_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/encoded_string_row_designator_boundaries.c", 0, 0},
    {"probes", "c11_standard_headers", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/c11_standard_headers.c", 0, 0},
    {"probes", "iso646_operator_macro_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/iso646_operator_macro_boundaries.c", 0, 0},
    {"probes", "stdalign_header_macro_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/stdalign_header_macro_boundaries.c", 0, 0},
    {"probes", "generic_long_double", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/generic_long_double.c", 0, 0},
    {"probes", "float_binary_macro_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/float_binary_macro_boundaries.c", 0, 0},
    {"probes", "float_rounds_fenv_state", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/float_rounds_fenv_state.c", 0, 0},
    {"probes", "float_long_double_macro_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/float_long_double_macro_boundaries.c", 0, 0},
    {"probes", "stdint_limit_constant_type_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/stdint_limit_constant_type_boundaries.c", 0, 0},
    {"probes", "inttypes_max_format_macro_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/inttypes_max_format_macro_boundaries.c", 0, 0},
    {"probes", "inttypes_least_fast_format_macro_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/inttypes_least_fast_format_macro_boundaries.c", 0, 0},
    {"probes", "limits_integer_macro_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/limits_integer_macro_boundaries.c", 0, 0},
    {"probes", "stdlib_macro_runtime_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/stdlib_macro_runtime_boundaries.c", 0, 0},
    {"probes", "stdio_macro_constant_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/stdio_macro_constant_boundaries.c", 0, 0},
    {"probes", "assert_reinclude_ndebug_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/assert_reinclude_ndebug_boundaries.c", 0, 0},
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
    {"probes", "file_scope_aggregate_compound_literal_subobjects", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/file_scope_aggregate_compound_literal_subobjects.c", 0, 0},
    {"probes", "file_scope_qualified_bitfield_compound_literal_subobjects", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/file_scope_qualified_bitfield_compound_literal_subobjects.c", 0, 0},
    {"probes", "file_scope_qualified_union_compound_literal_subobjects", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/file_scope_qualified_union_compound_literal_subobjects.c", 0, 0},
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
    {"probes", "static_tentative_incomplete_array_completion", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/static_tentative_incomplete_array_completion.c", 0, 0},
    {"probes", "tentative_definition_with_initializer", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/tentative_definition_with_initializer.c", 0, 0},
    {"probes", "function_parameter_adjustment_redeclaration", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/function_parameter_adjustment_redeclaration.c", 0, 0},
    {"probes", "block_scope_extern_binding", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/block_scope_extern_binding.c", 0, 0},
    {"probes", "block_scope_extern_alignment_redeclaration", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/block_scope_extern_alignment_redeclaration.c", 0, 0},
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
    {"probes", "comma_operand_conversion_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/comma_operand_conversion_boundaries.c", 0, 0},
    {"probes", "atomic_comma_operand_conversion_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/atomic_comma_operand_conversion_boundaries.c", 0, 0},
    {"probes", "array_parameter_const_adjustment", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/array_parameter_const_adjustment.c", 0, 0},
    {"probes", "block_scope_extern_array_binding", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/block_scope_extern_array_binding.c", 0, 0},
    {"probes", "prototype_void_oldstyle_definition", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/prototype_void_oldstyle_definition.c", 0, 0},
    {"probes", "parameter_shadows_function_name", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/parameter_shadows_function_name.c", 0, 0},
    {"probes", "vla_prototype_star_adjustment", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/vla_prototype_star_adjustment.c", 0, 0},
    {"probes", "nested_vla_star_prototype", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_vla_star_prototype.c", 0, 0},
    {"probes", "typedef_void_parameter_marker", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/typedef_void_parameter_marker.c", 0, 0},
    {"probes", "incomplete_parameter_prototype", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/incomplete_parameter_prototype.c", 0, 0},
    {"probes", "incomplete_return_prototype", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/incomplete_return_prototype.c", 0, 0},
    {"probes", "incomplete_record_parameter_prototype", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/incomplete_record_parameter_prototype.c", 0, 0},
    {"probes", "prototype_scope_tag_parameter_visibility", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/prototype_scope_tag_parameter_visibility.c", 0, 0},
    {"probes", "prototype_scope_type_query_visibility", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/prototype_scope_type_query_visibility.c", 0, 0},
    {"probes", "prototype_scope_repeated_enumerator_names", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/prototype_scope_repeated_enumerator_names.c", 0, 0},
    {"probes", "prototype_parameter_declaration_point", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/prototype_parameter_declaration_point.c", 0, 0},
    {"probes", "nested_prototype_parameter_scope", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_prototype_parameter_scope.c", 0, 0},
    {"probes", "enumerator_declaration_point", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/enumerator_declaration_point.c", 0, 0},
    {"probes", "typedef_object_declaration_point", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/typedef_object_declaration_point.c", 0, 0},
    {"probes", "typedef_enumerator_declaration_point", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/typedef_enumerator_declaration_point.c", 0, 0},
    {"probes", "typedef_for_scope_shadow", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/typedef_for_scope_shadow.c", 0, 0},
    {"probes", "block_scope_extern_visibility", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/block_scope_extern_visibility.c", 0, 0},
    {"probes", "block_extern_object_file_nonlinkage_shadow", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/block_extern_object_file_nonlinkage_shadow.c", 0, 0},
    {"probes", "block_function_nested_typedef_shadow", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/block_function_nested_typedef_shadow.c", 0, 0},
    {"probes", "block_function_file_nonlinkage_shadow", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/block_function_file_nonlinkage_shadow.c", 0, 0},
    {"probes", "nested_ordinary_identifier_shadow_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_ordinary_identifier_shadow_boundaries.c", 0, 0},
    {"probes", "nested_linkage_declaration_shadow_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_linkage_declaration_shadow_boundaries.c", 0, 0},
    {"probes", "incomplete_array_pointer_parameter", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/incomplete_array_pointer_parameter.c", 0, 0},
    {"probes", "array_parameter_outer_qualifiers", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/array_parameter_outer_qualifiers.c", 0, 0},
    {"probes", "conditional_void_pointer_null", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/conditional_void_pointer_null.c", 0, 0},
    {"probes", "oldstyle_prototype_default_promotion", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/oldstyle_prototype_default_promotion.c", 0, 0},
    {"probes", "declaration_qualifier_constraints", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/declaration_qualifier_constraints.c", 0, 0},
    {"probes", "restrict_object_pointer_types", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/restrict_object_pointer_types.c", 0, 0},
    {"probes", "alignas_global_static_storage", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/alignas_global_static_storage.c", 0, 0},
    {"probes", "alignas_redeclaration_consistency", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/alignas_redeclaration_consistency.c", 0, 0},
    {"probes", "alignas_direct_vla_type_name", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/alignas_direct_vla_type_name.c", 0, 0},
    {"probes", "dynamic_alignas_specifiers", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/dynamic_alignas_specifiers.c", 0, 0},
    {"probes", "dynamic_syntax_list_capacities", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/dynamic_syntax_list_capacities.c", 0, 0},
    {"probes", "goto_vla_scope_constraints", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/goto_vla_scope_constraints.c", 0, 0},
    {"probes", "goto_ice_array_scope_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/goto_ice_array_scope_boundaries.c", 0, 0},
    {"probes", "storage_class_constraints", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/storage_class_constraints.c", 0, 0},
    {"probes", "for_initializer_storage_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/for_initializer_storage_boundaries.c", 0, 0},
    {"probes", "standalone_tag_storage_class_specifiers", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/standalone_tag_storage_class_specifiers.c", 0, 0},
    {"probes", "standalone_tag_ignored_specifiers", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/standalone_tag_ignored_specifiers.c", 0, 0},
    {"probes", "type_specifier_combination_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/type_specifier_combination_boundaries.c", 0, 0},
    {"probes", "typedef_redeclaration_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/typedef_redeclaration_boundaries.c", 0, 0},
    {"probes", "function_specifier_constraints", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/function_specifier_constraints.c", 0, 0},
    {"probes", "atomic_type_constraints", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/atomic_type_constraints.c", 0, 0},
    {"probes", "atomic_qualifier_typedef_constraints", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/atomic_qualifier_typedef_constraints.c", 0, 0},
    {"probes", "static_assert_constraints", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/static_assert_constraints.c", 0, 0},
    {"probes", "static_assert_for_initializer_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/static_assert_for_initializer_boundaries.c", 0, 0},
    {"probes", "integer_constant_contexts", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/integer_constant_contexts.c", 0, 0},
    {"probes", "unsigned_integer_constant_expressions", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/unsigned_integer_constant_expressions.c", 0, 0},
    {"probes", "static_initializer_short_circuit", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/static_initializer_short_circuit.c", 0, 0},
    {"probes", "static_initializer_floating_conditions", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/static_initializer_floating_conditions.c", 0, 0},
    {"probes", "static_address_constant_expressions", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/static_address_constant_expressions.c", 0, 0},
    {"probes", "static_pointer_comparison_constants", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/static_pointer_comparison_constants.c", 0, 0},
    {"probes", "static_distinct_symbol_comparisons", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/static_distinct_symbol_comparisons.c", 0, 0},
    {"probes", "static_pointer_to_integer_constants", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/static_pointer_to_integer_constants.c", 0, 0},
    {"probes", "static_nonnull_void_pointer_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/static_nonnull_void_pointer_boundaries.c", 0, 0},
    {"probes", "enum_compatible_integer_types", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/enum_compatible_integer_types.c", 0, 0},
    {"probes", "signed_integer_constant_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/signed_integer_constant_boundaries.c", 0, 0},
    {"probes", "unsigned_long_long_constant_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/unsigned_long_long_constant_boundaries.c", 0, 0},
    {"probes", "constant_cast_sizeof_character", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/constant_cast_sizeof_character.c", 0, 0},
    {"probes", "unevaluated_type_query_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/unevaluated_type_query_boundaries.c", 0, 0},
    {"probes", "vla_sizeof_short_circuit_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/vla_sizeof_short_circuit_boundaries.c", 0, 0},
    {"probes", "vla_scope_reentry_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/vla_scope_reentry_boundaries.c", 0, 0},
    {"probes", "vla_scope_storage_release", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/vla_scope_storage_release.c", 0, 0},
    {"probes", "vla_sequential_scope_storage_release", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/vla_sequential_scope_storage_release.c", 0, 0},
    {"probes", "vla_goto_lifetime_checkpoint_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/vla_goto_lifetime_checkpoint_boundaries.c", 0, 0},
    {"probes", "vla_for_initializer_lifetime_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/vla_for_initializer_lifetime_boundaries.c", 0, 0},
    {"probes", "vla_for_init_body_lifetime_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/vla_for_init_body_lifetime_boundaries.c", 0, 0},
    {"probes", "vla_switch_exit_lifetime_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/vla_switch_exit_lifetime_boundaries.c", 0, 0},
    {"probes", "vla_switch_fallthrough_lifetime_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/vla_switch_fallthrough_lifetime_boundaries.c", 0, 0},
    {"probes", "vla_nested_switch_lifetime_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/vla_nested_switch_lifetime_boundaries.c", 0, 0},
    {"probes", "vla_if_branch_lifetime_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/vla_if_branch_lifetime_boundaries.c", 0, 0},
    {"probes", "vla_nested_goto_lifetime_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/vla_nested_goto_lifetime_boundaries.c", 0, 0},
    {"probes", "vla_return_lifetime_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/vla_return_lifetime_boundaries.c", 0, 0},
    {"probes", "vla_nested_loop_lifetime_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/vla_nested_loop_lifetime_boundaries.c", 0, 0},
    {"probes", "generic_unselected_constant_expression", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/generic_unselected_constant_expression.c", 0, 0},
    {"probes", "generic_unselected_bitfield_address", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/generic_unselected_bitfield_address.c", 0, 0},
    {"probes", "generic_null_pointer_constant_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/generic_null_pointer_constant_boundaries.c", 0, 0},
    {"probes", "generic_selected_compound_literal_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/generic_selected_compound_literal_boundaries.c", 0, 0},
    {"probes", "generic_selected_designator_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/generic_selected_designator_boundaries.c", 0, 0},
    {"probes", "generic_selected_pointer_to_vla_sizeof", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/generic_selected_pointer_to_vla_sizeof.c", 0, 0},
    {"probes", "generic_selected_register_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/generic_selected_register_boundaries.c", 0, 0},
    {"probes", "generic_selected_vla_designator_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/generic_selected_vla_designator_boundaries.c", 0, 0},
    {"probes", "generic_vla_derived_sizeof_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/generic_vla_derived_sizeof_boundaries.c", 0, 0},
    {"probes", "void_pointer_cast_null_constant_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/void_pointer_cast_null_constant_boundaries.c", 0, 0},
    {"probes", "conditional_unselected_constant_expression", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/conditional_unselected_constant_expression.c", 0, 0},
    {"probes", "statement_declaration_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/statement_declaration_boundaries.c", 0, 0},
    {"probes", "generic_selection_constraints", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/generic_selection_constraints.c", 0, 0},
    {"probes", "generic_qualified_aggregate_conversion_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/generic_qualified_aggregate_conversion_boundaries.c", 0, 0},
    {"probes", "const_function_pointer_typedef_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/const_function_pointer_typedef_boundaries.c", 0, 0},
    {"probes", "generic_qualified_scalar_cast_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/generic_qualified_scalar_cast_boundaries.c", 0, 0},
    {"probes", "generic_restrict_atomic_pointer_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/generic_restrict_atomic_pointer_boundaries.c", 0, 0},
    {"probes", "qualified_aggregate_value_context_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/qualified_aggregate_value_context_boundaries.c", 0, 0},
    {"probes", "register_aggregate_value_context_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/register_aggregate_value_context_boundaries.c", 0, 0},
    {"probes", "anonymous_member_aggregate_value_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/anonymous_member_aggregate_value_boundaries.c", 0, 0},
    {"probes", "aggregate_anonymous_enum_declarations", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/aggregate_anonymous_enum_declarations.c", 0, 0},
    {"probes", "flexible_array_value_context_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/flexible_array_value_context_boundaries.c", 0, 0},
    {"probes", "overaligned_flexible_array_layout_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/overaligned_flexible_array_layout_boundaries.c", 0, 0},
    {"probes", "bitfield_aggregate_value_abi_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/bitfield_aggregate_value_abi_boundaries.c", 0, 0},
    {"probes", "overaligned_aggregate_value_abi_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/overaligned_aggregate_value_abi_boundaries.c", 0, 0},
    {"probes", "overaligned_compound_literal_storage_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/overaligned_compound_literal_storage_boundaries.c", 0, 0},
    {"probes", "overaligned_compound_literal_recursive_identity", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/overaligned_compound_literal_recursive_identity.c", 0, 0},
    {"probes", "overaligned_vla_storage_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/overaligned_vla_storage_boundaries.c", 0, 0},
    {"probes", "overaligned_vla_recursive_frames", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/overaligned_vla_recursive_frames.c", 0, 0},
    {"probes", "overaligned_vla_scope_exit_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/overaligned_vla_scope_exit_boundaries.c", 0, 0},
    {"probes", "overaligned_vla_typedef_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/overaligned_vla_typedef_boundaries.c", 0, 0},
    {"probes", "overaligned_pointer_to_vla_typedef_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/overaligned_pointer_to_vla_typedef_boundaries.c", 0, 0},
    {"probes", "overaligned_atomic_storage_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/overaligned_atomic_storage_boundaries.c", 0, 0},
    {"probes", "complex_member_aggregate_value_abi_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/complex_member_aggregate_value_abi_boundaries.c", 0, 0},
    {"probes", "nested_array_member_aggregate_value_abi_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_array_member_aggregate_value_abi_boundaries.c", 0, 0},
    {"probes", "integer_member_aggregate_value_abi_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/integer_member_aggregate_value_abi_boundaries.c", 0, 0},
    {"probes", "const_subobject_aggregate_value_abi_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/const_subobject_aggregate_value_abi_boundaries.c", 0, 0},
    {"probes", "nested_floating_aggregate_value_abi_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_floating_aggregate_value_abi_boundaries.c", 0, 0},
    {"probes", "pointer_member_aggregate_value_abi_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/pointer_member_aggregate_value_abi_boundaries.c", 0, 0},
    {"probes", "atomic_member_aggregate_value_abi_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/atomic_member_aggregate_value_abi_boundaries.c", 0, 0},
    {"probes", "volatile_member_aggregate_value_abi_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/volatile_member_aggregate_value_abi_boundaries.c", 0, 0},
    {"probes", "volatile_discarded_access_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/volatile_discarded_access_boundaries.c", 0, 0},
    {"probes", "volatile_complex_discarded_access_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/volatile_complex_discarded_access_boundaries.c", 0, 0},
    {"probes", "volatile_aggregate_discarded_access_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/volatile_aggregate_discarded_access_boundaries.c", 0, 0},
    {"probes", "volatile_complex_assignment_result_snapshots", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/volatile_complex_assignment_result_snapshots.c", 0, 0},
    {"probes", "compound_literal_scalar_lvalue", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/compound_literal_scalar_lvalue.c", 0, 0},
    {"probes", "compound_literal_block_identity", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/compound_literal_block_identity.c", 0, 0},
    {"probes", "compound_literal_recursive_frame_identity", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/compound_literal_recursive_frame_identity.c", 0, 0},
    {"probes", "union_compound_literal_recursive_identity", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/union_compound_literal_recursive_identity.c", 0, 0},
    {"probes", "compound_literal_scalar_array_recursive_identity", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/compound_literal_scalar_array_recursive_identity.c", 0, 0},
    {"probes", "integer_compound_literal_recursive_identity", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/integer_compound_literal_recursive_identity.c", 0, 0},
    {"probes", "floating_compound_literal_recursive_identity", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/floating_compound_literal_recursive_identity.c", 0, 0},
    {"probes", "pointer_compound_literal_recursive_identity", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/pointer_compound_literal_recursive_identity.c", 0, 0},
    {"probes", "typedef_compound_literal_recursive_identity", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/typedef_compound_literal_recursive_identity.c", 0, 0},
    {"probes", "restrict_compound_literal_recursive_identity", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/restrict_compound_literal_recursive_identity.c", 0, 0},
    {"probes", "complex_compound_literal_recursive_identity", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/complex_compound_literal_recursive_identity.c", 0, 0},
    {"probes", "qualified_compound_literal_recursive_identity", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/qualified_compound_literal_recursive_identity.c", 0, 0},
    {"probes", "union_common_initial_sequence", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/union_common_initial_sequence.c", 0, 0},
    {"probes", "character_object_representation_copy", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/character_object_representation_copy.c", 0, 0},
    {"probes", "allocated_object_effective_type_copy", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/allocated_object_effective_type_copy.c", 0, 0},
    {"probes", "aggregate_initial_member_pointer_interconversion", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/aggregate_initial_member_pointer_interconversion.c", 0, 0},
    {"probes", "atomic_brace_initializer_extension", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/atomic_brace_initializer_extension.c", 0, 0},
    {"probes", "type_query_register_constraints", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/type_query_register_constraints.c", 0, 0},
    {"probes", "function_argument_return_constraints", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/function_argument_return_constraints.c", 0, 0},
    {"probes", "function_pointer_constraint_positive_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/function_pointer_constraint_positive_boundaries.c", 0, 0},
    {"probes", "function_return_qualifier_compatibility_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/function_return_qualifier_compatibility_boundaries.c", 0, 0},
    {"probes", "nested_callback_return_qualifier_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_callback_return_qualifier_boundaries.c", 0, 0},
    {"probes", "nested_callback_parameter_adjustment_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_callback_parameter_adjustment_boundaries.c", 0, 0},
    {"probes", "nested_callback_prototype_variadic_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_callback_prototype_variadic_boundaries.c", 0, 0},
    {"probes", "nested_callback_return_function_prototype_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_callback_return_function_prototype_boundaries.c", 0, 0},
    {"probes", "nested_callback_pointer_constraint_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_callback_pointer_constraint_boundaries.c", 0, 0},
    {"probes", "nested_callback_parameter_qualifier_adjustment_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_callback_parameter_qualifier_adjustment_boundaries.c", 0, 0},
    {"probes", "nested_callback_return_pointer_qualifier_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_callback_return_pointer_qualifier_boundaries.c", 0, 0},
    {"probes", "nested_callback_return_function_pointer_qualifier_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_callback_return_function_pointer_qualifier_boundaries.c", 0, 0},
    {"probes", "nested_callback_function_pointer_parameter_qualifier_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_callback_function_pointer_parameter_qualifier_boundaries.c", 0, 0},
    {"probes", "nested_callback_qualified_aggregate_return_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_callback_qualified_aggregate_return_boundaries.c", 0, 0},
    {"probes", "nested_callback_atomic_aggregate_return_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_callback_atomic_aggregate_return_boundaries.c", 0, 0},
    {"probes", "nested_callback_large_atomic_aggregate_return_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_callback_large_atomic_aggregate_return_boundaries.c", 0, 0},
    {"probes", "nested_callback_atomic_complex_return_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_callback_atomic_complex_return_boundaries.c", 0, 0},
    {"probes", "nested_callback_atomic_union_return_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_callback_atomic_union_return_boundaries.c", 0, 0},
    {"probes", "nested_callback_atomic_enum_return_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_callback_atomic_enum_return_boundaries.c", 0, 0},
    {"probes", "nested_callback_atomic_subinteger_return_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_callback_atomic_subinteger_return_boundaries.c", 0, 0},
    {"probes", "nested_callback_atomic_floating_return_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_callback_atomic_floating_return_boundaries.c", 0, 0},
    {"probes", "nested_callback_atomic_wide_integer_return_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_callback_atomic_wide_integer_return_boundaries.c", 0, 0},
    {"probes", "nested_callback_large_atomic_aggregate_parameter_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_callback_large_atomic_aggregate_parameter_boundaries.c", 0, 0},
    {"probes", "nested_callback_large_atomic_union_parameter_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_callback_large_atomic_union_parameter_boundaries.c", 0, 0},
    {"probes", "nested_callback_atomic_complex_parameter_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_callback_atomic_complex_parameter_boundaries.c", 0, 0},
    {"probes", "nested_callback_atomic_enum_parameter_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_callback_atomic_enum_parameter_boundaries.c", 0, 0},
    {"probes", "nested_callback_atomic_subinteger_parameter_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_callback_atomic_subinteger_parameter_boundaries.c", 0, 0},
    {"probes", "nested_callback_atomic_floating_parameter_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_callback_atomic_floating_parameter_boundaries.c", 0, 0},
    {"probes", "nested_callback_atomic_wide_integer_parameter_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_callback_atomic_wide_integer_parameter_boundaries.c", 0, 0},
    {"probes", "nested_callback_atomic_pointer_parameter_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_callback_atomic_pointer_parameter_boundaries.c", 0, 0},
    {"probes", "function_default_argument_promotions", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/function_default_argument_promotions.c", 0, 0},
    {"probes", "variadic_complex_arguments", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/variadic_complex_arguments.c", 0, 0},
    {"probes", "binary_operator_constraints", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/binary_operator_constraints.c", 0, 0},
    {"probes", "conditional_operator_constraints", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/conditional_operator_constraints.c", 0, 0},
    {"probes", "atomic_conditional_value_conversion_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/atomic_conditional_value_conversion_boundaries.c", 0, 0},
    {"probes", "atomic_pointer_conversion_constraints", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/atomic_pointer_conversion_constraints.c", 0, 0},
    {"probes", "array_pointer_qualification_constraints", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/array_pointer_qualification_constraints.c", 0, 0},
    {"probes", "complex_compound_assignment", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/complex_compound_assignment.c", 0, 0},
    {"probes", "atomic_compound_assignment", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/atomic_compound_assignment.c", 0, 0},
    {"probes", "integer_runtime_arithmetic_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/integer_runtime_arithmetic_boundaries.c", 0, 0},
    {"probes", "floating_runtime_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/floating_runtime_boundaries.c", 0, 0},
    {"probes", "bitfield_signedness_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/bitfield_signedness_boundaries.c", 0, 0},
    {"probes", "control_flow_enum_compound_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/control_flow_enum_compound_boundaries.c", 0, 0},
    {"probes", "call_conversion_qualifier_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/call_conversion_qualifier_boundaries.c", 0, 0},
    {"probes", "narrow_integer_return_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/narrow_integer_return_boundaries.c", 0, 0},
    {"probes", "formatted_scan_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/formatted_scan_boundaries.c", 0, 0},
    {"probes", "wide_format_scan_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/wide_format_scan_boundaries.c", 0, 0},
    {"probes", "formatted_output_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/formatted_output_boundaries.c", 0, 0},
    {"probes", "formatted_output_integer_base_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/formatted_output_integer_base_boundaries.c", 0, 0},
    {"probes", "formatted_output_string_character_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/formatted_output_string_character_boundaries.c", 0, 0},
    {"probes", "formatted_output_floating_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/formatted_output_floating_boundaries.c", 0, 0},
    {"probes", "formatted_output_narrow_wide_composition", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/formatted_output_narrow_wide_composition.c", 0, 0},
    {"probes", "formatted_output_wide_character_conversion_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/formatted_output_wide_character_conversion_boundaries.c", 0, 0},
    {"probes", "formatted_output_large_size_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/formatted_output_large_size_boundaries.c", 0, 0},
    {"probes", "variadic_function_pointer_wat_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/variadic_function_pointer_wat_boundaries.c", 0, 0},
    {"probes", "sprintf_common_formatter_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/sprintf_common_formatter_boundaries.c", 0, 0},
    {"probes", "wide_format_output_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/wide_format_output_boundaries.c", 0, 0},
    {"probes", "wide_format_integer_base_output_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/wide_format_integer_base_output_boundaries.c", 0, 0},
    {"probes", "wide_format_string_character_output_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/wide_format_string_character_output_boundaries.c", 0, 0},
    {"probes", "wide_format_floating_output_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/wide_format_floating_output_boundaries.c", 0, 0},
    {"probes", "wide_format_input_va_list_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/wide_format_input_va_list_boundaries.c", 0, 0},
    {"probes", "wide_format_stream_output_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/wide_format_stream_output_boundaries.c", 0, 0},
    {"probes", "wide_format_stream_input_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/wide_format_stream_input_boundaries.c", 0, 0},
    {"probes", "literal_vla_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/literal_vla_boundaries.c", 0, 0},
    {"probes", "stdatomic_pointer_operations", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/stdatomic_pointer_operations.c", 0, 0},
    {"probes", "stdatomic_typedef_order_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/stdatomic_typedef_order_boundaries.c", 0, 0},
    {"probes", "stdatomic_generic_object_load_store", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/stdatomic_generic_object_load_store.c", 0, 0},
    {"probes", "stdatomic_generic_object_exchange", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/stdatomic_generic_object_exchange.c", 0, 0},
    {"probes", "stdatomic_enum_generic_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/stdatomic_enum_generic_boundaries.c", 0, 0},
    {"probes", "stdatomic_enum_compatible_expected_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/stdatomic_enum_compatible_expected_boundaries.c", 0, 0},
    {"probes", "stdatomic_const_volatile_object_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/stdatomic_const_volatile_object_boundaries.c", 0, 0},
    {"probes", "stdatomic_generic_operand_conversions", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/stdatomic_generic_operand_conversions.c", 0, 0},
    {"probes", "stdatomic_memory_order_argument_constraints", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/stdatomic_memory_order_argument_constraints.c", 0, 0},
    {"probes", "stdatomic_kill_dependency_constraints", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/stdatomic_kill_dependency_constraints.c", 0, 0},
    {"probes", "stdatomic_lock_free_query_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/stdatomic_lock_free_query_boundaries.c", 0, 0},
    {"probes", "stdatomic_lock_free_macro_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/stdatomic_lock_free_macro_boundaries.c", 0, 0},
    {"probes", "stdatomic_generic_return_type_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/stdatomic_generic_return_type_boundaries.c", 0, 0},
    {"probes", "stdarg_macro_contract_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/stdarg_macro_contract_boundaries.c", 0, 0},
    {"probes", "stdarg_target_va_list_abi_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/stdarg_target_va_list_abi_boundaries.c", 0, 0},
    {"probes", "stddef_stdbool_macro_contract_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/stddef_stdbool_macro_contract_boundaries.c", 0, 0},
    {"probes", "atomic_qualified_layout", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/atomic_qualified_layout.c", 0, 0},
    {"probes", "atomic_float_complex_value_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/atomic_float_complex_value_boundaries.c", 0, 0},
    {"probes", "atomic_wide_complex_value_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/atomic_wide_complex_value_boundaries.c", 0, 0},
    {"probes", "atomic_wide_complex_compound_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/atomic_wide_complex_compound_boundaries.c", 0, 0},
    {"probes", "atomic_aggregate_value_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/atomic_aggregate_value_boundaries.c", 0, 0},
    {"probes", "atomic_discarded_object_access_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/atomic_discarded_object_access_boundaries.c", 0, 0},
    {"probes", "atomic_discarded_scalar_access_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/atomic_discarded_scalar_access_boundaries.c", 0, 0},
    {"probes", "atomic_cv_qualified_discarded_access_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/atomic_cv_qualified_discarded_access_boundaries.c", 0, 0},
    {"probes", "atomic_compound_literal_value_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/atomic_compound_literal_value_boundaries.c", 0, 0},
    {"probes", "atomic_variadic_value_promotion_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/atomic_variadic_value_promotion_boundaries.c", 0, 0},
    {"probes", "atomic_unprototyped_value_promotion_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/atomic_unprototyped_value_promotion_boundaries.c", 0, 0},
    {"probes", "atomic_enum_value_promotion_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/atomic_enum_value_promotion_boundaries.c", 0, 0},
    {"probes", "atomic_enum_signedness_expression_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/atomic_enum_signedness_expression_boundaries.c", 0, 0},
    {"probes", "atomic_enum_integer_operator_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/atomic_enum_integer_operator_boundaries.c", 0, 0},
    {"probes", "atomic_enum_qualified_storage_update_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/atomic_enum_qualified_storage_update_boundaries.c", 0, 0},
    {"probes", "atomic_enum_discarded_type_query_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/atomic_enum_discarded_type_query_boundaries.c", 0, 0},
    {"probes", "unprototyped_void_zero_signature_refinement", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/unprototyped_void_zero_signature_refinement.c", 0, 0},
    {"probes", "unprototyped_void_parameter_signature_refinement", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/unprototyped_void_parameter_signature_refinement.c", 0, 0},
    {"probes", "unprototyped_function_pointer_promotion_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/unprototyped_function_pointer_promotion_boundaries.c", 0, 0},
    {"probes", "atomic_aggregate_member_array_layout", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/atomic_aggregate_member_array_layout.c", 0, 0},
    {"probes", "atomic_union_member_array_layout", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/atomic_union_member_array_layout.c", 0, 0},
    {"probes", "union_padded_struct_initializer_offsets", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/union_padded_struct_initializer_offsets.c", 0, 0},
    {"probes", "union_bitfield_initializer_offsets", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/union_bitfield_initializer_offsets.c", 0, 0},
    {"probes", "union_mixed_relocation_initializer_offsets", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/union_mixed_relocation_initializer_offsets.c", 0, 0},
    {"probes", "union_array_active_member_offsets", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/union_array_active_member_offsets.c", 0, 0},
    {"probes", "union_array_sparse_designators", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/union_array_sparse_designators.c", 0, 0},
    {"probes", "union_repeated_designator_last_wins", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/union_repeated_designator_last_wins.c", 0, 0},
    {"probes", "aggregate_repeated_designator_last_wins", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/aggregate_repeated_designator_last_wins.c", 0, 0},
    {"probes", "aggregate_copy_designator_override", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/aggregate_copy_designator_override.c", 0, 0},
    {"probes", "deep_initializer_designator_chain", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/deep_initializer_designator_chain.c", 0, 0},
    {"probes", "incomplete_array_designator_positional", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/incomplete_array_designator_positional.c", 0, 0},
    {"probes", "union_aggregate_positional_continuation", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/union_aggregate_positional_continuation.c", 0, 0},
    {"probes", "nested_union_initializer_cursor", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_union_initializer_cursor.c", 0, 0},
    {"probes", "anonymous_union_relocation_initializer", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/anonymous_union_relocation_initializer.c", 0, 0},
    {"probes", "nested_union_activation_identity", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_union_activation_identity.c", 0, 0},
    {"probes", "nested_union_string_bitfield_activation", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_union_string_bitfield_activation.c", 0, 0},
    {"probes", "triple_nested_union_activation_reset", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/triple_nested_union_activation_reset.c", 0, 0},
    {"probes", "bitfield_overlapping_storage_units", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/bitfield_overlapping_storage_units.c", 0, 0},
    {"probes", "zero_width_unnamed_bitfield_initializer", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/zero_width_unnamed_bitfield_initializer.c", 0, 0},
    {"probes", "aligned_bitfield_record_layout_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/aligned_bitfield_record_layout_boundaries.c", 0, 0},
    {"probes", "mixed_base_bitfield_tail_layout", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/mixed_base_bitfield_tail_layout.c", 0, 0},
    {"probes", "packed_bitfield_tail_layout", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/packed_bitfield_tail_layout.c", 0, 0},
    {"probes", "hir_ir_dynamic_capacity_boundaries", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/hir_ir_dynamic_capacity_boundaries.c", 0, 0},
    {"probes", "deep_binary_expression_pipeline", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/deep_binary_expression_pipeline.c", 0, 0},
    {"probes", "deep_call_expression_pipeline", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/deep_call_expression_pipeline.c", 0, 0},
    {"probes", "deep_cast_expression_pipeline", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/deep_cast_expression_pipeline.c", 0, 0},
    {"probes", "deep_comma_expression_pipeline", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/deep_comma_expression_pipeline.c", 0, 0},
    {"probes", "deep_integer_constant_expression", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/deep_integer_constant_expression.c", 0, 0},
    {"probes", "deep_short_circuit_constant_expression", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/deep_short_circuit_constant_expression.c", 0, 0},
    {"probes", "deep_ternary_expression_pipeline", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/deep_ternary_expression_pipeline.c", 0, 0},
    {"probes", "deep_unary_constant_expression", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/deep_unary_constant_expression.c", 0, 0},
    {"probes", "nested_function_macro_expansion", CASE_ASSERT_FILE, "test/fixtures/probes_found_bugs/nested_function_macro_expansion.c", 0, 0},
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
    {"probes", "atomic_enum_aligned_record_signature_xtu",
     "test/fixtures/probes_found_bugs/atomic_enum_aligned_record_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/atomic_enum_aligned_record_signature_xtu_other.c", 42},
    {"probes", "atomic_enum_packed_record_signature_xtu",
     "test/fixtures/probes_found_bugs/atomic_enum_packed_record_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/atomic_enum_packed_record_signature_xtu_other.c", 42},
    {"probes", "atomic_enum_flexible_record_signature_xtu",
     "test/fixtures/probes_found_bugs/atomic_enum_flexible_record_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/atomic_enum_flexible_record_signature_xtu_other.c", 42},
    {"probes", "atomic_enum_incomplete_record_signature_xtu",
     "test/fixtures/probes_found_bugs/atomic_enum_incomplete_record_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/atomic_enum_incomplete_record_signature_xtu_other.c", 42},
    {"probes", "atomic_enum_anonymous_union_data_signature_xtu",
     "test/fixtures/probes_found_bugs/atomic_enum_anonymous_union_data_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/atomic_enum_anonymous_union_data_signature_xtu_other.c", 42},
    {"probes", "atomic_enum_union_function_signature_xtu",
     "test/fixtures/probes_found_bugs/atomic_enum_union_function_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/atomic_enum_union_function_signature_xtu_other.c", 42},
    {"probes", "atomic_enum_mutual_record_signature_xtu",
     "test/fixtures/probes_found_bugs/atomic_enum_mutual_record_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/atomic_enum_mutual_record_signature_xtu_other.c", 42},
    {"probes", "atomic_enum_self_record_signature_xtu",
     "test/fixtures/probes_found_bugs/atomic_enum_self_record_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/atomic_enum_self_record_signature_xtu_other.c", 42},
    {"probes", "atomic_enum_record_data_signature_xtu",
     "test/fixtures/probes_found_bugs/atomic_enum_record_data_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/atomic_enum_record_data_signature_xtu_other.c", 42},
    {"probes", "atomic_enum_record_function_signature_xtu",
     "test/fixtures/probes_found_bugs/atomic_enum_record_function_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/atomic_enum_record_function_signature_xtu_other.c", 42},
    {"probes", "atomic_enum_array_data_signature_xtu",
     "test/fixtures/probes_found_bugs/atomic_enum_array_data_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/atomic_enum_array_data_signature_xtu_other.c", 42},
    {"probes", "atomic_enum_pointer_to_array_signature_xtu",
     "test/fixtures/probes_found_bugs/atomic_enum_pointer_to_array_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/atomic_enum_pointer_to_array_signature_xtu_other.c", 42},
    {"probes", "atomic_enum_callback_data_signature_xtu",
     "test/fixtures/probes_found_bugs/atomic_enum_callback_data_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/atomic_enum_callback_data_signature_xtu_other.c", 42},
    {"probes", "atomic_enum_callback_signature_xtu",
     "test/fixtures/probes_found_bugs/atomic_enum_callback_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/atomic_enum_callback_signature_xtu_other.c", 42},
    {"probes", "atomic_enum_container_signature_xtu",
     "test/fixtures/probes_found_bugs/atomic_enum_container_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/atomic_enum_container_signature_xtu_other.c", 42},
    {"probes", "atomic_enum_function_signature_xtu",
     "test/fixtures/probes_found_bugs/atomic_enum_function_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/atomic_enum_function_signature_xtu_other.c", 42},
    {"probes", "atomic_incomplete_record_pointer_signature_xtu",
     "test/fixtures/probes_found_bugs/atomic_incomplete_record_pointer_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/atomic_incomplete_record_pointer_signature_xtu_other.c", 42},
    {"probes", "atomic_incomplete_record_wrapper_signature_xtu",
     "test/fixtures/probes_found_bugs/atomic_incomplete_record_wrapper_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/atomic_incomplete_record_wrapper_signature_xtu_other.c", 42},
    {"probes", "atomic_mutual_record_signature_xtu",
     "test/fixtures/probes_found_bugs/atomic_mutual_record_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/atomic_mutual_record_signature_xtu_other.c", 42},
    {"probes", "atomic_self_referential_record_signature_xtu",
     "test/fixtures/probes_found_bugs/atomic_self_referential_record_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/atomic_self_referential_record_signature_xtu_other.c", 42},
    {"probes", "atomic_function_pointer_data_signature_xtu",
     "test/fixtures/probes_found_bugs/atomic_function_pointer_data_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/atomic_function_pointer_data_signature_xtu_other.c", 42},
    {"probes", "atomic_function_pointer_signature_xtu",
     "test/fixtures/probes_found_bugs/atomic_function_pointer_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/atomic_function_pointer_signature_xtu_other.c", 42},
    {"probes", "atomic_callback_function_signature_xtu",
     "test/fixtures/probes_found_bugs/atomic_callback_function_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/atomic_callback_function_signature_xtu_other.c", 42},
    {"probes", "atomic_function_signature_xtu",
     "test/fixtures/probes_found_bugs/atomic_function_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/atomic_function_signature_xtu_other.c", 42},
    {"probes", "atomic_pointer_function_signature_xtu",
     "test/fixtures/probes_found_bugs/atomic_pointer_function_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/atomic_pointer_function_signature_xtu_other.c", 42},
    {"probes", "atomic_pointer_return_signature_xtu",
     "test/fixtures/probes_found_bugs/atomic_pointer_return_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/atomic_pointer_return_signature_xtu_other.c", 42},
    {"probes", "atomic_pointer_to_array_signature_xtu",
     "test/fixtures/probes_found_bugs/atomic_pointer_to_array_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/atomic_pointer_to_array_signature_xtu_other.c", 42},
    {"probes", "atomic_pointer_to_record_signature_xtu",
     "test/fixtures/probes_found_bugs/atomic_pointer_to_record_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/atomic_pointer_to_record_signature_xtu_other.c", 42},
    {"probes", "atomic_pointer_array_signature_xtu",
     "test/fixtures/probes_found_bugs/atomic_pointer_array_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/atomic_pointer_array_signature_xtu_other.c", 42},
    {"probes", "atomic_pointer_multidimensional_array_signature_xtu",
     "test/fixtures/probes_found_bugs/atomic_pointer_multidimensional_array_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/atomic_pointer_multidimensional_array_signature_xtu_other.c", 42},
    {"probes", "atomic_pointer_global_signature_xtu",
     "test/fixtures/probes_found_bugs/atomic_pointer_global_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/atomic_pointer_global_signature_xtu_other.c", 42},
    {"probes", "atomic_record_member_signature_xtu",
     "test/fixtures/probes_found_bugs/atomic_record_member_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/atomic_record_member_signature_xtu_other.c", 42},
    {"probes", "anonymous_flexible_callback_signature_xtu",
     "test/fixtures/probes_found_bugs/anonymous_flexible_callback_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/anonymous_flexible_callback_signature_xtu_other.c", 42},
    {"probes", "anonymous_flexible_function_signature_xtu",
     "test/fixtures/probes_found_bugs/anonymous_flexible_function_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/anonymous_flexible_function_signature_xtu_other.c", 42},
    {"probes", "anonymous_flexible_global_signature_xtu",
     "test/fixtures/probes_found_bugs/anonymous_flexible_global_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/anonymous_flexible_global_signature_xtu_other.c", 42},
    {"probes", "anonymous_global_record_signature_xtu",
     "test/fixtures/probes_found_bugs/anonymous_global_record_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/anonymous_global_record_signature_xtu_other.c", 42},
    {"probes", "anonymous_global_union_signature_xtu",
     "test/fixtures/probes_found_bugs/anonymous_global_union_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/anonymous_global_union_signature_xtu_other.c", 42},
    {"probes", "nested_anonymous_global_union_signature_xtu",
     "test/fixtures/probes_found_bugs/nested_anonymous_global_union_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/nested_anonymous_global_union_signature_xtu_other.c", 42},
    {"probes", "static_internal_linkage_xtu",
     "test/fixtures/probes_found_bugs/static_internal_linkage_xtu_main.c",
     "test/fixtures/probes_found_bugs/static_internal_linkage_xtu_other.c", 42},
    {"probes", "file_scope_const_external_linkage_xtu",
     "test/fixtures/probes_found_bugs/file_scope_const_external_linkage_xtu_main.c",
     "test/fixtures/probes_found_bugs/file_scope_const_external_linkage_xtu_other.c", 42},
    {"probes", "qualified_external_objects_xtu",
     "test/fixtures/probes_found_bugs/qualified_external_objects_xtu_main.c",
     "test/fixtures/probes_found_bugs/qualified_external_objects_xtu_other.c", 42},
    {"probes", "inherited_static_linkage_xtu",
     "test/fixtures/probes_found_bugs/inherited_static_linkage_xtu_main.c",
     "test/fixtures/probes_found_bugs/inherited_static_linkage_xtu_other.c", 42},
    {"probes", "extern_funcptr_xtu",
     "test/fixtures/probes_found_bugs/extern_funcptr_xtu_main.c",
     "test/fixtures/probes_found_bugs/extern_funcptr_xtu_other.c", 42},
    {"probes", "function_return_pointee_qualifier_xtu",
     "test/fixtures/probes_found_bugs/function_return_pointee_qualifier_xtu_main.c",
     "test/fixtures/probes_found_bugs/function_return_pointee_qualifier_xtu_other.c", 42},
    {"probes", "function_parameter_qualifier_adjustment_xtu",
     "test/fixtures/probes_found_bugs/function_parameter_qualifier_adjustment_xtu_main.c",
     "test/fixtures/probes_found_bugs/function_parameter_qualifier_adjustment_xtu_other.c", 42},
    {"probes", "function_parameter_multidimensional_qualifier_xtu",
     "test/fixtures/probes_found_bugs/function_parameter_multidimensional_qualifier_xtu_main.c",
     "test/fixtures/probes_found_bugs/function_parameter_multidimensional_qualifier_xtu_other.c", 42},
    {"probes", "function_parameter_function_adjustment_xtu",
     "test/fixtures/probes_found_bugs/function_parameter_function_adjustment_xtu_main.c",
     "test/fixtures/probes_found_bugs/function_parameter_function_adjustment_xtu_other.c", 42},
    {"probes", "function_parameter_callback_return_qualifier_xtu",
     "test/fixtures/probes_found_bugs/function_parameter_callback_return_qualifier_xtu_main.c",
     "test/fixtures/probes_found_bugs/function_parameter_callback_return_qualifier_xtu_other.c", 42},
    {"probes", "function_parameter_callback_return_array_qualifier_xtu",
     "test/fixtures/probes_found_bugs/function_parameter_callback_return_array_qualifier_xtu_main.c",
     "test/fixtures/probes_found_bugs/function_parameter_callback_return_array_qualifier_xtu_other.c", 42},
    {"probes", "function_parameter_callback_return_function_pointer_xtu",
     "test/fixtures/probes_found_bugs/function_parameter_callback_return_function_pointer_xtu_main.c",
     "test/fixtures/probes_found_bugs/function_parameter_callback_return_function_pointer_xtu_other.c", 42},
    {"probes", "function_parameter_atomic_array_adjustment_xtu",
     "test/fixtures/probes_found_bugs/function_parameter_atomic_array_adjustment_xtu_main.c",
     "test/fixtures/probes_found_bugs/function_parameter_atomic_array_adjustment_xtu_other.c", 42},
    {"probes", "function_parameter_nested_pointer_qualifier_xtu",
     "test/fixtures/probes_found_bugs/function_parameter_nested_pointer_qualifier_xtu_main.c",
     "test/fixtures/probes_found_bugs/function_parameter_nested_pointer_qualifier_xtu_other.c", 42},
    {"probes", "enum_compatible_function_signature_xtu",
     "test/fixtures/probes_found_bugs/enum_compatible_function_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/enum_compatible_function_signature_xtu_other.c", 42},
    {"probes", "incomplete_array_bound_signature_xtu",
     "test/fixtures/probes_found_bugs/incomplete_array_bound_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/incomplete_array_bound_signature_xtu_other.c", 42},
    {"probes", "incomplete_global_array_signature_xtu",
     "test/fixtures/probes_found_bugs/incomplete_global_array_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/incomplete_global_array_signature_xtu_other.c", 0},
    {"probes", "incomplete_global_record_pointer_signature_xtu",
     "test/fixtures/probes_found_bugs/incomplete_global_record_pointer_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/incomplete_global_record_pointer_signature_xtu_other.c", 0},
    {"probes", "incomplete_global_record_object_signature_xtu",
     "test/fixtures/probes_found_bugs/incomplete_global_record_object_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/incomplete_global_record_object_signature_xtu_other.c", 0},
    {"probes", "aligned_global_definition_xtu",
     "test/fixtures/probes_found_bugs/aligned_global_definition_xtu_main.c",
     "test/fixtures/probes_found_bugs/aligned_global_definition_xtu_other.c", 0},
    {"probes", "aligned_global_data_reloc_xtu",
     "test/fixtures/probes_found_bugs/aligned_global_data_reloc_xtu_main.c",
     "test/fixtures/probes_found_bugs/aligned_global_data_reloc_xtu_other.c", 0},
    {"probes", "global_callback_parameter_qualifier_xtu",
     "test/fixtures/probes_found_bugs/global_callback_parameter_qualifier_xtu_main.c",
     "test/fixtures/probes_found_bugs/global_callback_parameter_qualifier_xtu_other.c", 42},
    {"probes", "global_enum_integer_compatible_xtu",
     "test/fixtures/probes_found_bugs/global_enum_integer_compatible_xtu_main.c",
     "test/fixtures/probes_found_bugs/global_enum_integer_compatible_xtu_other.c", 42},
    {"probes", "named_record_signature_xtu",
     "test/fixtures/probes_found_bugs/named_record_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/named_record_signature_xtu_other.c", 42},
    {"probes", "packed_indirect_record_signature_xtu",
     "test/fixtures/probes_found_bugs/packed_indirect_record_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/packed_indirect_record_signature_xtu_other.c", 0},
    {"probes", "packed_pointer_record_signature_xtu",
     "test/fixtures/probes_found_bugs/packed_pointer_record_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/packed_pointer_record_signature_xtu_other.c", 0},
    {"probes", "packed_callback_record_signature_xtu",
     "test/fixtures/probes_found_bugs/packed_callback_record_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/packed_callback_record_signature_xtu_other.c", 0},
    {"probes", "packed_global_record_signature_xtu",
     "test/fixtures/probes_found_bugs/packed_global_record_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/packed_global_record_signature_xtu_other.c", 0},
    {"probes", "packed_global_callback_signature_xtu",
     "test/fixtures/probes_found_bugs/packed_global_callback_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/packed_global_callback_signature_xtu_other.c", 0},
    {"probes", "incomplete_callback_record_signature_xtu",
     "test/fixtures/probes_found_bugs/incomplete_callback_record_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/incomplete_callback_record_signature_xtu_other.c", 0},
    {"probes", "record_member_alignment_signature_xtu",
     "test/fixtures/probes_found_bugs/record_member_alignment_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/record_member_alignment_signature_xtu_other.c", 0},
    {"probes", "union_member_order_signature_xtu",
     "test/fixtures/probes_found_bugs/union_member_order_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/union_member_order_signature_xtu_other.c", 0},
    {"probes", "unprototyped_funcptr_xtu",
     "test/fixtures/probes_found_bugs/unprototyped_funcptr_xtu_main.c",
     "test/fixtures/probes_found_bugs/unprototyped_funcptr_xtu_other.c", 42},
    {"probes", "unprototyped_address_then_direct_call_xtu",
     "test/fixtures/probes_found_bugs/unprototyped_address_then_direct_call_xtu_main.c",
     "test/fixtures/probes_found_bugs/unprototyped_address_then_direct_call_xtu_other.c", 42},
    {"probes", "unprototyped_direct_call_then_address_xtu",
     "test/fixtures/probes_found_bugs/unprototyped_direct_call_then_address_xtu_main.c",
     "test/fixtures/probes_found_bugs/unprototyped_direct_call_then_address_xtu_other.c", 42},
    {"probes", "unprototyped_repeated_direct_call_xtu",
     "test/fixtures/probes_found_bugs/unprototyped_repeated_direct_call_xtu_main.c",
     "test/fixtures/probes_found_bugs/unprototyped_repeated_direct_call_xtu_other.c", 42},
    {"probes", "unprototyped_void_parameter_xtu",
     "test/fixtures/probes_found_bugs/unprototyped_void_parameter_xtu_main.c",
     "test/fixtures/probes_found_bugs/unprototyped_void_parameter_xtu_other.c", 42},
    {"probes", "unprototyped_void_zero_xtu",
     "test/fixtures/probes_found_bugs/unprototyped_void_zero_xtu_main.c",
     "test/fixtures/probes_found_bugs/unprototyped_void_zero_xtu_other.c", 42},
    {"probes", "unprototyped_global_callback_signature_xtu",
     "test/fixtures/probes_found_bugs/unprototyped_global_callback_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/unprototyped_global_callback_signature_xtu_other.c", 0},
    {"probes", "unprototyped_funcptr_return_xtu",
     "test/fixtures/probes_found_bugs/unprototyped_funcptr_return_xtu_main.c",
     "test/fixtures/probes_found_bugs/unprototyped_funcptr_return_xtu_other.c", 42},
    {"probes", "unprototyped_enum_funcptr_xtu",
     "test/fixtures/probes_found_bugs/unprototyped_enum_funcptr_xtu_main.c",
     "test/fixtures/probes_found_bugs/unprototyped_enum_funcptr_xtu_other.c", 42},
    {"probes", "unprototyped_atomic_funcptr_xtu",
     "test/fixtures/probes_found_bugs/unprototyped_atomic_funcptr_xtu_main.c",
     "test/fixtures/probes_found_bugs/unprototyped_atomic_funcptr_xtu_other.c", 42},
    {"probes", "unprototyped_parameter_categories_xtu",
     "test/fixtures/probes_found_bugs/unprototyped_parameter_categories_xtu_main.c",
     "test/fixtures/probes_found_bugs/unprototyped_parameter_categories_xtu_other.c", 42},
    {"probes", "atomic_aggregate_callback_xtu",
     "test/fixtures/probes_found_bugs/atomic_aggregate_callback_xtu_main.c",
     "test/fixtures/probes_found_bugs/atomic_aggregate_callback_xtu_other.c", 42},
    {"probes", "nested_atomic_aggregate_callback_parameter_xtu",
     "test/fixtures/probes_found_bugs/nested_atomic_aggregate_callback_parameter_xtu_main.c",
     "test/fixtures/probes_found_bugs/nested_atomic_aggregate_callback_parameter_xtu_other.c", 42},
    {"probes", "nested_atomic_union_complex_callback_parameter_xtu",
     "test/fixtures/probes_found_bugs/nested_atomic_union_complex_callback_parameter_xtu_main.c",
     "test/fixtures/probes_found_bugs/nested_atomic_union_complex_callback_parameter_xtu_other.c", 42},
    {"probes", "nested_atomic_union_complex_callback_result_xtu",
     "test/fixtures/probes_found_bugs/nested_atomic_union_complex_callback_result_xtu_main.c",
     "test/fixtures/probes_found_bugs/nested_atomic_union_complex_callback_result_xtu_other.c", 42},
    {"probes", "nested_atomic_union_complex_callback_factory_xtu",
     "test/fixtures/probes_found_bugs/nested_atomic_union_complex_callback_factory_xtu_main.c",
     "test/fixtures/probes_found_bugs/nested_atomic_union_complex_callback_factory_xtu_other.c", 42},
    {"probes", "atomic_union_complex_callback_factory_data_xtu",
     "test/fixtures/probes_found_bugs/atomic_union_complex_callback_factory_data_xtu_main.c",
     "test/fixtures/probes_found_bugs/atomic_union_complex_callback_factory_data_xtu_other.c", 42},
    {"probes", "atomic_union_complex_callback_factory_container_xtu",
     "test/fixtures/probes_found_bugs/atomic_union_complex_callback_factory_container_xtu_main.c",
     "test/fixtures/probes_found_bugs/atomic_union_complex_callback_factory_container_xtu_other.c", 42},
    {"probes", "aggregate_value_abi_xtu_boundaries",
     "test/fixtures/probes_found_bugs/aggregate_value_abi_xtu_boundaries_main.c",
     "test/fixtures/probes_found_bugs/aggregate_value_abi_xtu_boundaries_other.c", 42},
    {"probes", "overaligned_callback_value_abi_xtu",
     "test/fixtures/probes_found_bugs/overaligned_callback_value_abi_xtu_main.c",
     "test/fixtures/probes_found_bugs/overaligned_callback_value_abi_xtu_other.c", 0},
    {"probes", "overaligned_variadic_value_abi_xtu",
     "test/fixtures/probes_found_bugs/overaligned_variadic_value_abi_xtu_main.c",
     "test/fixtures/probes_found_bugs/overaligned_variadic_value_abi_xtu_other.c", 0},
    {"probes", "overaligned_named_parameter_variadic_abi_xtu",
     "test/fixtures/probes_found_bugs/overaligned_named_parameter_variadic_abi_xtu_main.c",
     "test/fixtures/probes_found_bugs/overaligned_named_parameter_variadic_abi_xtu_other.c", 0},
    {"probes", "overaligned_va_list_forwarding_xtu",
     "test/fixtures/probes_found_bugs/overaligned_va_list_forwarding_xtu_main.c",
     "test/fixtures/probes_found_bugs/overaligned_va_list_forwarding_xtu_other.c", 0},
    {"probes", "overaligned_variadic_return_abi_xtu",
     "test/fixtures/probes_found_bugs/overaligned_variadic_return_abi_xtu_main.c",
     "test/fixtures/probes_found_bugs/overaligned_variadic_return_abi_xtu_other.c", 0},
    {"probes", "overaligned_vla_parameter_qualifier_xtu",
     "test/fixtures/probes_found_bugs/overaligned_vla_parameter_qualifier_xtu_main.c",
     "test/fixtures/probes_found_bugs/overaligned_vla_parameter_qualifier_xtu_other.c", 0},
    {"probes", "overaligned_vla_global_callback_xtu",
     "test/fixtures/probes_found_bugs/overaligned_vla_global_callback_xtu_main.c",
     "test/fixtures/probes_found_bugs/overaligned_vla_global_callback_xtu_other.c", 0},
    {"probes", "overaligned_vla_callback_factory_xtu",
     "test/fixtures/probes_found_bugs/overaligned_vla_callback_factory_xtu_main.c",
     "test/fixtures/probes_found_bugs/overaligned_vla_callback_factory_xtu_other.c", 0},
    {"probes", "overaligned_vla_callback_aggregate_return_xtu",
     "test/fixtures/probes_found_bugs/overaligned_vla_callback_aggregate_return_xtu_main.c",
     "test/fixtures/probes_found_bugs/overaligned_vla_callback_aggregate_return_xtu_other.c", 0},
    {"probes", "floating_rank_signature_xtu",
     "test/fixtures/probes_found_bugs/floating_rank_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/floating_rank_signature_xtu_other.c", 0},
    {"probes", "integer_rank_signature_xtu",
     "test/fixtures/probes_found_bugs/integer_rank_signature_xtu_main.c",
     "test/fixtures/probes_found_bugs/integer_rank_signature_xtu_other.c", 0},
    {"probes", "thread_local_xtu_boundaries",
     "test/fixtures/probes_found_bugs/thread_local_xtu_boundaries_main.c",
     "test/fixtures/probes_found_bugs/thread_local_xtu_boundaries_other.c", 42},
};

static const compile_fail_case_t compile_fail_cases[] = {
    {"cast_struct_from_nonscalar_rejected",
     "fixture:test/fixtures/should_reject/cast_struct_from_nonscalar.c",
     "[cast] struct 値へのキャストは未対応です（型不整合）"},
    {"cast_pointer_to_double_rejected",
     "fixture:test/fixtures/should_reject/cast_pointer_to_double.c",
     "E3064"},
    {"cast_double_to_pointer_rejected",
     "fixture:test/fixtures/should_reject/cast_double_to_pointer.c",
     "E3064"},
    {"cast_pointer_to_complex_rejected",
     "fixture:test/fixtures/should_reject/cast_pointer_to_complex.c",
     "E3064"},
    {"cast_complex_to_pointer_rejected",
     "fixture:test/fixtures/should_reject/cast_complex_to_pointer.c",
     "E3064"},
    {"cast_void_expression_to_integer_rejected",
     "fixture:test/fixtures/should_reject/cast_void_expression_to_integer.c",
     "E3064"},
    {"const_assign_rejected",
     "fixture:test/fixtures/should_reject/assign_const_scalar.c",
     "E3077"},
    {"assign_string_to_int_rejected",
     "fixture:test/fixtures/should_reject/assign_string_to_int.c",
     "E3099"},
    {"assign_int_to_ptr_implicit_rejected",
     "fixture:test/fixtures/should_reject/assign_int_to_ptr_implicit.c",
     "E3099"},
    {"pointer_initializer_binary_expression_rejected",
     "fixture:test/fixtures/should_reject/"
     "pointer_initializer_binary_expression.c",
     "E3099"},
    {"pointer_initializer_conditional_expression_rejected",
     "fixture:test/fixtures/should_reject/"
     "pointer_initializer_conditional_expression.c",
     "E3099"},
    {"pointer_initializer_comma_expression_rejected",
     "fixture:test/fixtures/should_reject/"
     "pointer_initializer_comma_expression.c",
     "E3099"},
    {"assign_struct_to_int_rejected",
     "fixture:test/fixtures/should_reject/assign_struct_to_int.c",
     "E3099"},
    {"assign_void_func_to_int_rejected",
     "fixture:test/fixtures/should_reject/assign_void_func_to_int.c",
     "E3099"},
    {"atomic_pointer_compound_assignment_rejected",
     "fixture:test/fixtures/should_reject/atomic_pointer_compound_assignment.c",
     "E3099"},
    {"atomic_struct_member_access_rejected",
     "fixture:test/fixtures/should_reject/atomic_struct_member_access.c",
     "E3064"},
    {"atomic_union_member_access_rejected",
     "fixture:test/fixtures/should_reject/atomic_union_member_access.c",
     "E3064"},
    {"atomic_struct_pointer_member_access_rejected",
     "fixture:test/fixtures/should_reject/"
     "atomic_struct_pointer_member_access.c",
     "E3064"},
    {"void_value_used_rejected",
     "fixture:test/fixtures/should_reject/void_value_used.c",
     "E3099"},
    {"struct_with_const_member_assignment_rejected",
     "fixture:test/fixtures/should_reject/"
     "struct_with_const_member_assignment.c",
     "E3077"},
    {"nested_struct_with_const_member_assignment_rejected",
     "fixture:test/fixtures/should_reject/"
     "nested_struct_with_const_member_assignment.c",
     "E3077"},
    {"struct_with_const_array_member_assignment_rejected",
     "fixture:test/fixtures/should_reject/"
     "struct_with_const_array_member_assignment.c",
     "E3077"},
    {"union_with_const_member_assignment_rejected",
     "fixture:test/fixtures/should_reject/"
     "union_with_const_member_assignment.c",
     "E3077"},
    {"typedef_const_member_assignment_rejected",
     "fixture:test/fixtures/should_reject/"
     "typedef_const_member_assignment.c",
     "E3077"},
    {"anonymous_union_const_member_assignment_rejected",
     "fixture:test/fixtures/should_reject/"
     "anonymous_union_const_member_assignment.c",
     "E3077"},
    {"const_pointer_member_assignment_rejected",
     "fixture:test/fixtures/should_reject/const_pointer_member_assignment.c",
     "E3077"},
    {"const_struct_member_assign_rejected",
     "fixture:test/fixtures/should_reject/assign_member_of_const_struct.c",
     "E3077"},
    {"const_struct_array_member_assign_rejected",
     "fixture:test/fixtures/should_reject/assign_member_of_const_struct_array.c",
     "E3077"},
    {"global_const_struct_array_member_assign_rejected",
     "fixture:test/fixtures/should_reject/assign_member_of_global_const_struct_array.c",
     "E3077"},
    {"const_struct_pointer_to_array_member_assign_rejected",
     "fixture:test/fixtures/should_reject/assign_member_through_const_struct_array_pointer.c",
     "E3077"},
    {"const_struct_pointer_member_assign_rejected",
     "fixture:test/fixtures/should_reject/assign_member_through_const_struct_pointer.c",
     "E3077"},
    {"const_struct_func_ret_pointer_member_assign_rejected",
     "fixture:test/fixtures/should_reject/assign_member_through_const_struct_pointer_return.c",
     "E3077"},
    {"const_struct_func_ret_pointer_to_array_member_assign_rejected",
     "fixture:test/fixtures/should_reject/assign_member_through_const_struct_array_pointer_return.c",
     "E3077"},
    {"const_struct_funcptr_ret_pointer_member_assign_rejected",
     "fixture:test/fixtures/should_reject/assign_member_through_const_struct_pointer_callback.c",
     "E3077"},
    {"const_struct_funcptr_ret_pointer_to_array_member_assign_rejected",
     "fixture:test/fixtures/should_reject/assign_member_through_const_struct_array_pointer_callback.c",
     "E3077"},
    {"const_qual_discard_assign_rejected",
     "fixture:test/fixtures/should_reject/assignment_discards_const_pointer.c",
     "E3078"},
    {"incompatible_prototype_oldstyle_definition_rejected",
     "fixture:test/fixtures/should_reject/"
     "incompatible_prototype_oldstyle_definition.c",
     "E3064"},
    {"oldstyle_float_prototype_rejected",
     "fixture:test/fixtures/should_reject/oldstyle_float_prototype.c",
     "E3064"},
    {"upgraded_prototype_argument_count_rejected",
     "fixture:test/fixtures/should_reject/"
     "upgraded_prototype_argument_count.c",
     "E3103"},
    {"vla_star_outside_parameter_rejected",
     "fixture:test/fixtures/should_reject/vla_star_outside_parameter.c",
     "E3064"},
    {"vla_static_star_rejected",
     "fixture:test/fixtures/should_reject/vla_static_star.c",
     "E3064"},
    {"block_static_vla_object_rejected",
     "fixture:test/fixtures/should_reject/static_local_vla.c",
     "E3064"},
    {"vla_initializer_rejected",
     "fixture:test/fixtures/should_reject/vla_initializer.c",
     "E3064"},
    {"local_incomplete_array_without_initializer_rejected",
     "fixture:test/fixtures/should_reject/"
     "local_incomplete_array_without_initializer.c",
     "E3064"},
    {"local_incomplete_array_scalar_initializer_rejected",
     "fixture:test/fixtures/should_reject/"
     "local_incomplete_array_scalar_initializer.c",
     "E3099"},
    {"local_incomplete_array_array_initializer_rejected",
     "fixture:test/fixtures/should_reject/"
     "local_incomplete_array_array_initializer.c",
     "E3099"},
    {"static_local_incomplete_array_without_initializer_rejected",
     "fixture:test/fixtures/should_reject/"
     "static_local_incomplete_array_without_initializer.c",
     "E3064"},
    {"static_local_incomplete_array_scalar_initializer_rejected",
     "fixture:test/fixtures/should_reject/"
     "static_local_incomplete_array_scalar_initializer.c",
     "E3099"},
    {"static_local_incomplete_array_array_initializer_rejected",
     "fixture:test/fixtures/should_reject/"
     "static_local_incomplete_array_array_initializer.c",
     "E3099"},
    {"local_fixed_array_scalar_initializer_rejected",
     "fixture:test/fixtures/should_reject/"
     "local_fixed_array_scalar_initializer.c",
     "E3099"},
    {"local_fixed_array_array_initializer_rejected",
     "fixture:test/fixtures/should_reject/"
     "local_fixed_array_array_initializer.c",
     "E3099"},
    {"static_local_fixed_array_scalar_initializer_rejected",
     "fixture:test/fixtures/should_reject/"
     "static_local_fixed_array_scalar_initializer.c",
     "E3099"},
    {"static_local_fixed_array_array_initializer_rejected",
     "fixture:test/fixtures/should_reject/"
     "static_local_fixed_array_array_initializer.c",
     "E3099"},
    {"file_scope_fixed_array_scalar_initializer_rejected",
     "fixture:test/fixtures/should_reject/"
     "file_scope_fixed_array_scalar_initializer.c",
     "E3099"},
    {"file_scope_fixed_array_array_initializer_rejected",
     "fixture:test/fixtures/should_reject/"
     "file_scope_fixed_array_array_initializer.c",
     "E3099"},
    {"file_scope_incomplete_array_scalar_initializer_rejected",
     "fixture:test/fixtures/should_reject/"
     "file_scope_incomplete_array_scalar_initializer.c",
     "E3064"},
    {"file_scope_incomplete_array_array_initializer_rejected",
     "fixture:test/fixtures/should_reject/"
     "file_scope_incomplete_array_array_initializer.c",
     "E3064"},
    {"local_fixed_array_mismatched_compound_literal_initializer_rejected",
     "fixture:test/fixtures/should_reject/"
     "local_fixed_array_mismatched_compound_literal_initializer.c",
     "E3099"},
    {"assignment_fixed_array_rejected",
     "fixture:test/fixtures/should_reject/assignment_fixed_array.c",
     "E3098"},
    {"assignment_string_to_array_rejected",
     "fixture:test/fixtures/should_reject/assignment_string_to_array.c",
     "E3098"},
    {"assignment_incomplete_array_rejected",
     "fixture:test/fixtures/should_reject/assignment_incomplete_array.c",
     "E3098"},
    {"assignment_to_assignment_result_rejected",
     "fixture:test/fixtures/should_reject/"
     "assignment_to_assignment_result.c",
     "E3062"},
    {"assignment_to_comma_result_rejected",
     "fixture:test/fixtures/should_reject/assignment_to_comma_result.c",
     "E3062"},
    {"assignment_to_conditional_result_rejected",
     "fixture:test/fixtures/should_reject/"
     "assignment_to_conditional_result.c",
     "E3062"},
    {"compound_assignment_to_comma_result_rejected",
     "fixture:test/fixtures/should_reject/"
     "compound_assignment_to_comma_result.c",
     "E3062"},
    {"compound_assignment_to_conditional_result_rejected",
     "fixture:test/fixtures/should_reject/"
     "compound_assignment_to_conditional_result.c",
     "E3062"},
    {"excess_scalar_initializer_file_scope_rejected",
     "fixture:test/fixtures/should_reject/"
     "excess_scalar_initializer_file_scope.c",
     "E3025"},
    {"excess_scalar_initializer_local_rejected",
     "fixture:test/fixtures/should_reject/"
     "excess_scalar_initializer_local.c",
     "E3025"},
    {"excess_scalar_initializer_static_local_rejected",
     "fixture:test/fixtures/should_reject/"
     "excess_scalar_initializer_static_local.c",
     "E3025"},
    {"excess_scalar_compound_literal_initializer_rejected",
     "fixture:test/fixtures/should_reject/"
     "excess_scalar_compound_literal_initializer.c",
     "E3025"},
    {"scalar_initializer_excess_braces_file_scope_rejected",
     "fixture:test/fixtures/should_reject/scalar_initializer_excess_braces_file_scope.c",
     "E3025"},
    {"scalar_initializer_excess_braces_local_rejected",
     "fixture:test/fixtures/should_reject/scalar_initializer_excess_braces_local.c",
     "E3025"},
    {"scalar_initializer_excess_braces_static_local_rejected",
     "fixture:test/fixtures/should_reject/scalar_initializer_excess_braces_static_local.c",
     "E3025"},
    {"scalar_compound_literal_excess_braces_rejected",
     "fixture:test/fixtures/should_reject/scalar_compound_literal_excess_braces.c",
     "E3025"},
    {"pointer_initializer_excess_braces_file_scope_rejected",
     "fixture:test/fixtures/should_reject/pointer_initializer_excess_braces_file_scope.c",
     "E3025"},
    {"scalar_initializer_recursive_excess_braces_rejected",
     "fixture:test/fixtures/should_reject/scalar_initializer_recursive_excess_braces.c",
     "E3025"},
    {"excess_array_initializer_file_scope_rejected",
     "fixture:test/fixtures/should_reject/"
     "excess_array_initializer_file_scope.c",
     "E3027"},
    {"excess_array_initializer_local_rejected",
     "fixture:test/fixtures/should_reject/"
     "excess_array_initializer_local.c",
     "E3027"},
    {"excess_array_initializer_static_local_rejected",
     "fixture:test/fixtures/should_reject/"
     "excess_array_initializer_static_local.c",
     "E3027"},
    {"excess_array_compound_literal_initializer_rejected",
     "fixture:test/fixtures/should_reject/"
     "excess_array_compound_literal_initializer.c",
     "E3027"},
    {"excess_struct_initializer_file_scope_rejected",
     "fixture:test/fixtures/should_reject/"
     "excess_struct_initializer_file_scope.c",
     "E3032"},
    {"excess_struct_initializer_local_rejected",
     "fixture:test/fixtures/should_reject/"
     "excess_struct_initializer_local.c",
     "E3032"},
    {"excess_struct_initializer_static_local_rejected",
     "fixture:test/fixtures/should_reject/"
     "excess_struct_initializer_static_local.c",
     "E3032"},
    {"excess_struct_compound_literal_initializer_rejected",
     "fixture:test/fixtures/should_reject/"
     "excess_struct_compound_literal_initializer.c",
     "E3032"},
    {"excess_union_initializer_file_scope_rejected",
     "fixture:test/fixtures/should_reject/"
     "excess_union_initializer_file_scope.c",
     "E3036"},
    {"excess_union_initializer_local_rejected",
     "fixture:test/fixtures/should_reject/"
     "excess_union_initializer_local.c",
     "E3036"},
    {"excess_union_initializer_static_local_rejected",
     "fixture:test/fixtures/should_reject/"
     "excess_union_initializer_static_local.c",
     "E3036"},
    {"excess_union_compound_literal_initializer_rejected",
     "fixture:test/fixtures/should_reject/"
     "excess_union_compound_literal_initializer.c",
     "E3036"},
    {"block_extern_vla_object_rejected",
     "fixture:test/fixtures/should_reject/extern_vla_object.c",
     "E3064"},
    {"block_extern_pointer_to_vla_rejected",
     "fixture:test/fixtures/should_reject/extern_pointer_to_vla.c",
     "E3064"},
    {"goto_into_vla_same_block_rejected",
     "fixture:test/fixtures/should_reject/goto_into_vla_same_block.c",
     "E3064"},
    {"goto_into_vla_nested_block_rejected",
     "fixture:test/fixtures/should_reject/goto_into_vla_nested_block.c",
     "E3064"},
    {"goto_into_vla_sibling_block_rejected",
     "fixture:test/fixtures/should_reject/goto_into_vla_sibling_block.c",
     "E3064"},
    {"goto_into_pointer_to_vla_scope_rejected",
     "fixture:test/fixtures/should_reject/goto_into_pointer_to_vla_scope.c",
     "E3064"},
    {"goto_into_vla_typedef_scope_rejected",
     "fixture:test/fixtures/should_reject/goto_into_vla_typedef_scope.c",
     "E3064"},
    {"goto_into_for_vla_scope_rejected",
     "fixture:test/fixtures/should_reject/goto_into_for_vla_scope.c",
     "E3064"},
    {"switch_case_into_vla_scope_rejected",
     "fixture:test/fixtures/should_reject/switch_case_into_vla_scope.c",
     "E3064"},
    {"switch_default_into_vla_scope_rejected",
     "fixture:test/fixtures/should_reject/switch_default_into_vla_scope.c",
     "E3064"},
    {"switch_case_into_pointer_to_vla_scope_rejected",
     "fixture:test/fixtures/should_reject/switch_case_into_pointer_to_vla_scope.c",
     "E3064"},
    {"switch_case_into_vla_typedef_scope_rejected",
     "fixture:test/fixtures/should_reject/switch_case_into_vla_typedef_scope.c",
     "E3064"},
    {"switch_nested_case_into_vla_scope_rejected",
     "fixture:test/fixtures/should_reject/switch_nested_case_into_vla_scope.c",
     "E3064"},
    {"goto_into_typedef_pointer_to_vla_scope_rejected",
     "fixture:test/fixtures/should_reject/goto_into_typedef_pointer_to_vla_scope.c",
     "E3064"},
    {"switch_case_into_typedef_pointer_to_vla_scope_rejected",
     "fixture:test/fixtures/should_reject/switch_case_into_typedef_pointer_to_vla_scope.c",
     "E3064"},
    {"switch_case_into_for_vla_scope_rejected",
     "fixture:test/fixtures/should_reject/switch_case_into_for_vla_scope.c",
     "E3064"},
    {"switch_default_into_for_vla_scope_rejected",
     "fixture:test/fixtures/should_reject/switch_default_into_for_vla_scope.c",
     "E3064"},
    {"switch_duplicate_case_unsigned_conversion_rejected",
     "fixture:test/fixtures/should_reject/switch_duplicate_case_unsigned_conversion.c",
     "E3060"},
    {"switch_duplicate_case_signed_conversion_rejected",
     "fixture:test/fixtures/should_reject/switch_duplicate_case_signed_conversion.c",
     "E3060"},
    {"switch_duplicate_case_promoted_uchar_rejected",
     "fixture:test/fixtures/should_reject/switch_duplicate_case_promoted_uchar.c",
     "E3060"},
    {"bitfield_derived_switch_duplicate_case_rejected",
     "fixture:test/fixtures/should_reject/"
     "bitfield_derived_switch_duplicate_case.c",
     "E3060"},
    {"switch_duplicate_case_wrapped_expression_rejected",
     "fixture:test/fixtures/should_reject/switch_duplicate_case_wrapped_expression.c",
     "E3060"},
    {"switch_duplicate_case_unsigned_shift_rejected",
     "fixture:test/fixtures/should_reject/switch_duplicate_case_unsigned_shift.c",
     "E3060"},
    {"switch_duplicate_case_unsigned_comparison_rejected",
     "fixture:test/fixtures/should_reject/switch_duplicate_case_unsigned_comparison.c",
     "E3060"},
    {"switch_duplicate_case_positive_enum_conversion_rejected",
     "fixture:test/fixtures/should_reject/switch_duplicate_case_positive_enum_conversion.c",
     "E3060"},
    {"switch_duplicate_case_negative_enum_conversion_rejected",
     "fixture:test/fixtures/should_reject/switch_duplicate_case_negative_enum_conversion.c",
     "E3060"},
    {"switch_duplicate_case_unsigned_long_conversion_rejected",
     "fixture:test/fixtures/should_reject/switch_duplicate_case_unsigned_long_conversion.c",
     "E3060"},
    {"static_initializer_evaluated_logical_and_divzero_rejected",
     "fixture:test/fixtures/should_reject/static_initializer_evaluated_logical_and_divzero.c",
     "E3116"},
    {"static_initializer_evaluated_logical_or_divzero_rejected",
     "fixture:test/fixtures/should_reject/static_initializer_evaluated_logical_or_divzero.c",
     "E3116"},
    {"static_initializer_evaluated_conditional_true_divzero_rejected",
     "fixture:test/fixtures/should_reject/static_initializer_evaluated_conditional_true_divzero.c",
     "E3116"},
    {"static_initializer_evaluated_conditional_false_divzero_rejected",
     "fixture:test/fixtures/should_reject/static_initializer_evaluated_conditional_false_divzero.c",
     "E3116"},
    {"static_array_initializer_evaluated_logical_divzero_rejected",
     "fixture:test/fixtures/should_reject/static_array_initializer_evaluated_logical_divzero.c",
     "E3116"},
    {"static_local_initializer_evaluated_logical_divzero_rejected",
     "fixture:test/fixtures/should_reject/static_local_initializer_evaluated_logical_divzero.c",
     "E3116"},
    {"const_array_parameter_reassignment_rejected",
     "fixture:test/fixtures/should_reject/const_array_parameter_reassignment.c",
     "E3077"},
    {"const_atomic_array_parameter_reassignment_rejected",
     "fixture:test/fixtures/should_reject/const_atomic_array_parameter_reassignment.c",
     "E3077"},
    {"static_const_restrict_array_parameter_reassignment_rejected",
     "fixture:test/fixtures/should_reject/static_const_restrict_array_parameter_reassignment.c",
     "E3077"},
    {"const_volatile_array_parameter_reassignment_rejected",
     "fixture:test/fixtures/should_reject/const_volatile_array_parameter_reassignment.c",
     "E3077"},
    {"restrict_nonpointer_prefix_rejected",
     "fixture:test/fixtures/should_reject/restrict_nonpointer_prefix.c",
     "E3064"},
    {"restrict_nonpointer_parameter_rejected",
     "fixture:test/fixtures/should_reject/restrict_nonpointer_parameter.c",
     "E3064"},
    {"restrict_nonpointer_parameter_declaration_rejected",
     "fixture:test/fixtures/should_reject/"
     "restrict_nonpointer_parameter_declaration.c",
     "E3064"},
    {"restrict_function_pointer_rejected",
     "fixture:test/fixtures/should_reject/restrict_function_pointer.c",
     "E3064"},
    {"restrict_typedef_function_pointer_rejected",
     "fixture:test/fixtures/should_reject/restrict_typedef_function_pointer.c",
     "E3064"},
    {"restrict_typedef_function_pointer_prefix_rejected",
     "fixture:test/fixtures/should_reject/"
     "restrict_typedef_function_pointer_prefix.c",
     "E3064"},
    {"restrict_function_pointer_type_name_rejected",
     "fixture:test/fixtures/should_reject/restrict_function_pointer_type_name.c",
     "E3064"},
    {"restrict_typedef_function_pointer_type_name_rejected",
     "fixture:test/fixtures/should_reject/"
     "restrict_typedef_function_pointer_type_name.c",
     "E3064"},
    {"restrict_builtin_scalar_type_name_rejected",
     "fixture:test/fixtures/should_reject/restrict_builtin_scalar_type_name.c",
     "E3117"},
    {"restrict_typedef_scalar_type_name_rejected",
     "fixture:test/fixtures/should_reject/restrict_typedef_scalar_type_name.c",
     "E3117"},
    {"restrict_function_pointer_global_rejected",
     "fixture:test/fixtures/should_reject/restrict_function_pointer_global.c",
     "E3064"},
    {"restrict_function_pointer_parameter_rejected",
     "fixture:test/fixtures/should_reject/restrict_function_pointer_parameter.c",
     "E3064"},
    {"restrict_function_pointer_parameter_declaration_rejected",
     "fixture:test/fixtures/should_reject/"
     "restrict_function_pointer_parameter_declaration.c",
     "E3064"},
    {"restrict_function_pointer_member_rejected",
     "fixture:test/fixtures/should_reject/restrict_function_pointer_member.c",
     "E3064"},
    {"restrict_function_pointer_typedef_declaration_rejected",
     "fixture:test/fixtures/should_reject/restrict_function_pointer_typedef.c",
     "E3064"},
    {"atomic_restrict_pointer_rejected",
     "fixture:test/fixtures/should_reject/atomic_restrict_pointer.c",
     "E3064"},
    {"restrict_atomic_pointer_rejected",
     "fixture:test/fixtures/should_reject/restrict_atomic_pointer.c",
     "E3064"},
    {"atomic_restrict_nested_pointer_rejected",
     "fixture:test/fixtures/should_reject/atomic_restrict_nested_pointer.c",
     "E3064"},
    {"atomic_restrict_typedef_pointer_rejected",
     "fixture:test/fixtures/should_reject/atomic_restrict_typedef_pointer.c",
     "E3064"},
    {"atomic_restrict_pointer_type_name_rejected",
     "fixture:test/fixtures/should_reject/atomic_restrict_pointer_type_name.c",
     "E3117"},
    {"atomic_restrict_pointer_parameter_rejected",
     "fixture:test/fixtures/should_reject/atomic_restrict_pointer_parameter.c",
     "E3064"},
    {"atomic_specifier_restrict_pointer_rejected",
     "fixture:test/fixtures/should_reject/atomic_specifier_restrict_pointer.c",
     "E3064"},
    {"array_qualifier_outside_parameter_rejected",
     "fixture:test/fixtures/should_reject/array_qualifier_outside_parameter.c",
     "E3064"},
    {"array_static_outside_parameter_rejected",
     "fixture:test/fixtures/should_reject/array_static_bound_outside_parameter.c",
     "E3064"},
    {"array_parameter_static_without_bound_rejected",
     "fixture:test/fixtures/should_reject/array_parameter_static_without_bound.c",
     "E3064"},
    {"array_parameter_duplicate_static_rejected",
     "fixture:test/fixtures/should_reject/array_parameter_duplicate_static.c",
     "E3064"},
    {"array_parameter_nested_static_rejected",
     "fixture:test/fixtures/should_reject/array_parameter_nested_static.c",
     "E3064"},
    {"array_parameter_nested_const_rejected",
     "fixture:test/fixtures/should_reject/array_parameter_nested_qualifier.c",
     "E3064"},
    {"array_parameter_nested_volatile_rejected",
     "fixture:test/fixtures/should_reject/array_parameter_nested_volatile.c",
     "E3064"},
    {"array_parameter_nested_restrict_rejected",
     "fixture:test/fixtures/should_reject/array_parameter_nested_restrict.c",
     "E3064"},
    {"array_parameter_nested_atomic_rejected",
     "fixture:test/fixtures/should_reject/array_parameter_nested_atomic.c",
     "E3064"},
    {"array_parameter_incomplete_element_rejected",
     "fixture:test/fixtures/should_reject/array_parameter_incomplete_element.c",
     "E3064"},
    {"array_parameter_star_definition_rejected",
     "fixture:test/fixtures/should_reject/array_parameter_star_definition.c",
     "E3064"},
    {"array_of_void_rejected",
     "fixture:test/fixtures/should_reject/array_of_void.c",
     "E3064"},
    {"array_of_function_rejected",
     "fixture:test/fixtures/should_reject/array_of_function.c",
     "E3064"},
    {"void_variable",
     "fixture:test/fixtures/should_reject/void_variable.c",
     "E3087"},
    {"pointer_to_array_incomplete_element_rejected",
     "fixture:test/fixtures/should_reject/pointer_to_array_incomplete_element.c",
     "E3064"},
    {"pointer_to_vla_incomplete_element_rejected",
     "fixture:test/fixtures/should_reject/pointer_to_vla_incomplete_element.c",
     "E3064"},
    {"named_void_parameter_rejected",
     "fixture:test/fixtures/should_reject/named_void_parameter.c",
     "E3064"},
    {"nested_named_void_parameter_rejected",
     "fixture:test/fixtures/should_reject/nested_named_void_parameter.c",
     "E3064"},
    {"qualified_void_parameter_rejected",
     "fixture:test/fixtures/should_reject/qualified_void_parameter.c",
     "E3064"},
    {"void_variadic_parameter_rejected",
     "fixture:test/fixtures/should_reject/void_variadic_parameter.c",
     "E3064"},
    {"void_multiple_parameters_rejected",
     "fixture:test/fixtures/should_reject/void_multiple_parameters.c",
     "E3064"},
    {"typedef_void_variadic_parameter_rejected",
     "fixture:test/fixtures/should_reject/typedef_void_variadic_parameter.c",
     "E3064"},
    {"incomplete_record_parameter_definition_rejected",
     "fixture:test/fixtures/should_reject/"
     "incomplete_record_parameter_definition.c",
     "E3064"},
    {"prototype_scope_tag_file_definition_rejected",
     "fixture:test/fixtures/should_reject/"
     "prototype_scope_tag_file_definition.c",
     "E3064"},
    {"repeated_prototype_scope_tag_declarations_rejected",
     "fixture:test/fixtures/should_reject/"
     "repeated_prototype_scope_tag_declarations.c",
     "E3064"},
    {"prototype_scope_enumerator_file_use_rejected",
     "fixture:test/fixtures/should_reject/"
     "prototype_scope_enumerator_file_use.c",
     "E3066"},
    {"incomplete_record_return_definition_rejected",
     "fixture:test/fixtures/should_reject/incomplete_record_return_definition.c",
     "E3064"},
    {"call_incomplete_return_rejected",
     "fixture:test/fixtures/should_reject/call_incomplete_return.c",
     "E3064"},
    {"pointer_to_array_incomplete_element_type_name_rejected",
     "fixture:test/fixtures/should_reject/"
     "pointer_to_array_incomplete_element_type_name.c",
     "E3117"},
    {"variadic_without_named_parameter_rejected",
     "fixture:test/fixtures/should_reject/variadic_only_parameter_list.c",
     "E3064"},
    {"duplicate_parameter_names_prototype_rejected",
     "fixture:test/fixtures/should_reject/duplicate_parameter_names_prototype.c",
     "E3067"},
    {"duplicate_parameter_names_definition_rejected",
     "fixture:test/fixtures/should_reject/duplicate_parameter_names_definition.c",
     "E3067"},
    {"prototype_parameter_self_reference_rejected",
     "fixture:test/fixtures/should_reject/prototype_parameter_self_reference.c",
     "E3066"},
    {"prototype_parameter_file_scope_use_rejected",
     "fixture:test/fixtures/should_reject/prototype_parameter_file_scope_use.c",
     "E3066"},
    {"nested_prototype_parameter_scope_leak_rejected",
     "fixture:test/fixtures/should_reject/nested_prototype_parameter_scope_leak.c",
     "E3066"},
    {"typedef_hidden_by_object_rejected",
     "fixture:test/fixtures/should_reject/typedef_hidden_by_object.c",
     "E2006"},
    {"typedef_hidden_by_parameter_rejected",
     "fixture:test/fixtures/should_reject/typedef_hidden_by_parameter.c",
     "E3064"},
    {"typedef_hidden_by_enumerator_rejected",
     "fixture:test/fixtures/should_reject/typedef_hidden_by_enumerator.c",
     "E2006"},
    {"typedef_hidden_in_for_scope_rejected",
     "fixture:test/fixtures/should_reject/typedef_hidden_in_for_scope.c",
     "E2006"},
    {"function_returning_array_rejected",
     "fixture:test/fixtures/should_reject/function_returning_array.c",
     "E3064"},
    {"function_returning_function_rejected",
     "fixture:test/fixtures/should_reject/function_returning_function.c",
     "E3064"},
    {"const_function_typedef_declaration_rejected",
     "fixture:test/fixtures/should_reject/"
     "const_function_typedef_declaration.c",
     "E3064"},
    {"volatile_function_typedef_declaration_rejected",
     "fixture:test/fixtures/should_reject/"
     "volatile_function_typedef_declaration.c",
     "E3064"},
    {"const_function_typedef_alias_rejected",
     "fixture:test/fixtures/should_reject/"
     "const_function_typedef_alias.c",
     "E3064"},
    {"const_function_typedef_parameter_rejected",
     "fixture:test/fixtures/should_reject/"
     "const_function_typedef_parameter.c",
     "E3064"},
    {"const_function_typedef_pointer_parameter_rejected",
     "fixture:test/fixtures/should_reject/"
     "const_function_typedef_pointer_parameter.c",
     "E3064"},
    {"const_function_typedef_type_name_rejected",
     "fixture:test/fixtures/should_reject/"
     "const_function_typedef_type_name.c",
     "E3117"},
    {"standalone_tag_restrict_qualifier_rejected",
     "fixture:test/fixtures/should_reject/"
     "standalone_tag_restrict_qualifier.c",
     "E3064"},
    {"standalone_tag_inline_specifier_rejected",
     "fixture:test/fixtures/should_reject/"
     "standalone_tag_inline_specifier.c",
     "E3064"},
    {"standalone_tag_noreturn_specifier_rejected",
     "fixture:test/fixtures/should_reject/"
     "standalone_tag_noreturn_specifier.c",
     "E3064"},
    {"standalone_anonymous_struct_rejected",
     "fixture:test/fixtures/should_reject/"
     "standalone_anonymous_struct.c",
     "E3064"},
    {"local_standalone_anonymous_union_rejected",
     "fixture:test/fixtures/should_reject/"
     "local_standalone_anonymous_union.c",
     "E3064"},
    {"empty_struct_definition_rejected",
     "fixture:test/fixtures/should_reject/empty_struct_definition.c",
     "E3064"},
    {"empty_union_object_rejected",
     "fixture:test/fixtures/should_reject/empty_union_object.c",
     "E3064"},
    {"empty_struct_typedef_rejected",
     "fixture:test/fixtures/should_reject/empty_struct_typedef.c",
     "E3064"},
    {"local_empty_struct_object_rejected",
     "fixture:test/fixtures/should_reject/local_empty_struct_object.c",
     "E3064"},
    {"static_assert_only_struct_rejected",
     "fixture:test/fixtures/should_reject/static_assert_only_struct.c",
     "E3064"},
    {"unnamed_bitfield_only_struct_rejected",
     "fixture:test/fixtures/should_reject/"
     "unnamed_bitfield_only_struct.c",
     "E3064"},
    {"anonymous_unnamed_bitfield_only_rejected",
     "fixture:test/fixtures/should_reject/"
     "anonymous_unnamed_bitfield_only.c",
     "E3064"},
    {"anonymous_member_direct_name_conflict_rejected",
     "fixture:test/fixtures/should_reject/"
     "anonymous_member_direct_name_conflict.c",
     "E3064"},
    {"anonymous_member_recursive_name_conflict_rejected",
     "fixture:test/fixtures/should_reject/"
     "anonymous_member_recursive_name_conflict.c",
     "E3064"},
    {"empty_translation_unit_rejected",
     "fixture:test/fixtures/should_reject/empty_translation_unit.c",
     ":2: E3064"},
    {"preprocessor_only_translation_unit_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocessor_only_translation_unit.c",
     ":2: E3064"},
    {"variably_modified_typedef_array_member_rejected",
     "fixture:test/fixtures/should_reject/"
     "variably_modified_typedef_array_member.c",
     "E3064"},
    {"variably_modified_typedef_pointer_member_rejected",
     "fixture:test/fixtures/should_reject/"
     "variably_modified_typedef_pointer_member.c",
     "E3064"},
    {"variably_modified_pointer_typedef_member_rejected",
     "fixture:test/fixtures/should_reject/"
     "variably_modified_pointer_typedef_member.c",
     "E3064"},
    {"variably_modified_typedef_union_member_rejected",
     "fixture:test/fixtures/should_reject/"
     "variably_modified_typedef_union_member.c",
     "E3064"},
    {"atomic_const_type_rejected",
     "fixture:test/fixtures/should_reject/atomic_const_type.c",
     "E3064"},
    {"atomic_volatile_type_rejected",
     "fixture:test/fixtures/should_reject/atomic_volatile_type.c",
     "E3064"},
    {"nested_atomic_type_rejected",
     "fixture:test/fixtures/should_reject/nested_atomic_type.c",
     "E3064"},
    {"atomic_void_type_rejected",
     "fixture:test/fixtures/should_reject/atomic_void_type.c",
     "E3064"},
    {"atomic_array_type_rejected",
     "fixture:test/fixtures/should_reject/atomic_array_type.c",
     "E3064"},
    {"atomic_function_type_rejected",
     "fixture:test/fixtures/should_reject/atomic_function_type.c",
     "E3064"},
    {"atomic_incomplete_record_rejected",
     "fixture:test/fixtures/should_reject/atomic_incomplete_record.c",
     "E3064"},
    {"atomic_const_pointer_type_rejected",
     "fixture:test/fixtures/should_reject/atomic_const_pointer_type.c",
     "E3064"},
    {"nested_atomic_pointer_type_rejected",
     "fixture:test/fixtures/should_reject/nested_atomic_pointer_type.c",
     "E3064"},
    {"atomic_typedef_function_qualifier_rejected",
     "fixture:test/fixtures/should_reject/atomic_typedef_function_qualifier.c",
     "E3064"},
    {"atomic_typedef_function_local_rejected",
     "fixture:test/fixtures/should_reject/atomic_typedef_function_local.c",
     "E3064"},
    {"atomic_typedef_function_member_rejected",
     "fixture:test/fixtures/should_reject/atomic_typedef_function_member.c",
     "E3064"},
    {"atomic_typedef_function_parameter_rejected",
     "fixture:test/fixtures/should_reject/atomic_typedef_function_parameter.c",
     "E3064"},
    {"atomic_typedef_function_typedef_rejected",
     "fixture:test/fixtures/should_reject/atomic_typedef_function_typedef.c",
     "E3064"},
    {"atomic_typedef_function_pointer_outer_rejected",
     "fixture:test/fixtures/should_reject/atomic_typedef_function_pointer_outer.c",
     "E3064"},
    {"atomic_typedef_array_qualifier_rejected",
     "fixture:test/fixtures/should_reject/atomic_typedef_array_qualifier.c",
     "E3064"},
    {"atomic_typedef_array_pointer_outer_rejected",
     "fixture:test/fixtures/should_reject/atomic_typedef_array_pointer_outer.c",
     "E3064"},
    {"atomic_typedef_array_type_name_rejected",
     "fixture:test/fixtures/should_reject/atomic_typedef_array_type_name.c",
     "E3064"},
    {"atomic_incomplete_record_pointer_outer_rejected",
     "fixture:test/fixtures/should_reject/atomic_incomplete_record_pointer_outer.c",
     "E3064"},
    {"atomic_bitfield_qualifier_rejected",
     "fixture:test/fixtures/should_reject/atomic_bitfield_qualifier.c",
     "E3064"},
    {"atomic_bitfield_specifier_rejected",
     "fixture:test/fixtures/should_reject/atomic_bitfield_specifier.c",
     "E3064"},
    {"atomic_bitfield_typedef_rejected",
     "fixture:test/fixtures/should_reject/atomic_bitfield_typedef.c",
     "E3064"},
    {"atomic_bitfield_unnamed_rejected",
     "fixture:test/fixtures/should_reject/atomic_bitfield_unnamed.c",
     "E3064"},
    {"atomic_rvalue_return_to_plain_type_rejected",
     "fixture:test/fixtures/should_reject/"
     "atomic_rvalue_return_to_plain_type.c",
     "E3108"},
    {"stdatomic_load_result_assignment_rejected",
     "fixture:test/fixtures/should_reject/stdatomic_load_result_assignment.c",
     "E3062"},
    {"stdatomic_plain_object_argument_rejected",
     "fixture:test/fixtures/should_reject/stdatomic_plain_object_argument.c",
     "E3120"},
    {"stdatomic_const_expected_argument_rejected",
     "fixture:test/fixtures/should_reject/stdatomic_const_expected_argument.c",
     "E3120"},
    {"stdatomic_compare_exchange_distinct_enum_expected_rejected",
     "fixture:test/fixtures/should_reject/stdatomic_compare_exchange_distinct_enum_expected.c",
     "E3120"},
    {"stdatomic_compare_exchange_incompatible_integer_expected_rejected",
     "fixture:test/fixtures/should_reject/stdatomic_compare_exchange_incompatible_integer_expected.c",
     "E3120"},
    {"stdatomic_compare_exchange_volatile_expected_rejected",
     "fixture:test/fixtures/should_reject/stdatomic_compare_exchange_volatile_expected.c",
     "E3120"},
    {"stdatomic_compare_exchange_atomic_expected_rejected",
     "fixture:test/fixtures/should_reject/stdatomic_compare_exchange_atomic_expected.c",
     "E3120"},
    {"stdatomic_const_object_store_rejected",
     "fixture:test/fixtures/should_reject/stdatomic_const_object_store.c",
     "E3120"},
    {"stdatomic_const_object_exchange_rejected",
     "fixture:test/fixtures/should_reject/stdatomic_const_object_exchange.c",
     "E3120"},
    {"stdatomic_const_object_compare_exchange_rejected",
     "fixture:test/fixtures/should_reject/stdatomic_const_object_compare_exchange.c",
     "E3120"},
    {"stdatomic_const_object_fetch_rejected",
     "fixture:test/fixtures/should_reject/stdatomic_const_object_fetch.c",
     "E3120"},
    {"stdatomic_const_object_init_rejected",
     "fixture:test/fixtures/should_reject/stdatomic_const_object_init.c",
     "E3120"},
    {"stdatomic_const_aggregate_store_rejected",
     "fixture:test/fixtures/should_reject/stdatomic_const_aggregate_store.c",
     "E3120"},
    {"stdatomic_const_flag_clear_rejected",
     "fixture:test/fixtures/should_reject/stdatomic_const_flag_clear.c",
     "E3120"},
    {"stdatomic_const_pointer_object_store_rejected",
     "fixture:test/fixtures/should_reject/stdatomic_const_pointer_object_store.c",
     "E3120"},
    {"stdatomic_const_enum_object_exchange_rejected",
     "fixture:test/fixtures/should_reject/stdatomic_const_enum_object_exchange.c",
     "E3120"},
    {"stdatomic_fetch_aggregate_operand_rejected",
     "fixture:test/fixtures/should_reject/stdatomic_fetch_aggregate_operand.c",
     "E3120"},
    {"stdatomic_fetch_floating_object_rejected",
     "#include <stdatomic.h>\nint main(void) { _Atomic(float) value = 1.0f; return atomic_fetch_add(&value, 2.0f); }\n",
     "E3120"},
    {"stdatomic_fetch_pointer_bitwise_rejected",
     "fixture:test/fixtures/should_reject/stdatomic_pointer_bitwise.c",
     "E3120"},
    {"stdatomic_pointer_incomplete_fetch_rejected",
     "fixture:test/fixtures/should_reject/"
     "stdatomic_pointer_incomplete_fetch.c",
     "E3120"},
    {"stdatomic_store_struct_order_rejected",
     "fixture:test/fixtures/should_reject/stdatomic_store_struct_order.c",
     "E3099"},
    {"stdatomic_load_pointer_order_rejected",
     "fixture:test/fixtures/should_reject/stdatomic_load_pointer_order.c",
     "E3099"},
    {"stdatomic_compare_exchange_struct_success_order_rejected",
     "fixture:test/fixtures/should_reject/stdatomic_compare_exchange_struct_success_order.c",
     "E3099"},
    {"stdatomic_compare_exchange_pointer_failure_order_rejected",
     "fixture:test/fixtures/should_reject/stdatomic_compare_exchange_pointer_failure_order.c",
     "E3099"},
    {"stdatomic_thread_fence_struct_order_rejected",
     "fixture:test/fixtures/should_reject/stdatomic_thread_fence_struct_order.c",
     "E3099"},
    {"stdatomic_flag_pointer_order_rejected",
     "fixture:test/fixtures/should_reject/stdatomic_flag_pointer_order.c",
     "E3099"},
    {"stdatomic_kill_dependency_struct_rejected",
     "#include <stdatomic.h>\nstruct value { int member; }; int main(void) { struct value input = {1}; struct value output = kill_dependency(input); return output.member; }\n",
     "E3100"},
    {"stdatomic_kill_dependency_union_rejected",
     "#include <stdatomic.h>\nunion value { int member; }; int main(void) { union value input = {1}; union value output = kill_dependency(input); return output.member; }\n",
     "E3100"},
    {"stdatomic_kill_dependency_void_rejected",
     "#include <stdatomic.h>\nint main(void) { kill_dependency((void)0); return 0; }\n",
     "E3100"},
    {"stdatomic_kill_dependency_result_assignment_rejected",
     "#include <stdatomic.h>\nint main(void) { int value = 1; kill_dependency(value) = 2; return value; }\n",
     "E3062"},
    {"stdatomic_lock_free_plain_object_rejected",
     "#include <stdatomic.h>\nint main(void) { int value = 1; return atomic_is_lock_free(&value); }\n",
     "E3120"},
    {"stdatomic_lock_free_void_pointer_rejected",
     "fixture:test/fixtures/should_reject/stdatomic_lock_free_void_pointer.c",
     "E3120"},
    {"stdatomic_lock_free_nonpointer_rejected",
     "fixture:test/fixtures/should_reject/stdatomic_lock_free_nonpointer.c",
     "E3120"},
    {"stdatomic_thread_fence_value_rejected",
     "fixture:test/fixtures/should_reject/stdatomic_thread_fence_value.c",
     "E3099"},
    {"stdatomic_signal_fence_value_rejected",
     "fixture:test/fixtures/should_reject/stdatomic_signal_fence_value.c",
     "E3099"},
    {"stdarg_va_start_value_rejected",
     "fixture:test/fixtures/should_reject/stdarg_va_start_value.c",
     "E3099"},
    {"stdarg_va_copy_value_rejected",
     "fixture:test/fixtures/should_reject/stdarg_va_copy_value.c",
     "E3099"},
    {"stddef_offsetof_nonaggregate_rejected",
     "fixture:test/fixtures/should_reject/stddef_offsetof_nonaggregate.c",
     "E3117"},
    {"stddef_offsetof_missing_member_rejected",
     "fixture:test/fixtures/should_reject/stddef_offsetof_missing_member.c",
     "E3064"},
    {"stddef_offsetof_bitfield_rejected",
     "fixture:test/fixtures/should_reject/stddef_offsetof_bitfield.c",
     "E3113"},
    {"stddef_offsetof_nonconstant_index_rejected",
     "#include <stddef.h>\nint index; struct value { int members[2]; }; int main(void) { return (int)offsetof(struct value, members[index]); }\n",
     "E3117"},
    {"static_assert_missing_message_rejected",
     "fixture:test/fixtures/should_reject/static_assert_missing_message.c",
     "E2006"},
    {"static_assert_identifier_message_rejected",
     "fixture:test/fixtures/should_reject/"
     "static_assert_identifier_message.c",
     "E3011"},
    {"static_assert_non_string_message_rejected",
     "fixture:test/fixtures/should_reject/"
     "static_assert_non_string_message.c",
     "E3011"},
    {"static_assert_float_condition_rejected",
     "fixture:test/fixtures/should_reject/"
     "static_assert_floating_condition.c",
     "E3010"},
    {"static_assert_comma_condition_rejected",
     "fixture:test/fixtures/should_reject/"
     "static_assert_comma_condition.c",
     "E3010"},
    {"static_assert_variable_condition_rejected",
     "fixture:test/fixtures/should_reject/"
     "static_assert_variable_condition.c",
     "E3010"},
    {"static_assert_floating_expression_cast_rejected",
     "fixture:test/fixtures/should_reject/"
     "static_assert_floating_expression_cast.c",
     "E3010"},
    {"static_assert_signed_overflow_rejected",
     "fixture:test/fixtures/should_reject/"
     "static_assert_signed_overflow.c",
     "E3010"},
    {"static_assert_sizeof_vla",
     "fixture:test/fixtures/should_reject/static_assert_sizeof_vla.c",
     "E3010"},
    {"conditional_unselected_call_enum",
     "fixture:test/fixtures/should_reject/conditional_unselected_call_enum.c",
     "E3064"},
    {"logical_unselected_call_static_assert",
     "fixture:test/fixtures/should_reject/"
     "logical_unselected_call_static_assert.c",
     "E3010"},
    {"conditional_selected_comma_enum",
     "fixture:test/fixtures/should_reject/conditional_selected_comma_enum.c",
     "E3064"},
    {"logical_selected_comma_static_assert",
     "fixture:test/fixtures/should_reject/"
     "logical_selected_comma_static_assert.c",
     "E3010"},
    {"static_assert_false_condition_rejected",
     "fixture:test/fixtures/should_reject/"
     "static_assert_false_condition.c",
     "E3012"},
    {"static_assert_for_initializer_false_rejected",
     "fixture:test/fixtures/should_reject/"
     "static_assert_for_initializer_false.c",
     "E3012"},
    {"static_assert_for_initializer_nonconstant_rejected",
     "fixture:test/fixtures/should_reject/"
     "static_assert_for_initializer_nonconstant.c",
     "E3010"},
    {"signed_constant_add_overflow_rejected",
     "fixture:test/fixtures/should_reject/signed_constant_add_overflow.c",
     "E3064"},
    {"signed_constant_add_underflow_rejected",
     "fixture:test/fixtures/should_reject/signed_constant_add_underflow.c",
     "E3064"},
    {"signed_constant_div_overflow_rejected",
     "fixture:test/fixtures/should_reject/signed_constant_div_overflow.c",
     "E3064"},
    {"signed_constant_mod_overflow_rejected",
     "fixture:test/fixtures/should_reject/signed_constant_mod_overflow.c",
     "E3064"},
    {"signed_constant_mul_both_negative_overflow_rejected",
     "fixture:test/fixtures/should_reject/"
     "signed_constant_mul_both_negative_overflow.c",
     "E3064"},
    {"signed_constant_mul_negative_rhs_overflow_rejected",
     "fixture:test/fixtures/should_reject/"
     "signed_constant_mul_negative_rhs_overflow.c",
     "E3064"},
    {"signed_constant_mul_overflow_rejected",
     "fixture:test/fixtures/should_reject/signed_constant_mul_overflow.c",
     "E3064"},
    {"signed_constant_negate_overflow_rejected",
     "fixture:test/fixtures/should_reject/"
     "signed_constant_negate_overflow.c",
     "E3064"},
    {"signed_constant_negative_shift_rejected",
     "fixture:test/fixtures/should_reject/"
     "signed_constant_negative_shift.c",
     "E3064"},
    {"signed_constant_shift_overflow_rejected",
     "fixture:test/fixtures/should_reject/"
     "signed_constant_shift_overflow.c",
     "E3064"},
    {"signed_constant_sub_negative_overflow_rejected",
     "fixture:test/fixtures/should_reject/"
     "signed_constant_sub_negative_overflow.c",
     "E3064"},
    {"signed_constant_sub_overflow_rejected",
     "fixture:test/fixtures/should_reject/signed_constant_sub_overflow.c",
     "E3064"},
    {"signed_long_long_add_overflow_rejected",
     "fixture:test/fixtures/should_reject/"
     "signed_long_long_add_overflow.c",
     "E3064"},
    {"signed_long_long_div_overflow_rejected",
     "fixture:test/fixtures/should_reject/"
     "signed_long_long_div_overflow.c",
     "E3064"},
    {"signed_long_long_shift_overflow_rejected",
     "fixture:test/fixtures/should_reject/"
     "signed_long_long_shift_overflow.c",
     "E3064"},
    {"unsigned_long_long_divzero_constant_rejected",
     "fixture:test/fixtures/should_reject/"
     "unsigned_long_long_divzero_constant.c",
     "E3010"},
    {"unsigned_long_long_negative_shift_count_rejected",
     "fixture:test/fixtures/should_reject/"
     "unsigned_long_long_negative_shift_count.c",
     "E3064"},
    {"unsigned_long_long_shift_width_rejected",
     "fixture:test/fixtures/should_reject/"
     "unsigned_long_long_shift_width.c",
     "E3064"},
    {"declaration_immediately_after_label_rejected",
     "fixture:test/fixtures/should_reject/declaration_immediately_after_label.c",
     "E3064"},
    {"declaration_immediately_after_case_rejected",
     "fixture:test/fixtures/should_reject/declaration_immediately_after_case.c",
     "E3064"},
    {"declaration_immediately_after_default_rejected",
     "fixture:test/fixtures/should_reject/declaration_immediately_after_default.c",
     "E3064"},
    {"enum_comma_constant_rejected",
     "fixture:test/fixtures/should_reject/enum_comma_constant.c",
     "E3064"},
    {"enum_float_constant_rejected",
     "fixture:test/fixtures/should_reject/enum_float_constant.c",
     "E3064"},
    {"enum_floating_expression_cast_rejected",
     "fixture:test/fixtures/should_reject/enum_floating_expression_cast.c",
     "E3064"},
    {"enum_implicit_value_overflow_rejected",
     "fixture:test/fixtures/should_reject/"
     "enum_implicit_value_overflow.c",
     "E3064"},
    {"local_enum_implicit_value_overflow_rejected",
     "fixture:test/fixtures/should_reject/"
     "local_enum_implicit_value_overflow.c",
     "E3064"},
    {"enumerator_self_reference_rejected",
     "fixture:test/fixtures/should_reject/enumerator_self_reference.c",
     "E3066"},
    {"case_comma_constant_rejected",
     "fixture:test/fixtures/should_reject/case_comma_constant.c",
     "E3064"},
    {"case_float_constant_rejected",
     "fixture:test/fixtures/should_reject/case_float_constant.c",
     "E3064"},
    {"bitfield_comma_width_rejected",
     "fixture:test/fixtures/should_reject/bitfield_comma_width.c",
     "E3064"},
    {"sizeof_generic_selected_bitfield_rejected",
     "fixture:test/fixtures/should_reject/"
     "sizeof_generic_selected_bitfield.c",
     "E3118"},
    {"sizeof_generic_selected_function_rejected",
     "fixture:test/fixtures/should_reject/"
     "sizeof_generic_selected_function.c",
     "E3117"},
    {"assign_generic_selected_const_rejected",
     "fixture:test/fixtures/should_reject/assign_generic_selected_const.c",
     "E3077"},
    {"function_pointer_from_nonzero_void_pointer_rejected",
     "fixture:test/fixtures/should_reject/"
     "function_pointer_from_nonzero_void_pointer.c",
     "E3099"},
    {"function_pointer_from_object_pointer_cast_rejected",
     "fixture:test/fixtures/should_reject/"
     "function_pointer_from_object_pointer_cast.c",
     "E3099"},
    {"bitfield_pointer_type_rejected",
     "fixture:test/fixtures/should_reject/bitfield_pointer_type.c",
     "E3064"},
    {"bitfield_array_type_rejected",
     "fixture:test/fixtures/should_reject/bitfield_array_type.c",
     "E3064"},
    {"bitfield_function_type_rejected",
     "fixture:test/fixtures/should_reject/bitfield_function_type.c",
     "E3064"},
    {"bitfield_negative_width_rejected",
     "fixture:test/fixtures/should_reject/bitfield_negative_width.c",
     "E3064"},
    {"bitfield_named_zero_width_rejected",
     "fixture:test/fixtures/should_reject/named_zero_width_bitfield.c",
     "E3064"},
    {"bool_bitfield_width_exceeds_one_rejected",
     "fixture:test/fixtures/should_reject/bool_bitfield_too_wide.c",
     "E3064"},
    {"bitfield_floating_type_rejected",
     "fixture:test/fixtures/should_reject/bitfield_floating_type.c",
     "E3064"},
    {"bitfield_nonconstant_width_rejected",
     "fixture:test/fixtures/should_reject/bitfield_nonconstant_width.c",
     "E3064"},
    {"bitfield_width_signed_overflow_rejected",
     "fixture:test/fixtures/should_reject/"
     "bitfield_width_signed_overflow.c",
     "E3064"},
    {"incomplete_enum_bitfield_rejected",
     "fixture:test/fixtures/should_reject/incomplete_enum_bitfield.c",
     "E3064"},
    {"empty_enum_rejected",
     "fixture:test/fixtures/should_reject/empty_enum.c",
     "E3064"},
    {"incomplete_enum_object_rejected",
     "fixture:test/fixtures/should_reject/incomplete_enum_object.c",
     "E3064"},
    {"incomplete_enum_member_rejected",
     "fixture:test/fixtures/should_reject/incomplete_enum_member.c",
     "E3064"},
    {"flexible_array_in_union_rejected",
     "fixture:test/fixtures/should_reject/flexible_array_in_union.c",
     "E3064"},
    {"flexible_array_without_prior_named_member_rejected",
     "fixture:test/fixtures/should_reject/flexible_array_without_prior_member.c",
     "E3064"},
    {"flexible_array_not_last_rejected",
     "fixture:test/fixtures/should_reject/member_after_flexible_array.c",
     "E3064"},
    {"flexible_array_nested_struct_member_rejected",
     "fixture:test/fixtures/should_reject/flexible_array_nested_struct_member.c",
     "E3064"},
    {"flexible_array_nested_union_member_rejected",
     "fixture:test/fixtures/should_reject/flexible_array_nested_union_member.c",
     "E3064"},
    {"flexible_array_pointer_to_array_rejected",
     "fixture:test/fixtures/should_reject/flexible_array_pointer_to_array.c",
     "E3064"},
    {"flexible_array_parameter_array_rejected",
     "fixture:test/fixtures/should_reject/flexible_array_parameter_array.c",
     "E3064"},
    {"flexible_array_compound_literal_array_rejected",
     "fixture:test/fixtures/should_reject/flexible_array_compound_literal_array.c",
     "E3064"},
    {"alignas_comma_constant_rejected",
     "fixture:test/fixtures/should_reject/alignas_comma_constant.c",
     "E3064"},
    {"static_array_comma_bound_rejected",
     "fixture:test/fixtures/should_reject/static_array_comma_bound.c",
     "E3064"},
    {"declaration_as_if_body_rejected",
     "fixture:test/fixtures/should_reject/declaration_as_if_body.c",
     "E3064"},
    {"declaration_as_else_body_rejected",
     "fixture:test/fixtures/should_reject/declaration_as_else_body.c",
     "E3064"},
    {"declaration_as_while_body_rejected",
     "fixture:test/fixtures/should_reject/declaration_as_while_body.c",
     "E3064"},
    {"declaration_as_do_body_rejected",
     "fixture:test/fixtures/should_reject/declaration_as_do_body.c",
     "E3064"},
    {"declaration_as_for_body_rejected",
     "fixture:test/fixtures/should_reject/declaration_as_for_body.c",
     "E3064"},
    {"declaration_as_switch_body_rejected",
     "fixture:test/fixtures/should_reject/declaration_as_switch_body.c",
     "E3064"},
    {"generic_incomplete_array_association_rejected",
     "fixture:test/fixtures/should_reject/"
     "generic_incomplete_array_association_type.c",
     "E3043"},
    {"generic_function_association_rejected",
     "fixture:test/fixtures/should_reject/"
     "generic_function_association_type.c",
     "E3043"},
    {"generic_void_association_rejected",
     "fixture:test/fixtures/should_reject/"
     "generic_void_association_type.c",
     "E3043"},
    {"generic_incomplete_record_association_rejected",
     "fixture:test/fixtures/should_reject/"
     "generic_incomplete_association_type.c",
     "E3043"},
    {"generic_implicit_incomplete_tag_association_rejected",
     "fixture:test/fixtures/should_reject/"
     "generic_implicit_incomplete_tag_association_type.c",
     "E3043"},
    {"generic_vla_association_rejected",
     "fixture:test/fixtures/should_reject/"
     "generic_vla_association_type.c",
     "E3064"},
    {"generic_duplicate_typedef_association_rejected",
     "fixture:test/fixtures/should_reject/"
     "generic_duplicate_typedef_association.c",
     "E3105"},
    {"generic_duplicate_positive_enum_compatible_type",
     "fixture:test/fixtures/should_reject/"
     "generic_duplicate_positive_enum_compatible_type.c",
     "E3105"},
    {"generic_duplicate_negative_enum_compatible_type",
     "fixture:test/fixtures/should_reject/"
     "generic_duplicate_negative_enum_compatible_type.c",
     "E3105"},
    {"generic_duplicate_qualified_parameter_function_pointer",
     "fixture:test/fixtures/should_reject/"
     "generic_duplicate_qualified_parameter_function_pointer.c",
     "E3105"},
    {"generic_duplicate_array_adjusted_function_pointer",
     "fixture:test/fixtures/should_reject/"
     "generic_duplicate_array_adjusted_function_pointer.c",
     "E3105"},
    {"generic_duplicate_function_adjusted_parameter_pointer",
     "fixture:test/fixtures/should_reject/"
     "generic_duplicate_function_adjusted_parameter_pointer.c",
     "E3105"},
    {"generic_duplicate_nested_callback_parameter_pointer_qualifier",
     "fixture:test/fixtures/should_reject/"
     "generic_duplicate_nested_callback_parameter_pointer_qualifier.c",
     "E3105"},
    {"generic_duplicate_nested_callback_array_adjustment",
     "fixture:test/fixtures/should_reject/"
     "generic_duplicate_nested_callback_array_adjustment.c",
     "E3105"},
    {"generic_duplicate_nested_callback_function_adjustment",
     "fixture:test/fixtures/should_reject/"
     "generic_duplicate_nested_callback_function_adjustment.c",
     "E3105"},
    {"generic_duplicate_nested_callback_unprototyped_int",
     "fixture:test/fixtures/should_reject/"
     "generic_duplicate_nested_callback_unprototyped_int.c",
     "E3105"},
    {"generic_duplicate_nested_callback_return_function_unprototyped_int",
     "fixture:test/fixtures/should_reject/"
     "generic_duplicate_nested_callback_return_function_unprototyped_int.c",
     "E3105"},
    {"generic_duplicate_nested_callback_restrict_array_adjustment",
     "fixture:test/fixtures/should_reject/"
     "generic_duplicate_nested_callback_restrict_array_adjustment.c",
     "E3105"},
    {"generic_duplicate_nested_callback_atomic_array_adjustment",
     "fixture:test/fixtures/should_reject/"
     "generic_duplicate_nested_callback_atomic_array_adjustment.c",
     "E3105"},
    {"generic_duplicate_nested_callback_function_pointer_parameter_qualifier",
     "fixture:test/fixtures/should_reject/"
     "generic_duplicate_nested_callback_function_pointer_parameter_qualifier.c",
     "E3105"},
    {"generic_unselected_undefined_identifier_rejected",
     "fixture:test/fixtures/should_reject/"
     "generic_unselected_undefined_identifier.c",
     "E3066"},
    {"generic_unselected_incomplete_compound_literal_rejected",
     "fixture:test/fixtures/should_reject/"
     "generic_unselected_incomplete_compound_literal.c",
     "E3114"},
    {"compound_literal_void_type",
     "fixture:test/fixtures/should_reject/compound_literal_void_type.c",
     "E3114"},
    {"compound_literal_function_type",
     "fixture:test/fixtures/should_reject/"
     "compound_literal_function_type.c",
     "E3114"},
    {"compound_literal_incomplete_record",
     "fixture:test/fixtures/should_reject/"
     "compound_literal_incomplete_record.c",
     "E3114"},
    {"compound_literal_vla",
     "fixture:test/fixtures/should_reject/compound_literal_vla.c",
     "E3064"},
    {"array_designator_negative",
     "fixture:test/fixtures/should_reject/array_designator_negative.c",
     "E3085"},
    {"array_designator_nonconstant",
     "fixture:test/fixtures/should_reject/"
     "array_designator_nonconstant.c",
     "E3085"},
    {"array_designator_out_of_bounds",
     "fixture:test/fixtures/should_reject/"
     "array_designator_out_of_bounds.c",
     "E3085"},
    {"struct_designator_unknown_member",
     "fixture:test/fixtures/should_reject/"
     "struct_designator_unknown_member.c",
     "E3084"},
    {"scalar_array_designator",
     "fixture:test/fixtures/should_reject/scalar_array_designator.c",
     "E3064"},
    {"empty_object_initializer_rejected",
     "fixture:test/fixtures/should_reject/empty_object_initializer.c",
     "E3115"},
    {"empty_compound_literal_initializer_rejected",
     "fixture:test/fixtures/should_reject/empty_compound_literal_initializer.c",
     "E3115"},
    {"file_scope_compound_literal_nonconstant_initializer_rejected",
     "fixture:test/fixtures/should_reject/"
     "file_scope_compound_literal_nonconstant_initializer.c",
     "E3116"},
    {"static_scalar_object_value_initializer_rejected",
     "fixture:test/fixtures/should_reject/"
     "static_scalar_object_value_initializer.c",
     "E3116"},
    {"static_aggregate_object_value_initializer_rejected",
     "fixture:test/fixtures/should_reject/"
     "static_aggregate_object_value_initializer.c",
     "E3116"},
    {"static_floating_object_value_initializer_rejected",
     "fixture:test/fixtures/should_reject/"
     "static_floating_object_value_initializer.c",
     "E3116"},
    {"static_complex_function_value_initializer_rejected",
     "fixture:test/fixtures/should_reject/"
     "static_complex_function_value_initializer.c",
     "E3116"},
    {"file_scope_compound_literal_qualifier_discard_rejected",
     "fixture:test/fixtures/should_reject/"
     "file_scope_compound_literal_qualifier_discard.c",
     "E3078"},
    {"block_static_scalar_object_value_initializer_rejected",
     "fixture:test/fixtures/should_reject/"
     "block_static_scalar_object_value_initializer.c",
     "E3116"},
    {"block_static_compound_literal_address_initializer_rejected",
     "fixture:test/fixtures/should_reject/"
     "block_static_compound_literal_address_initializer.c",
     "E3116"},
    {"block_static_automatic_address_initializer_rejected",
     "fixture:test/fixtures/should_reject/"
     "block_static_automatic_address_initializer.c",
     "E3116"},
    {"block_static_aggregate_object_value_initializer_rejected",
     "fixture:test/fixtures/should_reject/"
     "block_static_aggregate_object_value_initializer.c",
     "E3116"},
    {"block_static_complex_function_value_initializer_rejected",
     "fixture:test/fixtures/should_reject/"
     "block_static_complex_function_value_initializer.c",
     "E3116"},
    {"static_address_selected_assignment_rejected",
     "fixture:test/fixtures/should_reject/"
     "static_address_selected_assignment.c",
     "E3116"},
    {"static_address_selected_function_call_rejected",
     "fixture:test/fixtures/should_reject/"
     "static_address_selected_function_call.c",
     "E3116"},
    {"static_cross_symbol_one_past_comparison_rejected",
     "fixture:test/fixtures/should_reject/"
     "static_cross_symbol_one_past_comparison.c",
     "E3116"},
    {"static_cross_symbol_order_comparison_rejected",
     "fixture:test/fixtures/should_reject/"
     "static_cross_symbol_order_comparison.c",
     "E3116"},
    {"static_pointer_to_narrow_integer_rejected",
     "fixture:test/fixtures/should_reject/"
     "static_pointer_to_narrow_integer.c",
     "E3116"},
    {"static_string_pointer_comparisons_rejected",
     "fixture:test/fixtures/should_reject/"
     "static_string_pointer_comparisons.c",
     "E3116"},
    {"sizeof_incomplete_type_rejected",
     "fixture:test/fixtures/should_reject/sizeof_incomplete_type.c",
     "E3117"},
    {"sizeof_incomplete_expression_rejected",
     "fixture:test/fixtures/should_reject/sizeof_incomplete_expression.c",
     "E3117"},
    {"sizeof_incomplete_array_type_rejected",
     "fixture:test/fixtures/should_reject/sizeof_incomplete_array_type.c",
     "E3117"},
    {"sizeof_incomplete_array_expression_rejected",
     "fixture:test/fixtures/should_reject/sizeof_incomplete_array_expression.c",
     "E3117"},
    {"sizeof_flexible_array_member_rejected",
     "fixture:test/fixtures/should_reject/sizeof_flexible_array_member.c",
     "E3117"},
    {"sizeof_function_expression_rejected",
     "fixture:test/fixtures/should_reject/sizeof_function_expression.c",
     "E3117"},
    {"sizeof_function_type_rejected",
     "fixture:test/fixtures/should_reject/sizeof_function_type.c",
     "E3117"},
    {"alignof_incomplete_type_rejected",
     "fixture:test/fixtures/should_reject/alignof_incomplete_type.c",
     "E3117"},
    {"alignof_incomplete_array_type_rejected",
     "fixture:test/fixtures/should_reject/alignof_incomplete_array_type.c",
     "E3117"},
    {"alignof_function_type_rejected",
     "fixture:test/fixtures/should_reject/alignof_function_type.c",
     "E3117"},
    {"alignof_void_type_rejected",
     "fixture:test/fixtures/should_reject/alignof_void_type.c",
     "E3117"},
    {"sizeof_bitfield_rejected",
     "fixture:test/fixtures/should_reject/sizeof_bitfield.c",
     "E3118"},
    {"address_of_register_object_rejected",
     "fixture:test/fixtures/should_reject/address_of_register_object.c",
     "E3119"},
    {"address_generic_selected_register_object_rejected",
     "fixture:test/fixtures/should_reject/"
     "address_generic_selected_register_object.c",
     "E3119"},
    {"address_generic_selected_register_member_rejected",
     "fixture:test/fixtures/should_reject/"
     "address_generic_selected_register_member.c",
     "E3119"},
    {"address_generic_selected_register_array_rejected",
     "fixture:test/fixtures/should_reject/"
     "address_generic_selected_register_array.c",
     "E3119"},
    {"address_of_register_parameter_rejected",
     "fixture:test/fixtures/should_reject/"
     "address_of_register_parameter.c",
     "E3119"},
    {"address_of_register_atomic_parameter_rejected",
     "fixture:test/fixtures/should_reject/"
     "address_of_register_atomic_parameter.c",
     "E3119"},
    {"address_of_register_atomic_aggregate_parameter_rejected",
     "fixture:test/fixtures/should_reject/"
     "address_of_register_atomic_aggregate_parameter.c",
     "E3119"},
    {"subscript_int_rejected",
     "fixture:test/fixtures/should_reject/subscript_int.c",
     "E3064"},
    {"call_nonfunction_object_rejected",
     "fixture:test/fixtures/should_reject/call_nonfunction_object.c",
     "E3102"},
    {"too_few_args_rejected",
     "fixture:test/fixtures/should_reject/too_few_args.c",
     "E3103"},
    {"too_many_args_rejected",
     "fixture:test/fixtures/should_reject/too_many_args.c",
     "E3103"},
    {"call_argument_incompatible_pointer_rejected",
     "fixture:test/fixtures/should_reject/call_argument_incompatible_pointer.c",
     "E3120"},
    {"stdio_file_pointer_argument_rejected",
     "fixture:test/fixtures/should_reject/stdio_file_pointer_argument.c",
     "E3120"},
    {"posix_off_t_pointer_identity_rejected",
     "#include <sys/types.h>\nint main(void) { long value = 0; off_t *pointer = &value; return *pointer != 0; }",
     "E3099"},
    {"call_argument_nonzero_integer_to_pointer_rejected",
     "fixture:test/fixtures/should_reject/"
     "call_argument_nonzero_integer_to_pointer.c",
     "E3120"},
    {"call_argument_pointer_to_integer_rejected",
     "fixture:test/fixtures/should_reject/call_argument_pointer_to_integer.c",
     "E3120"},
    {"call_argument_incompatible_struct_rejected",
     "fixture:test/fixtures/should_reject/call_argument_incompatible_struct.c",
     "E3120"},
    {"call_argument_struct_to_integer_rejected",
     "fixture:test/fixtures/should_reject/call_argument_struct_to_integer.c",
     "E3120"},
    {"call_argument_integer_to_struct_rejected",
     "fixture:test/fixtures/should_reject/call_argument_integer_to_struct.c",
     "E3120"},
    {"call_argument_adds_nested_const_rejected",
     "fixture:test/fixtures/should_reject/call_argument_adds_nested_const.c",
     "E3120"},
    {"call_argument_discards_nested_const_rejected",
     "fixture:test/fixtures/should_reject/call_argument_discards_nested_const.c",
     "E3120"},
    {"call_argument_nested_const_pointer_rejected",
     "fixture:test/fixtures/should_reject/call_argument_nested_const_pointer.c",
     "E3120"},
    {"argument_incompatible_function_pointer",
     "fixture:test/fixtures/should_reject/"
     "argument_incompatible_function_pointer.c",
     "E3120"},
    {"variadic_fixed_argument_incompatible_rejected",
     "fixture:test/fixtures/should_reject/"
     "variadic_fixed_argument_incompatible.c",
     "E3120"},
    {"variadic_fixed_argument_discards_const_rejected",
     "fixture:test/fixtures/should_reject/"
     "variadic_fixed_argument_discards_const.c",
     "E3121"},
    {"return_incompatible_pointer_rejected",
     "fixture:test/fixtures/should_reject/return_incompatible_pointer.c",
     "E3108"},
    {"return_nonzero_integer_to_pointer_rejected",
     "fixture:test/fixtures/should_reject/return_wrong_type_ptr.c",
     "E3108"},
    {"return_pointer_to_integer_rejected",
     "fixture:test/fixtures/should_reject/return_pointer_to_integer.c",
     "E3108"},
    {"return_incompatible_struct_rejected",
     "fixture:test/fixtures/should_reject/return_incompatible_struct.c",
     "E3108"},
    {"return_integer_to_struct_rejected",
     "fixture:test/fixtures/should_reject/return_integer_to_struct.c",
     "E3108"},
    {"return_struct_to_integer_rejected",
     "fixture:test/fixtures/should_reject/return_struct_to_integer.c",
     "E3108"},
    {"binary_struct_equality_rejected",
     "fixture:test/fixtures/should_reject/binary_struct_equality.c",
     "E3122"},
    {"binary_pointer_nonzero_integer_equality_rejected",
     "fixture:test/fixtures/should_reject/"
     "binary_pointer_nonzero_integer_equality.c",
     "E3122"},
    {"binary_pointer_float_equality_rejected",
     "fixture:test/fixtures/should_reject/binary_pointer_float_equality.c",
     "E3122"},
    {"binary_complex_relational_rejected",
     "fixture:test/fixtures/should_reject/binary_complex_relational.c",
     "E3122"},
    {"binary_complex_modulo_rejected",
     "fixture:test/fixtures/should_reject/binary_complex_modulo.c",
     "E3122"},
    {"binary_complex_bitwise_rejected",
     "fixture:test/fixtures/should_reject/binary_complex_bitwise.c",
     "E3122"},
    {"binary_complex_shift_rejected",
     "fixture:test/fixtures/should_reject/binary_complex_shift.c",
     "E3122"},
    {"binary_struct_relational_rejected",
     "fixture:test/fixtures/should_reject/binary_struct_relational.c",
     "E3122"},
    {"binary_pointer_integer_relational_rejected",
     "fixture:test/fixtures/should_reject/binary_pointer_integer_relational.c",
     "E3122"},
    {"binary_struct_logical_and_rejected",
     "fixture:test/fixtures/should_reject/binary_struct_logical_and.c",
     "E3122"},
    {"binary_struct_logical_or_rejected",
     "fixture:test/fixtures/should_reject/binary_struct_logical_or.c",
     "E3122"},
    {"modulo_floating_operands_rejected",
     "fixture:test/fixtures/should_reject/modulo_floating_operands.c",
     "E3122"},
    {"bitwise_floating_operand_rejected",
     "fixture:test/fixtures/should_reject/bitwise_floating_operand.c",
     "E3122"},
    {"shift_floating_operand_rejected",
     "fixture:test/fixtures/should_reject/shift_floating_operand.c",
     "E3122"},
    {"binary_integer_shift_pointer_rejected",
     "fixture:test/fixtures/should_reject/binary_integer_shift_pointer.c",
     "E3122"},
    {"binary_pointer_add_floating_rejected",
     "fixture:test/fixtures/should_reject/binary_pointer_add_floating.c",
     "E3122"},
    {"binary_pointer_sub_floating_rejected",
     "fixture:test/fixtures/should_reject/binary_pointer_sub_floating.c",
     "E3122"},
    {"increment_void_pointer_rejected",
     "fixture:test/fixtures/should_reject/increment_void_pointer.c",
     "E3123"},
    {"decrement_incomplete_pointer_rejected",
     "fixture:test/fixtures/should_reject/decrement_incomplete_pointer.c",
     "E3123"},
    {"compound_void_pointer_add_rejected",
     "fixture:test/fixtures/should_reject/compound_void_pointer_add.c",
     "E3099"},
    {"compound_incomplete_pointer_add_rejected",
     "fixture:test/fixtures/should_reject/compound_incomplete_pointer_add.c",
     "E3099"},
    {"compound_function_pointer_add_rejected",
     "fixture:test/fixtures/should_reject/compound_function_pointer_add.c",
     "E3099"},
    {"compound_incomplete_array_pointer_add_rejected",
     "fixture:test/fixtures/should_reject/"
     "compound_incomplete_array_pointer_add.c",
     "E3099"},
    {"compound_complex_modulo_rejected",
     "fixture:test/fixtures/should_reject/compound_complex_modulo.c",
     "E3099"},
    {"compound_complex_bitwise_rejected",
     "fixture:test/fixtures/should_reject/compound_complex_bitwise.c",
     "E3099"},
    {"compound_complex_shift_rejected",
     "fixture:test/fixtures/should_reject/compound_complex_shift.c",
     "E3099"},
    {"conditional_pointer_nonzero_integer_rejected",
     "fixture:test/fixtures/should_reject/conditional_pointer_nonzero_integer.c",
     "E3101"},
    {"conditional_struct_condition_rejected",
     "fixture:test/fixtures/should_reject/conditional_struct_condition.c",
     "E3100"},
    {"conditional_distinct_struct_types_rejected",
     "fixture:test/fixtures/should_reject/conditional_distinct_struct_types.c",
     "E3101"},
    {"conditional_void_scalar_operands_rejected",
     "fixture:test/fixtures/should_reject/conditional_void_scalar_operands.c",
     "E3101"},
    {"conditional_incompatible_object_pointers_rejected",
     "fixture:test/fixtures/should_reject/"
     "conditional_incompatible_object_pointers.c",
     "E3101"},
    {"conditional_function_and_void_pointers",
     "fixture:test/fixtures/should_reject/"
     "conditional_function_and_void_pointers.c",
     "E3101"},
    {"conditional_different_structs_rejected",
     "fixture:test/fixtures/should_reject/conditional_different_structs.c",
     "E3101"},
    {"conditional_incompatible_function_pointers_rejected",
     "fixture:test/fixtures/should_reject/"
     "conditional_incompatible_function_pointers.c",
     "E3101"},
    {"conditional_incompatible_nested_callback_pointers",
     "fixture:test/fixtures/should_reject/"
     "conditional_incompatible_nested_callback_pointers.c",
     "E3101"},
    {"conditional_incompatible_positive_enum_function_pointer",
     "fixture:test/fixtures/should_reject/"
     "conditional_incompatible_positive_enum_function_pointer.c",
     "E3101"},
    {"conditional_incompatible_negative_enum_function_pointer",
     "fixture:test/fixtures/should_reject/"
     "conditional_incompatible_negative_enum_function_pointer.c",
     "E3101"},
    {"conditional_object_function_pointer_rejected",
     "fixture:test/fixtures/should_reject/"
     "conditional_object_function_pointer.c",
     "E3101"},
    {"conditional_differently_qualified_array_pointers_rejected",
     "fixture:test/fixtures/should_reject/"
     "conditional_differently_qualified_array_pointers.c",
     "E3101"},
    {"conditional_atomic_plain_pointers_rejected",
     "fixture:test/fixtures/should_reject/"
     "conditional_atomic_plain_pointers.c",
     "E3101"},
    {"enum_incompatible_integer_pointer",
     "fixture:test/fixtures/should_reject/"
     "assign_enum_incompatible_integer_pointer.c",
     "E3099"},
    {"enum_incompatible_nested_integer_pointer",
     "fixture:test/fixtures/should_reject/"
     "assign_enum_incompatible_nested_integer_pointer.c",
     "E3099"},
    {"enum_negative_incompatible_nested_unsigned_pointer",
     "fixture:test/fixtures/should_reject/"
     "assign_enum_negative_incompatible_nested_unsigned_pointer.c",
     "E3099"},
    {"enum_incompatible_integer_pointer_static_assert_rejected",
     "fixture:test/fixtures/should_reject/"
     "enum_incompatible_integer_pointer.c",
     "E3012"},
    {"enum_incompatible_nested_integer_pointer_static_assert_rejected",
     "fixture:test/fixtures/should_reject/"
     "enum_incompatible_nested_integer_pointer.c",
     "E3012"},
    {"enum_negative_incompatible_nested_unsigned_pointer_static_assert_rejected",
     "fixture:test/fixtures/should_reject/"
     "enum_negative_incompatible_nested_unsigned_pointer.c",
     "E3012"},
    {"distinct_enum_pointer_compatibility",
     "fixture:test/fixtures/should_reject/assign_distinct_enum_pointer.c",
     "E3099"},
    {"distinct_enum_pointer_static_assert_rejected",
     "fixture:test/fixtures/should_reject/distinct_enum_pointer_compatibility.c",
     "E3012"},
    {"assign_discards_const_pointer",
     "fixture:test/fixtures/should_reject/"
     "assign_discards_const_pointer.c",
     "E3078"},
    {"argument_discards_const_pointer",
     "fixture:test/fixtures/should_reject/"
     "argument_discards_const_pointer.c",
     "E3121"},
    {"return_discards_const_pointer",
     "fixture:test/fixtures/should_reject/"
     "return_discards_const_pointer.c",
     "E3109"},
    {"add_const_through_double_pointer",
     "fixture:test/fixtures/should_reject/"
     "add_const_through_double_pointer.c",
     "E3099"},
    {"discard_const_through_double_pointer",
     "fixture:test/fixtures/should_reject/"
     "discard_const_through_double_pointer.c",
     "E3099"},
    {"object_double_pointer_to_void_double_pointer",
     "fixture:test/fixtures/should_reject/"
     "object_double_pointer_to_void_double_pointer.c",
     "E3099"},
    {"function_pointer_to_void_pointer",
     "fixture:test/fixtures/should_reject/function_pointer_to_void_pointer.c",
     "E3099"},
    {"void_pointer_to_function_pointer",
     "fixture:test/fixtures/should_reject/void_pointer_to_function_pointer.c",
     "E3099"},
    {"compare_function_and_void_pointers",
     "fixture:test/fixtures/should_reject/compare_function_and_void_pointers.c",
     "E3122"},
    {"assign_incompatible_function_pointer",
     "fixture:test/fixtures/should_reject/assign_incompatible_function_pointer.c",
     "E3099"},
    {"compare_incompatible_function_pointers",
     "fixture:test/fixtures/should_reject/"
     "compare_incompatible_function_pointers.c",
     "E3122"},
    {"return_incompatible_function_pointer",
     "fixture:test/fixtures/should_reject/"
     "return_incompatible_function_pointer.c",
     "E3108"},
    {"assign_incompatible_nested_callback_pointer",
     "fixture:test/fixtures/should_reject/"
     "assign_incompatible_nested_callback_pointer.c",
     "E3099"},
    {"compare_incompatible_nested_callback_pointers",
     "fixture:test/fixtures/should_reject/"
     "compare_incompatible_nested_callback_pointers.c",
     "E3122"},
    {"argument_incompatible_nested_callback_pointer",
     "fixture:test/fixtures/should_reject/"
     "argument_incompatible_nested_callback_pointer.c",
     "E3120"},
    {"return_incompatible_nested_callback_pointer",
     "fixture:test/fixtures/should_reject/"
     "return_incompatible_nested_callback_pointer.c",
     "E3108"},
    {"function_redefinition_different_return_rejected",
     "fixture:test/fixtures/should_reject/func_redef_different_ret.c",
     "E3064"},
    {"function_parameter_int_long_mismatch_rejected",
     "fixture:test/fixtures/should_reject/"
     "function_parameter_int_long_mismatch.c",
     "E3064"},
    {"function_parameter_int_unsigned_mismatch_rejected",
     "fixture:test/fixtures/should_reject/"
     "function_parameter_int_unsigned_mismatch.c",
     "E3064"},
    {"function_parameter_char_signed_char_mismatch_rejected",
     "fixture:test/fixtures/should_reject/"
     "function_parameter_char_signed_char_mismatch.c",
     "E3064"},
    {"function_parameter_float_double_mismatch_rejected",
     "fixture:test/fixtures/should_reject/"
     "function_parameter_float_double_mismatch.c",
     "E3064"},
    {"function_parameter_pointer_base_mismatch_rejected",
     "fixture:test/fixtures/should_reject/"
     "function_parameter_pointer_base_mismatch.c",
     "E3064"},
    {"function_parameter_pointee_qualifier_mismatch_rejected",
     "fixture:test/fixtures/should_reject/"
     "function_parameter_pointee_qualifier_mismatch.c",
     "E3064"},
    {"function_parameter_nested_qualifier_mismatch_rejected",
     "fixture:test/fixtures/should_reject/"
     "function_parameter_nested_qualifier_mismatch.c",
     "E3064"},
    {"function_return_qualifier_redeclaration_mismatch",
     "fixture:test/fixtures/should_reject/"
     "function_return_qualifier_redeclaration_mismatch.c",
     "E3064"},
    {"function_pointer_return_qualifier_redeclaration_mismatch",
     "fixture:test/fixtures/should_reject/"
     "function_pointer_return_qualifier_redeclaration_mismatch.c",
     "E3064"},
    {"nested_array_inner_bound_function_redeclaration_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_array_inner_bound_function_redeclaration_mismatch.c",
     "E3064"},
    {"nested_array_inner_bound_function_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_array_inner_bound_function_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_return_array_bound_redeclaration_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_return_array_bound_redeclaration_mismatch.c",
     "E3064"},
    {"nested_callback_return_array_bound_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_return_array_bound_pointer_mismatch.c",
     "E3099"},
    {"function_return_qualifier_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "function_return_qualifier_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_return_qualifier_redeclaration_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_return_qualifier_redeclaration_mismatch.c",
     "E3064"},
    {"nested_callback_return_qualifier_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_return_qualifier_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_pointer_return_qualifier_redeclaration_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_pointer_return_qualifier_redeclaration_mismatch.c",
     "E3064"},
    {"nested_callback_pointer_return_qualifier_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_pointer_return_qualifier_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_return_pointee_qualifier_redeclaration_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_return_pointee_qualifier_redeclaration_mismatch.c",
     "E3064"},
    {"nested_callback_return_pointee_qualifier_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_return_pointee_qualifier_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_array_element_qualifier_redeclaration_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_array_element_qualifier_redeclaration_mismatch.c",
     "E3064"},
    {"nested_callback_array_element_qualifier_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_array_element_qualifier_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_unprototyped_narrow_redeclaration_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_unprototyped_narrow_redeclaration_mismatch.c",
     "E3064"},
    {"nested_callback_unprototyped_narrow_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_unprototyped_narrow_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_variadic_redeclaration_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_variadic_redeclaration_mismatch.c",
     "E3064"},
    {"nested_callback_variadic_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_variadic_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_return_function_narrow_redeclaration_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_return_function_narrow_redeclaration_mismatch.c",
     "E3064"},
    {"nested_callback_return_function_narrow_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_return_function_narrow_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_atomic_array_element_redeclaration_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_atomic_array_element_redeclaration_mismatch.c",
     "E3064"},
    {"nested_callback_atomic_array_element_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_atomic_array_element_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_return_volatile_pointer_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_return_volatile_pointer_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_return_restrict_pointer_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_return_restrict_pointer_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_return_atomic_pointer_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_return_atomic_pointer_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_return_atomic_pointee_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_return_atomic_pointee_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_return_function_const_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_return_function_const_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_return_function_volatile_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_return_function_volatile_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_return_function_atomic_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_return_function_atomic_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_atomic_function_pointer_parameter_redeclaration_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_atomic_function_pointer_parameter_redeclaration_mismatch.c",
     "E3064"},
    {"nested_callback_atomic_function_pointer_parameter_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_atomic_function_pointer_parameter_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_const_small_aggregate_return_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_const_small_aggregate_return_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_volatile_large_aggregate_return_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_volatile_large_aggregate_return_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_const_aggregate_return_redeclaration_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_const_aggregate_return_redeclaration_mismatch.c",
     "E3064"},
    {"nested_callback_atomic_aggregate_return_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_atomic_aggregate_return_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_atomic_aggregate_return_redeclaration_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_atomic_aggregate_return_redeclaration_mismatch.c",
     "E3064"},
    {"nested_callback_large_atomic_aggregate_return_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_large_atomic_aggregate_return_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_large_atomic_aggregate_return_redeclaration_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_large_atomic_aggregate_return_redeclaration_mismatch.c",
     "E3064"},
    {"nested_callback_large_atomic_aggregate_parameter_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_large_atomic_aggregate_parameter_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_large_atomic_aggregate_parameter_redeclaration_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_large_atomic_aggregate_parameter_redeclaration_mismatch.c",
     "E3064"},
    {"nested_callback_large_atomic_union_parameter_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_large_atomic_union_parameter_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_large_atomic_union_parameter_redeclaration_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_large_atomic_union_parameter_redeclaration_mismatch.c",
     "E3064"},
    {"nested_callback_atomic_complex_parameter_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_atomic_complex_parameter_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_atomic_complex_parameter_redeclaration_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_atomic_complex_parameter_redeclaration_mismatch.c",
     "E3064"},
    {"nested_callback_atomic_distinct_enum_parameter_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_atomic_distinct_enum_parameter_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_atomic_enum_signedness_parameter_redeclaration_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_atomic_enum_signedness_parameter_redeclaration_mismatch.c",
     "E3064"},
    {"nested_callback_atomic_subinteger_parameter_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_atomic_subinteger_parameter_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_atomic_subinteger_signedness_parameter_redeclaration_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_atomic_subinteger_signedness_parameter_redeclaration_mismatch.c",
     "E3064"},
    {"nested_callback_atomic_floating_parameter_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_atomic_floating_parameter_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_atomic_floating_rank_parameter_redeclaration_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_atomic_floating_rank_parameter_redeclaration_mismatch.c",
     "E3064"},
    {"nested_callback_atomic_long_parameter_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_atomic_long_parameter_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_atomic_long_rank_parameter_redeclaration_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_atomic_long_rank_parameter_redeclaration_mismatch.c",
     "E3064"},
    {"nested_callback_atomic_long_signedness_parameter_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_atomic_long_signedness_parameter_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_atomic_pointer_parameter_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_atomic_pointer_parameter_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_atomic_pointer_pointee_parameter_redeclaration_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_atomic_pointer_pointee_parameter_redeclaration_mismatch.c",
     "E3064"},
    {"nested_callback_atomic_complex_return_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_atomic_complex_return_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_atomic_complex_return_redeclaration_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_atomic_complex_return_redeclaration_mismatch.c",
     "E3064"},
    {"nested_callback_atomic_union_return_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_atomic_union_return_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_atomic_union_return_redeclaration_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_atomic_union_return_redeclaration_mismatch.c",
     "E3064"},
    {"nested_callback_atomic_distinct_enum_return_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_atomic_distinct_enum_return_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_atomic_enum_signedness_return_redeclaration_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_atomic_enum_signedness_return_redeclaration_mismatch.c",
     "E3064"},
    {"nested_callback_atomic_subinteger_return_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_atomic_subinteger_return_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_atomic_subinteger_signedness_return_redeclaration_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_atomic_subinteger_signedness_return_redeclaration_mismatch.c",
     "E3064"},
    {"nested_callback_atomic_floating_return_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_atomic_floating_return_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_atomic_floating_rank_return_redeclaration_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_atomic_floating_rank_return_redeclaration_mismatch.c",
     "E3064"},
    {"nested_callback_atomic_long_return_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_atomic_long_return_pointer_mismatch.c",
     "E3099"},
    {"nested_callback_atomic_long_rank_return_redeclaration_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_atomic_long_rank_return_redeclaration_mismatch.c",
     "E3064"},
    {"nested_callback_atomic_long_signedness_return_pointer_mismatch",
     "fixture:test/fixtures/should_reject/"
     "nested_callback_atomic_long_signedness_return_pointer_mismatch.c",
     "E3099"},
    {"assign_plain_to_atomic_pointer_rejected",
     "fixture:test/fixtures/should_reject/"
     "assign_plain_to_atomic_pointer.c",
     "E3099"},
    {"assign_atomic_to_plain_pointer_rejected",
     "fixture:test/fixtures/should_reject/"
     "assign_atomic_to_plain_pointer.c",
     "E3099"},
    {"assignment_plain_to_atomic_pointer_rejected",
     "fixture:test/fixtures/should_reject/"
     "assignment_plain_to_atomic_pointer.c",
     "E3099"},
    {"call_plain_to_atomic_pointer_rejected",
     "fixture:test/fixtures/should_reject/call_plain_to_atomic_pointer.c",
     "E3120"},
    {"call_atomic_to_plain_pointer_rejected",
     "fixture:test/fixtures/should_reject/call_atomic_to_plain_pointer.c",
     "E3120"},
    {"return_plain_to_atomic_pointer_rejected",
     "fixture:test/fixtures/should_reject/return_plain_to_atomic_pointer.c",
     "E3108"},
    {"return_atomic_to_plain_pointer_rejected",
     "fixture:test/fixtures/should_reject/return_atomic_to_plain_pointer.c",
     "E3108"},
    {"binary_atomic_plain_pointer_equality_rejected",
     "fixture:test/fixtures/should_reject/"
     "binary_atomic_plain_pointer_equality.c",
     "E3122"},
    {"binary_atomic_plain_pointer_relational_rejected",
     "fixture:test/fixtures/should_reject/"
     "binary_atomic_plain_pointer_relational.c",
     "E3122"},
    {"binary_atomic_plain_pointer_subtraction_rejected",
     "fixture:test/fixtures/should_reject/"
     "binary_atomic_plain_pointer_subtraction.c",
     "E3122"},
    {"initializer_const_array_pointer_to_plain_rejected",
     "fixture:test/fixtures/should_reject/"
     "initializer_const_array_pointer_to_plain.c",
     "E3078"},
    {"call_const_array_pointer_to_plain_rejected",
     "fixture:test/fixtures/should_reject/"
     "call_const_array_pointer_to_plain.c",
     "E3121"},
    {"return_const_array_pointer_to_plain_rejected",
     "fixture:test/fixtures/should_reject/"
     "return_const_array_pointer_to_plain.c",
     "E3109"},
    {"arrow_nonpointer_operand_rejected",
     "fixture:test/fixtures/should_reject/arrow_nonpointer_operand.c",
     "E3005"},
    {"address_of_rvalue_rejected",
     "fixture:test/fixtures/should_reject/address_of_rvalue.c",
     "E3112"},
    {"address_generic_selected_bitfield_rejected",
     "fixture:test/fixtures/should_reject/"
     "address_generic_selected_bitfield.c",
     "E3113"},
    {"deref_int_rejected",
     "fixture:test/fixtures/should_reject/deref_int.c",
     "E3064"},
    {"unary_plus_pointer_rejected",
     "fixture:test/fixtures/should_reject/unary_plus_pointer.c",
     "E3064"},
    {"logical_not_struct_rejected",
     "fixture:test/fixtures/should_reject/logical_not_struct.c",
     "E3064"},
    {"bitwise_not_pointer_rejected",
     "fixture:test/fixtures/should_reject/bitwise_not_pointer.c",
     "E3064"},
    {"bitwise_not_complex_rejected",
     "fixture:test/fixtures/should_reject/bitwise_not_complex.c",
     "E3064"},
    {"break_outside_loop_or_switch",
     "fixture:test/fixtures/should_reject/"
     "break_outside_loop_or_switch.c",
     "E3068"},
    {"continue_outside_loop",
     "fixture:test/fixtures/should_reject/continue_outside_loop.c",
     "E3068"},
    {"case_outside_switch",
     "fixture:test/fixtures/should_reject/case_outside_switch.c",
     "E3068"},
    {"default_outside_switch",
     "fixture:test/fixtures/should_reject/default_outside_switch.c",
     "E3068"},
    {"duplicate_label",
     "fixture:test/fixtures/should_reject/duplicate_label.c",
     "E3067"},
    {"duplicate_default",
     "fixture:test/fixtures/should_reject/duplicate_default.c",
     "E3061"},
    {"duplicate_case_simple",
     "fixture:test/fixtures/should_reject/duplicate_case_simple.c",
     "E3060"},
    {"goto_undefined_label",
     "fixture:test/fixtures/should_reject/"
     "goto_undefined_label.c",
     "E3064"},
    {"case_nonconstant_expression",
     "fixture:test/fixtures/should_reject/"
     "case_nonconstant_expression.c",
     "E3064"},
    {"switch_complex_control_rejected",
     "fixture:test/fixtures/should_reject/switch_complex_control.c",
     "E3107"},
    {"alignas_weaker_than_natural_rejected",
     "fixture:test/fixtures/should_reject/alignas_weaker_than_natural.c",
     "E3064"},
    {"alignas_non_power_of_two_rejected",
     "fixture:test/fixtures/should_reject/alignas_non_power_of_two.c",
     "E3064"},
    {"alignas_signed_overflow_rejected",
     "fixture:test/fixtures/should_reject/alignas_signed_overflow.c",
     "E3064"},
    {"alignas_incomplete_array_type_rejected",
     "fixture:test/fixtures/should_reject/"
     "alignas_incomplete_array_type.c",
     "E3064"},
    {"alignas_incomplete_array_typedef_rejected",
     "fixture:test/fixtures/should_reject/"
     "alignas_incomplete_array_typedef.c",
     "E3064"},
    {"alignas_typedef_rejected",
     "fixture:test/fixtures/should_reject/alignas_typedef.c",
     "E3064"},
    {"alignas_function_declaration_rejected",
     "fixture:test/fixtures/should_reject/"
     "alignas_function_declaration.c",
     "E3064"},
    {"alignas_function_definition_rejected",
     "fixture:test/fixtures/should_reject/"
     "alignas_function_definition.c",
     "E3064"},
    {"alignas_parameter_rejected",
     "fixture:test/fixtures/should_reject/alignas_parameter.c",
     "E3064"},
    {"alignas_bitfield_rejected",
     "fixture:test/fixtures/should_reject/alignas_bitfield.c",
     "E3064"},
    {"alignas_register_object_rejected",
     "fixture:test/fixtures/should_reject/alignas_register_object.c",
     "E3064"},
    {"alignas_redeclaration_missing_definition_rejected",
     "fixture:test/fixtures/should_reject/"
     "alignas_redeclaration_missing_definition.c",
     "E3064"},
    {"alignas_redeclaration_conflict_rejected",
     "fixture:test/fixtures/should_reject/"
     "alignas_redeclaration_conflict.c",
     "E3064"},
    {"alignas_zero_missing_definition_rejected",
     "fixture:test/fixtures/should_reject/"
     "alignas_zero_missing_definition.c",
     "E3064"},
    {"alignas_zero_conflicts_strict_rejected",
     "fixture:test/fixtures/should_reject/"
     "alignas_zero_conflicts_strict.c",
     "E3064"},
    {"alignas_aligned_then_plain_tentative_rejected",
     "fixture:test/fixtures/should_reject/"
     "alignas_aligned_then_plain_tentative.c",
     "E3064"},
    {"alignas_definition_then_aligned_extern_rejected",
     "fixture:test/fixtures/should_reject/"
     "alignas_definition_then_aligned_extern.c",
     "E3064"},
    {"alignas_block_scope_function_declaration_rejected",
     "fixture:test/fixtures/should_reject/"
     "alignas_block_scope_function_declaration.c",
     "E3064"},
    {"block_scope_extern_alignment_conflict_rejected",
     "fixture:test/fixtures/should_reject/"
     "block_scope_extern_alignment_conflict.c",
     "E3064"},
    {"block_scope_extern_alignment_missing_definition_rejected",
     "fixture:test/fixtures/should_reject/"
     "block_scope_extern_alignment_missing_definition.c",
     "E3064"},
    {"inline_object_rejected",
     "fixture:test/fixtures/should_reject/inline_object.c",
     "E3064"},
    {"noreturn_object_rejected",
     "fixture:test/fixtures/should_reject/noreturn_object.c",
     "E3064"},
    {"inline_main_function_definition_rejected",
     "fixture:test/fixtures/should_reject/"
     "inline_main_function_definition.c",
     "E3064"},
    {"noreturn_main_function_definition_rejected",
     "fixture:test/fixtures/should_reject/"
     "noreturn_main_function_definition.c",
     "E3064"},
    {"inline_main_function_declaration_rejected",
     "fixture:test/fixtures/should_reject/"
     "inline_main_function_declaration.c",
     "E3064"},
    {"noreturn_main_block_declaration_rejected",
     "fixture:test/fixtures/should_reject/"
     "noreturn_main_block_declaration.c",
     "E3064"},
    {"void_main_function_definition_rejected",
     "fixture:test/fixtures/should_reject/"
     "void_main_function_definition.c",
     "E3064"},
    {"qualified_main_function_declaration_rejected",
     "fixture:test/fixtures/should_reject/"
     "qualified_main_function_declaration.c",
     "E3064"},
    {"invalid_main_first_parameter_rejected",
     "fixture:test/fixtures/should_reject/"
     "invalid_main_first_parameter.c",
     "E3064"},
    {"invalid_main_second_parameter_rejected",
     "fixture:test/fixtures/should_reject/"
     "invalid_main_second_parameter.c",
     "E3064"},
    {"qualified_main_second_parameter_rejected",
     "fixture:test/fixtures/should_reject/"
     "qualified_main_second_parameter.c",
     "E3064"},
    {"invalid_main_block_declaration_rejected",
     "fixture:test/fixtures/should_reject/"
     "invalid_main_block_declaration.c",
     "E3064"},
    {"variadic_main_function_definition_rejected",
     "fixture:test/fixtures/should_reject/"
     "variadic_main_function_definition.c",
     "E3064"},
    {"variadic_main_function_declaration_rejected",
     "fixture:test/fixtures/should_reject/"
     "variadic_main_function_declaration.c",
     "E3064"},
    {"variadic_main_block_declaration_rejected",
     "fixture:test/fixtures/should_reject/"
     "variadic_main_block_declaration.c",
     "E3064"},
    {"inline_internal_object_reference_rejected",
     "fixture:test/fixtures/should_reject/"
     "inline_internal_object_reference.c",
     "E3064"},
    {"inline_internal_function_reference_rejected",
     "fixture:test/fixtures/should_reject/"
     "inline_internal_function_reference.c",
     "E3064"},
    {"inline_function_typedef_rejected",
     "fixture:test/fixtures/should_reject/inline_function_typedef.c",
     "E3064"},
    {"inline_function_pointer_object_rejected",
     "fixture:test/fixtures/should_reject/inline_function_pointer_object.c",
     "E3064"},
    {"noreturn_function_pointer_object_rejected",
     "fixture:test/fixtures/should_reject/noreturn_function_pointer_object.c",
     "E3064"},
    {"inline_mixed_function_object_declarators_rejected",
     "fixture:test/fixtures/should_reject/"
     "inline_mixed_function_object_declarators.c",
     "E3064"},
    {"noreturn_mixed_function_object_declarators_rejected",
     "fixture:test/fixtures/should_reject/"
     "noreturn_mixed_function_object_declarators.c",
     "E3064"},
    {"inline_function_pointer_parameter_rejected",
     "fixture:test/fixtures/should_reject/inline_function_pointer_parameter.c",
     "E3064"},
    {"noreturn_function_pointer_parameter_rejected",
     "fixture:test/fixtures/should_reject/"
     "noreturn_function_pointer_parameter.c",
     "E3064"},
    {"inline_function_pointer_member_rejected",
     "fixture:test/fixtures/should_reject/inline_function_pointer_member.c",
     "E3064"},
    {"noreturn_function_pointer_member_rejected",
     "fixture:test/fixtures/should_reject/noreturn_function_pointer_member.c",
     "E3064"},
    {"inline_function_type_name_rejected",
     "fixture:test/fixtures/should_reject/inline_function_type_name.c",
     "E3064"},
    {"noreturn_function_type_name_rejected",
     "fixture:test/fixtures/should_reject/noreturn_function_type_name.c",
     "E3064"},
    {"file_scope_auto_rejected",
     "fixture:test/fixtures/should_reject/file_scope_auto_object.c",
     "E3064"},
    {"file_scope_register_rejected",
     "fixture:test/fixtures/should_reject/file_scope_register_object.c",
     "E3064"},
    {"block_thread_local_without_static_or_extern_rejected",
     "fixture:test/fixtures/should_reject/block_thread_local_without_linkage.c",
     "E3064"},
    {"thread_local_function_declaration_rejected",
     "fixture:test/fixtures/should_reject/thread_local_function.c",
     "E3064"},
    {"auto_function_definition_rejected",
     "fixture:test/fixtures/should_reject/auto_function_definition.c",
     "E3064"},
    {"register_function_definition_rejected",
     "fixture:test/fixtures/should_reject/"
     "register_function_definition.c",
     "E3064"},
    {"block_static_function_declaration_rejected",
     "fixture:test/fixtures/should_reject/block_static_function.c",
     "E3064"},
    {"block_auto_function_declaration_rejected",
     "fixture:test/fixtures/should_reject/block_auto_function.c",
     "E3064"},
    {"block_scope_extern_initializer_rejected",
     "fixture:test/fixtures/should_reject/"
     "block_scope_extern_initializer.c",
     "E3064"},
    {"local_extern_after_automatic_rejected",
     "fixture:test/fixtures/should_reject/local_extern_after_automatic.c",
     "E3064"},
    {"automatic_after_local_extern_rejected",
     "fixture:test/fixtures/should_reject/automatic_after_local_extern.c",
     "E3067"},
    {"block_extern_after_typedef_rejected",
     "fixture:test/fixtures/should_reject/block_extern_after_typedef.c",
     "E3064"},
    {"block_extern_after_enum_constant_rejected",
     "fixture:test/fixtures/should_reject/"
     "block_extern_after_enum_constant.c",
     "E3064"},
    {"file_object_after_block_typedef_backing_rejected",
     "fixture:test/fixtures/should_reject/"
     "file_object_after_block_typedef_backing.c",
     "E3064"},
    {"file_object_after_block_enum_backing_rejected",
     "fixture:test/fixtures/should_reject/"
     "file_object_after_block_enum_backing.c",
     "E3064"},
    {"block_function_after_automatic_rejected",
     "fixture:test/fixtures/should_reject/block_function_after_automatic.c",
     "E3064"},
    {"block_function_after_typedef_rejected",
     "fixture:test/fixtures/should_reject/block_function_after_typedef.c",
     "E3064"},
    {"file_function_after_block_typedef_backing_rejected",
     "fixture:test/fixtures/should_reject/"
     "file_function_after_block_typedef_backing.c",
     "E3064"},
    {"block_function_after_enum_constant_rejected",
     "fixture:test/fixtures/should_reject/"
     "block_function_after_enum_constant.c",
     "E3064"},
    {"block_extern_object_out_of_scope_rejected",
     "fixture:test/fixtures/should_reject/"
     "block_extern_object_out_of_scope.c",
     "E3066"},
    {"block_extern_object_leaks_to_later_function_rejected",
     "fixture:test/fixtures/should_reject/"
     "block_extern_object_leaks_to_later_function.c",
     "E3066"},
    {"duplicate_local_variable_rejected",
     "fixture:test/fixtures/should_reject/dup_local_var.c",
     "E3067"},
    {"duplicate_typedef_conflict_rejected",
     "fixture:test/fixtures/should_reject/dup_typedef_conflict.c",
     "E3064"},
    {"repeated_vla_typedef_rejected",
     "fixture:test/fixtures/should_reject/repeated_vla_typedef.c",
     "E3064"},
    {"repeated_pointer_to_vla_typedef_rejected",
     "fixture:test/fixtures/should_reject/"
     "repeated_pointer_to_vla_typedef.c",
     "E3064"},
    {"repeated_vla_typedef_via_alias_rejected",
     "fixture:test/fixtures/should_reject/"
     "repeated_vla_typedef_via_alias.c",
     "E3064"},
    {"duplicate_enum_name_rejected",
     "fixture:test/fixtures/should_reject/dup_enum_name.c",
     "E3067"},
    {"automatic_after_block_function_rejected",
     "fixture:test/fixtures/should_reject/"
     "automatic_after_block_function.c",
     "E3067"},
    {"typedef_after_block_function_rejected",
     "fixture:test/fixtures/should_reject/"
     "typedef_after_block_function.c",
     "E3064"},
    {"enum_constant_after_block_function_rejected",
     "fixture:test/fixtures/should_reject/"
     "enum_constant_after_block_function.c",
     "E3064"},
    {"block_function_after_block_extern_object_rejected",
     "fixture:test/fixtures/should_reject/"
     "block_function_after_block_extern_object.c",
     "E3064"},
    {"block_extern_object_after_block_function_rejected",
     "fixture:test/fixtures/should_reject/"
     "block_extern_object_after_block_function.c",
     "E3064"},
    {"typedef_after_block_extern_object_rejected",
     "fixture:test/fixtures/should_reject/"
     "typedef_after_block_extern_object.c",
     "E3064"},
    {"enum_constant_after_block_extern_object_rejected",
     "fixture:test/fixtures/should_reject/"
     "enum_constant_after_block_extern_object.c",
     "E3064"},
    {"block_typedef_initializer_rejected",
     "fixture:test/fixtures/should_reject/block_typedef_initializer.c",
     "E3064"},
    {"typedef_after_parameter_rejected",
     "fixture:test/fixtures/should_reject/typedef_after_parameter.c",
     "E3064"},
    {"automatic_after_parameter_rejected",
     "fixture:test/fixtures/should_reject/automatic_after_parameter.c",
     "E3067"},
    {"enum_constant_after_parameter_rejected",
     "fixture:test/fixtures/should_reject/enum_constant_after_parameter.c",
     "E3064"},
    {"block_function_after_parameter_rejected",
     "fixture:test/fixtures/should_reject/block_function_after_parameter.c",
     "E3064"},
    {"for_initializer_typedef_declaration_rejected",
     "fixture:test/fixtures/should_reject/"
     "for_initializer_typedef_declaration.c",
     "E3064"},
    {"for_initializer_static_object_rejected",
     "fixture:test/fixtures/should_reject/for_initializer_static_object.c",
     "E3064"},
    {"for_initializer_extern_object_rejected",
     "fixture:test/fixtures/should_reject/for_initializer_extern_object.c",
     "E3064"},
    {"for_initializer_function_declaration_rejected",
     "fixture:test/fixtures/should_reject/"
     "for_initializer_function_declaration.c",
     "E3064"},
    {"for_initializer_standalone_tag_rejected",
     "fixture:test/fixtures/should_reject/"
     "for_initializer_standalone_tag.c",
     "E3064"},
    {"static_parameter_rejected",
     "fixture:test/fixtures/should_reject/static_parameter.c",
     "E3064"},
    {"extern_parameter_rejected",
     "fixture:test/fixtures/should_reject/parameter_extern_storage.c",
     "E3064"},
    {"static_aggregate_member_rejected",
     "fixture:test/fixtures/should_reject/aggregate_member_static_storage.c",
     "E3064"},
    {"thread_local_aggregate_member_rejected",
     "fixture:test/fixtures/should_reject/thread_local_aggregate_member.c",
     "E3064"},
    {"typedef_with_static_rejected",
     "fixture:test/fixtures/should_reject/typedef_with_static.c",
     "E3064"},
    {"duplicate_thread_local_rejected",
     "fixture:test/fixtures/should_reject/repeated_thread_local.c",
     "E3064"},
    {"thread_local_redeclaration_missing_specifier_rejected",
     "fixture:test/fixtures/should_reject/"
     "thread_local_redeclaration_missing_specifier.c",
     "E3064"},
    {"thread_local_redeclaration_added_specifier_rejected",
     "fixture:test/fixtures/should_reject/"
     "thread_local_redeclaration_added_specifier.c",
     "E3064"},
    {"typedef_standalone_tag_rejected",
     "fixture:test/fixtures/should_reject/typedef_standalone_tag.c",
     "E3064"},
    {"post_type_storage_class_in_type_name_rejected",
     "fixture:test/fixtures/should_reject/"
     "post_type_storage_class_in_type_name.c",
     "E3064"},
    {"typedef_thread_local_rejected",
     "fixture:test/fixtures/should_reject/typedef_thread_local.c",
     "E3064"},
    {"noreturn_typedef_function_rejected",
     "fixture:test/fixtures/should_reject/noreturn_typedef_function.c",
     "E3064"},
    {"inline_parameter_rejected",
     "fixture:test/fixtures/should_reject/inline_parameter.c",
     "E3064"},
    {"storage_class_conflict_rejected",
     "fixture:test/fixtures/should_reject/storage_class_conflict.c",
     "E3064"},
    {"object_extern_then_static_linkage_rejected",
     "fixture:test/fixtures/should_reject/"
     "object_extern_then_static_linkage.c",
     "E3064"},
    {"object_static_then_plain_linkage_rejected",
     "fixture:test/fixtures/should_reject/"
     "object_static_then_plain_linkage.c",
     "E3064"},
    {"object_plain_then_static_linkage_rejected",
     "fixture:test/fixtures/should_reject/"
     "object_plain_then_static_linkage.c",
     "E3064"},
    {"function_extern_then_static_linkage_rejected",
     "fixture:test/fixtures/should_reject/"
     "function_extern_then_static_linkage.c",
     "E3064"},
    {"function_plain_then_static_linkage_rejected",
     "fixture:test/fixtures/should_reject/"
     "function_plain_then_static_linkage.c",
     "E3064"},
    {"block_extern_then_file_static_linkage_rejected",
     "fixture:test/fixtures/should_reject/"
     "block_extern_then_file_static_linkage.c",
     "E3064"},
    {"thread_local_extern_then_static_linkage_rejected",
     "fixture:test/fixtures/should_reject/"
     "thread_local_extern_then_static_linkage.c",
     "E3064"},
    {"static_tentative_incomplete_record_rejected",
     "fixture:test/fixtures/should_reject/"
     "static_tentative_incomplete_record.c",
     "E3064"},
    {"unresolved_tentative_incomplete_record_rejected",
     "fixture:test/fixtures/should_reject/"
     "unresolved_tentative_incomplete_record.c",
     "E3037"},
    {"funcdef_unnamed_param_rejected",
     "fixture:test/fixtures/should_reject/funcdef_unnamed_parameter.c",
     "E3065"},
    {"c11_implicit_int_objects_rejected",
     "fixture:test/fixtures/should_reject/c11_implicit_int_objects.c",
     "E3088"},
    {"c11_implicit_return_type_rejected",
     "fixture:test/fixtures/should_reject/c11_implicit_return_type.c",
     "E3088"},
    {"old_style_parameter_missing_declaration_rejected",
     "fixture:test/fixtures/should_reject/"
     "old_style_parameter_missing_declaration.c",
     "E3088"},
    {"old_style_parameter_extra_declaration_rejected",
     "fixture:test/fixtures/should_reject/"
     "old_style_parameter_extra_declaration.c",
     "does not match an identifier in the old-style function parameter list"},
    {"old_style_parameter_duplicate_identifier_rejected",
     "fixture:test/fixtures/should_reject/"
     "old_style_parameter_duplicate_identifier.c",
     "duplicate parameter name 'value' in old-style function identifier list"},
    {"old_style_typedef_name_parameter_rejected",
     "fixture:test/fixtures/should_reject/old_style_typedef_name_parameter.c",
     "typedef name 'count_t' cannot be used as an old-style function parameter name"},
    {"old_style_parameter_duplicate_declaration_rejected",
     "fixture:test/fixtures/should_reject/"
     "old_style_parameter_duplicate_declaration.c",
     "old-style function parameter 'value' is declared more than once"},
    {"old_style_parameter_initializer_rejected",
     "fixture:test/fixtures/should_reject/old_style_parameter_initializer.c",
     "old-style function parameter cannot have an initializer"},
    {"old_style_const_array_parameter_reassignment_rejected",
     "fixture:test/fixtures/should_reject/"
     "old_style_const_array_parameter_reassignment.c",
     "E3077"},
    {"old_style_parameter_static_storage_rejected",
     "fixture:test/fixtures/should_reject/"
     "old_style_parameter_static_storage.c",
     "old-style parameter declaration may only use the 'register' storage class"},
    {"old_style_identifier_list_declaration_rejected",
     "fixture:test/fixtures/should_reject/"
     "old_style_identifier_list_declaration.c",
     "an identifier list is only permitted in an old-style function definition"},
    {"old_style_nested_identifier_list_rejected",
     "fixture:test/fixtures/should_reject/old_style_nested_identifier_list.c",
     "an identifier list is only permitted in the outermost declarator"},
    {"old_style_incompatible_float_prototype_rejected",
     "fixture:test/fixtures/should_reject/"
     "old_style_incompatible_float_prototype.c",
     "E3064"},
    {"old_style_incompatible_bool_prototype_rejected",
     "fixture:test/fixtures/should_reject/"
     "old_style_incompatible_bool_prototype.c",
     "E3064"},
    {"old_style_incompatible_complex_prototype_rejected",
     "fixture:test/fixtures/should_reject/"
     "old_style_incompatible_complex_prototype.c",
     "E3064"},
    {"old_style_array_pointee_qualifier_prototype_rejected",
     "fixture:test/fixtures/should_reject/"
     "old_style_array_pointee_qualifier_prototype.c",
     "E3064"},
    {"old_style_function_parameter_prototype_rejected",
     "fixture:test/fixtures/should_reject/"
     "old_style_function_parameter_prototype.c",
     "E3064"},
    {"old_style_variadic_function_parameter_prototype_rejected",
     "fixture:test/fixtures/should_reject/"
     "old_style_variadic_function_parameter_prototype.c",
     "E3064"},
    {"old_style_declaration_variadic_prototype_rejected",
     "fixture:test/fixtures/should_reject/"
     "old_style_declaration_variadic_prototype.c",
     "E3064"},
    {"old_style_parameter_count_prototype_rejected",
     "fixture:test/fixtures/should_reject/"
     "old_style_parameter_count_prototype.c",
     "E3064"},
    {"old_style_signed_enum_unsigned_prototype_rejected",
     "fixture:test/fixtures/should_reject/"
     "old_style_signed_enum_unsigned_prototype.c",
     "E3064"},
    {"old_style_unsigned_enum_signed_prototype_rejected",
     "fixture:test/fixtures/should_reject/"
     "old_style_unsigned_enum_signed_prototype.c",
     "E3064"},
    {"old_style_distinct_record_prototype_rejected",
     "fixture:test/fixtures/should_reject/"
     "old_style_distinct_record_prototype.c",
     "E3064"},
    {"old_style_record_kind_prototype_rejected",
     "fixture:test/fixtures/should_reject/"
     "old_style_record_kind_prototype.c",
     "E3064"},
    {"old_style_atomic_int_plain_prototype_rejected",
     "fixture:test/fixtures/should_reject/"
     "old_style_atomic_int_plain_prototype.c",
     "E3064"},
    {"old_style_plain_int_atomic_prototype_rejected",
     "fixture:test/fixtures/should_reject/"
     "old_style_plain_int_atomic_prototype.c",
     "E3064"},
    {"old_style_atomic_short_int_prototype_rejected",
     "fixture:test/fixtures/should_reject/"
     "old_style_atomic_short_int_prototype.c",
     "E3064"},
    {"old_style_atomic_pointer_plain_prototype_rejected",
     "fixture:test/fixtures/should_reject/"
     "old_style_atomic_pointer_plain_prototype.c",
     "E3064"},
    {"prototype_atomic_pointer_plain_redeclaration_rejected",
     "fixture:test/fixtures/should_reject/"
     "prototype_atomic_pointer_plain_redeclaration.c",
     "E3064"},
    {"pointer_to_array_bound_redeclaration_rejected",
     "fixture:test/fixtures/should_reject/"
     "pointer_to_array_bound_redeclaration.c",
     "E3064"},
    {"call_argument_atomic_function_pointer_rejected",
     "fixture:test/fixtures/should_reject/"
     "call_argument_atomic_function_pointer.c",
     "E3120"},
    {"atomic_aggregate_callback_assignment_rejected",
     "fixture:test/fixtures/should_reject/"
     "atomic_aggregate_callback_assignment.c",
     "E3099"},
    {"atomic_aggregate_callback_member_initializer_rejected",
     "fixture:test/fixtures/should_reject/"
     "atomic_aggregate_callback_member_initializer.c",
     "E3099"},
    {"atomic_aggregate_callback_conditional_rejected",
     "fixture:test/fixtures/should_reject/"
     "atomic_aggregate_callback_conditional.c",
     "E3101"},
    {"atomic_aggregate_callback_pointer_to_pointer_rejected",
     "fixture:test/fixtures/should_reject/"
     "atomic_aggregate_callback_pointer_to_pointer.c",
     "E3099"},
    {"atomic_aggregate_variadic_callback_assignment_rejected",
     "fixture:test/fixtures/should_reject/"
     "atomic_aggregate_variadic_callback_assignment.c",
     "E3099"},
    {"atomic_aggregate_callback_return_rejected",
     "fixture:test/fixtures/should_reject/"
     "atomic_aggregate_callback_return.c",
     "E3108"},
    {"old_style_parameter_enum_constant_scope_rejected",
     "fixture:test/fixtures/should_reject/"
     "old_style_parameter_enum_constant_scope.c",
     "E3066"},
    {"old_style_parameter_tag_scope_rejected",
     "fixture:test/fixtures/should_reject/"
     "old_style_parameter_tag_scope.c",
     "E3064"},
    {"old_style_parameter_later_bound_identifier_rejected",
     "fixture:test/fixtures/should_reject/"
     "old_style_parameter_later_bound_identifier.c",
     "E3066"},
    {"old_style_same_declaration_later_bound_identifier_rejected",
     "fixture:test/fixtures/should_reject/"
     "old_style_same_declaration_later_bound_identifier.c",
     "E3066"},
    {"c11_block_implicit_int_rejected",
     "fixture:test/fixtures/should_reject/c11_block_implicit_int.c",
     "E3088"},
    {"include_macro_invalid_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_include_macro_invalid.c",
     ":2: E1001"},
    {"include_wide_string_filename_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_include_wide_string.c",
     ":1: E1001"},
    {"include_utf16_string_filename_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_include_utf16_string.c",
     ":1: E1001"},
    {"include_utf32_string_filename_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_include_utf32_string.c",
     ":1: E1001"},
    {"include_utf8_string_filename_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_include_utf8_string.c",
     ":1: E1001"},
    {"include_macro_wide_string_filename_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_include_macro_wide_string.c",
     ":2: E1001"},
    {"include_macro_utf8_string_filename_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_include_macro_utf8_string.c",
     ":2: E1001"},
    {"pragma_operator_invalid_rejected",
     "fixture:test/fixtures/should_reject/pragma_operator_invalid.c",
     "E1043"},
    {"macro_define_missing_name_location_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_macro_define_missing_name.c",
     ":1: E1018"},
    {"macro_undef_missing_name_location_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_macro_undef_missing_name.c",
     ":1: E1018"},
    {"macro_redefinition_replacement_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_macro_redefinition.c",
     ":2: E1044"},
    {"macro_redefinition_form_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_macro_redefinition_form.c",
     ":2: E1044"},
    {"macro_redefinition_parameter_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_macro_redefinition_parameter.c",
     ":2: E1044"},
    {"macro_redefinition_whitespace_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_macro_redefinition_whitespace.c",
     ":2: E1044"},
    {"macro_duplicate_parameter_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_macro_duplicate_parameter.c",
     ":1: E1045"},
    {"macro_parameter_missing_comma_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_macro_parameter_missing_comma.c",
     ":1: E1046"},
    {"macro_parameter_trailing_comma_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_macro_parameter_trailing_comma.c",
     ":1: E1046"},
    {"macro_parameter_list_unclosed_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_macro_parameter_list_unclosed.c",
     ":1: E1046"},
    {"macro_va_args_parameter_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_macro_va_args_parameter.c",
     ":1: E1047"},
    {"macro_va_args_nonvariadic_body_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_macro_va_args_nonvariadic.c",
     ":1: E1047"},
    {"macro_va_args_name_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_macro_va_args_name.c",
     ":1: E1047"},
    {"macro_stringize_nonparameter_unused_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_macro_stringize_nonparameter.c",
     ":1: E1048"},
    {"macro_stringize_missing_parameter_unused_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_macro_stringize_missing_parameter.c",
     ":1: E1048"},
    {"macro_function_paste_leading_unused_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_macro_function_paste_leading.c",
     ":1: E1031"},
    {"macro_function_paste_trailing_unused_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_macro_paste_trailing.c",
     ":1: E1031"},
    {"macro_object_paste_leading_unused_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_macro_object_paste_leading.c",
     ":1: E1031"},
    {"macro_object_paste_trailing_unused_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_macro_object_paste_trailing.c",
     ":1: E1031"},
    {"macro_invalid_paste_result_location_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_macro_invalid_paste_result.c",
     ":2: E1030"},
    {"macro_object_invalid_paste_definition_location_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_macro_object_invalid_paste_definition.c",
     ":1: E1030"},
    {"macro_mapped_invalid_paste_invocation_location_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_macro_mapped_invalid_paste_invocation.c",
     "mapped_invocation.c:70: E1030"},
    {"macro_header_invalid_paste_definition_location_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_macro_header_invalid_paste_definition.c",
     "macro_invalid_paste_header.h:1: E1030"},
    {"preprocess_if_token_limit_location_rejected",
     "fixture:test/fixtures/compiler_limits/"
     "preprocess_if_token_limit_location.c",
     "if_token_limit.c:66: E1037"},
    {"preprocess_if_eval_limit_location_rejected",
     "fixture:test/fixtures/compiler_limits/"
     "preprocess_if_eval_limit_location.c",
     "if_eval_limit.c:77: E1038"},
    {"macro_expansion_limit_location_rejected",
     "fixture:test/fixtures/compiler_limits/"
     "macro_expansion_limit_location.c",
     "macro_expansion_limit.c:91: E1029"},
    {"macro_arg_expansion_limit_location_rejected",
     "fixture:test/fixtures/compiler_limits/"
     "macro_arg_expansion_limit_location.c",
     "macro_arg_expansion_limit.c:93: E1029"},
    {"predefined_macro_define_stdc_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_predefined_macro_define.c",
     ":1: E1049"},
    {"predefined_macro_define_stdc_version_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_predefined_macro_define_stdc_version.c",
     ":1: E1049"},
    {"predefined_macro_define_stdc_hosted_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_predefined_macro_define_stdc_hosted.c",
     ":1: E1049"},
    {"predefined_macro_define_stdc_no_threads_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_predefined_macro_define_stdc_no_threads.c",
     ":1: E1049"},
    {"predefined_macro_define_stdc_utf_16_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_predefined_macro_define_stdc_utf_16.c",
     ":1: E1049"},
    {"predefined_macro_define_stdc_utf_32_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_predefined_macro_define_stdc_utf_32.c",
     ":1: E1049"},
    {"predefined_macro_define_line_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_predefined_macro_define_line.c",
     ":1: E1049"},
    {"predefined_macro_define_file_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_predefined_macro_define_file.c",
     ":1: E1049"},
    {"predefined_macro_define_date_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_predefined_macro_define_date.c",
     ":1: E1049"},
    {"predefined_macro_define_time_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_predefined_macro_define_time.c",
     ":1: E1049"},
    {"predefined_macro_undef_stdc_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_predefined_macro_undef_stdc.c",
     ":1: E1049"},
    {"predefined_macro_undef_stdc_version_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_predefined_macro_undef_stdc_version.c",
     ":1: E1049"},
    {"predefined_macro_undef_stdc_hosted_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_predefined_macro_undef_stdc_hosted.c",
     ":1: E1049"},
    {"predefined_macro_undef_line_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_predefined_macro_undef_line.c",
     ":1: E1049"},
    {"predefined_macro_undef_file_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_predefined_macro_undef_file.c",
     ":1: E1049"},
    {"predefined_macro_undef_date_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_predefined_macro_undef_date.c",
     ":1: E1049"},
    {"predefined_macro_undef_time_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_predefined_macro_undef_time.c",
     ":1: E1049"},
    {"predefined_macro_undef_conditional_feature_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_predefined_macro_undef_stdc_no_threads.c",
     ":1: E1049"},
    {"predefined_macro_undef_stdc_utf_16_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_predefined_macro_undef_stdc_utf_16.c",
     ":1: E1049"},
    {"predefined_macro_undef_stdc_utf_32_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_predefined_macro_undef_stdc_utf_32.c",
     ":1: E1049"},
    {"macro_undef_va_args_rejected",
     "fixture:test/fixtures/should_reject/preprocess_macro_undef_va_args.c",
     ":1: E1047"},
    {"macro_undef_extra_identifier_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_macro_undef_extra_tokens.c",
     ":2: E1050"},
    {"macro_undef_extra_parens_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_macro_undef_extra_parens.c",
     ":2: E1050"},
    {"preprocess_ifdef_extra_tokens_rejected",
     "fixture:test/fixtures/should_reject/preprocess_ifdef_extra_tokens.c",
     ":2: E1051"},
    {"preprocess_ifndef_extra_tokens_rejected",
     "fixture:test/fixtures/should_reject/preprocess_ifndef_extra_tokens.c",
     ":1: E1051"},
    {"preprocess_else_extra_tokens_rejected",
     "fixture:test/fixtures/should_reject/preprocess_else_extra_tokens.c",
     ":3: E1051"},
    {"preprocess_endif_extra_tokens_rejected",
     "fixture:test/fixtures/should_reject/preprocess_endif_extra_tokens.c",
     ":3: E1051"},
    {"preprocess_unknown_directive_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_unknown_directive.c",
     ":1: E1052"},
    {"preprocess_nonidentifier_directive_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_nonidentifier_directive.c",
     ":1: E1052"},
    {"preprocess_unterminated_true_conditional_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_unterminated_true_conditional.c",
     ":1: E1053"},
    {"preprocess_unterminated_false_conditional_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_unterminated_false_conditional.c",
     ":1: E1053"},
    {"preprocess_if_empty_expression_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_if_empty_expression.c",
     ":1: E1008"},
    {"preprocess_if_comment_only_expression_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_if_comment_only_expression.c",
     ":1: E1008"},
    {"preprocess_if_macro_empty_expression_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_if_macro_empty_expression.c",
     ":2: E1008"},
    {"preprocess_elif_empty_expression_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_elif_empty_expression.c",
     ":3: E1008"},
    {"preprocess_if_floating_literal_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_if_floating_literal.c",
     ":1: E1007"},
    {"preprocess_if_string_literal_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_if_string_literal.c",
     ":1: E1008"},
    {"preprocess_if_cast_rejected",
     "fixture:test/fixtures/should_reject/preprocess_if_cast.c",
     ":1: E1008"},
    {"preprocess_if_comma_expression_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_if_comma_expression.c",
     ":1: E1012"},
    {"preprocess_if_sizeof_rejected",
     "fixture:test/fixtures/should_reject/preprocess_if_sizeof.c",
     ":1: E1008"},
    {"preprocess_if_incomplete_logical_and_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_if_incomplete_logical_and.c",
     ":1: E1008"},
    {"preprocess_if_incomplete_logical_or_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_if_incomplete_logical_or.c",
     ":1: E1008"},
    {"preprocess_if_incomplete_conditional_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_if_incomplete_conditional.c",
     ":1: E1008"},
    {"preprocess_if_division_by_zero_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_if_division_by_zero.c",
     ":1: E1009"},
    {"preprocess_if_modulo_by_zero_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_if_modulo_by_zero.c",
     ":1: E1009"},
    {"preprocess_defined_missing_operand_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_defined_missing_operand.c",
     ":1: E1010"},
    {"preprocess_defined_empty_operand_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_defined_empty_operand.c",
     ":1: E1010"},
    {"preprocess_defined_numeric_operand_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_defined_numeric_operand.c",
     ":1: E1010"},
    {"preprocess_defined_nested_parentheses_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_defined_nested_parentheses.c",
     ":2: E1010"},
    {"preprocess_defined_missing_rparen_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_defined_missing_rparen.c",
     ":2: E1011"},
    {"preprocess_defined_extra_tokens_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_defined_extra_tokens.c",
     ":2: E1012"},
    {"preprocess_stray_else_rejected",
     "fixture:test/fixtures/should_reject/preprocess_stray_else.c",
     ":1: E1019"},
    {"preprocess_stray_elif_rejected",
     "fixture:test/fixtures/should_reject/preprocess_stray_elif.c",
     ":1: E1021"},
    {"preprocess_stray_endif_rejected",
     "fixture:test/fixtures/should_reject/preprocess_stray_endif.c",
     ":1: E1023"},
    {"preprocess_duplicate_else_rejected",
     "fixture:test/fixtures/should_reject/preprocess_duplicate_else.c",
     ":5: E1020"},
    {"preprocess_elif_after_else_rejected",
     "fixture:test/fixtures/should_reject/preprocess_elif_after_else.c",
     ":5: E1022"},
    {"preprocess_ifdef_missing_name_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_ifdef_missing_name.c",
     ":1: E1018"},
    {"preprocess_ifdef_nonidentifier_name_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_ifdef_nonidentifier_name.c",
     ":1: E1018"},
    {"preprocess_ifndef_missing_name_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_ifndef_missing_name.c",
     ":1: E1018"},
    {"preprocess_ifndef_nonidentifier_name_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_ifndef_nonidentifier_name.c",
     ":1: E1018"},
    {"preprocess_error_empty_rejected",
     "fixture:test/fixtures/should_reject/preprocess_error_empty.c",
     ":1: E1033"},
    {"preprocess_error_location_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_error_location.c",
     ":1: E1033"},
    {"preprocess_error_token_spelling_rejected",
     "fixture:test/fixtures/should_reject/preprocess_error_token_spelling.c",
     "alpha+beta 0x2a L\"wide\" '\\n'"},
    {"preprocess_error_macro_name_not_expanded_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_error_macro_name_not_expanded.c",
     "error: MESSAGE"},
    {"preprocess_error_comment_whitespace_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_error_comment_whitespace.c",
     "error: alpha beta"},
    {"include_missing_filename_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_include_missing_filename.c",
     ":1: E1001"},
    {"include_empty_quoted_filename_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_include_empty_quoted_filename.c",
     ":1: E1001"},
    {"include_empty_angle_filename_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_include_empty_angle_filename.c",
     ":1: E1001"},
    {"include_angle_missing_gt_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_include_missing_gt.c",
     ":1: E1017"},
    {"include_extra_tokens_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_include_extra_tokens.c",
     ":1: E1001"},
    {"include_concatenated_filename_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_include_concatenated_filename.c",
     ":1: E1001"},
    {"include_macro_extra_tokens_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_include_macro_extra_tokens.c",
     ":2: E1001"},
    {"include_macro_concatenated_filename_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_include_macro_concatenated_filename.c",
     ":2: E1001"},
    {"include_not_found_location_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_include_not_found.c",
     ":1: E1034"},
    {"include_parent_directory_location_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_include_parent_directory.c",
     ":1: E1003"},
    {"include_absolute_path_location_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_include_absolute_path.c",
     ":1: E1002"},
    {"macro_invocation_zero_parameter_extra_argument_rejected",
     "fixture:test/fixtures/should_reject/"
     "macro_invocation_zero_parameter_extra_argument.c",
     ":2: E1024"},
    {"macro_invocation_zero_parameter_comma_argument_rejected",
     "fixture:test/fixtures/should_reject/"
     "macro_invocation_zero_parameter_comma_argument.c",
     ":2: E1024"},
    {"macro_invocation_single_parameter_extra_argument_rejected",
     "fixture:test/fixtures/should_reject/"
     "macro_invocation_single_parameter_extra_argument.c",
     ":2: E1024"},
    {"macro_invocation_two_parameter_missing_argument_rejected",
     "fixture:test/fixtures/should_reject/"
     "macro_invocation_two_parameter_missing_argument.c",
     ":2: E1024"},
    {"macro_invocation_two_parameter_empty_call_rejected",
     "fixture:test/fixtures/should_reject/"
     "macro_invocation_two_parameter_empty_call.c",
     ":2: E1024"},
    {"macro_invocation_missing_rparen_rejected",
     "fixture:test/fixtures/should_reject/macro_invocation_missing_rparen.c",
     ":2: E1026"},
    {"macro_invocation_nested_missing_rparen_rejected",
     "fixture:test/fixtures/should_reject/"
     "macro_invocation_nested_missing_rparen.c",
     ":2: E1026"},
    {"macro_invocation_variadic_named_argument_missing_rejected",
     "fixture:test/fixtures/should_reject/"
     "macro_invocation_variadic_named_argument_missing.c",
     ":2: E1024"},
    {"preprocess_header_unterminated_conditional_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_header_unterminated_conditional.c",
     "preprocess_conditional_open.h:1: E1053"},
    {"preprocess_header_cross_file_endif_rejected",
     "fixture:test/fixtures/should_reject/preprocess_header_cross_file_endif.c",
     "preprocess_conditional_close.h:1: E1023"},
    {"preprocess_header_cross_file_else_rejected",
     "fixture:test/fixtures/should_reject/preprocess_header_cross_file_else.c",
     "preprocess_conditional_else.h:1: E1019"},
    {"preprocess_header_cross_file_elif_rejected",
     "fixture:test/fixtures/should_reject/preprocess_header_cross_file_elif.c",
     "preprocess_conditional_elif.h:1: E1021"},
    {"line_directive_missing_number_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_line_missing_number.c",
     ":1: E1027"},
    {"line_directive_zero_number_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_line_zero_number.c",
     ":1: E1027"},
    {"line_directive_above_maximum_number_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_line_above_maximum_number.c",
     ":1: E1027"},
    {"line_directive_identifier_number_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_line_identifier_number.c",
     ":1: E1027"},
    {"line_directive_hex_number_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_line_hex_number.c",
     ":1: E1027"},
    {"line_directive_suffixed_number_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_line_suffixed_number.c",
     ":1: E1027"},
    {"line_directive_signed_number_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_line_signed_number.c",
     ":1: E1027"},
    {"line_directive_character_number_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_line_character_number.c",
     ":1: E1027"},
    {"line_directive_floating_number_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_line_floating_number.c",
     ":1: E1027"},
    {"line_directive_identifier_filename_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_line_identifier_filename.c",
     ":1: E1028"},
    {"line_directive_wide_filename_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_line_wide_filename.c",
     ":1: E1028"},
    {"line_directive_u8_filename_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_line_u8_filename.c",
     ":1: E1028"},
    {"line_directive_extra_tokens_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_line_extra_tokens.c",
     ":1: E1054"},
    {"line_directive_concatenated_filename_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_line_concatenated_filename.c",
     ":1: E1054"},
    {"line_directive_macro_extra_tokens_rejected",
     "fixture:test/fixtures/should_reject/"
     "preprocess_line_macro_extra_tokens.c",
     ":2: E1054"},
    {"static_character_array_string_too_long_rejected",
     "fixture:test/fixtures/should_reject/static_character_array_string_too_long.c",
     "E3027"},
    {"static_local_character_array_string_too_long_rejected",
     "fixture:test/fixtures/should_reject/static_local_character_array_string_too_long.c",
     "E3027"},
    {"static_character_array_embedded_null_too_long_rejected",
     "fixture:test/fixtures/should_reject/static_character_array_embedded_null_too_long.c",
     "E3027"},
    {"static_character_array_concatenated_string_too_long_rejected",
     "fixture:test/fixtures/should_reject/static_character_array_concatenated_string_too_long.c",
     "E3027"},
    {"automatic_character_array_string_too_long_rejected",
     "fixture:test/fixtures/should_reject/automatic_character_array_string_too_long.c",
     "E3027"},
    {"automatic_character_array_braced_string_too_long_rejected",
     "fixture:test/fixtures/should_reject/automatic_character_array_braced_string_too_long.c",
     "E3027"},
    {"character_array_member_string_too_long_rejected",
     "fixture:test/fixtures/should_reject/character_array_member_string_too_long.c",
     "E3027"},
    {"multidimensional_character_array_string_too_long_rejected",
     "fixture:test/fixtures/should_reject/multidimensional_character_array_string_too_long.c",
     "E3027"},
    {"character_array_compound_literal_string_too_long_rejected",
     "fixture:test/fixtures/should_reject/character_array_compound_literal_string_too_long.c",
     "E3027"},
    {"different_encoded_string_prefixes_rejected",
     "fixture:test/fixtures/should_reject/different_encoded_string_prefixes.c",
     "E3058"},
    {"string_array_utf16_signed_short_element_rejected",
     "fixture:test/fixtures/should_reject/"
     "string_array_utf16_signed_short_element.c",
     "E3099"},
    {"string_array_utf16_signed_short_inferred_rejected",
     "fixture:test/fixtures/should_reject/"
     "string_array_utf16_signed_short_inferred.c",
     "E3099"},
    {"string_array_utf32_signed_int_element_rejected",
     "fixture:test/fixtures/should_reject/"
     "string_array_utf32_signed_int_element.c",
     "E3099"},
    {"string_array_wide_unsigned_int_element_rejected",
     "fixture:test/fixtures/should_reject/"
     "string_array_wide_unsigned_int_element.c",
     "E3099"},
    {"string_array_utf16_local_signed_short_rejected",
     "fixture:test/fixtures/should_reject/"
     "string_array_utf16_local_signed_short.c",
     "E3099"},
    {"string_array_atomic_character_element_rejected",
     "fixture:test/fixtures/should_reject/"
     "string_array_atomic_character_element.c",
     "E3099"},
    {"string_array_typedef_enum_wide_element_rejected",
     "fixture:test/fixtures/should_reject/"
     "string_array_typedef_enum_wide_element.c",
     "E3099"},
    {"string_array_typedef_bool_element_rejected",
     "fixture:test/fixtures/should_reject/"
     "string_array_typedef_bool_element.c",
     "E3099"},
    {"string_array_typedef_enum_inferred_global_rejected",
     "fixture:test/fixtures/should_reject/"
     "string_array_typedef_enum_inferred_global.c",
     "E3099"},
    {"string_array_typedef_bool_inferred_global_rejected",
     "fixture:test/fixtures/should_reject/"
     "string_array_typedef_bool_inferred_global.c",
     "E3099"},
    {"string_array_typedef_enum_inferred_local_rejected",
     "fixture:test/fixtures/should_reject/"
     "string_array_typedef_enum_inferred_local.c",
     "E3099"},
    {"string_array_typedef_enum_inferred_static_local_rejected",
     "fixture:test/fixtures/should_reject/"
     "string_array_typedef_enum_inferred_static_local.c",
     "E3099"},
    {"string_array_typedef_enum_multidimensional_rejected",
     "fixture:test/fixtures/should_reject/"
     "string_array_typedef_enum_multidimensional.c",
     "E3099"},
    {"string_array_compound_literal_utf16_signed_short_rejected",
     "fixture:test/fixtures/should_reject/"
     "string_array_compound_literal_utf16_signed_short.c",
     "E3099"},
    {"string_array_compound_literal_utf16_inferred_signed_short_rejected",
     "fixture:test/fixtures/should_reject/"
     "string_array_compound_literal_utf16_inferred_signed_short.c",
     "E3099"},
    {"string_array_typedef_enum_compound_literal_rejected",
     "fixture:test/fixtures/should_reject/"
     "string_array_typedef_enum_compound_literal.c",
     "E3099"},
    {"encoded_multidimensional_string_overlong_global_rejected",
     "fixture:test/fixtures/should_reject/"
     "encoded_multidimensional_string_overlong_global.c",
     "E3027"},
    {"encoded_string_utf8_unicode_too_long_global_rejected",
     "fixture:test/fixtures/should_reject/"
     "encoded_string_utf8_unicode_too_long_global.c",
     "E3027"},
    {"encoded_string_utf16_unicode_too_long_local_rejected",
     "fixture:test/fixtures/should_reject/"
     "encoded_string_utf16_unicode_too_long_local.c",
     "E3027"},
    {"encoded_string_utf16_unicode_too_long_member_rejected",
     "fixture:test/fixtures/should_reject/"
     "encoded_string_utf16_unicode_too_long_member.c",
     "E3027"},
    {"encoded_string_utf16_unicode_too_long_multidimensional_rejected",
     "fixture:test/fixtures/should_reject/"
     "encoded_string_utf16_unicode_too_long_multidimensional.c",
     "E3027"},
    {"encoded_string_utf8_unicode_too_long_compound_rejected",
     "fixture:test/fixtures/should_reject/"
     "encoded_string_utf8_unicode_too_long_compound.c",
     "E3027"},
    {"encoded_string_utf8_embedded_null_too_long_global_rejected",
     "fixture:test/fixtures/should_reject/"
     "encoded_string_utf8_embedded_null_too_long_global.c",
     "E3027"},
    {"encoded_string_utf16_embedded_null_too_long_local_rejected",
     "fixture:test/fixtures/should_reject/"
     "encoded_string_utf16_embedded_null_too_long_local.c",
     "E3027"},
    {"encoded_string_utf32_embedded_null_too_long_member_rejected",
     "fixture:test/fixtures/should_reject/"
     "encoded_string_utf32_embedded_null_too_long_member.c",
     "E3027"},
    {"encoded_string_wide_embedded_null_too_long_multidimensional_rejected",
     "fixture:test/fixtures/should_reject/"
     "encoded_string_wide_embedded_null_too_long_multidimensional.c",
     "E3027"},
    {"encoded_string_utf16_embedded_null_too_long_compound_rejected",
     "fixture:test/fixtures/should_reject/"
     "encoded_string_utf16_embedded_null_too_long_compound.c",
     "E3027"},
    {"encoded_union_utf16_too_long_global_rejected",
     "fixture:test/fixtures/should_reject/"
     "encoded_union_utf16_too_long_global.c",
     "E3027"},
    {"encoded_union_utf32_too_long_nested_local_rejected",
     "fixture:test/fixtures/should_reject/"
     "encoded_union_utf32_too_long_nested_local.c",
     "E3027"},
    {"encoded_union_wide_too_long_array_designator_rejected",
     "fixture:test/fixtures/should_reject/"
     "encoded_union_wide_too_long_array_designator.c",
     "E3027"},
    {"encoded_union_utf8_too_long_compound_rejected",
     "fixture:test/fixtures/should_reject/"
     "encoded_union_utf8_too_long_compound.c",
     "E3027"},
    {"encoded_union_utf16_embedded_null_too_long_rejected",
     "fixture:test/fixtures/should_reject/"
     "encoded_union_utf16_embedded_null_too_long.c",
     "E3027"},
    {"encoded_union_incompatible_element_rejected",
     "fixture:test/fixtures/should_reject/"
     "encoded_union_incompatible_element.c",
     "E3099"},
    {"encoded_multidimensional_string_wrong_local_element_rejected",
     "fixture:test/fixtures/should_reject/"
     "encoded_multidimensional_string_wrong_local_element.c",
     "E3099"},
    {"encoded_multidimensional_string_wrong_global_member_rejected",
     "fixture:test/fixtures/should_reject/"
     "encoded_multidimensional_string_wrong_global_member.c",
     "E3099"},
    {"encoded_multidimensional_string_overlong_local_member_rejected",
     "fixture:test/fixtures/should_reject/"
     "encoded_multidimensional_string_overlong_local_member.c",
     "E3027"},
    {"encoded_multidimensional_string_wrong_compound_literal_rejected",
     "fixture:test/fixtures/should_reject/"
     "encoded_multidimensional_string_wrong_compound_literal.c",
     "E3099"},
    {"encoded_string_row_designator_overlong_rejected",
     "fixture:test/fixtures/should_reject/"
     "encoded_string_row_designator_overlong.c",
     "E3027"},
    {"encoded_string_row_designator_wrong_member_type_rejected",
     "fixture:test/fixtures/should_reject/"
     "encoded_string_row_designator_wrong_member_type.c",
     "E3099"},
    {"predefined_function_name_element_assignment_rejected",
     "fixture:test/fixtures/should_reject/"
     "predefined_function_name_element_assignment.c",
     "E3077"},
    {"predefined_function_name_mutable_pointer_rejected",
     "fixture:test/fixtures/should_reject/"
     "predefined_function_name_mutable_pointer.c",
     "E3078"},
    {"predefined_function_name_local_redeclaration_rejected",
     "fixture:test/fixtures/should_reject/"
     "predefined_function_name_local_redeclaration.c",
     "E3067"},
    {"predefined_function_name_typedef_redeclaration_rejected",
     "fixture:test/fixtures/should_reject/"
     "predefined_function_name_typedef_redeclaration.c",
     "E3067"},
    {"predefined_function_name_parameter_redeclaration_rejected",
     "fixture:test/fixtures/should_reject/"
     "predefined_function_name_parameter_redeclaration.c",
     "E3067"},
    {"predefined_function_name_function_redeclaration_rejected",
     "fixture:test/fixtures/should_reject/"
     "predefined_function_name_function_redeclaration.c",
     "E3067"},
    {"predefined_function_name_file_scope_use_rejected",
     "const char *name = __func__;\n"
     "int main(void) { return name != 0; }\n",
     "E3066"},
    {"tokenizer_hex_missing_digits_rejected",
     "fixture:test/fixtures/should_reject/"
     "tokenizer_hex_missing_digits.c",
     "E2017"},
    {"tokenizer_octal_invalid_digit_rejected",
     "fixture:test/fixtures/should_reject/"
     "tokenizer_octal_invalid_digit.c",
     "E2023"},
    {"tokenizer_int_suffix_duplicate_rejected",
     "fixture:test/fixtures/should_reject/"
     "tokenizer_int_suffix_duplicate.c",
     "E2016"},
    {"tokenizer_unterminated_comment_rejected",
     "fixture:test/fixtures/should_reject/"
     "tokenizer_unterminated_comment.c",
     "E2009"},
    {"tokenizer_unexpected_character_rejected",
     "fixture:test/fixtures/should_reject/"
     "tokenizer_unexpected_character.c",
     "E2028"},
    {"tokenizer_unterminated_string_rejected",
     "fixture:test/fixtures/should_reject/"
     "tokenizer_unterminated_string.c",
     "E2026"},
    {"tokenizer_invalid_escape_rejected",
     "fixture:test/fixtures/should_reject/"
     "tokenizer_invalid_escape.c",
     "E2013"},
    {"tokenizer_empty_character_rejected",
     "fixture:test/fixtures/should_reject/"
     "tokenizer_empty_character.c",
     "E2024"},
    {"tokenizer_unterminated_character_rejected",
     "fixture:test/fixtures/should_reject/"
     "tokenizer_unterminated_character.c",
     "E2025"},
    {"tokenizer_integer_too_large_rejected",
     "fixture:test/fixtures/should_reject/"
     "tokenizer_integer_too_large.c",
     "E2015"},
    {"tokenizer_binary_invalid_digit_rejected",
     "fixture:test/fixtures/should_reject/"
     "tokenizer_binary_invalid_digit.c",
     "E2022"},
    {"tokenizer_int_suffix_excess_long_rejected",
     "fixture:test/fixtures/should_reject/"
     "tokenizer_int_suffix_excess_long.c",
     "E2016"},
    {"tokenizer_numeric_suffix_concatenated_rejected",
     "fixture:test/fixtures/should_reject/"
     "tokenizer_numeric_suffix_concatenated.c",
     "E2018"},
    {"tokenizer_unicode_numeric_suffix_concatenated_rejected",
     "fixture:test/fixtures/should_reject/"
     "tokenizer_unicode_numeric_suffix_concatenated.c",
     "E2018"},
    {"tokenizer_numeric_multiple_decimal_points_rejected",
     "fixture:test/fixtures/should_reject/"
     "tokenizer_numeric_multiple_decimal_points.c",
     "E2018"},
    {"tokenizer_hex_escape_missing_digits_string_rejected",
     "fixture:test/fixtures/should_reject/"
     "tokenizer_hex_escape_missing_digits_string.c",
     "E2011"},
    {"tokenizer_hex_escape_nonhex_string_rejected",
     "fixture:test/fixtures/should_reject/"
     "tokenizer_hex_escape_nonhex_string.c",
     "E2011"},
    {"tokenizer_hex_escape_missing_digits_character_rejected",
     "fixture:test/fixtures/should_reject/"
     "tokenizer_hex_escape_missing_digits_character.c",
     "E2011"},
    {"tokenizer_ucn_short_string_rejected",
     "fixture:test/fixtures/should_reject/"
     "tokenizer_ucn_short_string.c",
     "E2012"},
    {"tokenizer_ucn_out_of_range_string_rejected",
     "fixture:test/fixtures/should_reject/"
     "tokenizer_ucn_out_of_range_string.c",
     "E2012"},
    {"tokenizer_ucn_surrogate_string_rejected",
     "fixture:test/fixtures/should_reject/"
     "tokenizer_ucn_surrogate_string.c",
     "E2012"},
    {"tokenizer_ucn_control_string_rejected",
     "fixture:test/fixtures/should_reject/"
     "tokenizer_ucn_control_string.c",
     "E2012"},
    {"tokenizer_ucn_control_identifier_rejected",
     "fixture:test/fixtures/should_reject/"
     "tokenizer_ucn_control_identifier.c",
     "E2027"},
    {"tokenizer_unterminated_empty_character_rejected",
     "fixture:test/fixtures/should_reject/"
     "tokenizer_unterminated_empty_character.c",
     "E2029"},
    {"bare_return_from_nonvoid_function_rejected",
     "fixture:test/fixtures/should_reject/"
     "bare_return_from_nonvoid_function.c",
     "E3005"},
    {"return_value_from_void_function_rejected",
     "fixture:test/fixtures/should_reject/"
     "return_value_from_void_function.c",
     "E3005"},
    {"parser_dot_on_scalar_rejected",
     "fixture:test/fixtures/should_reject/"
     "parser_dot_on_scalar.c",
     "E3005"},
    {"parser_arrow_on_scalar_pointer_rejected",
     "fixture:test/fixtures/should_reject/"
     "parser_arrow_on_scalar_pointer.c",
     "E3005"},
    {"parser_unknown_member_rejected",
     "fixture:test/fixtures/should_reject/"
     "parser_unknown_member.c",
     "E3064"},
    {"parser_generic_no_matching_association_rejected",
     "fixture:test/fixtures/should_reject/"
     "parser_generic_no_matching_association.c",
     "E3044"},
    {"parser_generic_duplicate_default_rejected",
     "fixture:test/fixtures/should_reject/"
     "parser_generic_duplicate_default.c",
     "E3104"},
    {"parser_struct_control_condition_rejected",
     "fixture:test/fixtures/should_reject/"
     "parser_struct_control_condition.c",
     "E3106"},
    {"parser_cast_target_array_rejected",
     "fixture:test/fixtures/should_reject/"
     "parser_cast_target_array.c",
     "E3064"},
    {"cast_struct_to_integer_rejected",
     "fixture:test/fixtures/should_reject/"
     "cast_struct_to_integer.c",
     "E3064"},
    {"parser_function_type_member_rejected",
     "fixture:test/fixtures/should_reject/"
     "parser_function_type_member.c",
     "E3064"},
    {"parser_incomplete_type_member_rejected",
     "fixture:test/fixtures/should_reject/"
     "parser_incomplete_type_member.c",
     "E3064"},
    {"anonymous_member_typedef_struct_rejected",
     "fixture:test/fixtures/should_reject/"
     "anonymous_member_typedef_struct.c",
     "E3065"},
    {"anonymous_member_typedef_union_rejected",
     "fixture:test/fixtures/should_reject/"
     "anonymous_member_typedef_union.c",
     "E3065"},
    {"anonymous_member_tagged_reference_rejected",
     "fixture:test/fixtures/should_reject/"
     "anonymous_member_tagged_reference.c",
     "E3065"},
    {"anonymous_member_named_definition_rejected",
     "fixture:test/fixtures/should_reject/"
     "anonymous_member_named_definition.c",
     "E3065"},
    {"aggregate_anonymous_enum_only_rejected",
     "fixture:test/fixtures/should_reject/"
     "aggregate_anonymous_enum_only.c",
     "E3064"},
    {"aggregate_named_enum_without_member_rejected",
     "fixture:test/fixtures/should_reject/"
     "aggregate_named_enum_without_member.c",
     "E3065"},
    {"zero_array_bound_rejected",
     "fixture:test/fixtures/should_reject/"
     "zero_array_bound.c",
     "E3096"},
    {"parser_variadic_parameter_not_last_rejected",
     "fixture:test/fixtures/should_reject/"
     "parser_variadic_parameter_not_last.c",
     "E2006"},
    {"parser_alignof_expression_rejected",
     "fixture:test/fixtures/should_reject/"
     "parser_alignof_expression.c",
     "E3064"},
    {"parser_atomic_missing_type_rejected",
     "fixture:test/fixtures/should_reject/"
     "parser_atomic_missing_type.c",
     "E3064"},
    {"parser_alignas_missing_parenthesis_rejected",
     "fixture:test/fixtures/should_reject/"
     "parser_alignas_missing_parenthesis.c",
     "E2006"},
    {"parser_invalid_type_specifier_combination_rejected",
     "fixture:test/fixtures/should_reject/"
     "parser_invalid_type_specifier_combination.c",
     "E3006"},
    {"repeated_float_type_specifier_rejected",
     "fixture:test/fixtures/should_reject/repeated_float_type_specifier.c",
     "E3006"},
    {"repeated_double_type_specifier_rejected",
     "fixture:test/fixtures/should_reject/repeated_double_type_specifier.c",
     "E3006"},
    {"repeated_void_type_specifier_rejected",
     "fixture:test/fixtures/should_reject/repeated_void_type_specifier.c",
     "E3006"},
    {"repeated_bool_type_specifier_rejected",
     "fixture:test/fixtures/should_reject/repeated_bool_type_specifier.c",
     "E3006"},
    {"long_long_double_type_specifier_rejected",
     "fixture:test/fixtures/should_reject/long_long_double_type_specifier.c",
     "E3006"},
    {"address_of_assignment_result_rejected",
     "fixture:test/fixtures/should_reject/"
     "address_of_assignment_result.c",
     "E3112"},
    {"address_of_comma_result_rejected",
     "fixture:test/fixtures/should_reject/address_of_comma_result.c",
     "E3112"},
    {"address_of_conditional_result_rejected",
     "fixture:test/fixtures/should_reject/"
     "address_of_conditional_result.c",
     "E3112"},
    {"address_of_aggregate_return_member_rejected",
     "fixture:test/fixtures/should_reject/"
     "address_of_aggregate_return_member.c",
     "E3112"},
    {"bitfield_addr_rejected",
     "fixture:test/fixtures/should_reject/bitfield_addr.c",
     "E3113"},
    {"address_of_parenthesized_bitfield_rejected",
     "fixture:test/fixtures/should_reject/"
     "address_of_parenthesized_bitfield.c",
     "E3113"},
    {"address_of_register_array_rejected",
     "fixture:test/fixtures/should_reject/"
     "address_of_register_array.c",
     "E3119"},
    {"address_of_register_struct_member_rejected",
     "fixture:test/fixtures/should_reject/"
     "address_of_register_struct_member.c",
     "E3119"},
    {"increment_array_rejected",
     "fixture:test/fixtures/should_reject/increment_array.c",
     "E3064"},
    {"decrement_function_rejected",
     "fixture:test/fixtures/should_reject/decrement_function.c",
     "E3064"},
    {"increment_const_scalar_rejected",
     "fixture:test/fixtures/should_reject/increment_const_scalar.c",
     "E3077"},
    {"increment_const_bitfield_rejected",
     "fixture:test/fixtures/should_reject/increment_const_bitfield.c",
     "E3077"},
    {"increment_complex_rejected",
     "fixture:test/fixtures/should_reject/increment_complex.c",
     "E3064"},
    {"increment_incomplete_pointer_rejected",
     "fixture:test/fixtures/should_reject/increment_incomplete_pointer.c",
     "E3123"},
    {"increment_function_pointer_rejected",
     "fixture:test/fixtures/should_reject/increment_function_pointer.c",
     "E3123"},
    {"compound_assign_const_scalar_rejected",
     "fixture:test/fixtures/should_reject/"
     "compound_assign_const_scalar.c",
     "E3077"},
    {"compound_assign_array_rejected",
     "fixture:test/fixtures/should_reject/compound_assign_array.c",
     "E3098"},
    {"compound_assign_struct_rejected",
     "fixture:test/fixtures/should_reject/compound_assign_struct.c",
     "E3099"},
    {"compound_assign_pointer_multiply_rejected",
     "fixture:test/fixtures/should_reject/"
     "compound_assign_pointer_multiply.c",
     "E3099"},
    {"compound_assign_pointer_bitwise_rejected",
     "fixture:test/fixtures/should_reject/"
     "compound_assign_pointer_bitwise.c",
     "E3099"},
    {"void_ptr_deref_rejected",
     "fixture:test/fixtures/should_reject/void_ptr_deref.c",
     "E3064"},
    {"incomplete_lvalue_conversion_rejected",
     "fixture:test/fixtures/should_reject/"
     "incomplete_lvalue_conversion.c",
     "E3064"},
    {"incomplete_lvalue_conversion_void_cast_rejected",
     "fixture:test/fixtures/should_reject/"
     "incomplete_lvalue_conversion_void_cast.c",
     "E3064"},
    {"incomplete_lvalue_conversion_comma_left_rejected",
     "fixture:test/fixtures/should_reject/"
     "incomplete_lvalue_conversion_comma_left.c",
     "E3064"},
    {"subscript_void_pointer_rejected",
     "fixture:test/fixtures/should_reject/subscript_void_pointer.c",
     "E3064"},
    {"subscript_incomplete_pointer_rejected",
     "fixture:test/fixtures/should_reject/"
     "subscript_incomplete_pointer.c",
     "E3064"},
    {"subscript_function_pointer_rejected",
     "fixture:test/fixtures/should_reject/"
     "subscript_function_pointer.c",
     "E3064"},
    {"add_two_pointers_rejected",
     "fixture:test/fixtures/should_reject/add_two_pointers.c",
     "E3122"},
    {"multiply_pointer_rejected",
     "fixture:test/fixtures/should_reject/multiply_pointer.c",
     "E3122"},
    {"void_pointer_arithmetic_rejected",
     "fixture:test/fixtures/should_reject/void_pointer_arithmetic.c",
     "E3122"},
    {"function_pointer_arithmetic_rejected",
     "fixture:test/fixtures/should_reject/"
     "function_pointer_arithmetic.c",
     "E3122"},
    {"incomplete_pointer_arithmetic_rejected",
     "fixture:test/fixtures/should_reject/"
     "incomplete_pointer_arithmetic.c",
     "E3122"},
    {"subtract_incompatible_pointers_rejected",
     "fixture:test/fixtures/should_reject/"
     "subtract_incompatible_pointers.c",
     "E3122"},
    {"subtract_atomic_plain_pointers_rejected",
     "fixture:test/fixtures/should_reject/"
     "subtract_atomic_plain_pointers.c",
     "E3122"},
    {"relational_incompatible_pointers_rejected",
     "fixture:test/fixtures/should_reject/"
     "relational_incompatible_pointers.c",
     "E3122"},
    {"relational_void_object_pointers_rejected",
     "fixture:test/fixtures/should_reject/"
     "relational_void_object_pointers.c",
     "E3122"},
    {"relational_function_pointers_rejected",
     "fixture:test/fixtures/should_reject/"
     "relational_function_pointers.c",
     "E3122"},
    {"equality_incompatible_pointers_rejected",
     "fixture:test/fixtures/should_reject/"
     "equality_incompatible_pointers.c",
     "E3122"},
    {"array_of_flexible_record_rejected",
     "fixture:test/fixtures/should_reject/array_of_flexible_record.c",
     "E3064"},
    {"array_bound_signed_overflow_rejected",
     "fixture:test/fixtures/should_reject/array_bound_signed_overflow.c",
     "E3064"},
    {"file_scope_vla_rejected",
     "fixture:test/fixtures/should_reject/file_scope_vla.c",
     "E3064"},
    {"assign_discards_volatile_pointer_rejected",
     "fixture:test/fixtures/should_reject/"
     "assign_discards_volatile_pointer.c",
     "E3078"},
    {"sizeof_incomplete_enum_rejected",
     "fixture:test/fixtures/should_reject/sizeof_incomplete_enum.c",
     "E3117"},
    {"char16_supplementary_character_rejected",
     "fixture:test/fixtures/should_reject/char16_supplementary_character.c",
     "E2025"},
    {"char16_hex_escape_out_of_range_rejected",
     "fixture:test/fixtures/should_reject/char16_hex_escape_out_of_range.c",
     "E2025"},
    {"gnu_statement_expression_rejected",
     "fixture:test/fixtures/should_reject/gnu_statement_expression.c",
     "E3096"},
    {"gnu_attribute_rejected",
     "int value __attribute__((unused)); int main(void) { return 0; }",
     "E3096"},
    {"gnu_pragma_rejected",
     "#define VALUE 1\n#pragma push_macro(\"VALUE\")\nint main(void) { return VALUE; }\n",
     "E3096"},
    {"gnu_array_range_designator_rejected",
     "fixture:test/fixtures/should_reject/gnu_array_range_designator.c",
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

static int run_compiler_expect_fail_with_diag(
    const char *program, const char *object_path, const char *input,
    const char *expected_diag, const char *log_path) {
  int pipefd[2];
  if (pipe(pipefd) != 0) return -1;

  pid_t pid = fork();
  if (pid == 0) {
    close(pipefd[0]);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);
    freopen("/dev/null", "w", stdout);
    if (object_path) {
      execl(program, program, "-c", "-o", object_path, input, (char *)NULL);
    } else {
      execl(program, program, input, (char *)NULL);
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
  if (expected_diag && expected_diag[0] != '\0' && !strstr(diag_buf, expected_diag)) return -1;
  return 0;
}

static int run_ag_c_expect_fail_with_diag(const char *input, const char *expected_diag,
                                          const char *log_path) {
  return run_compiler_expect_fail_with_diag(
      "./build/ag_c", NULL, input, expected_diag, log_path);
}

static int run_compiler_expect_success_capture_diag(
    const char *program, const char *object_path, const char *input,
    const char *log_path) {
  int pipefd[2];
  if (pipe(pipefd) != 0) return -1;
  pid_t pid = fork();
  if (pid == 0) {
    close(pipefd[0]);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);
    freopen("/dev/null", "w", stdout);
    if (object_path)
      execl(program, program, "-c", "-o", object_path, input,
            (char *)NULL);
    else
      execl(program, program, input, (char *)NULL);
    _exit(1);
  }
  close(pipefd[1]);
  if (pid < 0) {
    close(pipefd[0]);
    return -1;
  }
  char diag_buf[8192];
  size_t used = 0;
  for (;;) {
    char sink[512];
    char *destination = diag_buf + used;
    size_t room = sizeof(diag_buf) - 1 - used;
    if (room == 0) {
      destination = sink;
      room = sizeof(sink);
    }
    ssize_t nread = read(pipefd[0], destination, room);
    if (nread <= 0) break;
    if (used < sizeof(diag_buf) - 1) {
      size_t keep = (size_t)nread;
      if (keep > sizeof(diag_buf) - 1 - used)
        keep = sizeof(diag_buf) - 1 - used;
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
  return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
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

static int log_files_contain_all(
    const char *first_path, const char *second_path,
    const char *const *needles, size_t needle_count) {
  if (!first_path || !second_path || (!needles && needle_count != 0))
    return 0;
  for (size_t i = 0; i < needle_count; i++) {
    if (!log_file_contains_substr(first_path, needles[i]) ||
        !log_file_contains_substr(second_path, needles[i]))
      return 0;
  }
  return 1;
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

static const char *wasm_object_compile_fail_skip_reason(const char *name) {
  if (name &&
      strcmp(name, "posix_off_t_pointer_identity_rejected") == 0) {
    return "wasm32 off_t is long, so assigning &long is valid";
  }
  return NULL;
}

static int run_registered_compile_fail_cases(
    const char *program, int object_mode, const char *work_dir,
    const char *log_dir, const char *summary_label) {
  if (!program || !work_dir || !log_dir ||
      mkdir_p(work_dir) != 0 || mkdir_p(log_dir) != 0)
    return 1;

  const size_t count =
      sizeof(compile_fail_cases) / sizeof(compile_fail_cases[0]);
  size_t passed = 0;
  size_t skipped = 0;
  for (size_t i = 0; i < count; i++) {
    const compile_fail_case_t *tc = &compile_fail_cases[i];
    char src_path[PATH_MAX];
    char object_path[PATH_MAX];
    char log_path[PATH_MAX];
    snprintf(src_path, sizeof(src_path), "%s/%s.c", work_dir, tc->name);
    snprintf(object_path, sizeof(object_path), "%s/%s.o", work_dir, tc->name);
    snprintf(log_path, sizeof(log_path), "%s/compile_fail_%s.log",
             log_dir, tc->name);
    if (object_mode) unlink(object_path);

    const char *skip_reason =
        object_mode ? wasm_object_compile_fail_skip_reason(tc->name) : NULL;
    if (skip_reason) {
      printf("SKIP: %s (%s)\n", tc->name, skip_reason);
      fflush(stdout);
      skipped++;
      continue;
    }

    static const char fixture_prefix[] = "fixture:";
    const char *fixture_path =
        strncmp(
            tc->input, fixture_prefix,
            sizeof(fixture_prefix) - 1) == 0
            ? tc->input + sizeof(fixture_prefix) - 1
            : NULL;
    int failed =
        (fixture_path
             ? copy_source_file(fixture_path, src_path)
             : write_source_file(src_path, tc->input)) != 0 ||
        run_compiler_expect_fail_with_diag(
            program, object_mode ? object_path : NULL, src_path,
            tc->expected_diag, log_path) != 0;
    if (!failed && object_mode) unlink(object_path);
    if (failed) {
      fprintf(stderr, "Compile-fail case failed: %s (see %s)\n",
              tc->name, log_path);
      continue;
    }

    if (strcmp(tc->name, "c11_implicit_int_objects_rejected") == 0 ||
        strcmp(tc->name, "multiple_function_syntax_errors_reported") == 0) {
      FILE *log = fopen(log_path, "r");
      char diagnostics[8192] = {0};
      size_t length =
          log ? fread(diagnostics, 1, sizeof(diagnostics) - 1, log) : 0;
      if (log) fclose(log);
      diagnostics[length] = '\0';
      int diagnostic_count = 0;
      for (char *match = diagnostics;
           (match = strstr(match, tc->expected_diag)) != NULL;
           match += strlen(tc->expected_diag))
        diagnostic_count++;
      if (diagnostic_count != 2) {
        fprintf(stderr,
                "Compile-fail case did not emit two diagnostics: %s "
                "(see %s)\n",
                tc->name, log_path);
        continue;
      }
    }
    passed++;
  }

  if (summary_label) {
    printf("%s: %zu passed, %zu target-specific skipped, %zu registered\n",
           summary_label, passed, skipped, count);
  }
  return passed + skipped == count ? 0 : 1;
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
  if (fprintf(
          fp,
          "#line 91 \"macro_expansion_limit.c\"\n"
          "int main() { return X%d; }\n",
          levels) < 0) {
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
  if (fprintf(
          fp,
          "#line 66 \"if_token_limit.c\"\n"
          "#if ") < 0) {
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

static int write_pp_if_eval_limit_source(const char *path, int depth) {
  if (depth < 1) return -1;
  FILE *fp = fopen(path, "w");
  if (!fp) return -1;
  if (fprintf(
          fp,
          "#line 77 \"if_eval_limit.c\"\n"
          "#if ") < 0) {
    fclose(fp);
    return -1;
  }
  for (int i = 0; i < depth; i++) {
    if (fputc('(', fp) == EOF) {
      fclose(fp);
      return -1;
    }
  }
  if (fputc('1', fp) == EOF) {
    fclose(fp);
    return -1;
  }
  for (int i = 0; i < depth; i++) {
    if (fputc(')', fp) == EOF) {
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
            strcmp(sym, "_strcoll") == 0 || strcmp(sym, "_strxfrm") == 0 ||
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
            strcmp(sym, "_aligned_alloc") == 0 ||
            strcmp(sym, "_exit") == 0 || strcmp(sym, "_abort") == 0 ||
            strcmp(sym, "_atoi") == 0 || strcmp(sym, "_atol") == 0 ||
            strcmp(sym, "_puts") == 0 || strcmp(sym, "_fprintf") == 0 ||
            strcmp(sym, "_sprintf") == 0 || strcmp(sym, "_snprintf") == 0 ||
            strcmp(sym, "_vsnprintf") == 0 || strcmp(sym, "_vswprintf") == 0 ||
            strcmp(sym, "_vswscanf") == 0 || strcmp(sym, "_fwprintf") == 0 ||
            strcmp(sym, "_wprintf") == 0 || strcmp(sym, "_vfwprintf") == 0 ||
            strcmp(sym, "_vwprintf") == 0 ||
            strcmp(sym, "_tmpfile") == 0 || strcmp(sym, "_fputwc") == 0 ||
            strcmp(sym, "_fgetws") == 0 || strcmp(sym, "_rewind") == 0 ||
            strcmp(sym, "_fclose") == 0 || strcmp(sym, "_fputws") == 0 ||
            strcmp(sym, "_fgetwc") == 0 || strcmp(sym, "_fwscanf") == 0 ||
            strcmp(sym, "_wscanf") == 0 || strcmp(sym, "_vfwscanf") == 0 ||
            strcmp(sym, "_vwscanf") == 0 ||
            strcmp(sym, "_sscanf") == 0 || strcmp(sym, "_vsscanf") == 0 ||
            strcmp(sym, "_va_start") == 0 || strcmp(sym, "_va_end") == 0 ||
            strcmp(sym, "_abs") == 0 || strcmp(sym, "_labs") == 0 ||
            strcmp(sym, "_llabs") == 0 || strcmp(sym, "_div") == 0 ||
            strcmp(sym, "_ldiv") == 0 || strcmp(sym, "_lldiv") == 0 ||
            strcmp(sym, "_rand") == 0 || strcmp(sym, "_srand") == 0 ||
            strcmp(sym, "_qsort") == 0 || strcmp(sym, "_bsearch") == 0 ||
            strcmp(sym, "_atexit") == 0 || strcmp(sym, "_at_quick_exit") == 0 ||
            strcmp(sym, "_quick_exit") == 0 ||
            strcmp(sym, "_getenv") == 0 ||
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
            strcmp(sym, "_timespec_get") == 0 || strcmp(sym, "_getrusage") == 0 ||
            strcmp(sym, "_setjmp") == 0 || strcmp(sym, "_longjmp") == 0 ||
            strcmp(sym, "_signal") == 0 || strcmp(sym, "_raise") == 0 ||
            strcmp(sym, "_perror") == 0 || strcmp(sym, "_fopen") == 0 ||
            strcmp(sym, "_freopen") == 0 || strcmp(sym, "_tmpnam") == 0 ||
            strcmp(sym, "_fdopen") == 0 || strcmp(sym, "_remove") == 0 ||
            strcmp(sym, "_rename") == 0 ||
            strcmp(sym, "_open") == 0 || strcmp(sym, "_read") == 0 ||
            strcmp(sym, "_write") == 0 || strcmp(sym, "_lseek") == 0 ||
            strcmp(sym, "_close") == 0 || strcmp(sym, "_fstat") == 0 ||
            strcmp(sym, "_fclose") == 0 || strcmp(sym, "_fflush") == 0 ||
            strcmp(sym, "_setbuf") == 0 || strcmp(sym, "_setvbuf") == 0 ||
            strcmp(sym, "_fread") == 0 || strcmp(sym, "_fwrite") == 0 ||
            strcmp(sym, "_fputs") == 0 || strcmp(sym, "_fputc") == 0 ||
            strcmp(sym, "_putc") == 0 || strcmp(sym, "_fgetc") == 0 ||
            strcmp(sym, "_getc") == 0 || strcmp(sym, "_ungetc") == 0 ||
            strcmp(sym, "_fgets") == 0 || strcmp(sym, "_getline") == 0 ||
            strcmp(sym, "_fseek") == 0 || strcmp(sym, "_ftell") == 0 ||
            strcmp(sym, "_fgetpos") == 0 || strcmp(sym, "_fsetpos") == 0 ||
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
            strcmp(sym, "_nextafter") == 0 ||
            strcmp(sym, "_nextafterf") == 0 ||
            strcmp(sym, "_nextafterl") == 0 ||
            strcmp(sym, "_nexttoward") == 0 ||
            strcmp(sym, "_nexttowardf") == 0 ||
            strcmp(sym, "_nexttowardl") == 0 ||
            strcmp(sym, "_tgamma") == 0 || strcmp(sym, "_tgammaf") == 0 ||
            strcmp(sym, "_tgammal") == 0 ||
            strcmp(sym, "_lgamma") == 0 || strcmp(sym, "_lgammaf") == 0 ||
            strcmp(sym, "_lgammal") == 0 ||
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
            strcmp(sym, "_fwprintf") == 0 || strcmp(sym, "_wprintf") == 0 ||
            strcmp(sym, "_swprintf") == 0 || strcmp(sym, "_vfwprintf") == 0 ||
            strcmp(sym, "_vwprintf") == 0 || strcmp(sym, "_vswprintf") == 0 ||
            strcmp(sym, "_fwscanf") == 0 || strcmp(sym, "_wscanf") == 0 ||
            strcmp(sym, "_swscanf") == 0 || strcmp(sym, "_vfwscanf") == 0 ||
            strcmp(sym, "_vwscanf") == 0 || strcmp(sym, "_vswscanf") == 0 ||
            strcmp(sym, "_fgetwc") == 0 || strcmp(sym, "_getwc") == 0 ||
            strcmp(sym, "_getwchar") == 0 || strcmp(sym, "_fputwc") == 0 ||
            strcmp(sym, "_putwc") == 0 || strcmp(sym, "_putwchar") == 0 ||
            strcmp(sym, "_fgetws") == 0 || strcmp(sym, "_fputws") == 0 ||
            strcmp(sym, "_ungetwc") == 0 || strcmp(sym, "_fwide") == 0 ||
            strcmp(sym, "_mbrtowc") == 0 || strcmp(sym, "_wcrtomb") == 0 ||
            strcmp(sym, "_mbsrtowcs") == 0 || strcmp(sym, "_wcsrtombs") == 0 ||
            strcmp(sym, "_mbrlen") == 0 || strcmp(sym, "_mbsinit") == 0 ||
            strcmp(sym, "_mblen") == 0 || strcmp(sym, "_mbtowc") == 0 ||
            strcmp(sym, "_wctomb") == 0 || strcmp(sym, "_mbstowcs") == 0 ||
            strcmp(sym, "_wcstombs") == 0 ||
            strcmp(sym, "_btowc") == 0 || strcmp(sym, "_wctob") == 0 ||
            strcmp(sym, "_wcstol") == 0 || strcmp(sym, "_wcstoul") == 0 ||
            strcmp(sym, "_wcstoll") == 0 || strcmp(sym, "_wcstoull") == 0 ||
            strcmp(sym, "_wcstof") == 0 || strcmp(sym, "_wcstod") == 0 ||
            strcmp(sym, "_wcstold") == 0 ||
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
            strcmp(sym, "_imaxabs") == 0 || strcmp(sym, "_imaxdiv") == 0 ||
            strcmp(sym, "_strtoimax") == 0 || strcmp(sym, "_strtoumax") == 0 ||
            strcmp(sym, "_wcstoimax") == 0 || strcmp(sym, "_wcstoumax") == 0 ||
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

int main(int argc, char **argv) {
  if (argc == 2 &&
      strcmp(argv[1], "--wasm-object-compile-fail") == 0) {
    return run_registered_compile_fail_cases(
        "./build/ag_c_wasm", 1,
        "build/wasm32_obj/compile_fail",
        "build/wasm32_obj/compile_fail/logs",
        "wasm32 object compile-fail parity");
  }
  if (argc != 1) {
    fprintf(stderr, "usage: %s [--wasm-object-compile-fail]\n", argv[0]);
    return 2;
  }

  printf("Running E2E tests...\n");
  fflush(stdout);

  if (mkdir_p("build/e2e/logs") != 0) {
    fprintf(stderr, "Failed to create log directory\n");
    return 1;
  }
  {
    const char *fixture =
        "test/fixtures/probes_found_bugs/"
        "noreturn_cfg_termination_boundaries.c";
    const char *native_log =
        "build/e2e/logs/noreturn_cfg_native.log";
    const char *wasm_log =
        "build/e2e/logs/noreturn_cfg_wasm_object.log";
    if (run_compiler_expect_success_capture_diag(
            "./build/ag_c", NULL, fixture, native_log) != 0 ||
        run_compiler_expect_success_capture_diag(
            "./build/ag_c_wasm",
            "build/e2e/noreturn_cfg_diagnostics.o",
            fixture, wasm_log) != 0 ||
        log_file_contains_substr(native_log, "W3005") ||
        log_file_contains_substr(native_log, "W3017") ||
        log_file_contains_substr(wasm_log, "W3005") ||
        log_file_contains_substr(wasm_log, "W3017")) {
      fprintf(
          stderr,
          "noreturn CFG diagnostics regression "
          "(see %s and %s)\n",
          native_log, wasm_log);
      return 1;
    }
  }
  {
    const char *fixture =
        "test/fixtures/probes_found_bugs/"
        "noreturn_fallthrough_warning_boundaries.c";
    const char *native_log =
        "build/e2e/logs/noreturn_fallthrough_native.log";
    const char *wasm_log =
        "build/e2e/logs/noreturn_fallthrough_wasm_object.log";
    if (run_compiler_expect_success_capture_diag(
            "./build/ag_c", NULL, fixture, native_log) != 0 ||
        run_compiler_expect_success_capture_diag(
            "./build/ag_c_wasm",
            "build/e2e/noreturn_fallthrough_diagnostics.o",
            fixture, wasm_log) != 0 ||
        !log_file_contains_substr(native_log, "W3005") ||
        !log_file_contains_substr(native_log, "partial_if") ||
        !log_file_contains_substr(native_log, "partial_conditional") ||
        !log_file_contains_substr(native_log, "partial_short_circuit") ||
        !log_file_contains_substr(native_log, "indirect_call") ||
        !log_file_contains_substr(native_log, "while_true_can_break") ||
        !log_file_contains_substr(native_log, "while_false") ||
        !log_file_contains_substr(native_log, "constant_false_if") ||
        !log_file_contains_substr(native_log, "goto_out_reaches_end") ||
        !log_file_contains_substr(native_log, "switch_path_reaches_end") ||
        !log_file_contains_substr(native_log, "nested_outer_break_reaches_end") ||
        !log_file_contains_substr(native_log, "W3017") ||
        !log_file_contains_substr(wasm_log, "W3005") ||
        !log_file_contains_substr(wasm_log, "partial_if") ||
        !log_file_contains_substr(wasm_log, "partial_conditional") ||
        !log_file_contains_substr(wasm_log, "partial_short_circuit") ||
        !log_file_contains_substr(wasm_log, "indirect_call") ||
        !log_file_contains_substr(wasm_log, "while_true_can_break") ||
        !log_file_contains_substr(wasm_log, "while_false") ||
        !log_file_contains_substr(wasm_log, "constant_false_if") ||
        !log_file_contains_substr(wasm_log, "goto_out_reaches_end") ||
        !log_file_contains_substr(wasm_log, "switch_path_reaches_end") ||
        !log_file_contains_substr(wasm_log, "nested_outer_break_reaches_end") ||
        !log_file_contains_substr(wasm_log, "W3017")) {
      fprintf(
          stderr,
          "noreturn fallthrough diagnostics regression "
          "(see %s and %s)\n",
          native_log, wasm_log);
      return 1;
    }
  }
  {
    const char *fixture =
        "test/fixtures/probes_found_bugs/"
        "constant_condition_cfg_termination_boundaries.c";
    const char *native_log =
        "build/e2e/logs/constant_condition_cfg_native.log";
    const char *wasm_log =
        "build/e2e/logs/constant_condition_cfg_wasm_object.log";
    if (run_compiler_expect_success_capture_diag(
            "./build/ag_c", NULL, fixture, native_log) != 0 ||
        run_compiler_expect_success_capture_diag(
            "./build/ag_c_wasm",
            "build/e2e/constant_condition_cfg_diagnostics.o",
            fixture, wasm_log) != 0 ||
        log_file_contains_substr(native_log, "W3005") ||
        log_file_contains_substr(wasm_log, "W3005")) {
      fprintf(
          stderr,
          "constant-condition CFG diagnostics regression "
          "(see %s and %s)\n",
          native_log, wasm_log);
      return 1;
    }
  }
  {
    const char *fixture =
        "test/fixtures/probes_found_bugs/"
        "pointer_complex_constant_cfg_termination_boundaries.c";
    const char *native_log =
        "build/e2e/logs/pointer_complex_constant_cfg_native.log";
    const char *wasm_log =
        "build/e2e/logs/pointer_complex_constant_cfg_wasm_object.log";
    if (run_compiler_expect_success_capture_diag(
            "./build/ag_c", NULL, fixture, native_log) != 0 ||
        run_compiler_expect_success_capture_diag(
            "./build/ag_c_wasm",
            "build/e2e/pointer_complex_constant_cfg_diagnostics.o",
            fixture, wasm_log) != 0 ||
        log_file_contains_substr(native_log, "W3005") ||
        log_file_contains_substr(wasm_log, "W3005")) {
      fprintf(
          stderr,
          "pointer/complex constant CFG diagnostics regression "
          "(see %s and %s)\n",
          native_log, wasm_log);
      return 1;
    }
  }
  {
    const char *fixture =
        "test/fixtures/probes_found_bugs/"
        "pointer_complex_constant_cfg_warning_boundaries.c";
    const char *native_log =
        "build/e2e/logs/pointer_complex_constant_cfg_warning_native.log";
    const char *wasm_log =
        "build/e2e/logs/"
        "pointer_complex_constant_cfg_warning_wasm_object.log";
    static const char *const expected[] = {
        "W3005",
        "pointer_null_false",
        "function_pointer_null_false",
        "pointer_ternary_false",
        "complex_zero_false",
        "complex_literal_zero_false",
        "complex_compare_false",
        "complex_bool_cast_false",
        "complex_ternary_false",
        "side_effect_logical_and_false",
        "side_effect_comma_pointer_false",
        "floating_negative_zero_false",
        "complex_signed_zero_false",
        "complex_partial_initializer_false",
    };
    if (run_compiler_expect_success_capture_diag(
            "./build/ag_c", NULL, fixture, native_log) != 0 ||
        run_compiler_expect_success_capture_diag(
            "./build/ag_c_wasm",
            "build/e2e/pointer_complex_constant_cfg_warning_diagnostics.o",
            fixture, wasm_log) != 0 ||
        !log_files_contain_all(
            native_log, wasm_log, expected,
            sizeof(expected) / sizeof(expected[0]))) {
      fprintf(
          stderr,
          "pointer/complex constant CFG warning regression "
          "(see %s and %s)\n",
          native_log, wasm_log);
      return 1;
    }
  }
  {
    const char *fixture =
        "test/fixtures/probes_found_bugs/"
        "complex_after_required_cfg_termination_boundaries.c";
    const char *native_log =
        "build/e2e/logs/complex_after_required_cfg_native.log";
    const char *wasm_log =
        "build/e2e/logs/"
        "complex_after_required_cfg_wasm_object.log";
    if (run_compiler_expect_success_capture_diag(
            "./build/ag_c", NULL, fixture, native_log) != 0 ||
        run_compiler_expect_success_capture_diag(
            "./build/ag_c_wasm",
            "build/e2e/complex_after_required_cfg_diagnostics.o",
            fixture, wasm_log) != 0 ||
        log_file_contains_substr(native_log, "W3005") ||
        log_file_contains_substr(wasm_log, "W3005")) {
      fprintf(
          stderr,
          "complex after-required CFG diagnostics regression "
          "(see %s and %s)\n",
          native_log, wasm_log);
      return 1;
    }
  }
  {
    const char *fixture =
        "test/fixtures/probes_found_bugs/"
        "complex_after_required_cfg_warning_boundaries.c";
    const char *native_log =
        "build/e2e/logs/"
        "complex_after_required_cfg_warning_native.log";
    const char *wasm_log =
        "build/e2e/logs/"
        "complex_after_required_cfg_warning_wasm_object.log";
    static const char *const expected[] = {
        "W3005",
        "comma_complex_false",
        "cast_complex_false",
        "unary_complex_false",
        "add_complex_false",
        "multiply_complex_false",
        "selected_complex_false",
        "selected_else_complex_false",
        "complex_compare_false",
        "complex_component_false",
        "floating_to_complex_false",
        "compound_complex_false",
        "two_component_complex_false",
        "volatile_complex_unknown",
        "runtime_complex_unknown",
    };
    if (run_compiler_expect_success_capture_diag(
            "./build/ag_c", NULL, fixture, native_log) != 0 ||
        run_compiler_expect_success_capture_diag(
            "./build/ag_c_wasm",
            "build/e2e/"
            "complex_after_required_cfg_warning_diagnostics.o",
            fixture, wasm_log) != 0 ||
        !log_files_contain_all(
            native_log, wasm_log, expected,
            sizeof(expected) / sizeof(expected[0]))) {
      fprintf(
          stderr,
          "complex after-required CFG warning regression "
          "(see %s and %s)\n",
          native_log, wasm_log);
      return 1;
    }
  }
  {
    const char *fixture =
        "test/fixtures/probes_found_bugs/"
        "known_lvalue_address_cfg_termination_boundaries.c";
    const char *native_log =
        "build/e2e/logs/known_lvalue_address_cfg_native.log";
    const char *wasm_log =
        "build/e2e/logs/"
        "known_lvalue_address_cfg_wasm_object.log";
    if (run_compiler_expect_success_capture_diag(
            "./build/ag_c", NULL, fixture, native_log) != 0 ||
        run_compiler_expect_success_capture_diag(
            "./build/ag_c_wasm",
            "build/e2e/known_lvalue_address_cfg_diagnostics.o",
            fixture, wasm_log) != 0 ||
        log_file_contains_substr(native_log, "W3005") ||
        log_file_contains_substr(wasm_log, "W3005")) {
      fprintf(
          stderr,
          "known lvalue-address CFG diagnostics regression "
          "(see %s and %s)\n",
          native_log, wasm_log);
      return 1;
    }
  }
  {
    const char *fixture =
        "test/fixtures/probes_found_bugs/"
        "known_lvalue_address_cfg_warning_boundaries.c";
    const char *native_log =
        "build/e2e/logs/"
        "known_lvalue_address_cfg_warning_native.log";
    const char *wasm_log =
        "build/e2e/logs/"
        "known_lvalue_address_cfg_warning_wasm_object.log";
    static const char *const expected[] = {
        "W3005",
        "runtime_array_index_address_unknown",
        "runtime_string_index_address_unknown",
        "pointer_member_address_unknown",
        "scalar_compound_address_unknown",
        "aggregate_compound_member_address_unknown",
    };
    if (run_compiler_expect_success_capture_diag(
            "./build/ag_c", NULL, fixture, native_log) != 0 ||
        run_compiler_expect_success_capture_diag(
            "./build/ag_c_wasm",
            "build/e2e/"
            "known_lvalue_address_cfg_warning_diagnostics.o",
            fixture, wasm_log) != 0 ||
        !log_files_contain_all(
            native_log, wasm_log, expected,
            sizeof(expected) / sizeof(expected[0]))) {
      fprintf(
          stderr,
          "known lvalue-address CFG warning regression "
          "(see %s and %s)\n",
          native_log, wasm_log);
      return 1;
    }
  }
  {
    const char *fixture =
        "test/fixtures/probes_found_bugs/"
        "complex_scalar_cfg_termination_boundaries.c";
    const char *native_log =
        "build/e2e/logs/complex_scalar_cfg_native.log";
    const char *wasm_log =
        "build/e2e/logs/"
        "complex_scalar_cfg_wasm_object.log";
    if (run_compiler_expect_success_capture_diag(
            "./build/ag_c", NULL, fixture, native_log) != 0 ||
        run_compiler_expect_success_capture_diag(
            "./build/ag_c_wasm",
            "build/e2e/complex_scalar_cfg_diagnostics.o",
            fixture, wasm_log) != 0 ||
        log_file_contains_substr(native_log, "W3005") ||
        log_file_contains_substr(wasm_log, "W3005")) {
      fprintf(
          stderr,
          "complex scalar-conversion CFG diagnostics regression "
          "(see %s and %s)\n",
          native_log, wasm_log);
      return 1;
    }
  }
  {
    const char *fixture =
        "test/fixtures/probes_found_bugs/"
        "complex_scalar_cfg_warning_boundaries.c";
    const char *native_log =
        "build/e2e/logs/"
        "complex_scalar_cfg_warning_native.log";
    const char *wasm_log =
        "build/e2e/logs/"
        "complex_scalar_cfg_warning_wasm_object.log";
    static const char *const expected[] = {
        "W3005",
        "complex_to_double_false",
        "complex_to_float_false",
        "complex_to_int_false",
        "complex_to_unsigned_char_false",
        "complex_to_bool_false",
        "fractional_complex_to_int_false",
        "volatile_complex_to_int_unknown",
        "runtime_complex_to_int_unknown",
    };
    if (run_compiler_expect_success_capture_diag(
            "./build/ag_c", NULL, fixture, native_log) != 0 ||
        run_compiler_expect_success_capture_diag(
            "./build/ag_c_wasm",
            "build/e2e/"
            "complex_scalar_cfg_warning_diagnostics.o",
            fixture, wasm_log) != 0 ||
        !log_files_contain_all(
            native_log, wasm_log, expected,
            sizeof(expected) / sizeof(expected[0]))) {
      fprintf(
          stderr,
          "complex scalar-conversion CFG warning regression "
          "(see %s and %s)\n",
          native_log, wasm_log);
      return 1;
    }
  }
  {
    const char *fixture =
        "test/fixtures/probes_found_bugs/"
        "standard_constant_expression_cfg_termination_boundaries.c";
    const char *native_log =
        "build/e2e/logs/standard_constant_expression_cfg_native.log";
    const char *wasm_log =
        "build/e2e/logs/"
        "standard_constant_expression_cfg_wasm_object.log";
    if (run_compiler_expect_success_capture_diag(
            "./build/ag_c", NULL, fixture, native_log) != 0 ||
        run_compiler_expect_success_capture_diag(
            "./build/ag_c_wasm",
            "build/e2e/standard_constant_expression_cfg_diagnostics.o",
            fixture, wasm_log) != 0 ||
        log_file_contains_substr(native_log, "W3005") ||
        log_file_contains_substr(wasm_log, "W3005")) {
      fprintf(
          stderr,
          "standard constant-expression CFG diagnostics regression "
          "(see %s and %s)\n",
          native_log, wasm_log);
      return 1;
    }
  }
  {
    const char *fixture =
        "test/fixtures/probes_found_bugs/"
        "standard_constant_expression_cfg_warning_boundaries.c";
    const char *native_log =
        "build/e2e/logs/"
        "standard_constant_expression_cfg_warning_native.log";
    const char *wasm_log =
        "build/e2e/logs/"
        "standard_constant_expression_cfg_warning_wasm_object.log";
    static const char *const expected[] = {
        "W3005",
        "pointer_runtime_offset_unknown",
        "enum_false",
        "generic_false",
        "integer_compound_false",
        "floating_compound_false",
        "pointer_compound_false",
        "side_effect_integer_sub_false",
        "side_effect_floating_sub_false",
        "side_effect_floating_compare_false",
        "side_effect_integer_selected_false",
        "side_effect_floating_selected_false",
        "side_effect_pointer_selected_false",
        "side_effect_short_circuit_and_false",
        "volatile_integer_compound_unknown",
        "volatile_floating_compound_unknown",
        "volatile_pointer_compound_unknown",
    };
    if (run_compiler_expect_success_capture_diag(
            "./build/ag_c", NULL, fixture, native_log) != 0 ||
        run_compiler_expect_success_capture_diag(
            "./build/ag_c_wasm",
            "build/e2e/"
            "standard_constant_expression_cfg_warning_diagnostics.o",
            fixture, wasm_log) != 0 ||
        !log_files_contain_all(
            native_log, wasm_log, expected,
            sizeof(expected) / sizeof(expected[0]))) {
      fprintf(
          stderr,
          "standard constant-expression CFG warning regression "
          "(see %s and %s)\n",
          native_log, wasm_log);
      return 1;
    }
  }
  {
    const char *fixture =
        "test/fixtures/probes_found_bugs/"
        "stdlib_noreturn_cfg_termination_boundaries.c";
    const char *native_log =
        "build/e2e/logs/stdlib_noreturn_cfg_native.log";
    const char *wasm_log =
        "build/e2e/logs/stdlib_noreturn_cfg_wasm_object.log";
    if (run_compiler_expect_success_capture_diag(
            "./build/ag_c", NULL, fixture, native_log) != 0 ||
        run_compiler_expect_success_capture_diag(
            "./build/ag_c_wasm",
            "build/e2e/stdlib_noreturn_cfg_diagnostics.o",
            fixture, wasm_log) != 0 ||
        log_file_contains_substr(native_log, "W3005") ||
        log_file_contains_substr(wasm_log, "W3005")) {
      fprintf(
          stderr,
          "stdlib noreturn CFG diagnostics regression "
          "(see %s and %s)\n",
          native_log, wasm_log);
      return 1;
    }
  }
  {
    const char *fixture =
        "test/fixtures/probes_found_bugs/"
        "stdlib_noreturn_cfg_warning_boundaries.c";
    const char *native_log =
        "build/e2e/logs/stdlib_noreturn_cfg_warning_native.log";
    const char *wasm_log =
        "build/e2e/logs/stdlib_noreturn_cfg_warning_wasm_object.log";
    static const char *const expected[] = {
        "W3005",
        "conditional_assert_can_continue",
        "indirect_exit_can_continue",
        "constant_assert_success_can_continue",
    };
    if (run_compiler_expect_success_capture_diag(
            "./build/ag_c", NULL, fixture, native_log) != 0 ||
        run_compiler_expect_success_capture_diag(
            "./build/ag_c_wasm",
            "build/e2e/stdlib_noreturn_cfg_warning_diagnostics.o",
            fixture, wasm_log) != 0 ||
        !log_files_contain_all(
            native_log, wasm_log, expected,
            sizeof(expected) / sizeof(expected[0]))) {
      fprintf(
          stderr,
          "stdlib noreturn CFG warning regression "
          "(see %s and %s)\n",
          native_log, wasm_log);
      return 1;
    }
  }
  {
    const char *fixture =
        "test/fixtures/probes_found_bugs/"
        "setjmp_noreturn_cfg_termination_boundaries.c";
    const char *native_log =
        "build/e2e/logs/setjmp_noreturn_cfg_native.log";
    const char *wasm_log =
        "build/e2e/logs/setjmp_noreturn_cfg_wasm_object.log";
    if (run_compiler_expect_success_capture_diag(
            "./build/ag_c", NULL, fixture, native_log) != 0 ||
        run_compiler_expect_success_capture_diag(
            "./build/ag_c_wasm",
            "build/e2e/setjmp_noreturn_cfg_diagnostics.o",
            fixture, wasm_log) != 0 ||
        log_file_contains_substr(native_log, "W3005") ||
        log_file_contains_substr(wasm_log, "W3005")) {
      fprintf(
          stderr,
          "setjmp noreturn CFG diagnostics regression "
          "(see %s and %s)\n",
          native_log, wasm_log);
      return 1;
    }
  }
  {
    const char *fixture =
        "test/fixtures/probes_found_bugs/"
        "setjmp_noreturn_cfg_warning_boundaries.c";
    const char *native_log =
        "build/e2e/logs/setjmp_noreturn_cfg_warning_native.log";
    const char *wasm_log =
        "build/e2e/logs/setjmp_noreturn_cfg_warning_wasm_object.log";
    static const char *const expected[] = {
        "W3005",
        "indirect_longjmp_can_continue",
    };
    if (run_compiler_expect_success_capture_diag(
            "./build/ag_c", NULL, fixture, native_log) != 0 ||
        run_compiler_expect_success_capture_diag(
            "./build/ag_c_wasm",
            "build/e2e/setjmp_noreturn_cfg_warning_diagnostics.o",
            fixture, wasm_log) != 0 ||
        !log_files_contain_all(
            native_log, wasm_log, expected,
            sizeof(expected) / sizeof(expected[0]))) {
      fprintf(
          stderr,
          "setjmp noreturn CFG warning regression "
          "(see %s and %s)\n",
          native_log, wasm_log);
      return 1;
    }
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

  if (run_registered_compile_fail_cases(
          "./build/ag_c", 0, "build/e2e/compile_fail",
          "build/e2e/logs", NULL) != 0)
    return 1;
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
        copy_source_file(
            "test/fixtures/should_reject/tokenizer_integer_too_large.c",
            tok_limit_path) != 0 ||
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
        run_ag_c_expect_fail_profiled(
            pp_if_limit_path, "if_token_limit.c:66: E1037",
            log_path, 1024) != 0) {
      fprintf(stderr, "Compile-fail case failed: preprocess_if_token_limit (see %s)\n", log_path);
      return 1;
    }
  }
  {
    const char *pp_if_limit_path =
        "build/e2e/compile_fail/preprocess_if_eval_limit.c";
    const char *log_path =
        "build/e2e/logs/compile_fail_preprocess_if_eval_limit.log";
    if (mkdir_p("build/e2e/compile_fail") != 0 ||
        write_pp_if_eval_limit_source(pp_if_limit_path, 760) != 0 ||
        run_ag_c_expect_fail_profiled(
            pp_if_limit_path, "if_eval_limit.c:77: E1038",
            log_path, 1024) != 0) {
      fprintf(
          stderr,
          "Compile-fail case failed: preprocess_if_eval_limit (see %s)\n",
          log_path);
      return 1;
    }
  }
  {
    const char *pp_limit_path = "build/e2e/compile_fail/macro_expansion_limit.c";
    const char *log_path = "build/e2e/logs/compile_fail_macro_expansion_limit.log";
    if (mkdir_p("build/e2e/compile_fail") != 0 ||
        write_macro_expansion_limit_source(pp_limit_path, 19) != 0 ||
        run_ag_c_expect_fail_with_diag(
            pp_limit_path, "macro_expansion_limit.c:91: E1029",
            log_path) != 0) {
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
                     (sizeof(compile_fail_cases) /
                      sizeof(compile_fail_cases[0])) +
                     (sizeof(link2_cases) /
                      sizeof(link2_cases[0])) +
                     14);
  pass_count = failed ? 0 : test_count;

  free(categories);
  free(build_pids);
  free(pids);
  if (failed) return 1;
  printf("OK: All %d E2E tests passed! (%d/%d)\n", test_count, pass_count, test_count);
  return 0;
}
