#ifndef __STDARG_H
#define __STDARG_H

typedef struct {
  unsigned int gp_offset;
  unsigned int fp_offset;
  void *overflow_arg_area;
  void *reg_save_area;
} __va_elem;

typedef __va_elem va_list[1];

#ifdef __aarch64__
#define __VA_GP_MAX 64
#define __VA_FP_MAX 128
#else
#define __VA_GP_MAX 48
#define __VA_FP_MAX 112
#endif

#define va_start(ap, last) \
  do { *(ap) = *(__va_elem *)__va_area__; } while (0)

#define va_end(ap)

static void *__va_arg_mem(__va_elem *ap, int sz, int align) {
  unsigned long p = (unsigned long)ap->overflow_arg_area;
  if (align > 8)
    p = (p + 15) / 16 * 16;

  ap->overflow_arg_area = (void *)((p + sz + 7) / 8 * 8);
  return (void *)p;
}

static void *__va_arg_gp(__va_elem *ap, int sz, int align) {
  if (ap->gp_offset >= __VA_GP_MAX)
    return __va_arg_mem(ap, sz, align);

  char *r = (char *)ap->reg_save_area + ap->gp_offset;
  ap->gp_offset += 8;
  return r;
}

static void *__va_arg_fp(__va_elem *ap, int sz, int align) {
  if (ap->fp_offset >= __VA_FP_MAX)
    return __va_arg_mem(ap, sz, align);

  char *r = (char *)ap->reg_save_area + ap->fp_offset;
  ap->fp_offset += 8;
  return r;
}

#define va_arg(ap, ty)                                                  \
  ({                                                                    \
    int klass = __builtin_reg_class(ty);                                \
    *(ty *)(klass == 0 ? __va_arg_gp(ap, sizeof(ty), _Alignof(ty)) :    \
            klass == 1 ? __va_arg_fp(ap, sizeof(ty), _Alignof(ty)) :    \
            __va_arg_mem(ap, sizeof(ty), _Alignof(ty)));                \
  })

#define va_copy(dest, src) ((dest)[0] = (src)[0])

#define __GNUC_VA_LIST 1
typedef va_list __gnuc_va_list;

#endif
