#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>

typedef struct {
  const char *category;
  const char *name;
  const char *path;
} wasm_e2e_case_t;

typedef struct {
  const char *category;
  const char *name;
  const char *file_main;
  const char *file_other;
  int expected_i;
} wasm_link2_case_t;

#define MAX_EXTRA_CASES 1024

static wasm_e2e_case_t extra_cases[MAX_EXTRA_CASES];
static char extra_case_categories[MAX_EXTRA_CASES][64];
static char extra_case_names[MAX_EXTRA_CASES][192];
static char extra_case_paths[MAX_EXTRA_CASES][256];

static const wasm_link2_case_t link2_cases[] = {
    {"probes_found_bugs", "static_internal_linkage_xtu",
     "test/fixtures/probes_found_bugs/static_internal_linkage_xtu_main.c",
     "test/fixtures/probes_found_bugs/static_internal_linkage_xtu_other.c", 42},
};

static const wasm_e2e_case_t cases[] = {
    {"integer", "zero", "test/fixtures/integer/zero.c"},
    {"integer", "literal", "test/fixtures/integer/literal.c"},
    {"integer", "hex_literal", "test/fixtures/integer/hex_literal.c"},
    {"integer", "oct_literal", "test/fixtures/integer/oct_literal.c"},
    {"integer", "bin_literal", "test/fixtures/integer/bin_literal.c"},
    {"integer", "suffix_LL_U", "test/fixtures/integer/suffix_LL_U.c"},

    {"arithmetic", "add_sub", "test/fixtures/arithmetic/add_sub.c"},
    {"arithmetic", "spaces", "test/fixtures/arithmetic/spaces.c"},
    {"arithmetic", "mul", "test/fixtures/arithmetic/mul.c"},
    {"arithmetic", "paren", "test/fixtures/arithmetic/paren.c"},
    {"arithmetic", "div", "test/fixtures/arithmetic/div.c"},
    {"arithmetic", "mod", "test/fixtures/arithmetic/mod.c"},
    {"arithmetic", "mod_prec", "test/fixtures/arithmetic/mod_prec.c"},
    {"arithmetic", "unary_plus", "test/fixtures/arithmetic/unary_plus.c"},
    {"arithmetic", "unary_minus", "test/fixtures/arithmetic/unary_minus.c"},
    {"arithmetic", "logical_not_true", "test/fixtures/arithmetic/logical_not_true.c"},
    {"arithmetic", "logical_not_false", "test/fixtures/arithmetic/logical_not_false.c"},
    {"arithmetic", "bit_not", "test/fixtures/arithmetic/bit_not.c"},
    {"arithmetic", "pre_inc", "test/fixtures/arithmetic/pre_inc.c"},
    {"arithmetic", "post_inc", "test/fixtures/arithmetic/post_inc.c"},
    {"arithmetic", "pre_dec", "test/fixtures/arithmetic/pre_dec.c"},
    {"arithmetic", "post_dec", "test/fixtures/arithmetic/post_dec.c"},
    {"arithmetic", "add_eq", "test/fixtures/arithmetic/add_eq.c"},
    {"arithmetic", "sub_eq", "test/fixtures/arithmetic/sub_eq.c"},
    {"arithmetic", "mul_eq", "test/fixtures/arithmetic/mul_eq.c"},
    {"arithmetic", "div_eq", "test/fixtures/arithmetic/div_eq.c"},
    {"arithmetic", "mod_eq", "test/fixtures/arithmetic/mod_eq.c"},
    {"arithmetic", "shl_eq", "test/fixtures/arithmetic/shl_eq.c"},
    {"arithmetic", "shr_eq", "test/fixtures/arithmetic/shr_eq.c"},
    {"arithmetic", "and_eq", "test/fixtures/arithmetic/and_eq.c"},
    {"arithmetic", "xor_eq", "test/fixtures/arithmetic/xor_eq.c"},
    {"arithmetic", "or_eq", "test/fixtures/arithmetic/or_eq.c"},
    {"arithmetic", "comma_basic", "test/fixtures/arithmetic/comma_basic.c"},
    {"arithmetic", "comma_chain", "test/fixtures/arithmetic/comma_chain.c"},

    {"comparison", "eq1", "test/fixtures/comparison/eq1.c"},
    {"comparison", "eq2", "test/fixtures/comparison/eq2.c"},
    {"comparison", "neq1", "test/fixtures/comparison/neq1.c"},
    {"comparison", "neq2", "test/fixtures/comparison/neq2.c"},
    {"comparison", "lt1", "test/fixtures/comparison/lt1.c"},
    {"comparison", "lt2", "test/fixtures/comparison/lt2.c"},
    {"comparison", "lt3", "test/fixtures/comparison/lt3.c"},
    {"comparison", "le1", "test/fixtures/comparison/le1.c"},
    {"comparison", "le2", "test/fixtures/comparison/le2.c"},
    {"comparison", "le3", "test/fixtures/comparison/le3.c"},
    {"comparison", "gt1", "test/fixtures/comparison/gt1.c"},
    {"comparison", "gt2", "test/fixtures/comparison/gt2.c"},
    {"comparison", "gt3", "test/fixtures/comparison/gt3.c"},
    {"comparison", "ge1", "test/fixtures/comparison/ge1.c"},
    {"comparison", "ge2", "test/fixtures/comparison/ge2.c"},
    {"comparison", "ge3", "test/fixtures/comparison/ge3.c"},
    {"comparison", "log_and", "test/fixtures/comparison/log_and.c"},
    {"comparison", "log_or", "test/fixtures/comparison/log_or.c"},
    {"comparison", "log_prec", "test/fixtures/comparison/log_prec.c"},
    {"comparison", "short_and", "test/fixtures/comparison/short_and.c"},
    {"comparison", "short_or", "test/fixtures/comparison/short_or.c"},
    {"comparison", "short_and_sideeffect", "test/fixtures/comparison/short_and_sideeffect.c"},
    {"comparison", "short_or_sideeffect", "test/fixtures/comparison/short_or_sideeffect.c"},
    {"comparison", "ternary_true", "test/fixtures/comparison/ternary_true.c"},
    {"comparison", "ternary_false", "test/fixtures/comparison/ternary_false.c"},
    {"comparison", "ternary_nested", "test/fixtures/comparison/ternary_nested.c"},
    {"comparison", "ternary_chain", "test/fixtures/comparison/ternary_chain.c"},

    {"local_variables", "basic", "test/fixtures/local_variables/basic.c"},
    {"local_variables", "expr", "test/fixtures/local_variables/expr.c"},
    {"local_variables", "sum3", "test/fixtures/local_variables/sum3.c"},
    {"local_variables", "mul2", "test/fixtures/local_variables/mul2.c"},
    {"local_variables", "copy", "test/fixtures/local_variables/copy.c"},
    {"local_variables", "static_counter", "test/fixtures/local_variables/static_counter.c"},
    {"local_variables", "static_separate_funcs", "test/fixtures/local_variables/static_separate_funcs.c"},

    {"if_else", "if_true", "test/fixtures/if_else/if_true.c"},
    {"if_else", "if_false", "test/fixtures/if_else/if_false.c"},
    {"if_else", "branch1", "test/fixtures/if_else/branch1.c"},
    {"if_else", "branch2", "test/fixtures/if_else/branch2.c"},
    {"if_else", "literal1", "test/fixtures/if_else/literal1.c"},
    {"if_else", "literal0", "test/fixtures/if_else/literal0.c"},
    {"if_else", "fallthrough", "test/fixtures/if_else/fallthrough.c"},

    {"while", "count", "test/fixtures/while/count.c"},
    {"while", "zero", "test/fixtures/while/zero.c"},
    {"while", "do_once", "test/fixtures/while/do_once.c"},
    {"while", "do_loop", "test/fixtures/while/do_loop.c"},
    {"while", "break", "test/fixtures/while/break.c"},
    {"while", "continue", "test/fixtures/while/continue.c"},
    {"while", "for_break_continue", "test/fixtures/while/for_break_continue.c"},
    {"while", "do_continue", "test/fixtures/while/do_continue.c"},

    {"for", "sum10", "test/fixtures/for/sum10.c"},
    {"for", "inc", "test/fixtures/for/inc.c"},
    {"for", "post_inc_expr", "test/fixtures/for/post_inc_expr.c"},
    {"for", "empty_for", "test/fixtures/for/empty_for.c"},

    {"bitwise", "bit_and", "test/fixtures/bitwise/bit_and.c"},
    {"bitwise", "bit_xor", "test/fixtures/bitwise/bit_xor.c"},
    {"bitwise", "bit_or", "test/fixtures/bitwise/bit_or.c"},
    {"bitwise", "bit_precedence", "test/fixtures/bitwise/bit_precedence.c"},
    {"bitwise", "bit_vs_logical_prec", "test/fixtures/bitwise/bit_vs_logical_prec.c"},

    {"shift", "shl", "test/fixtures/shift/shl.c"},
    {"shift", "shr", "test/fixtures/shift/shr.c"},
    {"shift", "shift_precedence", "test/fixtures/shift/shift_precedence.c"},
    {"shift", "shift_by_zero", "test/fixtures/shift/shift_by_zero.c"},
    {"shift", "shift_large_bit", "test/fixtures/shift/shift_large_bit.c"},

    {"switch_edge", "match", "test/fixtures/switch_edge/match.c"},
    {"switch_edge", "default", "test/fixtures/switch_edge/default.c"},
    {"switch_edge", "case_const_expr", "test/fixtures/switch_edge/case_const_expr.c"},
    {"switch_edge", "break_in_switch", "test/fixtures/switch_edge/break_in_switch.c"},
    {"switch_edge", "continue_outer_loop", "test/fixtures/switch_edge/continue_outer_loop.c"},
    {"switch_edge", "goto_forward", "test/fixtures/switch_edge/goto_forward.c"},
    {"switch_edge", "goto_backward_loop", "test/fixtures/switch_edge/goto_backward_loop.c"},
    {"switch_edge", "goto_loop_switch", "test/fixtures/switch_edge/goto_loop_switch.c"},
    {"switch_edge", "goto_state_machine", "test/fixtures/switch_edge/goto_state_machine.c"},
    {"switch_edge", "nested_switch", "test/fixtures/switch_edge/nested_switch.c"},

    {"return", "literal", "test/fixtures/return/literal.c"},
    {"return", "expr", "test/fixtures/return/expr.c"},
    {"return", "var", "test/fixtures/return/var.c"},
    {"return", "sum", "test/fixtures/return/sum.c"},
    {"return", "if", "test/fixtures/return/if.c"},
    {"return", "while", "test/fixtures/return/while.c"},

    {"block", "stmts", "test/fixtures/block/stmts.c"},
    {"block", "sum", "test/fixtures/block/sum.c"},
    {"block", "for", "test/fixtures/block/for.c"},
    {"block", "while", "test/fixtures/block/while.c"},
    {"block", "if", "test/fixtures/block/if.c"},

    {"funcall", "noargs", "test/fixtures/funcall/noargs.c"},
    {"funcall", "add", "test/fixtures/funcall/add.c"},
    {"funcall", "twice", "test/fixtures/funcall/twice.c"},
    {"funcall", "multi", "test/fixtures/funcall/multi.c"},
    {"funcall", "rec", "test/fixtures/funcall/rec.c"},
    {"funcall", "tail_rec", "test/fixtures/funcall/tail_rec.c"},
    {"funcall", "comma_arg", "test/fixtures/funcall/comma_arg.c"},
    {"funcall", "prototype_decl", "test/fixtures/funcall/prototype_decl.c"},
    {"funcall", "paren_name_funcdef", "test/fixtures/funcall/paren_name_funcdef.c"},
    {"funcall", "param_array_decl", "test/fixtures/funcall/param_array_decl.c"},
    {"funcall", "funcptr_value_assign_call", "test/fixtures/funcall/funcptr_value_assign_call.c"},
    {"funcall", "fib_recursive", "test/fixtures/funcall/fib_recursive.c"},
    {"funcall", "abs_ternary", "test/fixtures/funcall/abs_ternary.c"},
    {"funcall", "funcptr_apply_multi", "test/fixtures/funcall/funcptr_apply_multi.c"},

    {"pointer", "deref", "test/fixtures/pointer/deref.c"},
    {"pointer", "assign", "test/fixtures/pointer/assign.c"},
    {"pointer", "arith_add", "test/fixtures/pointer/arith_add.c"},
    {"pointer", "arith_sub", "test/fixtures/pointer/arith_sub.c"},
    {"pointer", "ptr_subtract", "test/fixtures/pointer/ptr_subtract.c"},
    {"pointer", "global_funcptr_array", "test/fixtures/pointer/global_funcptr_array.c"},
    {"pointer", "arith_char", "test/fixtures/pointer/arith_char.c"},
    {"pointer", "triple_deref", "test/fixtures/pointer/triple_deref.c"},
    {"pointer", "write_via_pp", "test/fixtures/pointer/write_via_pp.c"},
    {"pointer", "retarget_via_pp", "test/fixtures/pointer/retarget_via_pp.c"},
    {"pointer", "swap_via_pp", "test/fixtures/pointer/swap_via_pp.c"},
    {"pointer", "pp_cmp", "test/fixtures/pointer/pp_cmp.c"},
    {"pointer", "arith_relative", "test/fixtures/pointer/arith_relative.c"},
    {"pointer", "char_pp_deref", "test/fixtures/pointer/char_pp_deref.c"},
    {"pointer", "triple_write", "test/fixtures/pointer/triple_write.c"},
    {"pointer", "pp_inc_deref", "test/fixtures/pointer/pp_inc_deref.c"},
    {"pointer", "inc_via_pp_func", "test/fixtures/pointer/inc_via_pp_func.c"},
    {"pointer", "pp_arith_scale", "test/fixtures/pointer/pp_arith_scale.c"},
    {"pointer", "pp_deref_add", "test/fixtures/pointer/pp_deref_add.c"},
    {"pointer", "pp_subscript", "test/fixtures/pointer/pp_subscript.c"},
    {"pointer", "ptr_array", "test/fixtures/pointer/ptr_array.c"},
    {"pointer", "ptr_array_write", "test/fixtures/pointer/ptr_array_write.c"},
    {"pointer", "array_ptr_2d", "test/fixtures/pointer/array_ptr_2d.c"},
    {"pointer", "array_ptr_2d_first", "test/fixtures/pointer/array_ptr_2d_first.c"},
    {"pointer", "param_int_ptr_subscript", "test/fixtures/pointer/param_int_ptr_subscript.c"},
    {"pointer", "param_char_ptr_subscript", "test/fixtures/pointer/param_char_ptr_subscript.c"},
    {"pointer", "param_short_ptr_subscript", "test/fixtures/pointer/param_short_ptr_subscript.c"},
    {"pointer", "param_int_pp_double_deref", "test/fixtures/pointer/param_int_pp_double_deref.c"},
    {"pointer", "funcptr_array_assign_and_call", "test/fixtures/pointer/funcptr_array_assign_and_call.c"},
    {"pointer", "funcptr_array_brace_init", "test/fixtures/pointer/funcptr_array_brace_init.c"},
    {"pointer", "funcptr_array_typedef_brace_init", "test/fixtures/pointer/funcptr_array_typedef_brace_init.c"},
    {"pointer", "funcptr_array_inferred_size", "test/fixtures/pointer/funcptr_array_inferred_size.c"},

    {"array", "idx", "test/fixtures/array/idx.c"},
    {"array", "loop", "test/fixtures/array/loop.c"},
    {"array", "sum", "test/fixtures/array/sum.c"},
    {"array", "brace_init", "test/fixtures/array/brace_init.c"},
    {"array", "brace_init_designated", "test/fixtures/array/brace_init_designated.c"},
    {"array", "brace_init_designated_gap", "test/fixtures/array/brace_init_designated_gap.c"},
    {"array", "brace_init_partial_zeroed", "test/fixtures/array/brace_init_partial_zeroed.c"},
    {"array", "sizeof_array_div_elem", "test/fixtures/array/sizeof_array_div_elem.c"},
    {"array", "struct_array_brace_init", "test/fixtures/array/struct_array_brace_init.c"},
    {"array", "struct_array_brace_partial", "test/fixtures/array/struct_array_brace_partial.c"},
    {"array", "char_array_string_init", "test/fixtures/array/char_array_string_init.c"},
    {"array", "inferred_size_brace", "test/fixtures/array/inferred_size_brace.c"},
    {"array", "inferred_size_designated", "test/fixtures/array/inferred_size_designated.c"},
    {"array", "inferred_size_trailing_comma", "test/fixtures/array/inferred_size_trailing_comma.c"},
    {"array", "inferred_size_char_brace", "test/fixtures/array/inferred_size_char_brace.c"},
    {"array", "inferred_size_string", "test/fixtures/array/inferred_size_string.c"},
    {"array", "multi_dim_decl", "test/fixtures/array/multi_dim_decl.c"},
    {"array", "multi_dim_init", "test/fixtures/array/multi_dim_init.c"},
    {"array", "multi_dim_init_sum", "test/fixtures/array/multi_dim_init_sum.c"},
    {"array", "inferred_size_2d_flat", "test/fixtures/array/inferred_size_2d_flat.c"},
    {"array", "inferred_size_2d_nested", "test/fixtures/array/inferred_size_2d_nested.c"},
    {"array", "three_dim_assign_read", "test/fixtures/array/three_dim_assign_read.c"},
    {"array", "four_dim_assign_read", "test/fixtures/array/four_dim_assign_read.c"},
    {"array", "param_2d_array_subscript", "test/fixtures/array/param_2d_array_subscript.c"},
    {"array", "param_3d_array_subscript", "test/fixtures/array/param_3d_array_subscript.c"},
    {"array", "param_explicit_ptr_to_2d", "test/fixtures/array/param_explicit_ptr_to_2d.c"},
    {"array", "param_typedef_array_ptr_sum", "test/fixtures/array/param_typedef_array_ptr_sum.c"},

    {"global_var", "initialized", "test/fixtures/global_var/initialized.c"},
    {"global_var", "initialized_modified", "test/fixtures/global_var/initialized_modified.c"},
    {"global_var", "array_rw", "test/fixtures/global_var/array_rw.c"},
    {"global_var", "array_sum", "test/fixtures/global_var/array_sum.c"},
    {"global_var", "tentative_rw", "test/fixtures/global_var/tentative_rw.c"},
    {"global_var", "tentative_multi_func", "test/fixtures/global_var/tentative_multi_func.c"},
    {"global_var", "global_struct_init", "test/fixtures/global_var/global_struct_init.c"},
    {"global_var", "global_struct_assign", "test/fixtures/global_var/global_struct_assign.c"},
    {"global_var", "local_extern", "test/fixtures/global_var/local_extern.c"},

    {"alignas", "lvar_value", "test/fixtures/alignas/lvar_value.c"},
    {"alignas", "lvar_align", "test/fixtures/alignas/lvar_align.c"},
    {"alignas", "global_var", "test/fixtures/alignas/global_var.c"},
    {"alignas", "struct_member", "test/fixtures/alignas/struct_member.c"},
    {"alignas", "alignas_alignof", "test/fixtures/alignas/alignas_alignof.c"},

    {"bitfield", "read", "test/fixtures/bitfield/read.c"},
    {"bitfield", "read_b", "test/fixtures/bitfield/read_b.c"},
    {"bitfield", "write_masked", "test/fixtures/bitfield/write_masked.c"},
    {"bitfield", "signed_neg", "test/fixtures/bitfield/signed_neg.c"},
    {"bitfield", "unsigned_wrap", "test/fixtures/bitfield/unsigned_wrap.c"},
    {"bitfield", "packing", "test/fixtures/bitfield/packing.c"},

    {"type_decl", "int_func", "test/fixtures/type_decl/int_func.c"},
    {"type_decl", "int_var", "test/fixtures/type_decl/int_var.c"},
    {"type_decl", "int_sum", "test/fixtures/type_decl/int_sum.c"},
    {"type_decl", "int_args", "test/fixtures/type_decl/int_args.c"},
    {"type_decl", "int_init", "test/fixtures/type_decl/int_init.c"},
    {"type_decl", "multi_decl_one_init", "test/fixtures/type_decl/multi_decl_one_init.c"},
    {"type_decl", "multi_decl_two_init", "test/fixtures/type_decl/multi_decl_two_init.c"},
    {"type_decl", "for_decl", "test/fixtures/type_decl/for_decl.c"},
    {"type_decl", "for_multi_decl_init", "test/fixtures/type_decl/for_multi_decl_init.c"},
    {"type_decl", "tag_def_struct", "test/fixtures/type_decl/tag_def_struct.c"},
    {"type_decl", "tag_def_and_ptr_decl", "test/fixtures/type_decl/tag_def_and_ptr_decl.c"},
    {"type_decl", "tag_def_union_enum", "test/fixtures/type_decl/tag_def_union_enum.c"},
    {"type_decl", "enum_const_ref", "test/fixtures/type_decl/enum_const_ref.c"},
    {"type_decl", "enum_const_expr", "test/fixtures/type_decl/enum_const_expr.c"},
    {"type_decl", "char", "test/fixtures/type_decl/char.c"},
    {"type_decl", "void", "test/fixtures/type_decl/void.c"},
    {"type_decl", "short", "test/fixtures/type_decl/short.c"},
    {"type_decl", "long", "test/fixtures/type_decl/long.c"},
    {"type_decl", "unsigned_decl", "test/fixtures/type_decl/unsigned_decl.c"},
    {"type_decl", "bool_decl", "test/fixtures/type_decl/bool_decl.c"},
    {"type_decl", "signed_decl", "test/fixtures/type_decl/signed_decl.c"},
    {"type_decl", "ptr_deref_add_eq", "test/fixtures/type_decl/ptr_deref_add_eq.c"},
    {"type_decl", "ptr_ptr_deref", "test/fixtures/type_decl/ptr_ptr_deref.c"},
    {"type_decl", "sizeof_int", "test/fixtures/type_decl/sizeof_int.c"},
    {"type_decl", "sizeof_int_ptr", "test/fixtures/type_decl/sizeof_int_ptr.c"},
    {"type_decl", "sizeof_int_array_type", "test/fixtures/type_decl/sizeof_int_array_type.c"},
    {"type_decl", "sizeof_ptr_to_array_type", "test/fixtures/type_decl/sizeof_ptr_to_array_type.c"},
    {"type_decl", "sizeof_funcptr_type", "test/fixtures/type_decl/sizeof_funcptr_type.c"},
    {"type_decl", "alignof_int", "test/fixtures/type_decl/alignof_int.c"},
    {"type_decl", "alignof_ptr", "test/fixtures/type_decl/alignof_ptr.c"},
    {"type_decl", "alignof_int_array_type", "test/fixtures/type_decl/alignof_int_array_type.c"},
    {"type_decl", "cast_chain", "test/fixtures/type_decl/cast_chain.c"},
    {"type_decl", "cast_double_to_int", "test/fixtures/type_decl/cast_double_to_int.c"},
    {"type_decl", "cast_func_double_to_int", "test/fixtures/type_decl/cast_func_double_to_int.c"},
    {"type_decl", "double_param_int_param_mix", "test/fixtures/type_decl/double_param_int_param_mix.c"},
    {"type_decl", "void_ptr_roundtrip", "test/fixtures/type_decl/void_ptr_roundtrip.c"},
    {"type_decl", "unsigned_wrap", "test/fixtures/type_decl/unsigned_wrap.c"},
    {"type_decl", "signed_char_neg", "test/fixtures/type_decl/signed_char_neg.c"},
    {"type_decl", "struct_copy_independent", "test/fixtures/type_decl/struct_copy_independent.c"},
    {"type_decl", "struct_return_value", "test/fixtures/type_decl/struct_return_value.c"},
    {"type_decl", "struct_ptr_arrow", "test/fixtures/type_decl/struct_ptr_arrow.c"},
    {"type_decl", "member_dot", "test/fixtures/type_decl/member_dot.c"},
    {"type_decl", "member_arrow", "test/fixtures/type_decl/member_arrow.c"},
    {"type_decl", "member_funcptr", "test/fixtures/type_decl/member_funcptr.c"},
    {"type_decl", "member_union", "test/fixtures/type_decl/member_union.c"},
    {"type_decl", "typedef_int", "test/fixtures/type_decl/typedef_int.c"},
    {"type_decl", "typedef_ptr", "test/fixtures/type_decl/typedef_ptr.c"},
    {"type_decl", "typedef_in_func", "test/fixtures/type_decl/typedef_in_func.c"},
    {"type_decl", "typedef_funcptr", "test/fixtures/type_decl/typedef_funcptr.c"},
    {"type_decl", "typedef_funcptr_nested", "test/fixtures/type_decl/typedef_funcptr_nested.c"},
    {"type_decl", "local_funcptr_array_decl", "test/fixtures/type_decl/local_funcptr_array_decl.c"},
    {"type_decl", "struct_member_funcptr_array_size", "test/fixtures/type_decl/struct_member_funcptr_array_size.c"},
    {"type_decl", "unsigned_long_ret_funcdef", "test/fixtures/type_decl/unsigned_long_ret_funcdef.c"},
    {"type_decl", "unsigned_long_decl", "test/fixtures/type_decl/unsigned_long_decl.c"},
    {"type_decl", "unsigned_long_long_decl", "test/fixtures/type_decl/unsigned_long_long_decl.c"},
    {"type_decl", "char_sign_extend", "test/fixtures/type_decl/char_sign_extend.c"},
    {"type_decl", "unsigned_char_zero_extend", "test/fixtures/type_decl/unsigned_char_zero_extend.c"},
    {"type_decl", "short_sign_extend", "test/fixtures/type_decl/short_sign_extend.c"},
    {"type_decl", "unsigned_short_zero_extend", "test/fixtures/type_decl/unsigned_short_zero_extend.c"},
    {"type_decl", "unsigned_div", "test/fixtures/type_decl/unsigned_div.c"},
    {"type_decl", "unsigned_mod", "test/fixtures/type_decl/unsigned_mod.c"},
    {"type_decl", "unsigned_shr", "test/fixtures/type_decl/unsigned_shr.c"},
    {"type_decl", "signed_shr_preserve", "test/fixtures/type_decl/signed_shr_preserve.c"},
    {"type_decl", "unsigned_cmp_lt", "test/fixtures/type_decl/unsigned_cmp_lt.c"},
    {"type_decl", "unsigned_cmp_le", "test/fixtures/type_decl/unsigned_cmp_le.c"},
    {"type_decl", "const_decl", "test/fixtures/type_decl/const_decl.c"},
    {"type_decl", "volatile_decl", "test/fixtures/type_decl/volatile_decl.c"},
    {"type_decl", "scalar_brace_init", "test/fixtures/type_decl/scalar_brace_init.c"},
    {"type_decl", "static_assert_toplevel", "test/fixtures/type_decl/static_assert_toplevel.c"},
    {"type_decl", "static_assert_stmt", "test/fixtures/type_decl/static_assert_stmt.c"},
    {"type_decl", "typedef_array_1d_local", "test/fixtures/type_decl/typedef_array_1d_local.c"},
    {"type_decl", "typedef_array_2d_local", "test/fixtures/type_decl/typedef_array_2d_local.c"},
    {"type_decl", "typedef_array_3d_local", "test/fixtures/type_decl/typedef_array_3d_local.c"},
    {"type_decl", "global_array_1d_init", "test/fixtures/type_decl/global_array_1d_init.c"},
    {"type_decl", "global_array_2d_init", "test/fixtures/type_decl/global_array_2d_init.c"},
    {"type_decl", "global_array_partial_init", "test/fixtures/type_decl/global_array_partial_init.c"},
    {"type_decl", "generic_int", "test/fixtures/type_decl/generic_int.c"},
    {"type_decl", "generic_double", "test/fixtures/type_decl/generic_double.c"},
    {"type_decl", "generic_ptr", "test/fixtures/type_decl/generic_ptr.c"},
    {"type_decl", "float1", "test/fixtures/type_decl/float1.c"},
    {"type_decl", "float2", "test/fixtures/type_decl/float2.c"},
    {"type_decl", "double1", "test/fixtures/type_decl/double1.c"},
    {"type_decl", "double2", "test/fixtures/type_decl/double2.c"},
    {"type_decl", "global_ptr_addr_init", "test/fixtures/type_decl/global_ptr_addr_init.c"},
    {"type_decl", "global_ptr_addr_write", "test/fixtures/type_decl/global_ptr_addr_write.c"},

    {"arithmetic", "mod_neg_lhs", "test/fixtures/arithmetic/mod_neg_lhs.c"},
    {"arithmetic", "mod_neg_rhs", "test/fixtures/arithmetic/mod_neg_rhs.c"},
    {"arithmetic", "postinc_add", "test/fixtures/arithmetic/postinc_add.c"},
    {"arithmetic", "postdec_sub", "test/fixtures/arithmetic/postdec_sub.c"},
    {"arithmetic", "postinc_unary_plus", "test/fixtures/arithmetic/postinc_unary_plus.c"},
    {"arithmetic", "postdec_unary_minus", "test/fixtures/arithmetic/postdec_unary_minus.c"},
    {"arithmetic", "postinc_mul", "test/fixtures/arithmetic/postinc_mul.c"},
    {"arithmetic", "preinc_add", "test/fixtures/arithmetic/preinc_add.c"},
    {"arithmetic", "postinc_neg", "test/fixtures/arithmetic/postinc_neg.c"},
    {"arithmetic", "postinc_chain", "test/fixtures/arithmetic/postinc_chain.c"},
    {"arithmetic", "neg_postinc", "test/fixtures/arithmetic/neg_postinc.c"},
    {"arithmetic", "postinc_postdec_mix", "test/fixtures/arithmetic/postinc_postdec_mix.c"},
    {"comparison", "ternary_deep_nest", "test/fixtures/comparison/ternary_deep_nest.c"},
    {"switch_edge", "case_enum_const_expr", "test/fixtures/switch_edge/case_enum_const_expr.c"},
    {"switch_edge", "case_in_block", "test/fixtures/switch_edge/case_in_block.c"},
    {"switch_edge", "continue_in_switch_for", "test/fixtures/switch_edge/continue_in_switch_for.c"},
    {"switch_edge", "duff_do_while", "test/fixtures/switch_edge/duff_do_while.c"},
    {"switch_edge", "duff_do_while_case2", "test/fixtures/switch_edge/duff_do_while_case2.c"},
    {"switch_edge", "fallthrough", "test/fixtures/switch_edge/fallthrough.c"},
    {"switch_edge", "fallthrough_multi", "test/fixtures/switch_edge/fallthrough_multi.c"},
    {"switch_edge", "goto_from_case", "test/fixtures/switch_edge/goto_from_case.c"},
    {"switch_edge", "goto_inside_case", "test/fixtures/switch_edge/goto_inside_case.c"},
    {"switch_edge", "goto_into_loop", "test/fixtures/switch_edge/goto_into_loop.c"},
    {"switch_edge", "goto_out_of_loop_switch", "test/fixtures/switch_edge/goto_out_of_loop_switch.c"},
    {"funcall", "param_array_static_restrict", "test/fixtures/funcall/param_array_static_restrict.c"},
    {"funcall", "param_funcptr_decl", "test/fixtures/funcall/param_funcptr_decl.c"},
    {"funcall", "funcdef_ret_funcptr", "test/fixtures/funcall/funcdef_ret_funcptr.c"},
    {"funcall", "funcdef_ret_funcptr_with_param", "test/fixtures/funcall/funcdef_ret_funcptr_with_param.c"},
    {"funcall", "funcdef_ret_nested_funcptr_arrayptr",
     "test/fixtures/funcall/funcdef_ret_nested_funcptr_arrayptr.c"},
    {"multichar_var", "foo", "test/fixtures/multichar_var/foo.c"},
    {"multichar_var", "hello", "test/fixtures/multichar_var/hello.c"},
    {"multichar_var", "x1x2", "test/fixtures/multichar_var/x1x2.c"},
    {"multichar_var", "args", "test/fixtures/multichar_var/args.c"},
    {"multichar_var", "loop", "test/fixtures/multichar_var/loop.c"},
    {"inline_func", "basic_inline", "test/fixtures/inline_func/basic_inline.c"},
    {"inline_func", "static_inline", "test/fixtures/inline_func/static_inline.c"},
    {"inline_func", "multi_inline", "test/fixtures/inline_func/multi_inline.c"},
    {"inline_func", "extern_inline", "test/fixtures/inline_func/extern_inline.c"},
    {"flex_array", "parse_ok", "test/fixtures/flex_array/parse_ok.c"},
    {"flex_array", "sizeof_flex", "test/fixtures/flex_array/sizeof_flex.c"},
    {"array", "brace_wrapped_string_concat", "test/fixtures/array/brace_wrapped_string_concat.c"},
    {"array", "brace_wrapped_string_explicit_size", "test/fixtures/array/brace_wrapped_string_explicit_size.c"},
    {"array", "brace_wrapped_string_init", "test/fixtures/array/brace_wrapped_string_init.c"},
    {"array", "const_expr_size", "test/fixtures/array/const_expr_size.c"},
    {"array", "five_dim_assign_read", "test/fixtures/array/five_dim_assign_read.c"},
    {"array", "four_dim_flat_init", "test/fixtures/array/four_dim_flat_init.c"},
    {"array", "four_dim_inferred_outer", "test/fixtures/array/four_dim_inferred_outer.c"},
    {"array", "four_dim_nested_init", "test/fixtures/array/four_dim_nested_init.c"},
    {"array", "inferred_size_2d_three_rows", "test/fixtures/array/inferred_size_2d_three_rows.c"},
    {"array", "inferred_size_string_concat", "test/fixtures/array/inferred_size_string_concat.c"},
    {"array", "param_2d_array_explicit_outer", "test/fixtures/array/param_2d_array_explicit_outer.c"},
    {"array", "param_explicit_ptr_to_3d", "test/fixtures/array/param_explicit_ptr_to_3d.c"},
    {"array", "param_typedef_2d_array_ptr", "test/fixtures/array/param_typedef_2d_array_ptr.c"},
    {"array", "param_typedef_array_ptr", "test/fixtures/array/param_typedef_array_ptr.c"},
    {"array", "three_dim_flat_init", "test/fixtures/array/three_dim_flat_init.c"},
    {"array", "three_dim_inferred_outer", "test/fixtures/array/three_dim_inferred_outer.c"},
    {"array", "three_dim_nested_init", "test/fixtures/array/three_dim_nested_init.c"},
    {"pointer", "struct_ptr_param_paren", "test/fixtures/pointer/struct_ptr_param_paren.c"},
    {"pragma_pack", "pack1_offset", "test/fixtures/pragma_pack/pack1_offset.c"},
    {"pragma_pack", "pack1_sizeof", "test/fixtures/pragma_pack/pack1_sizeof.c"},
    {"pragma_pack", "pack2_sizeof", "test/fixtures/pragma_pack/pack2_sizeof.c"},
    {"pragma_pack", "pack_n_no_push", "test/fixtures/pragma_pack/pack_n_no_push.c"},
    {"pragma_pack", "pop_restores", "test/fixtures/pragma_pack/pop_restores.c"},
    {"type_decl", "short_arr", "test/fixtures/type_decl/short_arr.c"},
    {"type_decl", "short_sum", "test/fixtures/type_decl/short_sum.c"},
    {"type_decl", "short_one", "test/fixtures/type_decl/short_one.c"},
    {"type_decl", "char_add_eq", "test/fixtures/type_decl/char_add_eq.c"},
    {"type_decl", "short_mul_eq", "test/fixtures/type_decl/short_mul_eq.c"},
    {"type_decl", "sizeof_bool", "test/fixtures/type_decl/sizeof_bool.c"},
    {"type_decl", "sizeof_int_ptr_const", "test/fixtures/type_decl/sizeof_int_ptr_const.c"},
    {"type_decl", "sizeof_int_ptr_volatile", "test/fixtures/type_decl/sizeof_int_ptr_volatile.c"},
    {"type_decl", "sizeof_int_ptr_restrict", "test/fixtures/type_decl/sizeof_int_ptr_restrict.c"},
    {"type_decl", "sizeof_parenthesized_array_type", "test/fixtures/type_decl/sizeof_parenthesized_array_type.c"},
    {"type_decl", "alignof_ptr_const", "test/fixtures/type_decl/alignof_ptr_const.c"},
    {"type_decl", "alignof_ptr_volatile", "test/fixtures/type_decl/alignof_ptr_volatile.c"},
    {"type_decl", "alignof_ptr_restrict", "test/fixtures/type_decl/alignof_ptr_restrict.c"},
    {"type_decl", "alignof_ptr_to_array_type", "test/fixtures/type_decl/alignof_ptr_to_array_type.c"},
    {"type_decl", "alignof_parenthesized_array_type",
     "test/fixtures/type_decl/alignof_parenthesized_array_type.c"},
    {"type_decl", "sizeof_expr_var", "test/fixtures/type_decl/sizeof_expr_var.c"},
    {"type_decl", "sizeof_struct_type", "test/fixtures/type_decl/sizeof_struct_type.c"},
    {"type_decl", "alignof_struct_type", "test/fixtures/type_decl/alignof_struct_type.c"},
    {"type_decl", "sizeof_struct_ptr_to_array_type", "test/fixtures/type_decl/sizeof_struct_ptr_to_array_type.c"},
    {"type_decl", "sizeof_struct_array_type", "test/fixtures/type_decl/sizeof_struct_array_type.c"},
    {"type_decl", "sizeof_typedef_ptr_to_array_type",
     "test/fixtures/type_decl/sizeof_typedef_ptr_to_array_type.c"},
    {"type_decl", "sizeof_typedef_array_type_2d", "test/fixtures/type_decl/sizeof_typedef_array_type_2d.c"},
    {"type_decl", "comma_expr_init", "test/fixtures/type_decl/comma_expr_init.c"},
    {"type_decl", "comma_sideeffect", "test/fixtures/type_decl/comma_sideeffect.c"},
    {"type_decl", "comma_assign_chain", "test/fixtures/type_decl/comma_assign_chain.c"},
    {"type_decl", "bitwise_swap_nibble", "test/fixtures/type_decl/bitwise_swap_nibble.c"},
    {"type_decl", "bitwise_mask_or", "test/fixtures/type_decl/bitwise_mask_or.c"},
    {"type_decl", "global_shadow_local", "test/fixtures/type_decl/global_shadow_local.c"},
    {"type_decl", "cast_int", "test/fixtures/type_decl/cast_int.c"},
    {"type_decl", "cast_char_wrap", "test/fixtures/type_decl/cast_char_wrap.c"},
    {"type_decl", "cast_short_wrap", "test/fixtures/type_decl/cast_short_wrap.c"},
    {"type_decl", "cast_bool_true", "test/fixtures/type_decl/cast_bool_true.c"},
    {"type_decl", "cast_bool_false", "test/fixtures/type_decl/cast_bool_false.c"},
    {"type_decl", "cast_unsigned", "test/fixtures/type_decl/cast_unsigned.c"},
    {"type_decl", "cast_enum", "test/fixtures/type_decl/cast_enum.c"},
    {"type_decl", "cast_tag_ptr", "test/fixtures/type_decl/cast_tag_ptr.c"},
    {"type_decl", "cast_struct_same_type", "test/fixtures/type_decl/cast_struct_same_type.c"},
    {"type_decl", "cast_union_same_type", "test/fixtures/type_decl/cast_union_same_type.c"},
    {"type_decl", "union_brace_init_value", "test/fixtures/type_decl/union_brace_init_value.c"},
    {"type_decl", "union_brace_init_designated", "test/fixtures/type_decl/union_brace_init_designated.c"},
    {"type_decl", "struct_brace_init_values", "test/fixtures/type_decl/struct_brace_init_values.c"},
    {"type_decl", "struct_brace_init_designated", "test/fixtures/type_decl/struct_brace_init_designated.c"},
    {"type_decl", "struct_partial_init", "test/fixtures/type_decl/struct_partial_init.c"},
    {"type_decl", "struct_designated_gap", "test/fixtures/type_decl/struct_designated_gap.c"},
    {"type_decl", "sizeof_funcall_int", "test/fixtures/type_decl/sizeof_funcall_int.c"},
    {"type_decl", "sizeof_funcall_double", "test/fixtures/type_decl/sizeof_funcall_double.c"},
    {"type_decl", "sizeof_no_side_effect", "test/fixtures/type_decl/sizeof_no_side_effect.c"},
    {"type_decl", "typedef_struct_forward_tag", "test/fixtures/type_decl/typedef_struct_forward_tag.c"},
    {"type_decl", "typedef_struct_anon_top", "test/fixtures/type_decl/typedef_struct_anon_top.c"},
    {"type_decl", "typedef_union_forward_tag", "test/fixtures/type_decl/typedef_union_forward_tag.c"},
    {"type_decl", "typedef_union_anon_top", "test/fixtures/type_decl/typedef_union_anon_top.c"},
    {"type_decl", "typedef_local_struct_forward_tag", "test/fixtures/type_decl/typedef_local_struct_forward_tag.c"},
    {"type_decl", "typedef_local_struct_anon", "test/fixtures/type_decl/typedef_local_struct_anon.c"},
    {"type_decl", "typedef_local_union_forward_tag", "test/fixtures/type_decl/typedef_local_union_forward_tag.c"},
    {"type_decl", "typedef_local_union_anon", "test/fixtures/type_decl/typedef_local_union_anon.c"},
    {"type_decl", "typedef_funcptr_array_nested", "test/fixtures/type_decl/typedef_funcptr_array_nested.c"},
    {"type_decl", "typedef_local_funcptr_nested", "test/fixtures/type_decl/typedef_local_funcptr_nested.c"},
    {"type_decl", "toplevel_funcptr_array_decl", "test/fixtures/type_decl/toplevel_funcptr_array_decl.c"},
    {"type_decl", "struct_member_funcptr_array_decl", "test/fixtures/type_decl/struct_member_funcptr_array_decl.c"},
    {"type_decl", "typedef_funcptr_param", "test/fixtures/type_decl/typedef_funcptr_param.c"},
    {"type_decl", "typedef_ret_funcdef", "test/fixtures/type_decl/typedef_ret_funcdef.c"},
    {"type_decl", "typedef_ptr_ret_proto", "test/fixtures/type_decl/typedef_ptr_ret_proto.c"},
    {"type_decl", "unnamed_param_prototype", "test/fixtures/type_decl/unnamed_param_prototype.c"},
    {"type_decl", "signed_short_decl", "test/fixtures/type_decl/signed_short_decl.c"},
    {"type_decl", "signed_char_decl", "test/fixtures/type_decl/signed_char_decl.c"},
    {"type_decl", "duplicate_qualifiers_decl", "test/fixtures/type_decl/duplicate_qualifiers_decl.c"},
    {"type_decl", "duplicate_qualifiers_param", "test/fixtures/type_decl/duplicate_qualifiers_param.c"},
    {"type_decl", "duplicate_postfix_const_cast", "test/fixtures/type_decl/duplicate_postfix_const_cast.c"},
    {"type_decl", "storage_specs_local", "test/fixtures/type_decl/storage_specs_local.c"},
    {"type_decl", "long_double_sizeof", "test/fixtures/type_decl/long_double_sizeof.c"},
    {"type_decl", "complex_sizeof", "test/fixtures/type_decl/complex_sizeof.c"},
    {"type_decl", "complex_float_sizeof", "test/fixtures/type_decl/complex_float_sizeof.c"},
    {"type_decl", "extern_inline_funcspec", "test/fixtures/type_decl/extern_inline_funcspec.c"},
    {"type_decl", "noreturn_spec_parse", "test/fixtures/type_decl/noreturn_spec_parse.c"},
    {"type_decl", "static_assert_typedef_array_sizeof",
     "test/fixtures/type_decl/static_assert_typedef_array_sizeof.c"},
    {"type_decl", "typedef_array_4d_local", "test/fixtures/type_decl/typedef_array_4d_local.c"},
    {"type_decl", "inline_array_addr_cast", "test/fixtures/type_decl/inline_array_addr_cast.c"},
    {"type_decl", "typedef_array_addr_cast", "test/fixtures/type_decl/typedef_array_addr_cast.c"},
    {"type_decl", "typedef_array_addr_func_arg", "test/fixtures/type_decl/typedef_array_addr_func_arg.c"},
    {"type_decl", "typedef_array_user_suffix", "test/fixtures/type_decl/typedef_array_user_suffix.c"},
    {"type_decl", "typedef_array_ptr_param_3d", "test/fixtures/type_decl/typedef_array_ptr_param_3d.c"},
    {"type_decl", "typedef_array_sizeof", "test/fixtures/type_decl/typedef_array_sizeof.c"},
    {"type_decl", "typedef_array_init", "test/fixtures/type_decl/typedef_array_init.c"},
    {"type_decl", "typedef_array_5d_local", "test/fixtures/type_decl/typedef_array_5d_local.c"},
    {"type_decl", "inline_array_1d_addr_cast", "test/fixtures/type_decl/inline_array_1d_addr_cast.c"},
    {"type_decl", "inline_array_2d_addr_cast", "test/fixtures/type_decl/inline_array_2d_addr_cast.c"},
    {"type_decl", "global_array_3d_init", "test/fixtures/type_decl/global_array_3d_init.c"},
    {"type_decl", "global_typedef_array_2d_init", "test/fixtures/type_decl/global_typedef_array_2d_init.c"},
    {"type_decl", "alignas_atomic_prefix", "test/fixtures/type_decl/alignas_atomic_prefix.c"},
    {"type_decl", "atomic_type_spec", "test/fixtures/type_decl/atomic_type_spec.c"},
    {"type_decl", "atomic_type_qual_postfix", "test/fixtures/type_decl/atomic_type_qual_postfix.c"},
    {"type_decl", "atomic_type_qual_postfix_ptr", "test/fixtures/type_decl/atomic_type_qual_postfix_ptr.c"},
    {"evil", "anon_enum_negative", "test/fixtures/evil/anon_enum_negative.c"},
    {"evil", "sizeof_no_eval", "test/fixtures/evil/sizeof_no_eval.c"},
    {"evil", "cast_uchar_neg", "test/fixtures/evil/cast_uchar_neg.c"},
    {"evil", "dowhile_break", "test/fixtures/evil/dowhile_break.c"},
    {"evil", "uint_max_plus1_zero", "test/fixtures/evil/uint_max_plus1_zero.c"},
    {"evil", "multi_shift", "test/fixtures/evil/multi_shift.c"},
    {"evil", "static_assert_sizeof", "test/fixtures/evil/static_assert_sizeof.c"},
    {"evil", "uchar_wrap", "test/fixtures/evil/uchar_wrap.c"},
    {"evil", "null_stmt", "test/fixtures/evil/null_stmt.c"},
    {"evil", "int_max_inc_wraps", "test/fixtures/evil/int_max_inc_wraps.c"},
    {"evil", "anon_enum_assign", "test/fixtures/evil/anon_enum_assign.c"},
    {"evil", "logical_not_zero", "test/fixtures/evil/logical_not_zero.c"},
    {"evil", "uint_shr_no_signext", "test/fixtures/evil/uint_shr_no_signext.c"},
    {"evil", "assign_in_cond", "test/fixtures/evil/assign_in_cond.c"},
    {"evil", "post_const_int", "test/fixtures/evil/post_const_int.c"},
    {"evil", "mutual_recursion", "test/fixtures/evil/mutual_recursion.c"},
    {"evil", "uint_div_large", "test/fixtures/evil/uint_div_large.c"},
    {"evil", "large_imm_var", "test/fixtures/evil/large_imm_var.c"},
    {"evil", "dowhile_continue", "test/fixtures/evil/dowhile_continue.c"},
    {"evil", "for_scope_shadow", "test/fixtures/evil/for_scope_shadow.c"},
    {"evil", "uint_mul_wrap", "test/fixtures/evil/uint_mul_wrap.c"},
    {"evil", "complex_expr_8vars", "test/fixtures/evil/complex_expr_8vars.c"},
    {"evil", "null_stmt_mixed", "test/fixtures/evil/null_stmt_mixed.c"},
    {"evil", "uint_sub_wrap", "test/fixtures/evil/uint_sub_wrap.c"},
    {"evil", "addr_deref_chain", "test/fixtures/evil/addr_deref_chain.c"},
    {"evil", "struct_padding_sizeof", "test/fixtures/evil/struct_padding_sizeof.c"},
    {"evil", "char_127_plus1", "test/fixtures/evil/char_127_plus1.c"},
    {"evil", "while1_break", "test/fixtures/evil/while1_break.c"},
    {"evil", "logical_not_nonzero", "test/fixtures/evil/logical_not_nonzero.c"},
    {"evil", "nested_for_loops", "test/fixtures/evil/nested_for_loops.c"},
    {"evil", "int_max_plus1_wraps", "test/fixtures/evil/int_max_plus1_wraps.c"},
    {"evil", "max3_nested", "test/fixtures/evil/max3_nested.c"},
    {"evil", "global_sideeffect_seq", "test/fixtures/evil/global_sideeffect_seq.c"},
    {"evil", "signed_cmp_neg", "test/fixtures/evil/signed_cmp_neg.c"},
    {"evil", "neg_div_truncate", "test/fixtures/evil/neg_div_truncate.c"},
    {"evil", "signed_cmp_lt", "test/fixtures/evil/signed_cmp_lt.c"},
    {"evil", "char_subtract", "test/fixtures/evil/char_subtract.c"},
    {"evil", "large_imm_mod", "test/fixtures/evil/large_imm_mod.c"},
    {"evil", "bitwise_not", "test/fixtures/evil/bitwise_not.c"},
    {"evil", "post_const_char", "test/fixtures/evil/post_const_char.c"},
    {"evil", "block_shadow", "test/fixtures/evil/block_shadow.c"},
    {"evil", "collatz_recursion", "test/fixtures/evil/collatz_recursion.c"},
    {"evil", "char_neg_to_uint", "test/fixtures/evil/char_neg_to_uint.c"},
    {"evil", "nested_call", "test/fixtures/evil/nested_call.c"},
    {"evil", "char_overflow", "test/fixtures/evil/char_overflow.c"},
    {"evil", "nested_shadow", "test/fixtures/evil/nested_shadow.c"},
    {"func_name", "each_func_distinct", "test/fixtures/func_name/each_func_distinct.c"},
    {"func_name", "first_char_helper", "test/fixtures/func_name/first_char_helper.c"},
    {"func_name", "first_char_main", "test/fixtures/func_name/first_char_main.c"},
    {"string", "empty", "test/fixtures/string/empty.c"},
    {"string", "charlit", "test/fixtures/string/charlit.c"},
    {"string", "newline", "test/fixtures/string/newline.c"},
    {"string", "nul", "test/fixtures/string/nul.c"},
    {"string", "index", "test/fixtures/string/index.c"},
    {"string", "deref", "test/fixtures/string/deref.c"},
    {"string", "buf_idx", "test/fixtures/string/buf_idx.c"},
    {"string", "buf_sum", "test/fixtures/string/buf_sum.c"},
    {"string", "char_var", "test/fixtures/string/char_var.c"},
    {"stdheader", "stdbool_true", "test/fixtures/stdheader/stdbool_true.c"},
    {"stdheader", "stdbool_false", "test/fixtures/stdheader/stdbool_false.c"},
    {"stdheader", "stddef_null", "test/fixtures/stdheader/stddef_null.c"},
    {"stdheader", "stddef_size_t", "test/fixtures/stdheader/stddef_size_t.c"},
    {"stdheader", "stddef_wchar_t", "test/fixtures/stdheader/stddef_wchar_t.c"},
    {"stdheader", "limits_char_bit", "test/fixtures/stdheader/limits_char_bit.c"},
    {"stdheader", "limits_int_max", "test/fixtures/stdheader/limits_int_max.c"},
    {"stdheader", "limits_int_min", "test/fixtures/stdheader/limits_int_min.c"},
    {"stdheader", "stdint_int32", "test/fixtures/stdheader/stdint_int32.c"},
    {"stdheader", "stdint_uint8", "test/fixtures/stdheader/stdint_uint8.c"},
    {"struct_arg", "small_member", "test/fixtures/struct_arg/small_member.c"},
    {"struct_arg", "small_sum", "test/fixtures/struct_arg/small_sum.c"},
    {"struct_arg", "mid_sum", "test/fixtures/struct_arg/mid_sum.c"},
    {"struct_ret", "return_member", "test/fixtures/struct_ret/return_member.c"},
    {"struct_ret", "make_and_sum", "test/fixtures/struct_ret/make_and_sum.c"},
    {"struct_ret", "chain_call", "test/fixtures/struct_ret/chain_call.c"},
    {"struct_ret", "ret_12b_sum", "test/fixtures/struct_ret/ret_12b_sum.c"},
    {"struct_ret", "ret_12b_member_c", "test/fixtures/struct_ret/ret_12b_member_c.c"},
    {"struct_ret", "ret_16b_sum", "test/fixtures/struct_ret/ret_16b_sum.c"},
    {"struct_ret", "ret_20b_indirect", "test/fixtures/struct_ret/ret_20b_indirect.c"},
    {"struct_ret", "ret_24b_member_f", "test/fixtures/struct_ret/ret_24b_member_f.c"},
    {"struct_ret", "ret_40b_sum", "test/fixtures/struct_ret/ret_40b_sum.c"},
};

