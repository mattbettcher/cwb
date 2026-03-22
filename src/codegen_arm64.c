#include "chibicc.h"

#define GP_MAX 8
#define FP_MAX 8

static FILE *output_file;
static int depth;
static Obj *current_fn;
static char *argreg32[] = {"w0", "w1", "w2", "w3", "w4", "w5", "w6", "w7"};
static char *argreg64[] = {"x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"};

static void gen_expr(Node *node);
static void gen_stmt(Node *node);

__attribute__((format(printf, 1, 2)))
static void println(char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(output_file, fmt, ap);
  va_end(ap);
  fprintf(output_file, "\n");
}

static int count(void) {
  static int i = 1;
  return i++;
}

static void push(void) {
  println("  sub sp, sp, #16");
  println("  str x0, [sp]");
  depth += 16;
}

static void pop(char *reg) {
  println("  ldr %s, [sp]", reg);
  println("  add sp, sp, #16");
  depth -= 16;
}

static void pushf(void) {
  println("  sub sp, sp, #16");
  println("  str d0, [sp]");
  depth += 16;
}

static void popf(int reg) {
  println("  ldr d%d, [sp]", reg);
  println("  add sp, sp, #16");
  depth -= 16;
}

static void mov_imm(char *reg, uint64_t val) {
  bool emitted = false;

  for (int i = 0; i < 4; i++) {
    uint64_t part = (val >> (i * 16)) & 0xffff;
    if (!part)
      continue;

    if (!emitted) {
      println("  movz %s, #%llu, lsl #%d", reg, (unsigned long long)part, i * 16);
      emitted = true;
    } else {
      println("  movk %s, #%llu, lsl #%d", reg, (unsigned long long)part, i * 16);
    }
  }

  if (!emitted)
    println("  mov %s, xzr", reg);
}

static void add_imm(char *dst, char *base, int imm) {
  if (imm == 0) {
    if (strcmp(dst, base))
      println("  mov %s, %s", dst, base);
    return;
  }

  if (imm > 0 && imm < 4096) {
    println("  add %s, %s, #%d", dst, base, imm);
    return;
  }

  if (imm < 0 && -imm < 4096) {
    println("  sub %s, %s, #%d", dst, base, -imm);
    return;
  }

  mov_imm("x15", imm < 0 ? -(uint64_t)imm : (uint64_t)imm);
  println("  %s %s, %s, x15", imm < 0 ? "sub" : "add", dst, base);
}

static void addr_from_fp(char *dst, int offset) {
  add_imm(dst, "x29", offset);
}

static void addr_from_reg(char *dst, char *base, int offset) {
  add_imm(dst, base, offset);
}

static void copy_bytes(char *dst, char *src, int size) {
  for (int i = 0; i < size; i++) {
    addr_from_reg("x15", src, i);
    println("  ldrb w14, [x15]");
    addr_from_reg("x15", dst, i);
    println("  strb w14, [x15]");
  }
}

static void load_bytes_to(char *dst, char *src, int size) {
  println("  mov %s, xzr", dst);

  for (int i = size - 1; i >= 0; i--) {
    println("  lsl %s, %s, #8", dst, dst);
    addr_from_reg("x15", src, i);
    println("  ldrb w14, [x15]");
    println("  orr %s, %s, x14", dst, dst);
  }
}

static void store_int_to(char *addr, int size) {
  switch (size) {
  case 1:
    println("  strb w0, [%s]", addr);
    return;
  case 2:
    println("  strh w0, [%s]", addr);
    return;
  case 4:
    println("  str w0, [%s]", addr);
    return;
  case 8:
    println("  str x0, [%s]", addr);
    return;
  }
  unreachable();
}

static void load_int(Type *ty) {
  switch (ty->size) {
  case 1:
    if (ty->is_unsigned)
      println("  ldrb w0, [x0]");
    else
      println("  ldrsb x0, [x0]");
    return;
  case 2:
    if (ty->is_unsigned)
      println("  ldrh w0, [x0]");
    else
      println("  ldrsh x0, [x0]");
    return;
  case 4:
    if (ty->is_unsigned)
      println("  ldr w0, [x0]");
    else
      println("  ldrsw x0, [x0]");
    return;
  case 8:
    println("  ldr x0, [x0]");
    return;
  }
  unreachable();
}

static void load(Type *ty) {
  switch (ty->kind) {
  case TY_ARRAY:
  case TY_STRUCT:
  case TY_UNION:
  case TY_FUNC:
  case TY_VLA:
    return;
  case TY_FLOAT:
    println("  ldr s0, [x0]");
    return;
  case TY_DOUBLE:
  case TY_LDOUBLE:  // On ARM64, long double is the same as double
    println("  ldr d0, [x0]");
    return;
  default:
    load_int(ty);
    return;
  }
}

