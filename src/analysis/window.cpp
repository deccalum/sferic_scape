#include "analysis/window.h"

#include <cmath>
#include <stdexcept>

#include "core/constants.h"

namespace sferic {
namespace analysis {

// Modified Bessel function of first kind, order 0 (for Kaiser window)
static double bessel_i0(double x) {
  double sum = 1.0;
  double term = 1.0;
  for (int k = 1; k <= 25; ++k) {
    term *= (x / (2.0 * k)) * (x / (2.0 * k));
    sum += term;
    if (term < 1e-12 * sum) break;
  }
  return sum;
}

std::vector<double> make_window(WindowType type, size_t size, double kaiser_beta) {
  if (size == 0) return {};

  std::vector<double> w(size);
  const double N = static_cast<double>(size);

  switch (type) {
    case WindowType::RECTANGULAR:
      for (size_t i = 0; i < size; ++i) w[i] = 1.0;
      break;

    case WindowType::HANN:
      for (size_t i = 0; i < size; ++i)
        w[i] = 0.5 * (1.0 - std::cos(TWO_PI * static_cast<double>(i) / (N - 1.0)));
      break;

    case WindowType::BLACKMAN_HARRIS: {
      // 4-term Blackman-Harris (92 dB sidelobe)
      constexpr double a0 = 0.35875;
      constexpr double a1 = 0.48829;
      constexpr double a2 = 0.14128;
      constexpr double a3 = 0.01168;
      for (size_t i = 0; i < size; ++i) {
        double n = static_cast<double>(i) / (N - 1.0);
        w[i] = a0 - a1 * std::cos(TWO_PI * n) + a2 * std::cos(2.0 * TWO_PI * n) -
               a3 * std::cos(3.0 * TWO_PI * n);
      }
      break;
    }

    case WindowType::KAISER: {
      double denom = bessel_i0(kaiser_beta);
      double half = (N - 1.0) / 2.0;
      for (size_t i = 0; i < size; ++i) {
        double ratio = (static_cast<double>(i) - half) / half;
        w[i] = bessel_i0(kaiser_beta * std::sqrt(1.0 - ratio * ratio)) / denom;
      }
      break;
    }
  }

  return w;
}

double coherent_gain(const std::vector<double>& window) {
  if (window.empty()) return 0.0;
  double sum = 0.0;
  for (double v : window) sum += v;
  return sum / static_cast<double>(window.size());
}

double energy_gain(const std::vector<double>& window) {
  if (window.empty()) return 0.0;
  double sum = 0.0;
  for (double v : window) sum += v * v;
  return sum / static_cast<double>(window.size());
}

}  // namespace analysis
}  // namespace sferic
