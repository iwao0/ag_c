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
    if (strstr(line, "#include") && strstr(line, "assert.h")) continue;
    fputs(line, out);
  }
  fclose(in);
  fclose(out);
  return 0;
}

static int run_wabt_case(const char *case_id, const char *wat_path, int *executed) {
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
  if (!strstr(buf, "main() => i32:0")) {
    fprintf(stderr, "FAIL: %s expected 'main() => i32:0'\n", case_id);
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
  return run_wabt_case(case_id, wat_path, executed);
}

int main(void) {
  if (mkdir_p("build/wasm32_e2e") != 0) {
    fprintf(stderr, "FAIL: mkdir build/wasm32_e2e\n");
    return 1;
  }

  int failures = 0;
  int executed = 0;
  size_t ncases = sizeof(cases) / sizeof(cases[0]);
  for (size_t i = 0; i < ncases; i++) {
    int did_execute = 0;
    failures += run_case(&cases[i], &did_execute);
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
