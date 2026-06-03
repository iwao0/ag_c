// 型キャストのチェーン
// 期待: exit=42
int main(void) {
    return (int)(char)(short)(long)42;
}