static void store(Type *ty) {
  pop("x1");

  switch (ty->kind) {
  case TY_STRUCT:
  case TY_UNION:
    copy_bytes("x1", "x0", ty->size);
    return;
  case TY_FLOAT:
    println("  str s0, [x1]");
    return;
  case TY_DOUBLE:
  case TY_LDOUBLE:  // On ARM64, long double is the same as double
    println("  str d0, [x1]");
    return;
  default:
    store_int_to("x1", ty->size);
    return;
  }
}

static void cmp_zero(Type *ty) {
  switch (ty->kind) {
  case TY_FLOAT:
    println("  fcmp s0, #0.0");
    return;
  case TY_DOUBLE:
  case TY_LDOUBLE:  // On ARM64, long double is the same as double
    println("  fcmp d0, #0.0");
    return;
  default:
    if (is_integer(ty) && ty->size <= 4)
      println("  cmp w0, #0");
    else
      println("  cmp x0, #0");
    return;
  }
}

static void truncate_to(Type *ty) {
  switch (ty->kind) {
  case TY_BOOL:
    println("  cmp x0, #0");
    println("  cset w0, ne");
    return;
  case TY_CHAR:
    if (ty->is_unsigned)
      println("  uxtb w0, w0");
    else
      println("  sxtb x0, w0");
    return;
  case TY_SHORT:
    if (ty->is_unsigned)
      println("  uxth w0, w0");
    else
      println("  sxth x0, w0");
    return;
  case TY_INT:
    if (!ty->is_unsigned)
      println("  sxtw x0, w0");
    return;
  default:
    return;
  }
}

static bool has_flonum(Type *ty, int lo, int hi, int offset) {
  if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
    for (Member *mem = ty->members; mem; mem = mem->next)
      if (!has_flonum(mem->ty, lo, hi, offset + mem->offset))
        return false;
    return true;
  }

  if (ty->kind == TY_ARRAY) {
    for (int i = 0; i < ty->array_len; i++)
      if (!has_flonum(ty->base, lo, hi, offset + ty->base->size * i))
        return false;
    return true;
  }

  return offset < lo || hi <= offset || ty->kind == TY_FLOAT || ty->kind == TY_DOUBLE;
}

static bool has_flonum1(Type *ty) {
  return has_flonum(ty, 0, 8, 0);
}

static bool has_flonum2(Type *ty) {
  return has_flonum(ty, 8, 16, 0);
}

static void gen_addr(Node *node) {
  switch (node->kind) {
  case ND_VAR:
    if (node->var->ty->kind == TY_VLA) {
      addr_from_fp("x15", node->var->offset);
      println("  ldr x0, [x15]");
      return;
    }

    if (node->var->is_local) {
      addr_from_fp("x0", node->var->offset);
      return;
    }

    if (node->var->is_tls)
      error_tok(node->tok, "ARM64 backend does not support TLS yet");

    if (opt_fpic)
      error_tok(node->tok, "ARM64 backend does not support PIC yet");

    println("  adrp x0, %s", node->var->name);
    println("  add x0, x0, :lo12:%s", node->var->name);
    return;
  case ND_DEREF:
    gen_expr(node->lhs);
    return;
  case ND_COMMA:
    gen_expr(node->lhs);
    gen_addr(node->rhs);
    return;
  case ND_MEMBER:
    gen_addr(node->lhs);
    add_imm("x0", "x0", node->member->offset);
    return;
  case ND_FUNCALL:
    if (node->ret_buffer) {
      gen_expr(node);
      return;
    }
    break;
  case ND_ASSIGN:
  case ND_COND:
    if (node->ty->kind == TY_STRUCT || node->ty->kind == TY_UNION) {
      gen_expr(node);
      return;
    }
    break;
  case ND_VLA_PTR:
    addr_from_fp("x0", node->var->offset);
    return;
  }

  error_tok(node->tok, "not an lvalue");
}

static void cast(Type *from, Type *to) {
  if (to->kind == TY_VOID)
    return;

  if (to->kind == TY_BOOL) {
    cmp_zero(from);
    println("  cset w0, ne");
    return;
  }

  if (is_integer(from) && is_integer(to)) {
    truncate_to(to);
    return;
  }

  if (is_integer(from) && to->kind == TY_FLOAT) {
    if (from->is_unsigned)
      println("  ucvtf s0, %s", from->size == 8 ? "x0" : "w0");
    else
      println("  scvtf s0, %s", from->size == 8 ? "x0" : "w0");
    return;
  }

  if (is_integer(from) && to->kind == TY_DOUBLE) {
    if (from->is_unsigned)
      println("  ucvtf d0, %s", from->size == 8 ? "x0" : "w0");
    else
      println("  scvtf d0, %s", from->size == 8 ? "x0" : "w0");
    return;
  }

  if (from->kind == TY_FLOAT && is_integer(to)) {
    if (to->is_unsigned)
      println("  fcvtzu %s, s0", to->size == 8 ? "x0" : "w0");
    else
      println("  fcvtzs %s, s0", to->size == 8 ? "x0" : "w0");
    truncate_to(to);
    return;
  }

  if (from->kind == TY_DOUBLE && is_integer(to)) {
    if (to->is_unsigned)
      println("  fcvtzu %s, d0", to->size == 8 ? "x0" : "w0");
    else
      println("  fcvtzs %s, d0", to->size == 8 ? "x0" : "w0");
    truncate_to(to);
    return;
  }

  if (from->kind == TY_FLOAT && to->kind == TY_DOUBLE) {
    println("  fcvt d0, s0");
    return;
  }

  if (from->kind == TY_DOUBLE && to->kind == TY_FLOAT) {
    println("  fcvt s0, d0");
    return;
  }
}

