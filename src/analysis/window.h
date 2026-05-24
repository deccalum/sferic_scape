#pragma once

#include <cstddef>
#include <vector>

namespace sferic {
namespace analysis {

enum class WindowType {
  HANN,
  BLACKMAN_HARRIS,
  KAISER,
  RECTANGULAR,
};

// Generate a window of the given type and size.
// All windows are normalized so that their sum equals the window size
// (i.e., a rectangular window is all 1.0).
std::vector<double> make_window(WindowType type, size_t size, double kaiser_beta = 8.0);

// Coherent gain of a window (mean value). Used for amplitude correction.
double coherent_gain(const std::vector<double>& window);

// Energy gain of a window (mean of squared values). Used for power/energy correction.
double energy_gain(const std::vector<double>& window);

}  // namespace analysis
}  // namespace sferic
