// 局所 VLA `struct P arr[n]` のタグ情報 carry。
// register_vla_lvar_and_append_alloc の呼び出し元 (decl.c) が
// psx_decl_set_var_tag を呼んでおらず、ローカル VLA `struct P arr[n]` で
// `arr[i].m` の `.` が「左辺は構造体/共用体でない」E3005 で弾かれていた。
#include <assert.h>

struct P { int a; double b; };
union U { int n; struct P p; };

int main(void) {
    int n = 3;

    /* (a) struct VLA: 直接メンバ access */
    {
        struct P arr[n];
        for (int i = 0; i < n; i++) {
            arr[i].a = i + 1;
            arr[i].b = (i + 1) * 0.5;
        }
        int s = 0;
        double sf = 0;
        for (int i = 0; i < n; i++) {
            s += arr[i].a;
            sf += arr[i].b;
        }
        assert(s == 6);
        assert(sf > 2.999 && sf < 3.001);
    }

    /* (b) struct VLA: アドレス取得 + ポインタ経由 */
    {
        struct P arr[n];
        arr[0].a = 7;
        struct P *p = &arr[0];
        assert(p->a == 7);
    }

    /* (c) union VLA */
    {
        union U arr[n];
        arr[0].n = 100;
        arr[1].p.a = 200;
        assert(arr[0].n == 100);
        assert(arr[1].p.a == 200);
    }

    /* (d) mixed const/VLA dim (続き31 の経路) + struct */
    {
        struct P arr[2][n];
        arr[0][0].a = 10;
        arr[1][n-1].a = 20;
        assert(arr[0][0].a == 10);
        assert(arr[1][n-1].a == 20);
    }

    return 0;
}
