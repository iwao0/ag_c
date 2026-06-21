# HANDOFF — ag_c バグ修正セッション

最終更新: 2026-06-21

## このセッションの目的
clang との差分テスト（同一 C ソースを ag_c と clang でコンパイルして exit code を
比較）で ag_c の miscompile / コンパイルエラーを炙り出し、修正して回帰テストを追加する。

## 2026-06-21 セッションの修正（bug_coverage.md の ⬜ 未着手をすべて消化）
現在 `make test` = **984 E2E + unit/parser/preprocess/fuzz/IR すべて green**、回帰 fixture は
`test/fixtures/probes_found_bugs/` 登録済み。`docs/differential_testing/bug_coverage.md` を
索引として更新済み。テーマ別:

- **ファイルスコープ static の内部リンケージ**（72b23c2）。`static` 変数/関数/関数内 static を
  `.global` で出し、無初期化 static を `.comm`(外部 common) にしていたため別 TU の同名 static と
  duplicate symbol 衝突/共有していた (C11 6.2.2p3 違反)。`is_static` を global_var_t / ir_func_t /
  node_func_t に追加し parser から伝播、codegen で `.global` 抑制 + 無初期化を `.zerofill __bss`。
  fixture: static_internal_linkage。**複数 TU の差分テストで発見**。
- **クロス TU (複数ファイル) E2E ケース種別を追加**（39e4f37）。`test_e2e.c` の `link2_cases[]` で
  2 ファイルを同じ接頭辞で namespace して category binary に一緒にリンクし、別 TU の同名シンボル
  衝突を検査できる。fixture: static_internal_linkage_xtu_{main,other}。
- **printf 浮動小数書式は green**（ab4355b、docs のみ）。`%f/%g/%e/%E`・精度/幅/フラグ・偶数丸め・
  Inf/NaN・float→double 可変長昇格・snprintf を stdout 比較。書式は libc 任せで ag_c は引数を
  正しく渡しており全一致。
- **`_Generic` の複雑な派生型照合**（5632b86 局所変数 / 1cbf0cb cast / cc1e788 グローバル変数）。
  関数ポインタの引数リスト (`int(*)(int)` vs `int(*)(int,int)`) や深いネスト型
  (`int(*(*)(void))[3]`) を generic_type_t の構造的フィールドでは区別できなかった。型を
  **正規化トークン文字列** (`psx_serialize_decl_type_tokens`) にして strcmp 照合する。control が
  局所変数のときは宣言時に名前で記録 (decl.c の副テーブル)、グローバルは永続表、cast は cast 経路
  で type_sig を採用。fixture: generic_complex_derived_type{,_global}。
- **ストリーミング前方先読み境界バグ**（1d584e8、既存バグ）。`_Generic` の型照合はカーソルを
  進めず `t->next` で型全体を先読みするが、ストリーミング生成器の materialize 窓を越えると
  `t->next==NULL` を踏み有効な型を誤却下 (E2006 ': 必要')。深い先読みの直前に
  `tk_ensure_lookahead()`（プリプロセッサが登録するフック経由で `pps_refill`、parser↔preprocess は
  疎結合維持）で窓を満たす。fixture: generic_streaming_lookahead。詳細は bug_coverage.md。

## 2026-06-18 セッションの修正（11 件、7e39081 まで）
現在 `make test` = **914 E2E + unit/parser/preprocess/fuzz/IR すべて green**、各修正は
ASAN クリーン、`test/fixtures/probes_found_bugs/` に回帰 fixture 登録済み。
- 符号付き short/char キャストの符号拡張（インライン使用で sign-extend、f9f153f）。
  fixture: cast_short_char_sign_extend。
- 2D 配列の行のポインタ算術 `*(m[i]+k)` / `*(p+k)+j`（node_is_ptr_for_arith で行を
  ポインタ判定、build_node_deref で 8B 以下の行も崩壊、1fd9eb1）。array_row_decay_pointer_arith。
