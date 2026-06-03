// ネストした switch
// 外側 case 1 内側 case 2 → return 22
// 期待: exit=22
int main(void) {
    int o = 1;
    switch (o) {
        case 1: {
            int in = 2;
            switch (in) {
                case 1: return 11;
                case 2: return 22;
                default: return 33;
            }
        }
        case 2: return 44;
        default: return 55;
    }
}
