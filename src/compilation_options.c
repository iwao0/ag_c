#include "compilation_options.h"

void ag_compilation_options_init_defaults(
    ag_compilation_options_t *options) {
  if (!options) return;
  *options = (ag_compilation_options_t){
      .enable_size_compatible_nonscalar_cast = true,
      .enable_struct_scalar_pointer_cast = true,
      .enable_union_scalar_pointer_cast = true,
      .enable_union_array_member_nonbrace_init = true,
  };
}
