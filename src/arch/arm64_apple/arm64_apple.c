/*
 * arm64 Apple ABI: 共有インフラとデータセクション出力。
 *
 * ag_c は AST → IR → ASM の経路で関数本体を生成する (arm64_apple_ir.c)。
 * このファイルは IR バックエンドと main.c が共有する以下を提供する:
 *   - cg_emit_mov_imm: ARM64 即値ロードヘルパ
 *   - gen_string_literals / gen_float_literals / gen_global_vars: lowering 済み
 *     translation-unit data IR のデータセクション emit
 *
 * 旧 AST 直 codegen (gen / gen_stmt / gen_expr ...) はここから削除済み。
 * Phase 7o で IR 経路が fixture 100% を通過したため、AST 経路は不要になった。
 */

#include "arm64_apple_emit.h"
#include "../../codegen_backend.h"
#include <stdint.h>
#include <string.h>

/* AArch64 即値ロード: 16bit に収まらない値は movz/movk シーケンス。
 * IR バックエンドからも共有するため非 static。 */
void cg_emit_mov_imm_in(
    ag_codegen_emit_context_t *emit_context,
    const char *reg, long long val) {
  uint64_t uval = (uint64_t)val;
  if (val >= 0 && val <= 0xFFFF) {
    cg_emitf_in(emit_context, "  mov %s, #%lld\n", reg, val);
    return;
  }
  if (val < 0 && val >= -0x10000) {
    cg_emitf_in(emit_context, "  mov %s, #%lld\n", reg, val);
    return;
  }
  int first = 1;
  for (int shift = 0; shift < 64; shift += 16) {
    uint64_t chunk = (uval >> shift) & 0xFFFF;
    if (chunk == 0 && !first) continue;
    if (first) {
      cg_emitf_in(emit_context, "  movz %s, #%llu, lsl #%d\n",
                  reg, (unsigned long long)chunk, shift);
      first = 0;
    } else {
      cg_emitf_in(emit_context, "  movk %s, #%llu, lsl #%d\n",
                  reg, (unsigned long long)chunk, shift);
    }
  }
}

static int asm_symbol_byte_is_unquoted(unsigned char byte) {
  return (byte >= 'a' && byte <= 'z') ||
         (byte >= 'A' && byte <= 'Z') ||
         (byte >= '0' && byte <= '9') ||
         byte == '_' || byte == '.' || byte == '$';
}

static void emit_quoted_symbol_part(
    ag_codegen_emit_context_t *emit_context,
    const char *part, size_t length) {
  for (size_t i = 0; i < length; i++) {
    unsigned char byte = (unsigned char)part[i];
    if (byte == '"' || byte == '\\')
      cg_emitf_in(emit_context, "\\");
    cg_emitf_in(emit_context, "%c", byte);
  }
}

void cg_emit_asm_symbol_in(
    ag_codegen_emit_context_t *emit_context,
    const char *prefix, const char *name, int name_len,
    const char *suffix) {
  if (!prefix) prefix = "";
  if (!name) {
    name = "";
    name_len = 0;
  }
  if (name_len < 0) name_len = 0;
  if (!suffix) suffix = "";
  size_t prefix_len = strlen(prefix);
  size_t suffix_len = strlen(suffix);
  int quote = 0;
  for (size_t i = 0; i < prefix_len && !quote; i++)
    quote = !asm_symbol_byte_is_unquoted((unsigned char)prefix[i]);
  for (int i = 0; i < name_len && !quote; i++)
    quote = !asm_symbol_byte_is_unquoted((unsigned char)name[i]);
  for (size_t i = 0; i < suffix_len && !quote; i++)
    quote = !asm_symbol_byte_is_unquoted((unsigned char)suffix[i]);

  if (!quote) {
    cg_emitf_in(
        emit_context, "%s%.*s%s",
        prefix, name_len, name, suffix);
    return;
  }
  cg_emitf_in(emit_context, "\"");
  emit_quoted_symbol_part(emit_context, prefix, prefix_len);
  emit_quoted_symbol_part(emit_context, name, (size_t)name_len);
  emit_quoted_symbol_part(emit_context, suffix, suffix_len);
  cg_emitf_in(emit_context, "\"");
}

