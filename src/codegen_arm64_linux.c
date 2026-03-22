#include "chibicc.h"

static char *asm_symbol_name(char *name, bool is_definition) {
  (void)is_definition;
  return name;
}

static void emit_file_directive(FILE *out, int file_no, char *name) {
  fprintf(out, "  .file %d \"%s\"\n", file_no, name);
}

static void emit_global_directive(FILE *out, char *name, bool is_static) {
  fprintf(out, "  %s %s\n", is_static ? ".local" : ".globl", name);
}

static void emit_text_symbol_header(FILE *out, char *name) {
  fprintf(out, "  .text\n");
  fprintf(out, "  .type %s, %%function\n", name);
  fprintf(out, "%s:\n", name);
}

static void emit_data_symbol_header(FILE *out, char *name, int size, int align) {
  fprintf(out, "  .data\n");
  fprintf(out, "  .type %s, %%object\n", name);
  fprintf(out, "  .size %s, %d\n", name, size);
  fprintf(out, "  .balign %d\n", align);
  fprintf(out, "%s:\n", name);
}

static void emit_bss_symbol_header(FILE *out, char *name, int align, int size) {
  fprintf(out, "  .bss\n");
  fprintf(out, "  .balign %d\n", align);
  fprintf(out, "%s:\n", name);
  fprintf(out, "  .zero %d\n", size);
}

static void emit_common_symbol(FILE *out, char *name, int size, int align) {
  fprintf(out, "  .comm %s, %d, %d\n", name, size, align);
}

static void emit_data_reloc(FILE *out, char *label, long addend) {
  fprintf(out, "  .xword %s%+ld\n", label, addend);
}

static void emit_addr_of_global(FILE *out, char *reg, char *name) {
  fprintf(out, "  adrp %s, %s\n", reg, name);
  fprintf(out, "  add %s, %s, :lo12:%s\n", reg, reg, name);
}

const Arm64TargetOps arm64_linux_target_ops = {
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
