// ファイルスコープで `T *p = (T[]){...}` (ポインタ変数を配列複合リテラルで初期化) が SIGBUS。
// apply_toplevel_object_initializer の strip heuristic が `(T){...}` を無条件で剥がして
// `T *p = {...}` (= 複数値で初期化) に変換していたため、先頭要素値がポインタスロットに
// 書き込まれて実行時 SIGBUS。
//
// 修正: 集約 (配列 / struct 値 / union 値) のときだけ strip し、ポインタ・スカラ var では
// 式経路 (ps_expr_assign) で compound literal を hidden gvar に materialize させる。
// ただしポインタ var + 単一文字列 `char *p = (char[N]){"str"}` 形は等価なので例外的に strip
// を許す (peek で TK_STRING + 単一要素を判定)。
#include <assert.h>

/* 数値配列 -> int* */
int *ptr_i = (int []){10, 20, 30};
/* 別サイズ配列 -> char* */
unsigned char *ptr_u = (unsigned char []){1, 2, 3, 4};
/* 単一文字列 -> char* (strip 許可ケース) */
char *ptr_s = (char[6]){"hi"};
/* sized array */
int *ptr_sized = (int [3]){100, 200, 300};
/* pointer-element array compound literal */
int target_value = 11;
int **ptr_ptrs = (int *[]){&target_value, &target_value};
int **ptr_ptrs_sized = (int *[2]){&target_value, &target_value};
typedef int *IntPtr;
IntPtr *typedef_ptrs = (IntPtr[]){&target_value, &target_value};
struct Node {
    int value;
};
typedef struct Node *NodePtr;
struct Node node_a = {13};
struct Node node_b = {17};
struct Node **node_ptrs = (struct Node *[]){&node_a, &node_b};
NodePtr *typedef_node_ptrs = (NodePtr[]){&node_a, &node_b};

int grid_a[2][3] = {{1, 2, 3}, {4, 5, 6}};
int grid_b[2][3] = {{7, 8, 9}, {10, 11, 12}};
int (*(*grid_ptrs)[2])[3] = &(int (*[2])[3]){grid_a, grid_b};
typedef int (*RowPtr)[3];
RowPtr *typedef_grid_ptrs = (RowPtr[]){grid_a, grid_b};

struct RowHolder {
    RowPtr *rows;
};

struct SingleRowHolder {
    RowPtr row;
};

struct RowArrayHolder {
    RowPtr rows[2];
};

struct AnonRowHolder {
    struct {
        RowPtr rows[2];
    };
};

struct RowHolder global_row_holder = {(RowPtr[]){grid_a, grid_b}};
struct SingleRowHolder global_single_row_holder = {grid_b};
struct RowArrayHolder global_row_array_holder = {{grid_a, grid_b}};
struct AnonRowHolder global_anon_row_holder = {{{grid_a, grid_b}}};
struct AnonRowHolder global_anon_row_holder_designated = {{{[1] = grid_b, [0] = grid_a}}};

static int use_flat_param(int (*rows[2])[3]) {
    return rows[0][0][2] + rows[1][1][1];
}

static int use_nested_param(int (*(*rows)[2])[3]) {
    return (*rows)[0][1][2] + (*rows)[1][0][0];
}

static int local_pointer_element_compound_literal(void) {
    int x = 41;
    int y = 1;
    int **ptrs = (int *[]){&x, &y};
    IntPtr *typedef_ptrs = (IntPtr[]){&x, &y};
    struct Node a = {5};
    struct Node b = {7};
    struct Node **nodes = (struct Node *[]){&a, &b};
    NodePtr *typedef_nodes = (NodePtr[]){&a, &b};
    int direct = ((struct Node *[]){&a, &b})[1]->value;
    return **ptrs + *typedef_ptrs[1] + nodes[0]->value + typedef_nodes[1]->value + direct;
}

static int local_pointer_to_array_element_compound_literal(void) {
    int x[2][3] = {{13, 14, 15}, {16, 17, 18}};
    int y[2][3] = {{19, 20, 21}, {22, 23, 24}};
    int (*(*ptrs)[2])[3] = &(int (*[2])[3]){x, y};
    RowPtr *typedef_ptrs = (RowPtr[]){x, y};
    struct RowHolder holder = {(RowPtr[]){x, y}};
    struct SingleRowHolder single = {y};
    struct RowArrayHolder array_holder = {{x, y}};
    struct AnonRowHolder anon_holder = {{{x, y}}};
    int direct = ((int (*[2])[3]){x, y})[1][1][2];
    return (*ptrs)[0][1][2] + (*ptrs)[1][0][1] +
           typedef_ptrs[0][0][2] + typedef_ptrs[1][1][0] +
           holder.rows[0][1][1] + holder.rows[1][0][2] +
           single.row[1][2] + array_holder.rows[0][1][2] +
           array_holder.rows[1][0][1] + anon_holder.rows[0][0][1] +
           anon_holder.rows[1][1][2] + direct;
}

int main(void) {
    assert(ptr_i[0] == 10 && ptr_i[1] == 20 && ptr_i[2] == 30);
    assert(ptr_u[0] == 1 && ptr_u[3] == 4);
    assert(ptr_s[0] == 'h' && ptr_s[1] == 'i' && ptr_s[2] == 0);
    assert(ptr_sized[0] == 100 && ptr_sized[2] == 300);
    assert(**ptr_ptrs == 11 && *ptr_ptrs[1] == 11);
    assert(**ptr_ptrs_sized == 11 && *ptr_ptrs_sized[1] == 11);
    assert(**typedef_ptrs == 11 && *typedef_ptrs[1] == 11);
    assert(node_ptrs[0]->value == 13 && node_ptrs[1]->value == 17);
    assert(typedef_node_ptrs[0]->value == 13 && typedef_node_ptrs[1]->value == 17);
    assert(local_pointer_element_compound_literal() == 61);
    assert((*grid_ptrs)[0][0][1] == 2 && (*grid_ptrs)[1][1][2] == 12);
    assert(typedef_grid_ptrs[0][1][0] == 4 && typedef_grid_ptrs[1][0][2] == 9);
    assert(global_row_holder.rows[0][1][0] == 4 && global_row_holder.rows[1][0][2] == 9);
    assert(global_single_row_holder.row[0][1] == 8 && global_single_row_holder.row[1][2] == 12);
    assert(global_row_array_holder.rows[0][1][2] == 6 && global_row_array_holder.rows[1][0][1] == 8);
    assert(global_anon_row_holder.rows[0][1][2] == 6 && global_anon_row_holder.rows[1][1][1] == 11);
    assert(global_anon_row_holder_designated.rows[0][0][2] == 3 &&
           global_anon_row_holder_designated.rows[1][1][0] == 10);
    assert(use_flat_param((int (*[2])[3]){grid_a, grid_b}) == 14);
    assert(use_nested_param(&(int (*[2])[3]){grid_a, grid_b}) == 13);
    assert(local_pointer_to_array_element_compound_literal() == 237);
    return 0;
}
