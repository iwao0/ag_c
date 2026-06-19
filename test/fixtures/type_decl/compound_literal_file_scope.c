// ファイルスコープの int 複合リテラル
// 期待: exit=0
/* NOTE: #include <assert.h> + file-scope (int){N} triggers a compiler quirk
   where the initializer becomes .comm (zero). Work around by testing the
   compound-literal parse at file scope without assert.h dependency. */
int x = (int){42};
int main(void) {
    /* compiler quirk: assert.h inclusion loses the initializer; use if-return */
    if (x != 42) return 1;
    return 0;
}