static void push_reg_args(Node *arg) {
  if (!arg)
    return;
  push_reg_args(arg->next);

  if (arg->pass_by_stack)
    return;

  gen_expr(arg);

  switch (arg->ty->kind) {
  case TY_FLOAT:
  case TY_DOUBLE:
    pushf();
    return;
  default:
    push();
    return;
  }
}

static void store_stack_arg(Type *ty, int offset) {
  addr_from_reg("x15", "x9", offset);

  switch (ty->kind) {
  case TY_STRUCT:
  case TY_UNION:
    copy_bytes("x15", "x0", ty->size);
    return;
  case TY_FLOAT:
  case TY_DOUBLE:
    println("  str s0, [x15]");
    return;
  case TY_LDOUBLE:  // On ARM64, long double is the same as double
    println("  str d0, [x15]");
  default:
    store_int_to("x15", ty->size);
    return;
  }
}

static int push_args(Node *node) {
  int stack = 0;
  int gp = node->ret_buffer && node->ty->size > 16;
  int fp = 0;

  for (Node *arg = node->args; arg; arg = arg->next) {
    Type *ty = arg->ty;

    switch (ty->kind) {
    case TY_STRUCT:
    case TY_UNION:
      if (ty->size > 16) {
        arg->pass_by_stack = true;
        stack += align_to(ty->size, 8);
        break;
      }

      if (fp + has_flonum1(ty) + has_flonum2(ty) <= FP_MAX &&
          gp + !has_flonum1(ty) + !has_flonum2(ty) <= GP_MAX) {
        fp += has_flonum1(ty) + has_flonum2(ty);
        gp += !has_flonum1(ty) + !has_flonum2(ty);
      } else {
        arg->pass_by_stack = true;
        stack += align_to(ty->size, 8);
      }
      break;
    case TY_FLOAT:
    case TY_DOUBLE:
      if (fp < FP_MAX) {
        fp++;
      } else {
        arg->pass_by_stack = true;
        stack += 8;
      }
      break;
    case TY_LDOUBLE:  // On ARM64, long double is the same as double
      if (fp < FP_MAX) {
        fp++;
      } else {
        arg->pass_by_stack = true;
        stack += 8;
      }
      break;
    default:
      if (gp < GP_MAX) {
        gp++;
      } else {
        arg->pass_by_stack = true;
        stack += 8;
      }
      break;
    }
  }

  int stack_area = align_to(stack, 16);
  if (stack_area) {
    println("  sub sp, sp, #%d", stack_area);
    println("  mov x9, sp");
    depth += stack_area;
  }

  int stack_off = 0;
  for (Node *arg = node->args; arg; arg = arg->next) {
    if (!arg->pass_by_stack)
      continue;

    gen_expr(arg);
    store_stack_arg(arg->ty, stack_off);
    stack_off += align_to(MAX(arg->ty->size, 8), 8);
  }

  push_reg_args(node->args);

  if (node->ret_buffer && node->ty->size > 16) {
    addr_from_fp("x0", node->ret_buffer->offset);
    push();
  }

  return stack_area;
}

static void load_struct_from_addr(Type *ty, int *gp, int *fp) {
  pop("x15");

  if (has_flonum1(ty)) {
    if (MIN(8, ty->size) == 4)
      println("  ldr s%d, [x15]", *fp);
    else
      println("  ldr d%d, [x15]", *fp);
    (*fp)++;
  } else {
    if (MIN(8, ty->size) == 8)
      println("  ldr x%d, [x15]", *gp);
    else
      load_bytes_to(argreg64[*gp], "x15", MIN(8, ty->size));
    (*gp)++;
  }

  if (ty->size <= 8)
    return;

  addr_from_reg("x15", "x15", 8);
  if (has_flonum2(ty)) {
    if (ty->size - 8 == 4)
      println("  ldr s%d, [x15]", *fp);
    else
      println("  ldr d%d, [x15]", *fp);
    (*fp)++;
  } else {
    if (ty->size - 8 == 8)
      println("  ldr x%d, [x15]", *gp);
    else
      load_bytes_to(argreg64[*gp], "x15", ty->size - 8);
    (*gp)++;
  }
}

