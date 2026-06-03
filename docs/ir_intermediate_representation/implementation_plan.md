# 実装計画：中間表現 (IR) の導入

## 背景と目的

現状 ag_c は AST を直接 walk して ARM64 アセンブリを出力する構造で、`src/arch/arm64_apple.c` (約 2000 行) にコード生成が集約されている。
スタック push/pop モデルのため `str x0, [sp, #-16]!` / `ldr x0, [sp], #16` が頻発し、生成コードが冗長。
また他アーキテクチャを追加するときの再利用性が無い。

ここで AST と ASM の間に **中間表現 (IR)** を導入し、構造を `AST → IR → ASM` に変更する。
動機は次の 3 点:

1. **最適化** — 定数畳み込み、コピー伝播、デッドコード削除を IR 上で実装する。
2. **コード品質** — IR + レジスタ割り付けで push/pop を削減し、生成コードを大幅に短くする。
3. **マルチターゲット** — IR を中間層にして、x86_64 等の追加アーキテクチャは IR→ASM のみ書けば良い構造にする。

## 設計の主要分岐 (要レビュー)

以下は妥当性とトレードオフを示した上で **推奨案** を出している。確定前に承認が必要。

### 分岐 1: IR の抽象レベル

| 案 | 内容 | メリット | デメリット |
|----|------|---------|----------|
| 高レベル | 構造体メンバアクセス、配列添字を IR ノードで保持 | AST に近く、デバッグしやすい | 最適化の表現力が弱い |
| **中レベル (推奨)** | chibicc 風。型情報は持つが、構造体は base+offset に展開、配列添字は scale+add に展開 | バランスが良い、最適化しやすい | 設計判断がやや必要 |
| 低レベル | LLVM IR 風。完全に型抽象を剥がし、3 番地命令の連続 | 最適化の表現力が最強、SSA 化しやすい | ボイラープレートが多い |

### 分岐 2: SSA vs 非 SSA

| 案 | 内容 | メリット | デメリット |
|----|------|---------|----------|
| **非 SSA (推奨)** | 各 vreg は複数回書ける | 単純、コンパクト | 一部最適化 (SCCP, GVN) は弱い |
| SSA | φ 関数で版を分ける | 最適化が強力 | 実装が複雑、入口で覚悟が要る |

非 SSA で始めて、必要があれば後で SSA 化フェーズを足す方針が現実的。

### 分岐 3: 仮想レジスタ vs 物理レジスタ

| 案 | 内容 |
|----|------|
| **無限 vreg (推奨)** | 命令は v0, v1, v2, ... の virtual register で書き、レジスタ割り付けフェーズで物理化 |
| 物理 reg 直結 | x0, x1, x9 等を直接使う |

無限 vreg を採用しないと「マルチターゲット」目的が達成できないので、これは推奨というより必須。

### 分岐 4: メモリモデル (ローカル変数の扱い)

| 案 | 内容 |
|----|------|
| **alloca + load/store (推奨)** | 各ローカル変数は IR の `alloca` 命令でフレームスロットを確保、変数参照は `load`、代入は `store` で表す。SSA 化や escape 解析と相性が良い |
| 直接フレームオフセット | AST の lvar->offset をそのまま IR に持ち込み、専用命令で参照 |

## 命令セット (たたき台)

```
算術 (整数)    : ADD, SUB, MUL, DIV, MOD, NEG
ビット演算     : AND, OR, XOR, SHL, SHR, NOT
比較           : EQ, NE, LT, LE, ULT, ULE  (signed/unsigned)
浮動小数       : FADD, FSUB, FMUL, FDIV, FNEG
変換           : ZEXT, SEXT, TRUNC, F2I, I2F, F2F (s↔d)
メモリ         : LOAD, STORE, ALLOCA, LEA
即値           : LOAD_IMM, LOAD_FP_IMM, LOAD_STR, LOAD_SYM
制御           : BR, BR_COND, RET
関数           : CALL
スコープ       : LABEL
パラメータ     : PARAM (関数 prologue で仮引数を vreg に読む)
組込           : VA_ARG_AREA (Apple ARM64 ABI 用、現 ND_VA_ARG_AREA 相当)
```

各命令は 3 番地形式: `dst = op src1, src2`。比較命令は条件コードを vreg として返す (i1 相当)。
分岐は条件 vreg を取って `BR_COND cond, true_label, false_label`。

## データ構造の骨格

詳細は `src/ir/ir.h` を参照 (スケルトン版を別途配置)。要点:

```c
typedef enum { IR_TY_I8, IR_TY_I16, IR_TY_I32, IR_TY_I64, IR_TY_F32, IR_TY_F64, IR_TY_PTR } ir_type_t;

typedef struct {
  int id;            // -1 = immediate, >=0 = vreg
  ir_type_t type;
  long long imm;
} ir_val_t;

typedef enum { IR_NOP, IR_ADD, IR_SUB, ..., IR_RET } ir_op_t;

typedef struct ir_inst_t {
  struct ir_inst_t *next;
  ir_op_t op;
  ir_val_t dst, src1, src2;
  // op に応じて使うサブ情報
  int label_id;       // BR / BR_COND / LABEL
  int else_label_id;  // BR_COND
  char *sym;          // CALL / LOAD_SYM
  int sym_len;
  ir_val_t *args;     // CALL
  int nargs;
} ir_inst_t;

typedef struct ir_block_t {
  struct ir_block_t *next;
  int id;
  ir_inst_t *head, *tail;
} ir_block_t;

typedef struct ir_func_t {
  struct ir_func_t *next;
  char *name;
  int name_len;
  ir_block_t *entry;
  int next_vreg_id;
  int next_block_id;
  int frame_size;
  int is_variadic;
} ir_func_t;
```