- 3D 以上の中間行のポインタ算術 `*(*(t[i]+j)+k)`（多段ストライド伝播、47975d4）。
  array_row_decay_3d_pointer_arith。**変則 `*(t+1)[0]` 形は未対応（既存）**。
- 関数ポインタ変数の float/double 戻り値（callee の pointee_fp_kind を funcall へ、0b980b0）。
  funcptr_fp_return。**配列要素/struct メンバ/global の funcptr 戻り、int→fp 引数昇格は未対応**。
- static struct/union 局所の永続化（global lowering + stmt.c の static フラグ復元、8167e8e）。
  static_local_struct_persist。**インライン定義の匿名タグは未対応（codegen でタグ消失）**。
- i32 比較を 32bit 幅（w レジスタ）で行う + sub-int 戻り値の切り詰め（42b9d54）。
  `int f(int x){return x-1;} f(0)!=-1` 等の負値インライン比較を是正。戻り型 unsigned 追跡
  （ret_is_unsigned）と funcall への伝播も追加。int_cmp_width_and_subint_return。
- unsigned char/short 戻り値のゼロ拡張（前項の回帰修正、1b5e1df）。
- 匿名 struct/union メンバ昇格時の fp_kind/is_bool/is_unsigned/outer_stride 伝播（f4bf0bd）。
  `struct { union { int n; float f; }; }` の `s.f` 整数化を是正。anon_member_fp_unsigned_promote。
- グローバルポインタ配列 `&data[n]` / `data+n` 初期化（resolve_global_addr_init + codegen で
  `_sym+off`、138cd70）。global_ptr_array_addr_init。
- グローバル集約の `.member[idx]` / `.member.sub` designator チェーン（1e843b4）。
  global_designator_member_index。
- ローカル designator の struct leaf を `{...}` で brace 初期化（7e39081）。
  local_designator_aggregate_leaf。**union leaf は未対応（union 配列要素 brace init が別途未対応）**。

手法・再現手順は冒頭の「作業のやり方」を見ること。下記「既知の未対応」も更新済み。

## 作業のやり方（再現手順）
- 差分テストヘルパ:
  - `/tmp/agc_check.sh <file.c>` — CWD 非依存。`include/` を使う `#include` は解決しない。
  - `/tmp/agc_check_root.sh <file.c>` — **リポジトリルートから** ag_c を実行。`include/stdarg.h`
    等の同梱ヘッダ (`#include`) を解決できる。stdarg 等を使うプローブはこちら。
  - どちらも `ag_c → .s → clang でアセンブル → 実行` と `clang -w 直接` の exit code を比較し
    `OK`/`MISMATCH`/`AGC_COMPILE_FAIL` を表示する。
  - **クロス TU (複数ファイル)**: 各 .c を ag_c で .s 化 → clang で個別アセンブル → 一緒にリンク
    し、`clang -w -I include` の直接ビルドと exit code を比較する使い捨てスクリプトを `/tmp` に
    作って使った（`agc_check_root.sh` が `cd $ROOT` するので **絶対パス** で渡すこと）。恒久回帰は
    `test_e2e.c` の `link2_cases[]`（2 ファイルを同接頭辞で namespace して category binary に
    リンク）に追加する。
  - **stdout 比較 (printf 等)**: exit code でなく標準出力を比較したいときは、ag_c 版と
    `clang -w -I include` 版を実行して stdout を diff する小スクリプトを `/tmp` に作る。
  - これら `/tmp/*.sh` は ephemeral。`Write` ツールで作る（`echo >` は使わない方針）。
- ag_c はアセンブリを **stdout** に出力する (`./build/ag_c foo.c > foo.s`)。`-o` や `-I` の
  コマンドラインフラグは無い。include 検索は CWD 相対の `include/`（preprocess.c の
  `k_include_search_roots`）。
- ビルド: `make`（日本語診断 `-DDIAG_LANG_JA`）。Edit 直後に mtime 粒度で再ビルドされない
  ことがあるので `touch <file>` してから `make`。
