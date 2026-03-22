#include "chibicc.h"

int align_to(int n, int align) {
  return (n + align - 1) / align * align;
}

void codegen(Obj *prog, FILE *out) {
  switch (current_target) {
  case TARGET_X86_64:
    codegen_x86(prog, out);
    return;
  case TARGET_AARCH64_LINUX:
  case TARGET_AARCH64_DARWIN:
    codegen_arm64(prog, out);
    return;
  }

  unreachable();
}