static int mkdir_p(const char *path) {
  char tmp[512];
  size_t len = strlen(path);
  if (len >= sizeof(tmp)) return -1;
  memcpy(tmp, path, len + 1);
  for (char *p = tmp + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';
      if (mkdir(tmp, 0777) != 0) {
        /* Ignore already-existing directories without pulling in errno. */
      }
      *p = '/';
    }
  }
  mkdir(tmp, 0777);
  return 0;
}

static int slurp(const char *path, char *buf, size_t cap) {
  FILE *fp = fopen(path, "rb");
  if (!fp || cap == 0) return -1;
  size_t n = fread(buf, 1, cap - 1, fp);
  fclose(fp);
  buf[n] = '\0';
  return 0;
}

static int command_available(const char *cmd) {
  char probe[256];
  snprintf(probe, sizeof(probe), "command -v %s > /dev/null 2>&1", cmd);
  return system(probe) == 0;
}

static void sanitize(const char *in, char *out, size_t cap) {
  size_t j = 0;
  for (size_t i = 0; in[i] && j + 1 < cap; i++) {
    char c = in[i];
    out[j++] = (c == '/' || c == '-' || c == '.') ? '_' : c;
  }
  out[j] = '\0';
}

static int write_transformed_source(const wasm_e2e_case_t *tc, const char *out_path) {
  FILE *in = fopen(tc->path, "rb");
  if (!in) {
    fprintf(stderr, "FAIL: open fixture %s\n", tc->path);
    return 1;
  }
  FILE *out = fopen(out_path, "wb");
  if (!out) {
    fclose(in);
    fprintf(stderr, "FAIL: create %s\n", out_path);
    return 1;
  }
  fprintf(out, "#define assert(expr) do { if (!(expr)) return 100; } while (0)\n");
  char line[4096];
  while (fgets(line, sizeof(line), in)) {
    char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#' && strstr(p, "include") && strstr(p, "assert.h")) continue;
    fputs(line, out);
  }
  fclose(in);
  fclose(out);
  return 0;
}