## ファイル配置

```
src/ir/
  ir.h              IR データ構造、Op 一覧、ir_val_t ヘルパ
  ir_builder.c      AST → IR 変換 (AST walker + ir_emit_* ヘルパ)
  ir_builder.h
  ir_print.c        IR ダンプ (デバッグ・テスト用)
  ir_opt.c          最適化パス (定数畳み込み / DCE / コピー伝播)
  ir_opt.h
src/arch/
  arm64_apple_ir.c  IR → ARM64 ASM (新規) — 段階的に旧 arm64_apple.c から置き換え
  arm64_apple.c     既存 (移行期間中は両方存在)
test/
  test_ir_print.c   IR ビルダーの単体テスト
```

## 移行ステップ (段階計画)

各ステップで `make test` が通る状態を維持する。フラグまたは関数切り替えで AST 直 codegen と並走させる。

### Phase 0: 設計レビュー (現在)

- 本文書のレビューと、設計分岐の確定。
- `src/ir/ir.h` スケルトンの提出。
- **コードは書かない。コミットしない。**

### Phase 1: IR データ構造とプリンタ

- `src/ir/ir.h` を確定し、`ir_inst_t` / `ir_block_t` / `ir_func_t` のアロケータヘルパを実装。
- `ir_print.c` で IR を人が読める形式で出力 (デバッグ・将来のテスト用)。
- 既存 codegen には触らない。`make test` 712 そのまま通る。

### Phase 2: 算術式だけ IR 経由に

- 環境変数または config (例: `AG_USE_IR=1`) で AST 直と IR 経由を切り替える。
- 算術式 (ND_ADD / ND_SUB / ND_MUL / ND_DIV / ND_NUM / ND_LVAR スカラ) の AST→IR 変換と IR→ASM を実装。
- ローカル変数は alloca + load/store で表現。
- 関数全体ではなく式単位で試す (e.g. `int main() { return 1 + 2; }` だけ IR を通す)。
- レジスタ割り付けは初期実装として全 vreg をフレームスロットに割り当てる (= push/pop と同等の生成コード)。動作確認が目的。

### Phase 3: 制御フローを IR 経由に

- if / while / for / break / continue / return を IR の BR / BR_COND / LABEL で表現。
- 基本ブロックの分割を確実にする。
- ここまでで「シンプルな関数なら IR 経由でビルド可能」状態。

### Phase 4: 関数呼び出し、配列、ポインタ

- ND_FUNCALL → IR_CALL。引数評価とレジスタ/スタック振り分けは IR レベルで完結。
- 配列添字、deref、addr-of を IR の LOAD/STORE/LEA で表現。
- 構造体 (>16B 含む) の取り扱い。Apple ARM64 ABI の variadic も含めて IR_VA_ARG_AREA で対応。
- typedef array dims (今回の修正で導入) も IR レベルに reflect。

### Phase 5: レジスタ割り付け (Linear Scan)

- vreg の live range を計算 (まずは関数全体スキャンの単純版)。
- 物理レジスタ x9..x15 (caller-saved) / x19..x28 (callee-saved) を線形スキャンで割り付け。
- spill は alloca 経由でフレームスロットへ。
- 生成コードから push/pop を一掃する。

### Phase 6: 最適化パス

- 定数畳み込み (peephole)。
- コピー伝播。
- デッドコード削除 (use-def 解析)。
- 各パスは optional、フラグで有効化。

### Phase 7: 旧 codegen 撤去

- `src/arch/arm64_apple.c` を新 IR 経由版に置き換え、AST 直のコードを削除。
- README / docs 更新。

### Phase 8: マルチターゲット準備 (任意)

- IR→ASM のインターフェースを明確化 (codegen_backend.h を IR ベースで再定義)。
- x86_64 バックエンドの骨格を追加 (実装は別フェーズ)。

## リスクと対策

| リスク | 対策 |
|--------|------|
| 移行中にテストが落ちる | Phase 2 以降は環境変数で AST 直と並走させ、`make test` が常に通る前提を崩さない |
| IR 設計の早すぎる決定で後で書き直し | Phase 0 のレビュー段階を明示し、確定するまでコードに踏み出さない |
| 既存 ABI/calling convention 等のレアケース漏れ | 既存 fixture を IR 経由でも走らせる E2E 体制 (Phase 2 から) |
| スコープ広がりで完走できない | Phase 1〜3 だけでも生成コード品質に効くので、無理せず段階で止められる構造にする |

## 完了条件

各 Phase の出口で以下を満たすこと:

- `make test` 全パス (フラグ無効時で 736、有効時でカバー範囲のものが全通)。
- 該当 Phase の IR 命令 / 変換ルール / 生成 ASM のサンプルを `docs/ir_intermediate_representation/phases/<N>.md` 等に記録。
- ベンチマーク (簡単な数値計算ループ) で生成バイト数 / 命令数の改善を測定 (Phase 5 以降)。

## 確認事項 (このレビュー段階)

このまま進めて良いか以下を確認させてほしい:

1. 抽象レベル: **中レベル** で良いか
2. SSA: 当面 **非 SSA** で良いか
3. メモリモデル: **alloca + load/store** で良いか
4. ファイル配置 (`src/ir/`) で良いか
5. 段階計画 (Phase 0〜8) の粒度と順序

承認後、Phase 1 で `src/ir/ir.h` を確定し、Phase 2 から実装に入る。