- 全テスト: `make test`（E2E + parser/preprocess 単体 + fuzz + IR）。E2E は
  `test/test_e2e.c` に登録、fixture は `test/fixtures/probes_found_bugs/`。
- **メモリ系バグは ASAN で特定**するのが有効だった（間欠 SIGSEGV を 1 回で再現）。
  zsh は単語分割しないので `${=SRCS}` を使う:
  ```
  clang -std=c11 -g -O0 -fsanitize=address -DDIAG_LANG_JA -Isrc \
    src/*.c src/config/*.c src/arch/*.c src/tokenizer/*.c src/parser/*.c \
    src/preprocess/*.c src/ir/*.c src/diag/diag.c src/diag/error_catalog.c \
    src/diag/ui_texts.c src/diag/warning_catalog.c src/diag/messages_ja.c -o /tmp/ag_c_asan
  ```
  注意: グロブ (`src/*.c`) は **コマンドラインに直書き** で展開する。`SRCS="src/*.c ..."` を
  `${=SRCS}` で渡すとグロブ展開されず `no such file` になる（単語分割しかしないため）。
  messages_en.c / messages_all.c は messages_ja.c と重複定義になるので入れない。
- 新規 fixture の登録は `test/test_e2e.c` に 2 系統:
  - `CASE_INT_FILE`（exit code で判定、慣習で 42）:
    `{"probes", "<name>", CASE_INT_FILE, "...<name>.c", 42, 0}`。
  - `CASE_ASSERT_FILE`（fixture 内の `assert()` が期待を自己記述、成功で main が 0 を返す。今回の
    `_Generic`/linkage fixture はこちら）: `{"probes", "<name>", CASE_ASSERT_FILE, "...<name>.c", 0, 0}`。
    `#include <assert.h>` と `int main(void){ assert(...); return 0; }` 形式。
  - 回帰検証は「修正を一時的に外して fixture が落ちる」ことまで確認する（バグ注入 → make / 該当
    fixture 実行で失敗を確認 → 復元）。`touch` してから `make`（mtime 粒度の取りこぼし回避）。

## アーキテクチャ早見
tokenizer → preprocess → parser（`src/parser/`）→ IR builder（`src/ir/ir_builder.c`）→
ARM64 codegen（`src/arch/arm64_apple*.c`）。ターゲットは Apple Silicon ARM64。

- ノード型情報のアクセサ: `src/parser/node_utils.c`（`psx_node_type_size` /
  `psx_node_is_pointer` / `psx_node_is_unsigned` / `psx_node_pointer_qual_levels` /
  `psx_node_base_deref_size` / `psx_node_set_unsigned` など）。`node->kind` で分岐し、
  ND_LVAR は `mem`、ND_GVAR/DEREF/ASSIGN は `node_mem_t`、その他は base を読む（フィールド
  が分散しているので **読み書きでフィールドを一致させること**）。
- struct メンバ情報は `tag_member_t`（semantic_ctx.c）/ `tag_member_info_t`（header）。
  メンバ属性（fp_kind / is_bool / is_unsigned / outer_stride）は setter/getter を介して
  hash table に保存し、`psx_ctx_get/find_tag_member_info` で取得。新属性を足すときは
  ① 両構造体にフィールド ② setter/getter ③ struct_layout.c で設定 ④ build_member_deref_node
  で deref ノードへ伝播、の 4 箇所を揃える（`is_bool` を雛形にできる）。
- struct 値の ABI（簡略版）: 引数は**ポインタ渡し**（callee が memcpy）、>16B 戻り値は
  x8 経由の ret_area。`build_node_funcall` は値文脈で >8B struct 戻り値の ret_area を確保
  しアドレスを返す。
- グローバル初期化は `psx_parse_global_brace_init_flat`（parser.c）が **フラット index** に
  値を書き、`emit_global_struct_init`（arm64_apple.c）が struct レイアウトに沿って出力する
  （入れ子 struct は再帰展開）。

