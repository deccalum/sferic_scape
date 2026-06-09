#include "analysis/distribution_fitter.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <numeric>
#include <string>
#include <vector>

#include "core/logger.h"

namespace sferic {
namespace analysis {

// Regularised lower incomplete gamma P(a, x).
// Series expansion for x < a+1; Lentz continued fraction for x >= a+1.
// Accurate to ~1e-10; converges in O(a) iterations.  (Numerical Recipes §6.2)
static double gamma_p(double a, double x) {
  if (x <= 0.0) return 0.0;
  if (x > 1e6) return 1.0;
  const double lna = a * std::log(x) - x - std::lgamma(a);
  if (x < a + 1.0) {
    double ap = a;
    double del = 1.0 / a;
    double sum = del;
    for (int i = 0; i < 600; ++i) {
      ++ap;
      del *= x / ap;
      sum += del;
      if (std::abs(del) <= std::abs(sum) * 1e-12) break;
    }
    return std::clamp(std::exp(lna) * sum, 0.0, 1.0);
  } else {
    constexpr double kFpMin = 1e-300;
    double b = x + 1.0 - a, c = 1.0 / kFpMin, d = 1.0 / b, h = d;
    for (int i = 1; i <= 600; ++i) {
      const double an = -static_cast<double>(i) * (static_cast<double>(i) - a);
      b += 2.0;
      d = an * d + b;
      if (std::abs(d) < kFpMin) d = kFpMin;
      c = b + an / c;
      if (std::abs(c) < kFpMin) c = kFpMin;
      d = 1.0 / d;
      const double del = d * c;
      h *= del;
      if (std::abs(del - 1.0) <= 1e-12) break;
    }
    return std::clamp(1.0 - std::exp(lna) * h, 0.0, 1.0);
  }
}

// Regularised incomplete beta I_x(a, b) via continued fraction (Lentz method).
// Used for the StudentT CDF.  (Numerical Recipes §6.4)
static double beta_p(double a, double b, double x) {
  if (x <= 0.0) return 0.0;
  if (x >= 1.0) return 1.0;
  const double lbc = std::lgamma(a + b) - std::lgamma(a) - std::lgamma(b) + a * std::log(x) +
                     b * std::log(1.0 - x);
  // Continued fraction via Lentz for I_x(a,b) when x < (a+1)/(a+b+2);
  // otherwise use symmetry I_x(a,b) = 1 - I_{1-x}(b,a).
  const bool flip = x > (a + 1.0) / (a + b + 2.0);
  const double xa = flip ? 1.0 - x : x;
  const double aa = flip ? b : a;
  const double bb = flip ? a : b;
  constexpr double kFpMin = 1e-300;
  double c = 1.0, d = 1.0 - (aa + bb) * xa / (aa + 1.0);
  if (std::abs(d) < kFpMin) d = kFpMin;
  d = 1.0 / d;
  double h = d;
  for (int m = 1; m <= 200; ++m) {
    const double dm = static_cast<double>(m);
    // Even step
    double num = dm * (bb - dm) * xa / ((aa + 2.0 * dm - 1.0) * (aa + 2.0 * dm));
    d = 1.0 + num * d;
    if (std::abs(d) < kFpMin) d = kFpMin;
    d = 1.0 / d;
    c = 1.0 + num / c;
    if (std::abs(c) < kFpMin) c = kFpMin;
    h *= d * c;
    // Odd step
    num = -(aa + dm) * (aa + bb + dm) * xa / ((aa + 2.0 * dm) * (aa + 2.0 * dm + 1.0));
    d = 1.0 + num * d;
    if (std::abs(d) < kFpMin) d = kFpMin;
    d = 1.0 / d;
    c = 1.0 + num / c;
    if (std::abs(c) < kFpMin) c = kFpMin;
    const double del = d * c;
    h *= del;
    if (std::abs(del - 1.0) <= 1e-12) break;
  }
  const double cf = std::exp(lbc) * h / aa;
  return flip ? std::clamp(1.0 - cf, 0.0, 1.0) : std::clamp(cf, 0.0, 1.0);
}

DistributionFit fit_distribution(std::span<const double> samples) {
  const size_t n = samples.size();
  if (n < 4) return DistributionFit{};
  const double dn = static_cast<double>(n);
  const double mean = std::accumulate(samples.begin(), samples.end(), 0.0) / dn;
  double variance = 0.0;
  for (double x : samples) variance += (x - mean) * (x - mean);
  variance /= dn;
  const double stddev = std::sqrt(variance);

  std::vector<double> sorted(samples.begin(), samples.end());
  std::sort(sorted.begin(), sorted.end());

  const bool all_positive = sorted.front() > 0.0;

  struct Scored {
    DistributionKind kind;
    double p;
    bool skipped;
  };
  std::vector<Scored> scored;
  scored.reserve(12);

  // Kolmogorov Q — maps max empirical/theoretical CDF deviation D to a p-value.
  // Series converges rapidly; 10 terms is accurate for sqrt(n)*D > 0.5.
  // ks_D captures the D of the most recent call — read into the candidate's ks_statistic.
  double ks_D = 1.0;
  const auto ks_p = [&]<typename F>(F cdf) -> double {
    double D = 0.0;
    for (size_t i = 0; i < n; ++i) {
      const double Fn_hi = static_cast<double>(i + 1) / dn;
      const double Fn_lo = static_cast<double>(i) / dn;
      const double Fc = cdf(sorted[i]);
      D = std::max(D, std::max(std::abs(Fn_hi - Fc), std::abs(Fn_lo - Fc)));
    }
    ks_D = D;
    const double t = std::sqrt(dn) * D;
    if (t <= 0.0) return 1.0;
    double q = 0.0;
    for (int k = 1; k <= 10; ++k)
      q += (k % 2 == 1 ? 1.0 : -1.0) * std::exp(-2.0 * static_cast<double>(k * k) * t * t);
    return std::clamp(2.0 * q, 0.0, 1.0);
  };

  DistributionFit best;
  best.sample_count = n;
  best.ks_p_value = -1.0;  // sentinel — no candidate accepted yet

  const auto try_dist = [&](DistributionFit d) {
    d.ks_statistic = ks_D;  // D from the ks_p call that just scored this candidate
    scored.push_back({d.kind, d.ks_p_value, false});
    if (d.ks_p_value > best.ks_p_value) best = std::move(d);
  };

  const auto skip_dist = [&](DistributionKind k) {
    scored.push_back({k, 0.0, true});
  };

  if (stddev > 1e-12) {
    DistributionFit d;
    d.kind = DistributionKind::Normal;
    d.params[0] = mean;
    d.params[1] = stddev;
    d.sample_count = n;
    d.ks_p_value = ks_p([&](double x) {
      return 0.5 * std::erfc(-(x - mean) / (stddev * std::sqrt(2.0)));
    });
    try_dist(std::move(d));
  } else {
    skip_dist(DistributionKind::Normal);
  }

  if (all_positive) {
    double log_mean = 0.0;
    for (double x : samples) log_mean += std::log(x);
    log_mean /= dn;

    double log_var = 0.0;
    for (double x : samples) {
      const double dv = std::log(x) - log_mean;
      log_var += dv * dv;
    }
    const double log_std = std::sqrt(log_var / dn);

    if (log_std > 1e-12) {
      DistributionFit d;
      d.kind = DistributionKind::Lognormal;
      d.params[0] = log_mean;
      d.params[1] = log_std;
      d.sample_count = n;
      d.ks_p_value = ks_p([&](double x) -> double {
        if (x <= 0.0) return 0.0;
        return 0.5 * std::erfc(-(std::log(x) - log_mean) / (log_std * std::sqrt(2.0)));
      });
      try_dist(std::move(d));
    } else {
      skip_dist(DistributionKind::Lognormal);
    }
  } else {
    skip_dist(DistributionKind::Lognormal);
  }

  if (all_positive && variance > 1e-24 && mean > 1e-12) {
    const double alpha = (mean * mean) / variance;
    const double theta = variance / mean;
    if (alpha >= 0.1) {
      DistributionFit d;
      d.kind = DistributionKind::Gamma;
      d.params[0] = alpha;
      d.params[1] = theta;
      d.sample_count = n;
      d.ks_p_value = ks_p([&](double x) -> double {
        return (x <= 0.0) ? 0.0 : gamma_p(alpha, x / theta);
      });
      try_dist(std::move(d));
    } else {
      skip_dist(DistributionKind::Gamma);
    }
  } else {
    skip_dist(DistributionKind::Gamma);
  }

  // CV monotonically decreasing in shape a; Exponential (a=1) has CV=1.
  if (all_positive && stddev > 1e-12 && mean > 1e-12) {
    const double cv_target = stddev / mean;
    const auto weibull_cv = [](double a) -> double {
      const double g1 = std::tgamma(1.0 + 1.0 / a);
      const double g2 = std::tgamma(1.0 + 2.0 / a);
      const double ratio = g2 / (g1 * g1);
      return (std::isfinite(ratio) && ratio > 1.0) ? std::sqrt(ratio - 1.0) : 0.0;
    };
    if (weibull_cv(0.1) >= cv_target && cv_target > 1e-12) {
      double a_lo = 0.1, a_hi = 100.0;
      for (int i = 0; i < 80; ++i) {
        const double a_mid = 0.5 * (a_lo + a_hi);
        (weibull_cv(a_mid) > cv_target ? a_lo : a_hi) = a_mid;
      }
      const double a = 0.5 * (a_lo + a_hi);
      const double g1 = std::tgamma(1.0 + 1.0 / a);
      if (g1 > 1e-12) {
        const double b = mean / g1;
        DistributionFit d;
        d.kind = DistributionKind::Weibull;
        d.params[0] = a;
        d.params[1] = b;
        d.sample_count = n;
        d.ks_p_value = ks_p([&](double x) -> double {
          return (x <= 0.0) ? 0.0 : 1.0 - std::exp(-std::pow(x / b, a));
        });
        try_dist(std::move(d));
      } else {
        skip_dist(DistributionKind::Weibull);
      }
    } else {
      skip_dist(DistributionKind::Weibull);
    }
  } else {
    skip_dist(DistributionKind::Weibull);
  }

  if (all_positive && mean > 1e-12) {
    const double lambda = 1.0 / mean;
    DistributionFit d;
    d.kind = DistributionKind::Exponential;
    d.params[0] = lambda;
    d.sample_count = n;
    d.ks_p_value = ks_p([&](double x) -> double {
      return (x < 0.0) ? 0.0 : 1.0 - std::exp(-lambda * x);
    });
    try_dist(std::move(d));
  } else {
    skip_dist(DistributionKind::Exponential);
  }

  if (all_positive && mean > 1e-12) {
    const double nu = mean;
    DistributionFit d;
    d.kind = DistributionKind::ChiSquared;
    d.params[0] = nu;
    d.sample_count = n;
    d.ks_p_value = ks_p([&](double x) -> double {
      return (x <= 0.0) ? 0.0 : gamma_p(nu / 2.0, x / 2.0);
    });
    try_dist(std::move(d));
  } else {
    skip_dist(DistributionKind::ChiSquared);
  }

  // location μ = mean − γ·β, scale β = σ·√6/π  (γ = Euler-Mascheroni)
  if (stddev > 1e-12) {
    constexpr double kEulerMascheroni = 0.5772156649;
    const double beta = stddev * std::numbers::pi / std::sqrt(6.0);
    const double mu = mean - kEulerMascheroni * beta;
    DistributionFit d;
    d.kind = DistributionKind::ExtremeValue;
    d.params[0] = mu;
    d.params[1] = beta;
    d.sample_count = n;
    d.ks_p_value = ks_p([&](double x) -> double {
      return std::exp(-std::exp(-(x - mu) / beta));
    });
    try_dist(std::move(d));
  } else {
    skip_dist(DistributionKind::ExtremeValue);
  }

  {
    const double loc = sorted[n / 2];
    const double scale = std::max((sorted[3 * n / 4] - sorted[n / 4]) / 2.0, 1e-12);
    DistributionFit d;
    d.kind = DistributionKind::Cauchy;
    d.params[0] = loc;
    d.params[1] = scale;
    d.sample_count = n;
    d.ks_p_value = ks_p([&](double x) {
      return 0.5 + std::atan((x - loc) / scale) / std::numbers::pi;
    });
    try_dist(std::move(d));
  }

  if (stddev > 1e-12) {
    const double mu = sorted[n / 2];
    const double nu = (variance > 1.0) ? std::clamp(2.0 * variance / (variance - 1.0), 2.1, 100.0)
                                       : 30.0;  // empirical — converges to Normal
    DistributionFit d;
    d.kind = DistributionKind::StudentT;
    d.params[0] = nu;
    d.sample_count = n;
    d.ks_p_value = ks_p([&](double x) -> double {
      const double t = (x - mu) / stddev;
      const double t2 = t * t;
      const double ib = beta_p(0.5, nu / 2.0, t2 / (t2 + nu));
      return 0.5 + (t >= 0.0 ? 0.5 : -0.5) * ib;
    });
    try_dist(std::move(d));
  } else {
    skip_dist(DistributionKind::StudentT);
  }

  // weights = interleaved [x₀,y₀, x₁,y₁, ...] (CDF knot pairs).
  // Non-parametric: fits any shape. DPR tolerance controls knot count vs fidelity.
  // KS stat computed from the PL interpolation, not from the tolerance — honest.
  {
    constexpr double kPLTol = 0.02;  // empirical — 2% CDF perpendicular tolerance

    const double x_lo = sorted.front();
    const double x_hi = sorted.back();
    const double x_range = (x_hi > x_lo) ? (x_hi - x_lo) : 1.0;

    std::vector<size_t> knots;
    knots.reserve(64);
    knots.push_back(0);

    // DPR on empirical CDF in normalized (x,y) space.
    // Perpendicular distance from each point to the chord between endpoints.
    const auto rdp = [&](this auto& self, size_t lo, size_t hi) -> void {
      if (hi <= lo + 1) return;
      const double xa = (sorted[lo] - x_lo) / x_range;
      const double ya = (lo + 0.5) / dn;
      const double xb = (sorted[hi] - x_lo) / x_range;
      const double yb = (hi + 0.5) / dn;
      const double dx = xb - xa;
      const double dy = yb - ya;
      const double chord_sq = dx * dx + dy * dy;

      double max_dev = 0.0;
      size_t max_i = lo + 1;

      for (size_t i = lo + 1; i < hi; ++i) {
        const double xi = (sorted[i] - x_lo) / x_range;
        const double yi = (i + 0.5) / dn;
        double dev;
        if (chord_sq < 1e-24) {
          dev = std::hypot(xi - xa, yi - ya);
        } else {
          const double t = ((xi - xa) * dx + (yi - ya) * dy) / chord_sq;
          const double px = xa + t * dx;
          const double py = ya + t * dy;
          dev = std::hypot(xi - px, yi - py);
        }
        if (dev > max_dev) {
          max_dev = dev;
          max_i = i;
        }
      }

      if (max_dev > kPLTol) {
        self(lo, max_i);
        knots.push_back(max_i);
        self(max_i, hi);
      }
    };

    rdp(0, n - 1);
    knots.push_back(n - 1);
    std::sort(knots.begin(), knots.end());

    DistributionFit d;
    d.kind = DistributionKind::PiecewiseLinear;
    d.sample_count = n;
    d.weights.resize(knots.size() * 2);
    for (size_t k = 0; k < knots.size(); ++k) {
      d.weights[2 * k] = sorted[knots[k]];
      d.weights[2 * k + 1] = (knots[k] + 0.5) / dn;
    }

    // KS via piecewise-linear interpolation of the CDF knots
    const size_t nk = knots.size();
    d.ks_p_value = ks_p([&](double x) -> double {
      if (x <= d.weights[0]) return d.weights[1];
      if (x >= d.weights[2 * (nk - 1)]) return d.weights[2 * (nk - 1) + 1];
      size_t lo = 0, hi = nk - 1;
      while (hi - lo > 1) {
        const size_t mid = (lo + hi) / 2;
        (d.weights[2 * mid] <= x ? lo : hi) = mid;
      }
      const double x0 = d.weights[2 * lo], y0 = d.weights[2 * lo + 1];
      const double x1 = d.weights[2 * hi], y1 = d.weights[2 * hi + 1];
      const double t = (x1 > x0) ? (x - x0) / (x1 - x0) : 0.0;
      return y0 + t * (y1 - y0);
    });

    try_dist(std::move(d));
  }

  // One Info line per family; winner marked with *.  Skipped entries show —.
  {
    const auto fmt = [&](DistributionKind k) -> std::string {
      for (const Scored& s : scored) {
        if (s.kind != k) continue;
        if (s.skipped) return "—";
        const std::string pstr = (s.p == 0.0) ? "0" : std::format("{:.2e}", s.p);
        return (s.kind == best.kind) ? (pstr + "*") : pstr;
      }
      return "—";
    };

    const std::string pl_extra = (best.kind == DistributionKind::PiecewiseLinear)
                                     ? std::format(" ({} knots)", best.weights.size() / 2)
                                     : "";

    SFERIC_LOG(
        Info,
        std::format("scores n={} mean={:.4g} stddev={:.4g}  best={} similarity={:.1f}%\n"
                    "  sym: Normal={}  StudentT={}  Cauchy={}  ExtremeValue={}\n"
                    "  pos: Lognormal={}  Gamma={}  Weibull={}  Exponential={}  ChiSquared={}\n"
                    "  np:  PiecewiseLinear={}{}", n, mean, stddev,
                    distribution_name(best.kind), fit_similarity(best),
                    fmt(DistributionKind::Normal),     fmt(DistributionKind::StudentT),
                    fmt(DistributionKind::Cauchy),     fmt(DistributionKind::ExtremeValue),
                    fmt(DistributionKind::Lognormal),  fmt(DistributionKind::Gamma),
                    fmt(DistributionKind::Weibull),    fmt(DistributionKind::Exponential),
                    fmt(DistributionKind::ChiSquared), fmt(DistributionKind::PiecewiseLinear),
                    pl_extra));
  }
  return best;
}

double fit_similarity(const DistributionFit& fit) {
  if (fit.sample_count == 0) return 0.0;
  return 100.0 * (1.0 - std::clamp(fit.ks_statistic, 0.0, 1.0));
}

const char* distribution_name(DistributionKind kind) {
  switch (kind) {
    case DistributionKind::UniformReal:       return "UniformReal";
    case DistributionKind::UniformInt:        return "UniformInt";
    case DistributionKind::Bernoulli:         return "Bernoulli";
    case DistributionKind::Binomial:          return "Binomial";
    case DistributionKind::NegativeBinomial:  return "NegativeBinomial";
    case DistributionKind::Geometric:         return "Geometric";
    case DistributionKind::Poisson:           return "Poisson";
    case DistributionKind::Exponential:       return "Exponential";
    case DistributionKind::Gamma:             return "Gamma";
    case DistributionKind::Weibull:           return "Weibull";
    case DistributionKind::ExtremeValue:      return "ExtremeValue";
    case DistributionKind::Normal:            return "Normal";
    case DistributionKind::Lognormal:         return "Lognormal";
    case DistributionKind::ChiSquared:        return "ChiSquared";
    case DistributionKind::Cauchy:            return "Cauchy";
    case DistributionKind::FisherF:           return "FisherF";
    case DistributionKind::StudentT:          return "StudentT";
    case DistributionKind::Discrete:          return "Discrete";
    case DistributionKind::PiecewiseConstant: return "PiecewiseConstant";
    case DistributionKind::PiecewiseLinear:   return "PiecewiseLinear";
  }
  return "Unknown";
}

}  // namespace analysis
}  // namespace sferic
