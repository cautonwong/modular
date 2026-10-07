#ifndef UTILS_MATH_H_
#define UTILS_MATH_H_

/*
 * The reference's vendored Fusion includes the host firmware's utils_math.h, and uses one thing from
 * it: SQ. This is that definition, copied from util/utils_math.h:61 - double evaluation and all, which
 * is the reference's own choice of macro - so that the library beside it compiles as the reference
 * compiles it, without this repository's maths header having to grow a dependency on a vendor's.
 */

#define SQ(x) ((x) * (x))

#endif /* UTILS_MATH_H_ */
