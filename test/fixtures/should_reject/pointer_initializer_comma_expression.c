// comma式は整数定数式ではなく、値が非zeroならpointerへ暗黙変換できない。
int main(void) { int *pointer = (0, 1); return pointer != 0; }
