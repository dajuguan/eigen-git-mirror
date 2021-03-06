// This file is part of Eigen, a lightweight C++ template library
// for linear algebra.
//
// Copyright (C) 2014 Pedro Gonnet (pedro.gonnet@gmail.com)
//
// This Source Code Form is subject to the terms of the Mozilla
// Public License v. 2.0. If a copy of the MPL was not distributed
// with this file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef EIGEN_MATH_FUNCTIONS_AVX_H
#define EIGEN_MATH_FUNCTIONS_AVX_H

/* The sin and cos functions of this file are loosely derived from
 * Julien Pommier's sse math library: http://gruntthepeon.free.fr/ssemath/
 */

namespace Eigen {

namespace internal {

inline Packet8i pshiftleft(Packet8i v, int n)
{
#ifdef EIGEN_VECTORIZE_AVX2
  return _mm256_slli_epi32(v, n);
#else
  __m128i lo = _mm_slli_epi32(_mm256_extractf128_si256(v, 0), n);
  __m128i hi = _mm_slli_epi32(_mm256_extractf128_si256(v, 1), n);
  return _mm256_insertf128_si256(_mm256_castsi128_si256(lo), (hi), 1);
#endif
}

// Sine function
// Computes sin(x) by wrapping x to the interval [-Pi/4,3*Pi/4] and
// evaluating interpolants in [-Pi/4,Pi/4] or [Pi/4,3*Pi/4]. The interpolants
// are (anti-)symmetric and thus have only odd/even coefficients
template <>
EIGEN_DEFINE_FUNCTION_ALLOWING_MULTIPLE_DEFINITIONS EIGEN_UNUSED Packet8f
psin<Packet8f>(const Packet8f& _x) {
  Packet8f x = _x;

  // Some useful values.
  _EIGEN_DECLARE_CONST_Packet8i(one, 1);
  _EIGEN_DECLARE_CONST_Packet8f(one, 1.0f);
  _EIGEN_DECLARE_CONST_Packet8f(two, 2.0f);
  _EIGEN_DECLARE_CONST_Packet8f(one_over_four, 0.25f);
  _EIGEN_DECLARE_CONST_Packet8f(one_over_pi, 3.183098861837907e-01f);
  _EIGEN_DECLARE_CONST_Packet8f(neg_pi_first, -3.140625000000000e+00f);
  _EIGEN_DECLARE_CONST_Packet8f(neg_pi_second, -9.670257568359375e-04f);
  _EIGEN_DECLARE_CONST_Packet8f(neg_pi_third, -6.278329571784980e-07f);
  _EIGEN_DECLARE_CONST_Packet8f(four_over_pi, 1.273239544735163e+00f);

  // Map x from [-Pi/4,3*Pi/4] to z in [-1,3] and subtract the shifted period.
  Packet8f z = pmul(x, p8f_one_over_pi);
  Packet8f shift = _mm256_floor_ps(padd(z, p8f_one_over_four));
  x = pmadd(shift, p8f_neg_pi_first, x);
  x = pmadd(shift, p8f_neg_pi_second, x);
  x = pmadd(shift, p8f_neg_pi_third, x);
  z = pmul(x, p8f_four_over_pi);

  // Make a mask for the entries that need flipping, i.e. wherever the shift
  // is odd.
  Packet8i shift_ints = _mm256_cvtps_epi32(shift);
  Packet8i shift_isodd = _mm256_castps_si256(_mm256_and_ps(_mm256_castsi256_ps(shift_ints), _mm256_castsi256_ps(p8i_one)));
  Packet8i sign_flip_mask = pshiftleft(shift_isodd, 31);

  // Create a mask for which interpolant to use, i.e. if z > 1, then the mask
  // is set to ones for that entry.
  Packet8f ival_mask = _mm256_cmp_ps(z, p8f_one, _CMP_GT_OQ);

  // Evaluate the polynomial for the interval [1,3] in z.
  _EIGEN_DECLARE_CONST_Packet8f(coeff_right_0, 9.999999724233232e-01f);
  _EIGEN_DECLARE_CONST_Packet8f(coeff_right_2, -3.084242535619928e-01f);
  _EIGEN_DECLARE_CONST_Packet8f(coeff_right_4, 1.584991525700324e-02f);
  _EIGEN_DECLARE_CONST_Packet8f(coeff_right_6, -3.188805084631342e-04f);
  Packet8f z_minus_two = psub(z, p8f_two);
  Packet8f z_minus_two2 = pmul(z_minus_two, z_minus_two);
  Packet8f right = pmadd(p8f_coeff_right_6, z_minus_two2, p8f_coeff_right_4);
  right = pmadd(right, z_minus_two2, p8f_coeff_right_2);
  right = pmadd(right, z_minus_two2, p8f_coeff_right_0);

  // Evaluate the polynomial for the interval [-1,1] in z.
  _EIGEN_DECLARE_CONST_Packet8f(coeff_left_1, 7.853981525427295e-01f);
  _EIGEN_DECLARE_CONST_Packet8f(coeff_left_3, -8.074536727092352e-02f);
  _EIGEN_DECLARE_CONST_Packet8f(coeff_left_5, 2.489871967827018e-03f);
  _EIGEN_DECLARE_CONST_Packet8f(coeff_left_7, -3.587725841214251e-05f);
  Packet8f z2 = pmul(z, z);
  Packet8f left = pmadd(p8f_coeff_left_7, z2, p8f_coeff_left_5);
  left = pmadd(left, z2, p8f_coeff_left_3);
  left = pmadd(left, z2, p8f_coeff_left_1);
  left = pmul(left, z);

  // Assemble the results, i.e. select the left and right polynomials.
  left = _mm256_andnot_ps(ival_mask, left);
  right = _mm256_and_ps(ival_mask, right);
  Packet8f res = _mm256_or_ps(left, right);

  // Flip the sign on the odd intervals and return the result.
  res = _mm256_xor_ps(res, _mm256_castsi256_ps(sign_flip_mask));
  return res;
}

template <>
EIGEN_DEFINE_FUNCTION_ALLOWING_MULTIPLE_DEFINITIONS EIGEN_UNUSED Packet8f
plog<Packet8f>(const Packet8f& _x) {
  return plog_float(_x);
}

// Exponential function. Works by writing "x = m*log(2) + r" where
// "m = floor(x/log(2)+1/2)" and "r" is the remainder. The result is then
// "exp(x) = 2^m*exp(r)" where exp(r) is in the range [-1,1).
template <>
EIGEN_DEFINE_FUNCTION_ALLOWING_MULTIPLE_DEFINITIONS EIGEN_UNUSED Packet8f
pexp<Packet8f>(const Packet8f& _x) {
  return pexp_float(_x);
}

// Hyperbolic Tangent function.
template <>
EIGEN_DEFINE_FUNCTION_ALLOWING_MULTIPLE_DEFINITIONS EIGEN_UNUSED Packet8f
ptanh<Packet8f>(const Packet8f& x) {
  return internal::generic_fast_tanh_float(x);
}

template <>
EIGEN_DEFINE_FUNCTION_ALLOWING_MULTIPLE_DEFINITIONS EIGEN_UNUSED Packet4d
pexp<Packet4d>(const Packet4d& x) {
  return pexp_double(x);
}

// Functions for sqrt.
// The EIGEN_FAST_MATH version uses the _mm_rsqrt_ps approximation and one step
// of Newton's method, at a cost of 1-2 bits of precision as opposed to the
// exact solution. It does not handle +inf, or denormalized numbers correctly.
// The main advantage of this approach is not just speed, but also the fact that
// it can be inlined and pipelined with other computations, further reducing its
// effective latency. This is similar to Quake3's fast inverse square root.
// For detail see here: http://www.beyond3d.com/content/articles/8/
#if EIGEN_FAST_MATH
template <>
EIGEN_DEFINE_FUNCTION_ALLOWING_MULTIPLE_DEFINITIONS EIGEN_UNUSED Packet8f
psqrt<Packet8f>(const Packet8f& _x) {
  Packet8f half = pmul(_x, pset1<Packet8f>(.5f));
  Packet8f denormal_mask = _mm256_and_ps(
      _mm256_cmp_ps(_x, pset1<Packet8f>((std::numeric_limits<float>::min)()),
                    _CMP_LT_OQ),
      _mm256_cmp_ps(_x, _mm256_setzero_ps(), _CMP_GE_OQ));

  // Compute approximate reciprocal sqrt.
  Packet8f x = _mm256_rsqrt_ps(_x);
  // Do a single step of Newton's iteration.
  x = pmul(x, psub(pset1<Packet8f>(1.5f), pmul(half, pmul(x,x))));
  // Flush results for denormals to zero.
  return _mm256_andnot_ps(denormal_mask, pmul(_x,x));
}
#else
template <> EIGEN_DEFINE_FUNCTION_ALLOWING_MULTIPLE_DEFINITIONS EIGEN_UNUSED
Packet8f psqrt<Packet8f>(const Packet8f& x) {
  return _mm256_sqrt_ps(x);
}
#endif
template <> EIGEN_DEFINE_FUNCTION_ALLOWING_MULTIPLE_DEFINITIONS EIGEN_UNUSED
Packet4d psqrt<Packet4d>(const Packet4d& x) {
  return _mm256_sqrt_pd(x);
}
#if EIGEN_FAST_MATH

template<> EIGEN_DEFINE_FUNCTION_ALLOWING_MULTIPLE_DEFINITIONS EIGEN_UNUSED
Packet8f prsqrt<Packet8f>(const Packet8f& _x) {
  _EIGEN_DECLARE_CONST_Packet8f_FROM_INT(inf, 0x7f800000);
  _EIGEN_DECLARE_CONST_Packet8f_FROM_INT(nan, 0x7fc00000);
  _EIGEN_DECLARE_CONST_Packet8f(one_point_five, 1.5f);
  _EIGEN_DECLARE_CONST_Packet8f(minus_half, -0.5f);
  _EIGEN_DECLARE_CONST_Packet8f_FROM_INT(flt_min, 0x00800000);

  Packet8f neg_half = pmul(_x, p8f_minus_half);

  // select only the inverse sqrt of positive normal inputs (denormals are
  // flushed to zero and cause infs as well).
  Packet8f le_zero_mask = _mm256_cmp_ps(_x, p8f_flt_min, _CMP_LT_OQ);
  Packet8f x = _mm256_andnot_ps(le_zero_mask, _mm256_rsqrt_ps(_x));

  // Fill in NaNs and Infs for the negative/zero entries.
  Packet8f neg_mask = _mm256_cmp_ps(_x, _mm256_setzero_ps(), _CMP_LT_OQ);
  Packet8f zero_mask = _mm256_andnot_ps(neg_mask, le_zero_mask);
  Packet8f infs_and_nans = _mm256_or_ps(_mm256_and_ps(neg_mask, p8f_nan),
                                        _mm256_and_ps(zero_mask, p8f_inf));

  // Do a single step of Newton's iteration.
  x = pmul(x, pmadd(neg_half, pmul(x, x), p8f_one_point_five));

  // Insert NaNs and Infs in all the right places.
  return _mm256_or_ps(x, infs_and_nans);
}

#else
template <> EIGEN_DEFINE_FUNCTION_ALLOWING_MULTIPLE_DEFINITIONS EIGEN_UNUSED
Packet8f prsqrt<Packet8f>(const Packet8f& x) {
  _EIGEN_DECLARE_CONST_Packet8f(one, 1.0f);
  return _mm256_div_ps(p8f_one, _mm256_sqrt_ps(x));
}
#endif

template <> EIGEN_DEFINE_FUNCTION_ALLOWING_MULTIPLE_DEFINITIONS EIGEN_UNUSED
Packet4d prsqrt<Packet4d>(const Packet4d& x) {
  _EIGEN_DECLARE_CONST_Packet4d(one, 1.0);
  return _mm256_div_pd(p4d_one, _mm256_sqrt_pd(x));
}


}  // end namespace internal

}  // end namespace Eigen

#endif  // EIGEN_MATH_FUNCTIONS_AVX_H
