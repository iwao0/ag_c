/*
 * C digraphs are alternative spellings of the corresponding punctuators in
 * preprocessing directives, macro operators, and ordinary C syntax.
 */
%:include <assert.h>
%:include <string.h>

%:define VALUE 42
%:define PREFIX 7
%:define prefix 8
%:define PRE pre
%:define FIX fix
%:define STRINGIZE_RAW(value) %: value
%:define STRINGIZE(value) STRINGIZE_RAW(value)
%:define PASTE_RAW(left, right) left %:%: right
%:define PASTE(left, right) PASTE_RAW(left, right)

%:if VALUE != 42
%:error digraph directive handling failed
%:endif

struct Pair <% int first; int second; %>;

int main(void) <%
  int values<:3:> = <% 11, 13, 17 %>;
  struct Pair pair = <% .first = 19, .second = 23 %>;

  assert(values<:0:> == 11);
  assert(values<:1:> == 13);
  assert(values<:2:> == 17);
  assert(pair.first + pair.second == 42);
  assert(PASTE_RAW(PRE, FIX) == 7);
  assert(PASTE(PRE, FIX) == 8);
  assert(strcmp(STRINGIZE_RAW(VALUE), "VALUE") == 0);
  assert(strcmp(STRINGIZE(VALUE), "42") == 0);
  return 0;
%>
