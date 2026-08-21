// 非zeroの整数条件式はnull pointer constantではなく、pointerへ暗黙変換できない。
int main(void) { int *pointer = 1 ? 1 : 2; return pointer != 0; }