## 直近セッションの要約（db98d34〜e0b5190、22 件）
現在 `make test` = **904 E2E + unit/parser/preprocess/fuzz/IR すべて green**、各修正は ASAN
クリーン、`test/fixtures/probes_found_bugs/` に回帰 fixture 登録済み。テーマ別の内訳:
- 型幅/キャスト: sub-int の (int)/(unsigned) 符号（1317698）、(int)/(long) の 32/64bit
  切り詰め・拡張（1c8e358, 6070c7e, 8ddafa6）、long リテラルの i64 型付け（365d8c0）、
  インライン `*(int*)ptr` の pointee サイズ（d44947f）
- ポインタ/配列: struct ポインタ配列メンバ（db98d34）、多次元配列の行 decay（a2a8328）、
  struct/union ポインタ算術スケール（b6a42ec）、配列へのポインタ（>4B struct, 局所2D）
  （10072cc, 5a47279）
- 集約: ネスト designator の `[idx]`（aadf3b7）、struct 配列メンバ brace init（1684a8c）、
  _Bool 初期化子正規化（5b3d592）
- struct ABI: 3/5/6/7B struct の値渡し/返し（050e1bf）
- **float/double**（codegen の穴を集中的に）: 配列メンバ access/init（10b9748）、
  多次元 subscript の fp load（10d291d）、ブール条件分岐（ec96e30）、`&&`/`||`（87a24a1）、
  static ローカル float init（f873777）
- 型機能: `_Alignof` の集約アラインメント（3e8a4d1）、`_Generic` の文字列/long リテラル（e0b5190）

詳細は下記テーマ別セクション参照。手法・再現手順は冒頭の「作業のやり方」を見ること。

## このセッションで修正した内容（コミット c1a8f83 以降）
テーマごと。すべて `make test` green、該当パスは ASAN クリーン、fixture 登録済み。

### 64bit 幅 / long
- ポインタ三項の null 定数分岐が 4 バイトしか書かず garbage（d150e3b）
- ternary / binop / リテラル / 戻り値 / 引数の 64bit 幅取りこぼし（4329e49, 56e96fe,
  a9a965c, fcbd1e4）— long の値が下位 32bit に化ける一連
- 入れ子三項の long 分岐（fcbd1e4 に含む）

### ポインタ / subscript（`build_subscript_deref` 周辺の「配列 vs 多段ポインタ」取り違え）
- struct ポインタ局所変数の type_size が 8 でなく pointee サイズで、`p=p->next` が 16B
  struct コピー扱いになり隣接スタック破壊（f0b096c）
- 単段ポインタ subscript 結果がスカラなのに pointer 扱い（6fe7dce）
- 多段ポインタ subscript の pql 減算（d0ff96c）/ base_deref_size 伝播（8e937e0）
- `long *a` 仮引数 subscript / 呼び出し側 long 戻り値幅（a9a965c）

### 集約初期化（C11 6.7.9）— ローカル & グローバル
- 多次元配列メンバ `int a[2][2]`（4b92768）
- 重複 designator 後勝ち（4a5942d、**ユーザー確認のうえ C11 準拠を選択**）
- designator 後の位置指定継続（df23e17）
- ネスト struct メンバ brace 省略 `{1,2,3}`（17edc06）
- struct 配列フラット展開 / 配列メンバ部分充填→designator（b225c4c）
- グローバル入れ子 struct 初期化の data 出力（5be6940）
- グローバル designator の slot 計算（入れ子 struct / struct 配列 `[N]=`）（1b79768）
- 関数ポインタ配列 struct メンバの brace 初期化（7e6912c）
- **struct 配列メンバ** `struct P pts[N]` の要素 brace 初期化（1684a8c）。ローカルは
  parse_member_initializer が要素を parse_struct_initializer へ委譲、グローバルは
  emit_global_struct_members_rec が各要素 struct を再帰展開。positional/designated/
  部分ゼロfill 対応。差分テストで発見。**未対応**: グローバルでネストメンバ内の配列添字
  designator `{.items={[2]={.a=7}}}` はグローバル flat パーサの別制約（h83k）。
