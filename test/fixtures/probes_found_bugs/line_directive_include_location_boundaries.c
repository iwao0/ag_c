/*
 * An include has its own logical #line state.  Returning from it must restore
 * the parent's line delta and escaped/Unicode logical filename exactly.
 */
#include <assert.h>
#include <string.h>

#line 100 "outer\\source-\u03A9.c"
static const char parent_file_before_include[] = __FILE__;
static const int parent_line_before_include = __LINE__;
#include "test/fixtures/probes_found_bugs/line_directive_include_location_header.h"
static const char parent_file_after_include[] = __FILE__;
static const int parent_line_after_include = __LINE__;
static const char included_macro_call_file[] = INCLUDED_LOCATION_FILE();
static const int included_macro_call_line = INCLUDED_LOCATION_LINE();

int main(void) {
  assert(strcmp(parent_file_before_include, "outer\\source-\u03A9.c") == 0);
  assert(strcmp(parent_file_after_include, parent_file_before_include) == 0);
  assert(parent_line_before_include == 101);
  assert(parent_line_after_include == parent_line_before_include + 3);
  assert(strcmp(included_macro_call_file, parent_file_before_include) == 0);
  assert(included_macro_call_line == parent_line_after_include + 2);

  assert(strcmp(included_mapped_file, "inner\\header-\u03A9.h") == 0);
  assert(included_mapped_line == 701);
  return 0;
}