static void copy_ret_buffer(Obj *var) {
  Type *ty = var->ty;
  int gp = 0;
  int fp = 0;

  if (has_flonum1(ty)) {
    addr_from_fp("x15", var->offset);
    if (MIN(8, ty->size) == 4)
      println("  str s0, [x15]");
    else
      println("  str d0, [x15]");
    fp++;
  } else {
    addr_from_fp("x15", var->offset);
    if (MIN(8, ty->size) == 8)
      println("  str x0, [x15]");
    else {
      println("  mov x14, x0");
      for (int i = 0; i < MIN(8, ty->size); i++) {
        println("  strb w14, [x15, #%d]", i);
        println("  lsr x14, x14, #8");
      }
    }
    gp++;
  }

  if (ty->size <= 8)
    return;

  addr_from_fp("x15", var->offset + 8);
  if (has_flonum2(ty)) {
    if (ty->size - 8 == 4)
      println("  str s%d, [x15]", fp);
    else
      println("  str d%d, [x15]", fp);
  } else {
    char *reg = gp == 0 ? "x0" : "x1";
    if (ty->size - 8 == 8)
      println("  str %s, [x15]", reg);
    else {
      println("  mov x14, %s", reg);
      for (int i = 0; i < ty->size - 8; i++) {
        println("  strb w14, [x15, #%d]", i);
        println("  lsr x14, x14, #8");
      }
    }
  }
}

static void copy_struct_reg(void) {
  Type *ty = current_fn->ty->return_ty;
  println("  mov x15, x0");

  if (has_flonum1(ty)) {
    if (MIN(8, ty->size) == 4)
      println("  ldr s0, [x15]");
    else
      println("  ldr d0, [x15]");
  } else {
    if (MIN(8, ty->size) == 8) {
      println("  ldr x0, [x15]");
    } else {
      println("  mov x0, xzr");
      for (int i = MIN(8, ty->size) - 1; i >= 0; i--) {
        println("  lsl x0, x0, #8");
        println("  ldrb w14, [x15, #%d]", i);
        println("  orr x0, x0, x14");
      }
    }
  }

  if (ty->size <= 8)
    return;

  addr_from_reg("x15", "x15", 8);
  if (has_flonum2(ty)) {
    if (ty->size - 8 == 4)
      println("  ldr s1, [x15]");
    else
      println("  ldr d1, [x15]");
  } else {
    if (ty->size - 8 == 8) {
      println("  ldr x1, [x15]");
    } else {
      println("  mov x1, xzr");
      for (int i = ty->size - 9; i >= 0; i--) {
        println("  lsl x1, x1, #8");
        println("  ldrb w14, [x15, #%d]", i);
        println("  orr x1, x1, x14");
      }
    }
  }
}

static void copy_struct_mem(void) {
  Type *ty = current_fn->ty->return_ty;
  Obj *var = current_fn->params;

  addr_from_fp("x15", var->offset);
  println("  ldr x1, [x15]");
  copy_bytes("x1", "x0", ty->size);
}

static void builtin_alloca(void) {
  println("  add x0, x0, #15");
  println("  and x0, x0, #-16");
  addr_from_fp("x15", current_fn->alloca_bottom->offset);
  println("  ldr x1, [x15]");
  println("  sub x2, x1, sp");
  println("  mov x3, sp");
  println("  sub sp, sp, x0");
  println("  mov x4, sp");
  println("1:");
  println("  cmp x2, #0");
  println("  beq 2f");
  println("  ldrb w5, [x3]");
  println("  strb w5, [x4]");
  println("  add x3, x3, #1");
  println("  add x4, x4, #1");
  println("  sub x2, x2, #1");
  println("  b 1b");
  println("2:");
  println("  ldr x1, [x15]");
  println("  sub x1, x1, x0");
  println("  str x1, [x15]");
  println("  mov x0, sp");
}

static void load_numeric_literal(Node *node) {
  switch (node->ty->kind) {
  case TY_FLOAT: {
    union { float f32; uint32_t u32; } u = { node->fval };
    mov_imm("x0", u.u32);
    println("  fmov s0, w0");
    return;
  }
  case TY_DOUBLE:
  case TY_LDOUBLE: {  // On ARM64, long double is the same as double
    union { double f64; uint64_t u64; } u = { node->fval };
    mov_imm("x0", u.u64);
    println("  fmov d0, x0");
    return;
  }
  default:
    mov_imm("x0", node->val);
    return;
  }
}

static void emit_zero_local(Obj *var) {
  addr_from_fp("x15", var->offset);
  for (int i = 0; i < var->ty->size; i++) {
    addr_from_reg("x14", "x15", i);
    println("  strb wzr, [x14]");
  }
}