- メンバパス途中に配列添字を含む designator `.m.x[1].b` / `.arr[i].f`（aadf3b7）。
  `consume_nested_designator_and_build_assign` を `.member` と `[idx]` の任意連鎖を
  辿るよう一般化し、indexed 専用ヘルパを統合（scalar/union/配列要素・境界チェック維持）。

### struct ポインタ配列メンバ
- `struct N *arr[N]` メンバの subscript 結果が struct ポインタと認識されず
  `h.arr[i]->v` が E3005（db98d34）。`build_member_deref_node` がポインタ配列メンバに
  pql=1 / base_deref_size=要素 pointee サイズ を立て、`build_subscript_deref` の
  「要素がポインタ」分岐に乗せて is_tag_pointer を伝播（ローカル `T *arr[N]` と同じ表現）。

### unsigned 符号拡張（load / 比較の signedness）— **全経路を網羅**
- `(int)`/`(unsigned)` キャストが operand の符号を更新せず比較が誤符号（5b474f7）。
  ※ binop ノード（シフト）の is_unsigned は LSR/ASR を兼ねるので終端値ノードのみ更新。
- unsigned グローバル / struct・union メンバ（70d91ab）、long/char/short 幅（3a61e09）、
  配列要素 / ポインタ deref（14ca899）、typedef したグローバル（8bd7ec8）
- **char/short を (int)/(signed)/(unsigned) へ明示キャスト**すると operand の
  is_unsigned 上書きが load 拡張 (ldrsh/ldrh) を変え値が化けていた（1317698）。
  `(unsigned)(short)-1`→65535、`(int)(unsigned short)0xFFFF`→-1。sub-int は load
  符号性を保ち、(unsigned) は & 0xffffffff で 32bit unsigned へ折り返す（符号混在の
  インライン比較/除算も是正）。差分テストで発見。

### 多次元配列の decay（行→ポインタ）
- 多次元配列の途中次元（行）を**値**として使う（`int *q=m[0]`、`*(*(m+1)+2)`、`**m`）と
  int* へ崩壊せず値ロードして garbage（a2a8328）。① build_node_deref の崩壊判定が
  is_pointer=1 を要求していたのを `deref_size>0 && type_size>8` に緩和（struct 値は
  deref_size=0 で除外）。② 通常多次元配列は ND_ADDR(deref_size=行, inner_deref_size=要素)
  表現で、build_unary_deref_node が `*m`/`*(m+k)` の内側ストライドを引き継いでいなかった。
  ND_ADD を辿り 1 段シフト。3D・関数引数 `int(*)[N]`・グローバル 2D も対応。差分テストで発見。

### _Bool 初期化子の正規化
- `_Bool` は 0/1 を保持すべき（C11 6.3.1.2、非ゼロ→1）だが、スカラ変数 init のみ正規化され
  配列・struct メンバ・グローバルの各 init 経路は生値を格納（`_Bool f[]={5}` が 5）（5b3d592）。
  全 init サイトで (value!=0) を適用: ローカル配列 / ローカル struct スカラ・配列メンバ /
  グローバル配列（global_var_t に elem_is_bool 追加）/ グローバル struct メンバ（emit で
  tag member is_bool 参照）。`bool_normalize_if` ヘルパ。差分テストで発見。

### (int)/(signed)/(unsigned) による long の 32bit 切り詰め
- `(int)long` が 32bit に切り詰められず、代入/戻り値では store 幅で偶然合うがインライン比較/
  演算で 64bit のまま使われ `(int)0x100000000L == 0` や `(int)long_var == 0` が偽（1c8e358）。
  定数は値を切り詰め、long 値は `(x<<32)>>32`（signed=算術 / unsigned=論理シフト）で低32bitを
  64bitへ拡張。sub-int・ポインタ→int は対象外。差分テストで発見。