/* ------------------------------------------------------------------ */
/* データセクション (translation-unit data IR を emit) */
/* ------------------------------------------------------------------ */

typedef struct {
  int has_narrow;
  int has_wide;
} string_lit_kind_scan_t;

static void emit_data_object_bytes(
    ag_codegen_emit_context_t *emit_context,
    const ir_data_object_t *object) {
  cg_emitf_in(emit_context, "%s:\n", object->name);
  for (int i = 0; i < object->byte_size; i++)
    cg_emitf_in(emit_context, "  .byte %u\n", (unsigned)object->bytes[i]);
}

void gen_string_literals_in(
    ag_codegen_emit_context_t *emit_context,
    const ir_data_module_t *data_module) {
  /* narrow char 文字列のみ __TEXT,__cstring に置く。
   * u"..." / U"..." / L"..." は内部にゼロバイトを含み得るため __DATA,__const へ。 */
  string_lit_kind_scan_t scan = {0};
  for (const ir_data_object_t *object = data_module ? data_module->objects : NULL;
       object; object = object->next) {
    if (object->kind != IR_DATA_STRING) continue;
    if (object->element_size == 1) scan.has_narrow = 1;
    else scan.has_wide = 1;
  }
  if (!scan.has_narrow && !scan.has_wide) return;
  if (scan.has_narrow)
    cg_emitf_in(emit_context, ".section __TEXT,__cstring\n");
  for (const ir_data_object_t *object = data_module ? data_module->objects : NULL;
       object; object = object->next) {
    if (object->kind == IR_DATA_STRING && object->element_size == 1)
      emit_data_object_bytes(emit_context, object);
  }
  if (scan.has_wide) {
    cg_emitf_in(emit_context, ".section __DATA,__const\n");
    cg_emitf_in(emit_context, ".align 2\n");
    for (const ir_data_object_t *object = data_module ? data_module->objects : NULL;
         object; object = object->next) {
      if (object->kind == IR_DATA_STRING && object->element_size != 1)
        emit_data_object_bytes(emit_context, object);
    }
  }
  cg_emitf_in(emit_context, ".text\n");
}

void gen_float_literals_in(
    ag_codegen_emit_context_t *emit_context,
    const ir_data_module_t *data_module) {
  int has_float = 0;
  for (const ir_data_object_t *object = data_module ? data_module->objects : NULL;
       object; object = object->next) {
    if (object->kind == IR_DATA_FLOAT) has_float = 1;
  }
  if (!has_float) return;
  cg_emitf_in(emit_context, ".section __DATA,__data\n");
  cg_emitf_in(emit_context, ".align 3\n");
  for (const ir_data_object_t *object = data_module ? data_module->objects : NULL;
       object; object = object->next) {
    if (object->kind != IR_DATA_FLOAT) continue;
    emit_data_object_bytes(emit_context, object);
  }
  cg_emitf_in(emit_context, ".text\n");
}

static int log2_alignment(int alignment) {
  int exponent = 0;
  while (alignment > 1) {
    alignment >>= 1;
    exponent++;
  }
  return exponent;
}

static void emit_relocation_target(
                                   ag_codegen_emit_context_t *emit_context,
                                   const ir_data_module_t *module,
                                   const ir_data_reloc_t *reloc) {
  const char *directive = reloc->width == 8 ? ".quad" :
                          reloc->width == 4 ? ".long" :
                          reloc->width == 2 ? ".short" : ".byte";
  const ir_data_object_t *target = ir_data_module_find_object(
      module, reloc->target, reloc->target_len);
  int raw_label = target && target->kind == IR_DATA_STRING;
  cg_emitf_in(emit_context, "  %s ", directive);
  cg_emit_asm_symbol_in(
      emit_context, raw_label ? "" : "_",
      reloc->target, reloc->target_len, "");
  if (reloc->addend > 0)
    cg_emitf_in(emit_context, "+%lld", reloc->addend);
  else if (reloc->addend < 0)
    cg_emitf_in(emit_context, "%lld", reloc->addend);
  cg_emitf_in(emit_context, "\n");
}