static int run_wabt_case(const char *case_id, const char *wat_path, int expected_i, int *executed) {
  *executed = 0;
  if (!command_available("wat2wasm") || !command_available("wasm-validate") ||
      !command_available("wasm-interp")) {
    return 0;
  }
  *executed = 1;
  char wasm_path[512];
  char log_path[512];
  snprintf(wasm_path, sizeof(wasm_path), "build/wasm32_e2e/%s.wasm", case_id);
  snprintf(log_path, sizeof(log_path), "build/wasm32_e2e/%s.interp.log", case_id);

  char cmd[1536];
  snprintf(cmd, sizeof(cmd), "wat2wasm %s -o %s", wat_path, wasm_path);
  if (system(cmd) != 0) {
    fprintf(stderr, "FAIL: wat2wasm %s\n", case_id);
    return 1;
  }
  snprintf(cmd, sizeof(cmd), "wasm-validate %s", wasm_path);
  if (system(cmd) != 0) {
    fprintf(stderr, "FAIL: wasm-validate %s\n", case_id);
    return 1;
  }
  snprintf(cmd, sizeof(cmd), "wasm-interp %s --run-all-exports > %s", wasm_path, log_path);
  if (system(cmd) != 0) {
    fprintf(stderr, "FAIL: wasm-interp %s\n", case_id);
    return 1;
  }

  char buf[8192];
  if (slurp(log_path, buf, sizeof(buf)) != 0) return 1;
  char expected[64];
  snprintf(expected, sizeof(expected), "main() => i32:%d", expected_i);
  if (!strstr(buf, expected)) {
    fprintf(stderr, "FAIL: %s expected '%s'\n", case_id, expected);
    return 1;
  }
  return 0;
}