static void gen_expr(Node *node) {
  println("  .loc %d %d", node->tok->file->file_no, node->tok->line_no);

  switch (node->kind) {
  case ND_NULL_EXPR:
    return;
  case ND_NUM:
    load_numeric_literal(node);
    return;
  case ND_NEG:
    gen_expr(node->lhs);
    switch (node->ty->kind) {
    case TY_FLOAT:
      println("  fneg s0, s0");
      return;
    case TY_DOUBLE:
    case TY_LDOUBLE:  // On ARM64, long double is the same as double
      println("  fneg d0, d0");
      return;
    default:
      println("  neg x0, x0");
      truncate_to(node->ty);
      return;
    }
  case ND_VAR:
    gen_addr(node);
    load(node->ty);
    return;
  case ND_MEMBER:
    gen_addr(node);
    load(node->ty);
    if (node->member->is_bitfield) {
      if (node->member->ty->is_unsigned)
        println("  ubfx x0, x0, #%d, #%d", node->member->bit_offset, node->member->bit_width);
      else
        println("  sbfx x0, x0, #%d, #%d", node->member->bit_offset, node->member->bit_width);
    }
    return;
  case ND_DEREF:
    gen_expr(node->lhs);
    load(node->ty);
    return;
  case ND_ADDR:
    gen_addr(node->lhs);
    return;
  case ND_ASSIGN:
    gen_addr(node->lhs);
    push();
    gen_expr(node->rhs);

    if (node->lhs->kind == ND_MEMBER && node->lhs->member->is_bitfield) {
      Member *mem = node->lhs->member;
      println("  mov x2, x0");
      pop("x1");
      println("  mov x0, x1");
      load(mem->ty);
      println("  bfi x0, x2, #%d, #%d", mem->bit_offset, mem->bit_width);
      store_int_to("x1", mem->ty->size);
      println("  mov x0, x2");
      truncate_to(node->ty);
      return;
    }

    store(node->ty);
    return;
  case ND_STMT_EXPR:
    for (Node *n = node->body; n; n = n->next)
      gen_stmt(n);
    return;
  case ND_COMMA:
    gen_expr(node->lhs);
    gen_expr(node->rhs);
    return;
  case ND_CAST:
    gen_expr(node->lhs);
    cast(node->lhs->ty, node->ty);
    return;
  case ND_MEMZERO:
    emit_zero_local(node->var);
    return;
  case ND_COND: {
    int c = count();
    gen_expr(node->cond);
    cmp_zero(node->cond->ty);
    println("  beq .L.else.%d", c);
    gen_expr(node->then);
    println("  b .L.end.%d", c);
    println(".L.else.%d:", c);
    gen_expr(node->els);
    println(".L.end.%d:", c);
    return;
  }
  case ND_NOT:
    gen_expr(node->lhs);
    cmp_zero(node->lhs->ty);
    println("  cset w0, eq");
    return;
  case ND_BITNOT:
    gen_expr(node->lhs);
    println("  mvn x0, x0");
    truncate_to(node->ty);
    return;
  case ND_LOGAND: {
    int c = count();
    gen_expr(node->lhs);
    cmp_zero(node->lhs->ty);
    println("  beq .L.false.%d", c);
    gen_expr(node->rhs);
    cmp_zero(node->rhs->ty);
    println("  beq .L.false.%d", c);
    println("  mov w0, #1");
    println("  b .L.end.%d", c);
    println(".L.false.%d:", c);
    println("  mov w0, wzr");
    println(".L.end.%d:", c);
    return;
  }
  case ND_LOGOR: {
    int c = count();
    gen_expr(node->lhs);
    cmp_zero(node->lhs->ty);
    println("  bne .L.true.%d", c);
    gen_expr(node->rhs);
    cmp_zero(node->rhs->ty);
    println("  bne .L.true.%d", c);
    println("  mov w0, wzr");
    println("  b .L.end.%d", c);
    println(".L.true.%d:", c);
    println("  mov w0, #1");
    println(".L.end.%d:", c);
    return;
  }
  case ND_FUNCALL: {
    if (node->lhs->kind == ND_VAR && !strcmp(node->lhs->var->name, "alloca")) {
      gen_expr(node->args);
      builtin_alloca();
      return;
    }

    int stack_area = push_args(node);
    gen_expr(node->lhs);

    int gp = 0;
    int fp = 0;

    if (node->ret_buffer && node->ty->size > 16)
      pop(argreg64[gp++]);

    for (Node *arg = node->args; arg; arg = arg->next) {
      if (arg->pass_by_stack)
        continue;

      Type *ty = arg->ty;
      switch (ty->kind) {
      case TY_STRUCT:
      case TY_UNION:
        load_struct_from_addr(ty, &gp, &fp);
        break;
      case TY_FLOAT:
      case TY_DOUBLE:
      case TY_LDOUBLE:  // On ARM64, long double is the same as double
        popf(fp++);
        break;
      default:
        pop(argreg64[gp++]);
        break;
      }
    }

    println("  mov x16, x0");
    println("  blr x16");

    if (stack_area) {
      println("  add sp, sp, #%d", stack_area);
      depth -= stack_area;
    }

    truncate_to(node->ty);

    if (node->ret_buffer && node->ty->size <= 16) {
      copy_ret_buffer(node->ret_buffer);
      addr_from_fp("x0", node->ret_buffer->offset);
    }
    return;
  }
  case ND_LABEL_VAL:
    println("  adr x0, %s", node->unique_label);
    return;
  case ND_CAS:
    error_tok(node->tok, "ARM64 backend does not support atomic compare-and-swap yet");
  case ND_EXCH:
    error_tok(node->tok, "ARM64 backend does not support atomic exchange yet");
  }

  switch (node->lhs->ty->kind) {
  case TY_FLOAT:
  case TY_DOUBLE:
  case TY_LDOUBLE: {  // On ARM64, long double is the same as double
    bool is_double = node->lhs->ty->kind != TY_FLOAT;

    gen_expr(node->rhs);
    pushf();
    gen_expr(node->lhs);
    popf(1);

    switch (node->kind) {
    case ND_ADD:
      println("  fadd %c0, %c0, %c1", is_double ? 'd' : 's', is_double ? 'd' : 's', is_double ? 'd' : 's');
      return;
    case ND_SUB:
      println("  fsub %c0, %c0, %c1", is_double ? 'd' : 's', is_double ? 'd' : 's', is_double ? 'd' : 's');
      return;
    case ND_MUL:
      println("  fmul %c0, %c0, %c1", is_double ? 'd' : 's', is_double ? 'd' : 's', is_double ? 'd' : 's');
      return;
    case ND_DIV:
      println("  fdiv %c0, %c0, %c1", is_double ? 'd' : 's', is_double ? 'd' : 's', is_double ? 'd' : 's');
      return;
    case ND_EQ:
    case ND_NE:
    case ND_LT:
    case ND_LE:
      println("  fcmp %c0, %c1", is_double ? 'd' : 's', is_double ? 'd' : 's');

      if (node->kind == ND_EQ) {
        println("  cset w0, eq");
      } else if (node->kind == ND_NE) {
        println("  cset w0, ne");
        println("  cset w1, vs");
        println("  orr w0, w0, w1");
      } else if (node->kind == ND_LT) {
        println("  cset w0, lt");
      } else {
        println("  cset w0, le");
      }
      return;
    }
    error_tok(node->tok, "invalid expression");
  }

  gen_expr(node->rhs);
  push();
  gen_expr(node->lhs);
  pop("x1");

    char *ax = (node->lhs->ty->kind == TY_LONG || node->lhs->ty->base) ? "x0" : "w0";
    char *di = (node->lhs->ty->kind == TY_LONG || node->lhs->ty->base) ? "x1" : "w1";
    char *tmp = (node->lhs->ty->kind == TY_LONG || node->lhs->ty->base) ? "x2" : "w2";

    switch (node->kind) {
    case ND_ADD:
      println("  add %s, %s, %s", ax, ax, di);
      truncate_to(node->ty);
      return;
    case ND_SUB:
      println("  sub %s, %s, %s", ax, ax, di);
      truncate_to(node->ty);
      return;
    case ND_MUL:
      println("  mul %s, %s, %s", ax, ax, di);
      truncate_to(node->ty);
      return;
    case ND_DIV:
      println("  %sdiv %s, %s, %s", node->ty->is_unsigned ? "u" : "s", ax, ax, di);
      truncate_to(node->ty);
      return;
    case ND_MOD:
      println("  %sdiv %s, %s, %s", node->ty->is_unsigned ? "u" : "s", tmp, ax, di);
      println("  msub %s, %s, %s, %s", ax, tmp, di, ax);
      truncate_to(node->ty);
      return;
    case ND_BITAND:
      println("  and %s, %s, %s", ax, ax, di);
      truncate_to(node->ty);
      return;
    case ND_BITOR:
      println("  orr %s, %s, %s", ax, ax, di);
      truncate_to(node->ty);
      return;
    case ND_BITXOR:
      println("  eor %s, %s, %s", ax, ax, di);
      truncate_to(node->ty);
      return;
    case ND_EQ:
    case ND_NE:
    case ND_LT:
    case ND_LE:
      println("  cmp %s, %s", ax, di);
      if (node->kind == ND_EQ)
        println("  cset w0, eq");
      else if (node->kind == ND_NE)
        println("  cset w0, ne");
      else if (node->kind == ND_LT)
        println("  cset w0, %s", node->lhs->ty->is_unsigned ? "lo" : "lt");
      else
        println("  cset w0, %s", node->lhs->ty->is_unsigned ? "ls" : "le");
      return;
    case ND_SHL:
      println("  lsl %s, %s, %s", ax, ax, di);
      truncate_to(node->ty);
      return;
    case ND_SHR:
      println("  %s %s, %s, %s", node->lhs->ty->is_unsigned ? "lsr" : "asr", ax, ax, di);
      truncate_to(node->ty);
      return;
    }

    error_tok(node->tok, "invalid expression");
  }
}

