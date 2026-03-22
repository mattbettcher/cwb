#include "chibicc.h"

static int align_log2(int align) {
  int n = 0;
  int a = MAX(align, 1);
  while ((1 << n) < a)
    n++;
  return n;
}

static char *asm_symbol_name(char *name, bool is_definition) {
  (void)is_definition;
  if (!strncmp(name, ".L", 2) || !strncmp(name, "L", 1))
    return name;
  if (name[0] == '_')
    return name;
  return format("_%s", name);
}

static void emit_file_directive(FILE *out, int file_no, char *name) {
  fprintf(out, "  .file %d \"%s\"\n", file_no, name);
}

static void emit_global_directive(FILE *out, char *name, bool is_static) {
  if (!is_static)
    fprintf(out, "  .globl %s\n", name);
}

static void emit_text_symbol_header(FILE *out, char *name) {
  fprintf(out, "  .section __TEXT,__text,regular,pure_instructions\n");
  fprintf(out, "  .p2align 2\n");
  fprintf(out, "%s:\n", name);
}

static void emit_data_symbol_header(FILE *out, char *name, int size, int align) {
  (void)size;
  fprintf(out, "  .section __DATA,__data\n");
  fprintf(out, "  .p2align %d\n", align_log2(align));
  fprintf(out, "%s:\n", name);
}

static void emit_bss_symbol_header(FILE *out, char *name, int align, int size) {
  fprintf(out, "  .zerofill __DATA,__bss,%s,%d,%d\n", name, size, align_log2(align));
}

static void emit_common_symbol(FILE *out, char *name, int size, int align) {
  fprintf(out, "  .comm %s, %d, %d\n", name, size, align_log2(align));
}

static void emit_data_reloc(FILE *out, char *label, long addend) {
  if (addend)
    fprintf(out, "  .quad %s%+ld\n", label, addend);
  else
    fprintf(out, "  .quad %s\n", label);
}

static void emit_addr_of_global(FILE *out, char *reg, char *name) {
  fprintf(out, "  adrp %s, %s@PAGE\n", reg, name);
  fprintf(out, "  add %s, %s, %s@PAGEOFF\n", reg, reg, name);
}

const Arm64TargetOps arm64_apple_target_ops = {
  .asm_symbol_name = asm_symbol_name,
  .emit_file_directive = emit_file_directive,
  .emit_global_directive = emit_global_directive,
  .emit_text_symbol_header = emit_text_symbol_header,
  .emit_data_symbol_header = emit_data_symbol_header,
  .emit_bss_symbol_header = emit_bss_symbol_header,
  .emit_common_symbol = emit_common_symbol,
  .emit_data_reloc = emit_data_reloc,
  .emit_addr_of_global = emit_addr_of_global,
};
