// 非zeroの整数複合式はnull pointer constantではなく、pointerへ暗黙変換できない。
int main(void) { int *pointer = 1 + 0; return pointer != 0; }