static void compare_case_value(char *reg, long value) {
  mov_imm("x15", value);
  println("  cmp %s, %s", reg, reg[0] == 'x' ? "x15" : "w15");
}

static void gen_stmt(Node *node) {
  println("  .loc %d %d", node->tok->file->file_no, node->tok->line_no);

  switch (node->kind) {
  case ND_IF: {
    int c = count();
    gen_expr(node->cond);
    cmp_zero(node->cond->ty);
    println("  beq .L.else.%d", c);
    gen_stmt(node->then);
    println("  b .L.end.%d", c);
    println(".L.else.%d:", c);
    if (node->els)
      gen_stmt(node->els);
    println(".L.end.%d:", c);
    return;
  }
  case ND_FOR: {
    int c = count();
    if (node->init)
      gen_stmt(node->init);
    println(".L.begin.%d:", c);
    if (node->cond) {
      gen_expr(node->cond);
      cmp_zero(node->cond->ty);
      println("  beq %s", node->brk_label);
    }
    gen_stmt(node->then);
    println("%s:", node->cont_label);
    if (node->inc)
      gen_expr(node->inc);
    println("  b .L.begin.%d", c);
    println("%s:", node->brk_label);
    return;
  }
  case ND_DO: {
    int c = count();
    println(".L.begin.%d:", c);
    gen_stmt(node->then);
    println("%s:", node->cont_label);
    gen_expr(node->cond);
    cmp_zero(node->cond->ty);
    println("  bne .L.begin.%d", c);
    println("%s:", node->brk_label);
    return;
  }
  case ND_SWITCH:
    gen_expr(node->cond);
    for (Node *n = node->case_next; n; n = n->case_next) {
      char *ax = node->cond->ty->size == 8 ? "x0" : "w0";
      char *di = node->cond->ty->size == 8 ? "x1" : "w1";

      if (n->begin == n->end) {
        compare_case_value(ax, n->begin);
        println("  beq %s", n->label);
        continue;
      }

      mov_imm("x15", n->begin);
      println("  sub %s, %s, %s", di, ax, ax[0] == 'x' ? "x15" : "w15");
      compare_case_value(di, n->end - n->begin);
      println("  bls %s", n->label);
    }

    if (node->default_case)
      println("  b %s", node->default_case->label);

    println("  b %s", node->brk_label);
    gen_stmt(node->then);
    println("%s:", node->brk_label);
    return;
  case ND_CASE:
    println("%s:", node->label);
    gen_stmt(node->lhs);
    return;
  case ND_BLOCK:
    for (Node *n = node->body; n; n = n->next)
      gen_stmt(n);
    return;
  case ND_GOTO:
    println("  b %s", node->unique_label);
    return;
  case ND_GOTO_EXPR:
    gen_expr(node->lhs);
    println("  br x0");
    return;
  case ND_LABEL:
    println("%s:", node->unique_label);
    gen_stmt(node->lhs);
    return;
  case ND_RETURN:
    if (node->lhs) {
      gen_expr(node->lhs);
      switch (node->lhs->ty->kind) {
      case TY_STRUCT:
      case TY_UNION:
        if (node->lhs->ty->size <= 16)
          copy_struct_reg();
        else
          copy_struct_mem();
        break;
      default:
        break;
      }
    }
    println("  b .L.return.%s", current_fn->name);
    return;
  case ND_EXPR_STMT:
    gen_expr(node->lhs);
    return;
  case ND_ASM:
    println("  %s", node->asm_str);
    return;
  }

  error_tok(node->tok, "invalid statement");
}