static int run_case(const wasm_e2e_case_t *tc, int *executed) {
  char cat[128];
  char name[128];
  char case_id[320];
  sanitize(tc->category, cat, sizeof(cat));
  sanitize(tc->name, name, sizeof(name));
  snprintf(case_id, sizeof(case_id), "%s_%s", cat, name);

  char src_path[512];
  char wat_path[512];
  char log_path[512];
  snprintf(src_path, sizeof(src_path), "build/wasm32_e2e/%s.c", case_id);
  snprintf(wat_path, sizeof(wat_path), "build/wasm32_e2e/%s.wat", case_id);
  snprintf(log_path, sizeof(log_path), "build/wasm32_e2e/%s.compile.log", case_id);

  if (write_transformed_source(tc, src_path) != 0) return 1;

  char cmd[1536];
  snprintf(cmd, sizeof(cmd), "./build/ag_c_wasm %s > %s 2> %s", src_path, wat_path, log_path);
  if (system(cmd) != 0) {
    fprintf(stderr, "FAIL: ag_c_wasm %s (see %s)\n", case_id, log_path);
    return 1;
  }
  return run_wabt_case(case_id, wat_path, 0, executed);
}

static int write_link2_wrapper_source(const wasm_link2_case_t *tc, const char *case_id,
                                      const char *out_path) {
  FILE *out = fopen(out_path, "wb");
  if (!out) {
    fprintf(stderr, "FAIL: create %s\n", out_path);
    return 1;
  }
  fprintf(out, "#define assert(expr) do { if (!(expr)) return 100; } while (0)\n");
  fprintf(out, "#define s __ag_%s_other_s\n", case_id);
  fprintf(out, "#define base __ag_%s_other_base\n", case_id);
  fprintf(out, "#include \"%s\"\n", tc->file_other);
  fprintf(out, "#undef base\n");
  fprintf(out, "#undef s\n");
  fprintf(out, "#include \"%s\"\n", tc->file_main);
  fclose(out);
  return 0;
}