### _Generic の制御式型推定（文字列・long リテラル）
- `_Generic("str", char*:...)` が文字列を char* に decay 認識せず（ptr_deref_size 未設定）default に、
  `_Generic(42L, long:...)` が long リテラルを int 扱い（scalar_size=4）で long にマッチせず（e0b5190）。
  文字列は ptr_deref_size=文字幅、NUM は int_is_long で scalar_size=8 に。差分テストで発見。

### _Alignof が集約型でサイズを返す
- `_Alignof` が sizeof 実装で、基本型は size==align で偶然合うが struct/union/配列で誤り
  （`_Alignof(struct{int,int})`=8（正4）、`_Alignof(int[10])`=40（正4）、`_Alignas(16)` 含む
  struct=32（正16））（3e8a4d1）。tag テーブルに agg_align を保存（layout が pending 経由で
  define に渡す）。_Alignof モードでは struct の align を返し、配列は要素数を掛けない。
  sizeof は不変。旧挙動を符号化していた E2E fixture 2 件・parser 単体 2 件も修正。差分テストで発見。

### static ローカル float/double の初期化
- `static float t = 0.5f;` が `.long 0` で出力され値が 0 になっていた（f873777）。
  try_lower_static_local_scalar が NUM の整数 `->val` のみ読み、float リテラル値 `->fval` を
  無視していた。fp なら fval/fp_kind を gv に伝播。差分テストで発見。

### float をブール条件/論理演算で使う（codegen）
- `f ? a : b` / `if(f)` / `while(f)` / `f && g` / `f || g` で float/double を真偽値として使うと、
  レジスタ圧で fp が spill されたとき codegen が 4B float を 8B 整数 load して上位 garbage を拾い、
  0.0 が真と誤判定（ec96e30 で条件分岐、87a24a1 で `&&`/`||`）。`emit_truthiness` ヘルパ（fp は
  `!= 0.0` の IR_FNE、整数は IR_NE）に統一し、emit_br_cond と build_node_logand_or 右辺で使用。
  差分テストで発見。

### 多次元 float/double 配列の subscript
- `float m[2][3]` の `m[i][j]` が整数 load で読まれていた（10d291d）。build_subscript_deref が
  float 配列 subscript 結果の fp_kind を常に base.fp_kind に載せていたため、多次元 1 段目（まだ行）
  の結果が float 値扱いになり、次段 subscript が pointee の fp 種別を失い整数 load。中間（行）は
  pointee_fp_kind、最終要素のみ base.fp_kind に（is_bool と同じ分岐）。`es > inner_ds` で多次元中間
  かを判定（`float *a` 仮引数は inner_ds=elem が立つので es 比較で区別）。差分テストで発見。

### struct の float/double 配列メンバ
- `struct S{ float v[N]; }` の `s.v[i]` アクセスと brace 初期化が整数扱いで値が化けた（10b9748）。
  アクセス: build_member_deref_node が配列メンバの fp_kind を base.fp_kind に入れていたのを
  pointee_fp_kind に（is_bool と同じ分岐、subscript 結果を fp load）。初期化: parse_member_initializer
  に member_fp_kind を通し要素 store を fp store に（build_member_array_elem_assign_node）。差分テストで発見。

### 局所「2次元配列へのポインタ」の mid_stride
- 局所 `T (*p)[N][M]` が mid_stride 未設定で `p[i][j]` が誤スケール（5a47279）。局所宣言経路は
  paren の `[N][M]` を積(outer_stride)としてのみ扱い第2 subscript 用 stride を立てていなかった。
  paren-array の先頭次元・次元数を捕捉し mid_stride=(積/先頭次元)*elem を設定。int/struct 共通。
  引数版は元から動作。差分テストで発見。