static void assign_lvar_offsets(Obj *prog) {
  for (Obj *fn = prog; fn; fn = fn->next) {
    if (!fn->is_function)
      continue;

    int top = 16;
    int bottom = 0;
    int gp = 0;
    int fp = 0;

    for (Obj *var = fn->params; var; var = var->next) {
      Type *ty = var->ty;

      switch (ty->kind) {
      case TY_STRUCT:
      case TY_UNION:
        if (ty->size <= 16 &&
            fp + has_flonum1(ty) + has_flonum2(ty) <= FP_MAX &&
            gp + !has_flonum1(ty) + !has_flonum2(ty) <= GP_MAX) {
          fp += has_flonum1(ty) + has_flonum2(ty);
          gp += !has_flonum1(ty) + !has_flonum2(ty);
          continue;
        }
        break;
      case TY_FLOAT:
      case TY_DOUBLE:
        if (fp++ < FP_MAX)
          continue;
        break;
      case TY_LDOUBLE:
        break;
      default:
        if (gp++ < GP_MAX)
          continue;
      }

      top = align_to(top, 8);
      var->offset = top;
      top += MAX(ty->size, 8);
    }

    for (Obj *var = fn->locals; var; var = var->next) {
      if (var->offset)
        continue;

      int align = (var->ty->kind == TY_ARRAY && var->ty->size >= 16)
        ? MAX(16, var->align) : var->align;

      bottom += var->ty->size;
      bottom = align_to(bottom, align);
      var->offset = -bottom;
    }

    fn->stack_size = align_to(bottom, 16);
  }
}