static int run_link2_case(const wasm_link2_case_t *tc, int *executed) {
  char cat[128];
  char name[128];
  char case_id[320];
  sanitize(tc->category, cat, sizeof(cat));
  sanitize(tc->name, name, sizeof(name));
  snprintf(case_id, sizeof(case_id), "link2_%s_%s", cat, name);

  char src_path[512];
  char wat_path[512];
  char log_path[512];
  snprintf(src_path, sizeof(src_path), "build/wasm32_e2e/%s.c", case_id);
  snprintf(wat_path, sizeof(wat_path), "build/wasm32_e2e/%s.wat", case_id);
  snprintf(log_path, sizeof(log_path), "build/wasm32_e2e/%s.compile.log", case_id);

  if (write_link2_wrapper_source(tc, case_id, src_path) != 0) return 1;

  char cmd[1536];
  snprintf(cmd, sizeof(cmd), "./build/ag_c_wasm %s > %s 2> %s", src_path, wat_path, log_path);
  if (system(cmd) != 0) {
    fprintf(stderr, "FAIL: ag_c_wasm %s (see %s)\n", case_id, log_path);
    return 1;
  }
  return run_wabt_case(case_id, wat_path, tc->expected_i, executed);
}

static int load_extra_cases(const char *path, size_t *out_count) {
  *out_count = 0;
  FILE *fp = fopen(path, "rb");
  if (!fp) {
    fprintf(stderr, "FAIL: open extra Wasm E2E case list %s\n", path);
    return 1;
  }

  char line[256];
  while (fgets(line, sizeof(line), fp)) {
    char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '#' || *p == '\n' || *p == '\0') continue;

    char *end = p + strlen(p);
    while (end > p && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t')) {
      *--end = '\0';
    }
    if (*p == '\0') continue;
    if (*out_count >= MAX_EXTRA_CASES) {
      fclose(fp);
      fprintf(stderr, "FAIL: too many extra Wasm E2E cases\n");
      return 1;
    }

    char *slash = strchr(p, '/');
    char *dot = strrchr(p, '.');
    if (!slash || !dot || strcmp(dot, ".c") != 0 || slash == p || dot <= slash + 1) {
      fclose(fp);
      fprintf(stderr, "FAIL: invalid extra Wasm E2E case path '%s'\n", p);
      return 1;
    }

    size_t idx = *out_count;
    size_t cat_len = (size_t)(slash - p);
    size_t name_len = (size_t)(dot - slash - 1);
    if (cat_len >= sizeof(extra_case_categories[idx]) || name_len >= sizeof(extra_case_names[idx])) {
      fclose(fp);
      fprintf(stderr, "FAIL: extra Wasm E2E case path too long '%s'\n", p);
      return 1;
    }
    memcpy(extra_case_categories[idx], p, cat_len);
    extra_case_categories[idx][cat_len] = '\0';
    memcpy(extra_case_names[idx], slash + 1, name_len);
    extra_case_names[idx][name_len] = '\0';
    snprintf(extra_case_paths[idx], sizeof(extra_case_paths[idx]), "test/fixtures/%s", p);

    extra_cases[idx].category = extra_case_categories[idx];
    extra_cases[idx].name = extra_case_names[idx];
    extra_cases[idx].path = extra_case_paths[idx];
    (*out_count)++;
  }
  fclose(fp);
  return 0;
}