static void emit_global_initializer(
                                    ag_codegen_emit_context_t *emit_context,
                                    const ir_data_module_t *module,
                                    const ir_data_object_t *object) {
  const ir_data_reloc_t *reloc = object->relocs;
  for (int offset = 0; offset < object->byte_size; ) {
    if (reloc && reloc->offset == offset) {
      emit_relocation_target(emit_context, module, reloc);
      offset += reloc->width;
      reloc = reloc->next;
    } else {
      cg_emitf_in(emit_context, "  .byte %u\n",
                  (unsigned)object->bytes[offset++]);
    }
  }
}

static void emit_global_object(
                               ag_codegen_emit_context_t *emit_context,
                               const ir_data_module_t *module,
                               const ir_data_object_t *object) {
  if (object->is_extern) return;
  if (object->is_thread_local) {
    /* _Thread_local: TLV descriptor + thread data/bss */
    if (object->has_explicit_initializer) {
      cg_emitf_in(
          emit_context,
          ".section __DATA,__thread_data,thread_local_regular\n");
      cg_emitf_in(emit_context, ".align %d\n",
                  log2_alignment(object->alignment));
      cg_emit_asm_symbol_in(
          emit_context, "_", object->name, object->name_len, "$tlv$init");
      cg_emitf_in(emit_context, ":\n");
      emit_global_initializer(emit_context, module, object);
    } else {
      cg_emitf_in(emit_context, ".tbss ");
      cg_emit_asm_symbol_in(
          emit_context, "_", object->name, object->name_len, "$tlv$init");
      cg_emitf_in(
          emit_context, ",%d,%d\n", object->byte_size,
          log2_alignment(object->alignment));
    }
    cg_emitf_in(
        emit_context, ".section __DATA,__thread_vars,thread_local_variables\n");
    if (!object->is_static) {
      cg_emitf_in(emit_context, ".global ");
      cg_emit_asm_symbol_in(
          emit_context, "_", object->name, object->name_len, "");
      cg_emitf_in(emit_context, "\n");
    }
    cg_emit_asm_symbol_in(
        emit_context, "_", object->name, object->name_len, "");
    cg_emitf_in(emit_context, ":\n");
    cg_emitf_in(emit_context, "  .quad __tlv_bootstrap\n");
    cg_emitf_in(emit_context, "  .quad 0\n");
    cg_emitf_in(emit_context, "  .quad ");
    cg_emit_asm_symbol_in(
        emit_context, "_", object->name, object->name_len, "$tlv$init");
    cg_emitf_in(emit_context, "\n");
    return;
  }
  if (object->has_explicit_initializer) {
    cg_emitf_in(emit_context, ".section __DATA,__data\n");
    if (!object->is_static) {
      cg_emitf_in(emit_context, ".global ");
      cg_emit_asm_symbol_in(
          emit_context, "_", object->name, object->name_len, "");
      cg_emitf_in(emit_context, "\n");
    }
    cg_emitf_in(emit_context, ".align %d\n",
                log2_alignment(object->alignment));
    cg_emit_asm_symbol_in(
        emit_context, "_", object->name, object->name_len, "");
    cg_emitf_in(emit_context, ":\n");
    emit_global_initializer(emit_context, module, object);
    return;
  }
  /* 暫定定義: .comm _name,size,log2align。
   * ただし static (内部リンケージ) は .comm (= common/外部シンボル) にすると別 TU の
   * 同名 static と共有/衝突するため、ローカルな .zerofill (__bss) に出す。 */
  int log2align = log2_alignment(object->alignment);
  if (object->is_static) {
    cg_emitf_in(emit_context, ".zerofill __DATA,__bss,");
    cg_emit_asm_symbol_in(
        emit_context, "_", object->name, object->name_len, "");
    cg_emitf_in(
        emit_context, ",%d,%d\n", object->byte_size, log2align);
  } else {
    cg_emitf_in(emit_context, ".comm ");
    cg_emit_asm_symbol_in(
        emit_context, "_", object->name, object->name_len, "");
    cg_emitf_in(
        emit_context, ",%d,%d\n", object->byte_size, log2align);
  }
}

void gen_global_vars_in(
    ag_codegen_emit_context_t *emit_context,
    const ir_data_module_t *data_module) {
  for (const ir_data_object_t *object = data_module ? data_module->objects : NULL;
       object; object = object->next) {
    if (object->kind == IR_DATA_OBJECT)
      emit_global_object(emit_context, data_module, object);
  }
}
