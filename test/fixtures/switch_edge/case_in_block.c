// ブロック内に case ラベルがあっても到達できる (extension)
// 期待: exit=20
int main(void) {
    switch (2) {
        case 1: { case 2: return 20; }
    }
    return 0;
}