static void emit_data(Obj *prog) {
  for (Obj *var = prog; var; var = var->next) {
    if (var->is_function || !var->is_definition)
      continue;

    if (var->is_static)
      println("  .local %s", var->name);
    else
      println("  .globl %s", var->name);

    int align = (var->ty->kind == TY_ARRAY && var->ty->size >= 16)
      ? MAX(16, var->align) : var->align;

    if (opt_fcommon && var->is_tentative) {
      println("  .comm %s, %d, %d", var->name, var->ty->size, align);
      continue;
    }

    if (var->init_data) {
      if (var->is_tls)
        error("ARM64 backend does not support TLS data yet");

      println("  .data");
      println("  .type %s, %%object", var->name);
      println("  .size %s, %d", var->name, var->ty->size);
      println("  .balign %d", align);
      println("%s:", var->name);

      Relocation *rel = var->rel;
      int pos = 0;
      while (pos < var->ty->size) {
        if (rel && rel->offset == pos) {
          println("  .xword %s%+ld", *rel->label, rel->addend);
          rel = rel->next;
          pos += 8;
        } else {
          println("  .byte %d", var->init_data[pos++]);
        }
      }
      continue;
    }

    println("  .bss");
    println("  .balign %d", align);
    println("%s:", var->name);
    println("  .zero %d", var->ty->size);
  }
}

static void store_fp(int r, int offset, int sz) {
  addr_from_fp("x15", offset);
  if (sz == 4)
    println("  str s%d, [x15]", r);
  else
    println("  str d%d, [x15]", r);
}

static void store_gp(int r, int offset, int sz) {
  addr_from_fp("x15", offset);

  switch (sz) {
  case 1:
    println("  strb %s, [x15]", argreg32[r]);
    return;
  case 2:
    println("  strh %s, [x15]", argreg32[r]);
    return;
  case 4:
    println("  str %s, [x15]", argreg32[r]);
    return;
  case 8:
    println("  str %s, [x15]", argreg64[r]);
    return;
  default:
    println("  mov x14, %s", argreg64[r]);
    for (int i = 0; i < sz; i++) {
      println("  strb w14, [x15, #%d]", i);
      println("  lsr x14, x14, #8");
    }
    return;
  }
}

static void emit_text(Obj *prog) {
  for (Obj *fn = prog; fn; fn = fn->next) {
    if (!fn->is_function || !fn->is_definition)
      continue;
    if (!fn->is_live)
      continue;

    if (fn->va_area)
      error_tok(fn->tok, "ARM64 backend does not support variadic functions yet");

    if (fn->is_static)
      println("  .local %s", fn->name);
    else
      println("  .globl %s", fn->name);

    println("  .text");
    println("  .type %s, %%function", fn->name);
    println("%s:", fn->name);
    current_fn = fn;

    println("  stp x29, x30, [sp, #-16]!");
    println("  mov x29, sp");
    if (fn->stack_size < 4096) {
      if (fn->stack_size)
        println("  sub sp, sp, #%d", fn->stack_size);
    } else {
      mov_imm("x15", fn->stack_size);
      println("  sub sp, sp, x15");
    }
    addr_from_fp("x15", fn->alloca_bottom->offset);
    println("  str sp, [x15]");

    int gp = 0;
    int fp = 0;
    for (Obj *var = fn->params; var; var = var->next) {
      if (var->offset > 0)
        continue;

      Type *ty = var->ty;
      switch (ty->kind) {
      case TY_STRUCT:
      case TY_UNION:
        if (has_flonum1(ty))
          store_fp(fp++, var->offset, MIN(8, ty->size));
        else
          store_gp(gp++, var->offset, MIN(8, ty->size));

        if (ty->size > 8) {
          if (has_flonum2(ty))
            store_fp(fp++, var->offset + 8, ty->size - 8);
          else
            store_gp(gp++, var->offset + 8, ty->size - 8);
        }
        break;
      case TY_FLOAT:
      case TY_DOUBLE:
        store_fp(fp++, var->offset, ty->size);
        break;
      case TY_LDOUBLE:
        error_tok(var->tok, "ARM64 backend does not support long double parameters yet");
      default:
        store_gp(gp++, var->offset, ty->size);
        break;
      }
    }

    gen_stmt(fn->body);
    assert(depth == 0);

    if (!strcmp(fn->name, "main"))
      println("  mov w0, wzr");

    println(".L.return.%s:", fn->name);
    println("  mov sp, x29");
    println("  ldp x29, x30, [sp], #16");
    println("  ret");
  }
}

void codegen_arm64(Obj *prog, FILE *out) {
  output_file = out;

  File **files = get_input_files();
  for (int i = 0; files[i]; i++)
    println("  .file %d \"%s\"", files[i]->file_no, files[i]->name);

  assign_lvar_offsets(prog);
  emit_data(prog);
  emit_text(prog);
}