### 配列へのポインタ（要素が >4B struct）
- `struct T (*p)[N]` の subscript が要素 struct >4B のとき壊れた（10072cc）。局所は
  lvar_is_pointer が `size>elem_size`（8>8 が偽）でポインタ非認識（4B struct は 8>4 で動作）、
  引数は struct ポインタ仮引数ブランチが `[N]` を無視し `struct T *` 扱い（行を跨げない）。
  局所は `outer_stride>0 && size==8` でポインタ認識、引数は is_tag_pointer クリア＋outer_stride
  設定。`long (*g)[N]` 引数の pql=1 化も抑止。差分テストで発見。

### long/long long リテラルの 64bit 型付け
- 値が 32bit に収まる long サフィックス付きリテラル（`2L` 等）が i32 扱いになり、`u * 2L`
  （u は unsigned >2^31）が 32bit 演算で wrap（365d8c0）。tokenizer は int_size に L を記録
  していたが parser が node に伝えていなかった。node_num_t に `int_is_long` を追加し
  parse_num_literal で伝播、build_node_num が long リテラルを i64 生成。is_unsigned サフィックスも
  node へ伝播。通常 int リテラルは値ベース型付けのまま（`w * 2u` の wrap は不変）。差分テストで発見。

### (long)unsigned_int の zero-extend（arithmetic）
- `(long)u + (long)u`（u は unsigned int で値 >2^31、合計が 2^32 超）が 32bit でラップ（8ddafa6）。
  `(long)` が type_size を 8 に広げない no-op のため二項演算 result_ty が I32 になり、符号なし
  2^32 ラップマスク（[ir_builder.c]~1230）で切り詰められていた。`coerce_to_type` は常に SEXT で
  unsigned widen に乗れないので、node_mem_t に `widen_zext_i64` フラグを追加し、`(long)unsigned`
  (8B未満) を ND_PTR_CAST(widen_zext_i64) でラップ → `build_node_ptr_cast` が IR_ZEXT を明示挿入。
  signed の (long) は SEXT で従来通り。差分テストで発見。

### インラインのポインタキャスト deref
- `*(int*)(cp+4)`（char* を別要素型へキャストして即 deref/添字）が、キャストで pointee
  サイズを更新せず元の char サイズ(1)でロードしていた（d44947f）。一旦変数 `int *p=...` に
  入れると正しく動いた。スカラ整数型への単段ポインタキャストを ND_PTR_CAST で deref_size
  更新。operand がポインタのときのみ（`(int*)0` 等の null 定数は ND_NUM のまま）、多段は除外。
  差分テストで発見。

### 小さい struct の値渡し/値返し ABI（サイズ 3/5/6/7）
- サイズが 1/2/4/8 でない struct/union（3/5/6/7B）を値渡し/値返しすると、先頭メンバ幅の
  スカラとして 1 レジスタで運ばれ先頭メンバしか残らなかった（`{char;short;uchar}` 6B が
  1B 扱い）（050e1bf）。簡略 ABI が至る所で `>8` を間接（memcpy/x8 ret_area）境界にしていた
  のが原因。`cg_size_needs_indirect_struct`（1/2/4/8 以外を間接）を追加し、引数渡し/受け取り・
  struct 代入・戻り値設定/受け取り・struct 戻り値呼出の内側 arg ループ・複合リテラル引数の
  全経路に適用。1/2/4/8B は従来通りレジスタ値渡し。差分テストで発見。

### struct/union ポインタ算術
- `&s[i]-&s[j]` / `sp+n` / `n+sp` / `sp-n` が要素サイズで割られず/掛けられず byte 単位
  だった（b6a42ec）。struct タグポインタは is_pointer でなく is_tag_pointer で表現され、
  `add()` の `psx_node_is_pointer` 判定が偽を返していた。`node_is_ptr_for_arith`
  （タグポインタも認識）を追加。複合 `+=`/`-=` は別経路で元から正常。差分テストで発見。

### long bitfield
- `unsigned long a:40` 等 >32bit / 32bit ユニットを跨ぐ bitfield（040da11）。storage を
  8 バイト許可 + 64bit load/store + マスク定数をレジスタ展開。

