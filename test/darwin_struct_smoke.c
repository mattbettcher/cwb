typedef struct {
  int a;
  int b;
} Pair;

static int add_pair(Pair p) {
  return p.a + p.b;
}

static Pair make_pair(void) {
  Pair p = {7, 8};
  return p;
}

int main(void) {
  Pair p = {4, 5};
  if (add_pair(p) != 9)
    return 1;

  Pair q = make_pair();
  if (q.a != 7)
    return 2;
  if (q.b != 8)
    return 3;

  return 0;
}