int main(void) {
  if (mkdir_p("build/wasm32_e2e") != 0) {
    fprintf(stderr, "FAIL: mkdir build/wasm32_e2e\n");
    return 1;
  }
  size_t nextra = 0;
  if (load_extra_cases("test/wasm32_e2e_extra_cases.txt", &nextra) != 0) return 1;

  int failures = 0;
  int executed = 0;
  size_t nstatic = sizeof(cases) / sizeof(cases[0]);
  size_t nlink2 = sizeof(link2_cases) / sizeof(link2_cases[0]);
  size_t ncases = nstatic + nextra + nlink2;
  for (size_t i = 0; i < nstatic; i++) {
    int did_execute = 0;
    failures += run_case(&cases[i], &did_execute);
    executed += did_execute;
  }
  for (size_t i = 0; i < nextra; i++) {
    int did_execute = 0;
    failures += run_case(&extra_cases[i], &did_execute);
    executed += did_execute;
  }
  for (size_t i = 0; i < nlink2; i++) {
    int did_execute = 0;
    failures += run_link2_case(&link2_cases[i], &did_execute);
    executed += did_execute;
  }
  if (failures) {
    fprintf(stderr, "wasm32 e2e tests failed: %d/%zu\n", failures, ncases);
    return 1;
  }
  if (executed == 0) {
    printf("wasm32 e2e tests compiled %zu cases (WABT tools unavailable, execution skipped)\n", ncases);
  } else {
    printf("wasm32 e2e tests passed (%zu compiled, %d executed)\n", ncases, executed);
  }
  return 0;
}