## 既知の未対応 / 次の着手候補
（差分テストで新しい miscompile / コンパイルエラーを炙り出すところから再開する。
2026-06-18 セッションで上記 11 件、2026-06-21 セッションでさらに数件を修正済み。下記は現時点で
残る既知の未対応。直近のプローブ（型×文脈の総当たり寄り）はかなり green で、発見コストが
上がっている。）

- **現状の索引は `docs/differential_testing/bug_coverage.md`**。チェック済み領域・⚠️/🔧 状態・
  既存バグ・「miscompile でなかった」領域をテーマ別に集約している。2026-06-21 時点で同ファイルの
  ⬜ 未着手候補（複数 TU/extern、printf 書式、`_Generic` 複雑派生型）は全消化。再開時はまず
  これを見て未探索の「型 × 宣言経路 × 使用文脈」の組合せを選ぶ。下記は古い既知の未対応の抜粋。

- **関数ポインタ FP 戻り値の残り**（funcptr 変数は 0b980b0 で対応済み）。callee が配列要素
  `ops[i]` / struct メンバ `s.f` / グローバル関数ポインタの場合は戻り型 fp_kind が funcall へ
  伝播せず戻り値を x0 で読む。subscript/メンバ/global_var_t への配線が要る。また間接呼び出しの
  **int→fp 引数昇格**（`double(*p)(double); p(4)` の整数リテラル）は仮引数型保存が必要で未対応。

- **codegen の符号性/幅の深い穴**。`unsigned long` / `unsigned char` 戻りの符号性は plain
  `unsigned` のみ追跡（ret_token_kind が TK_LONG/TK_CHAR に潰れる）。混在幅の比較で片側が
  i32・片側 i64 のケースは 64bit 比較になる（gen_inst_int_cmp は両 i32 のときだけ 32bit）。

- **多次元・ポインタの変則形**。`*(t+1)[0]`（pointer-to-2D-array に subscript+deref 混在）は
  コミット前から SIGSEGV（47975d4 の標準 3D 行算術とは別経路）。

- **union 集約初期化の穴**。`union U arr[2]={[1]={.n=5}}` / `.u[1]={...}`（union 配列要素の
  brace init）が誤値。local designator の union leaf は 7e39081 で意図的に E3064 のまま除外。

- グローバルのネスト brace 配列添字 `struct O o={.items={[2]={.a=7}}}`（`{[2]=...}` 形）は
  グローバル flat パーサの制約で E3064。`.items[2].a=` 形（designator チェーン）は 1e843b4 で対応済み。

- 同梱ヘッダに `complex.h` が無く `#include <complex.h>` が E1034。`_Complex` の言語機能自体は
  動く（creal/cimag 等のライブラリ関数を使わなければ可）。

## バグではない（仕様 / 既知の差異、追わない）
- statement expression `({...})`（GNU 拡張、非標準。プロジェクトは C11 準拠）。
- 過剰初期化子 `struct S s={{1,2},{3,4}}`（メンバ1個に2グループ）等は clang が警告して
  無視するが ag_c は E3064。意図的に厳格（miscompile ではない）。
- `s07.c`（深さ 10 万の再帰）が SIGSEGV。各フレームのスタック使用量が大きいことによる
  スタックオーバーフローで、誤コンパイルではない（深さ 100 では正しく動く）。
- 評価順序が未規定/UB のもの（`a[i++]=i` 等）。

## メモ
- 「型バリエーション × 宣言経路 × 使用文脈」の組み合わせで取りこぼしが多発する傾向。
  例: unsigned を直しても int だけ／local だけ／scalar だけ、になりがち。1 つ直したら
  long/char/short・global・typedef・配列要素・ポインタ deref まで広げて確認すると良い。
- 集約初期化・struct 値受け渡しは「ノード種別を特別扱いする分岐」が多く、新しいノード形
  （compound literal=ND_COMMA、subscript=ND_DEREF、funcall 戻り値=ND_FUNCALL）が漏れがち。
