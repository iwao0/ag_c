#ifndef AG_COMPILATION_OPTIONS_H
#define AG_COMPILATION_OPTIONS_H

#include <stdbool.h>

typedef struct ag_compilation_options_t {
  bool enable_size_compatible_nonscalar_cast;
  bool enable_struct_scalar_pointer_cast;
  bool enable_union_scalar_pointer_cast;
  bool enable_union_array_member_nonbrace_init;
} ag_compilation_options_t;

void ag_compilation_options_init_defaults(
    ag_compilation_options_t *options);

#endif
