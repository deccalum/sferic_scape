#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace sferic {
namespace analysis {

// Standard <random> family; the fitting stage selects the best match by
// Kolmogorov-Smirnov p-value after maximum-likelihood estimation of params.
enum class DistributionKind {
  // Uniform
  UniformReal,       // params: [a, b]
  UniformInt,        // params: [a, b]
  // Bernoulli / counting
  Bernoulli,         // params: [p]
  Binomial,          // params: [n, p]
  NegativeBinomial,  // params: [k, p]
  Geometric,         // params: [p]
  // Poisson family
  Poisson,           // params: [mean]
  Exponential,       // params: [lambda]
  Gamma,             // params: [alpha, beta]     — shape, scale
  Weibull,           // params: [a, b]            — shape, scale
  ExtremeValue,      // params: [a, b]            — location, scale (Gumbel I)
  // Normal family
  Normal,            // params: [mean, stddev]
  Lognormal,         // params: [mean, stddev]    — of log(x)
  ChiSquared,        // params: [n]               — degrees of freedom
  Cauchy,            // params: [a, b]            — location, scale; heavy tails
  FisherF,           // params: [m, n]            — degrees of freedom
  StudentT,          // params: [n]               — degrees of freedom
  // Sampling / arbitrary
  Discrete,          //            weights vector — integer outcomes with given weights
  PiecewiseConstant, //            weights vector — histogram approximation
  PiecewiseLinear,   //            weights vector — trapezoid approximation;
                     //            params[0] used as an auxiliary scalar (e.g. amplitude scale)
};

// Result of fit_distribution(): the winning distribution family with its
// estimated parameters and a KS goodness-of-fit score (higher = better fit).
struct DistributionFit {
  DistributionKind kind = DistributionKind::Normal;
  std::array<double, 4> params{};  // semantics per DistributionKind above
  std::vector<double> weights;     // non-empty only for Discrete / Piecewise kinds
  double ks_p_value = 0.0;         // KS goodness-of-fit p-value — higher = better fit
  size_t sample_count = 0;         // observations used; 0 = fit was skipped
};

// Fits every applicable distribution and returns the winner by KS p-value.
// Requires at least 4 samples (IQR quantile estimation). Returns a default
// DistributionFit with sample_count == 0 for shorter inputs.
DistributionFit fit_distribution(std::span<const double> samples);

}  // namespace analysis
}  // namespace sferic
