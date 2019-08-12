#ifndef _MACROS_H_
#define _MACROS_H_

// --------------------
//    マクロ集
// --------------------

// --- enum用のマクロ

// enumに対してrange forで回せるようにするためのhack(速度低下があるかも知れないので速度の要求されるところでは使わないこと)
#define ENABLE_RANGE_OPERATORS_ON(X,ZERO,NB)     \
  inline X operator*(X x) { return x; }          \
  inline X begin(X) { return ZERO; }             \
  inline X end(X) { return NB; }

ENABLE_RANGE_OPERATORS_ON(Color, COLOR_ZERO, COLOR_NB)

#define COLOR Color()

#endif
