#include "analysis/dsp.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

#include "core/logger.h"

namespace sferic::analysis {

double percentile(std::vector<double> v, double p) {
  if (v.empty()) return 0.0;
  std::sort(v.begin(), v.end());
  return v[std::min(v.size() - 1, static_cast<size_t>(p * static_cast<double>(v.size())))];
}

std::vector<double> trailing_percentile(const std::vector<double>& a, size_t win, double p) {
  if (win <= 1 || a.empty()) return a;
  const size_t w = std::min(win, a.size());
  std::vector<double> out(a.size());
  for (size_t i = w - 1; i < a.size(); ++i) {
    std::vector<double> s(a.begin() + static_cast<std::ptrdiff_t>(i + 1 - w),
                          a.begin() + static_cast<std::ptrdiff_t>(i + 1));
    out[i] = percentile(std::move(s), p);
  }
  for (size_t i = 0; i + 1 < w; ++i) out[i] = out[w - 1];
  return out;
}

std::vector<double> moving_average(const std::vector<double>& a, size_t window) {
  if (window <= 1) return a;
  const long n = static_cast<long>(a.size());
  const long off = static_cast<long>((window - 1) / 2);
  std::vector<double> out(a.size());
  for (long i = 0; i < n; ++i) {
    const long jhi = std::min(n - 1, i + off);
    const long jlo = std::max<long>(0, i + off - static_cast<long>(window - 1));
    double s = 0.0;
    for (long j = jlo; j <= jhi; ++j) s += a[static_cast<size_t>(j)];
    out[static_cast<size_t>(i)] = s / static_cast<double>(jhi - jlo + 1);
  }
  return out;
}

double ols_slope(std::span<const double> x, std::span<const double> y) {
  const size_t m = std::min(x.size(), y.size());
  if (m < 2) return 0.0;
  double xbar = 0.0;
  double ybar = 0.0;
  for (size_t i = 0; i < m; ++i) {
    xbar += x[i];
    ybar += y[i];
  }
  xbar /= static_cast<double>(m);
  ybar /= static_cast<double>(m);
  double sxy = 0.0;
  double sxx = 0.0;
  for (size_t i = 0; i < m; ++i) {
    const double d = x[i] - xbar;
    sxy += d * (y[i] - ybar);
    sxx += d * d;
  }
  return sxx > 0.0 ? sxy / sxx : 0.0;
}

double ols_slope_uniform(std::span<const double> y, double dx) {
  const size_t m = y.size();
  if (m < 2) return 0.0;
  double tbar = 0.0;
  double ybar = 0.0;
  for (size_t i = 0; i < m; ++i) {
    tbar += static_cast<double>(i) * dx;
    ybar += y[i];
  }
  tbar /= static_cast<double>(m);
  ybar /= static_cast<double>(m);
  double sxy = 0.0;
  double sxx = 0.0;
  for (size_t i = 0; i < m; ++i) {
    const double dt = static_cast<double>(i) * dx - tbar;
    sxy += dt * (y[i] - ybar);
    sxx += dt * dt;
  }
  return sxx > 0.0 ? sxy / sxx : 0.0;
}

double otsu_threshold(std::vector<double> values, double min_significance) {
  std::vector<double> v;
  for (const double x : values)
    if (x > 0.0 && std::isfinite(x)) v.push_back(x);
  std::sort(v.begin(), v.end());
  const size_t n = v.size();
  if (n < 4) return 0.0;
  std::vector<double> logs(n);
  for (size_t i = 0; i < n; ++i) logs[i] = std::log(v[i]);
  std::vector<double> prefix_sum(n + 1, 0.0), prefix_sq(n + 1, 0.0);
  for (size_t i = 0; i < n; ++i) {
    prefix_sum[i + 1] = prefix_sum[i] + logs[i];
    prefix_sq[i + 1] = prefix_sq[i] + logs[i] * logs[i];
  }
  const double total = prefix_sum[n];
  double best_between = -1.0;
  size_t best_i = 0;
  for (size_t i = 1; i < n; ++i) {
    const double w0 = static_cast<double>(i) / static_cast<double>(n);
    const double m0 = prefix_sum[i] / static_cast<double>(i);
    const double m1 = (total - prefix_sum[i]) / static_cast<double>(n - i);
    const double between = w0 * (1.0 - w0) * (m0 - m1) * (m0 - m1);
    if (between > best_between) {
      best_between = between;
      best_i = i;
    }
  }
  const double m0 = prefix_sum[best_i] / static_cast<double>(best_i);
  const double m1 = (total - prefix_sum[best_i]) / static_cast<double>(n - best_i);
  const double ss0 =
      prefix_sq[best_i] - 2.0 * m0 * prefix_sum[best_i] + static_cast<double>(best_i) * m0 * m0;
  const double ss1 = (prefix_sq[n] - prefix_sq[best_i]) - 2.0 * m1 * (total - prefix_sum[best_i]) +
                     static_cast<double>(n - best_i) * m1 * m1;
  const double within = (ss0 + ss1) / static_cast<double>(n);
  const double significance =
      within > 1e-12 ? best_between / within : std::numeric_limits<double>::infinity();
  const double split = std::exp(0.5 * (logs[best_i - 1] + logs[best_i]));
  SFERIC_LOG(Info, "otsu n=" + std::to_string(n) + " significance=" + std::to_string(significance) +
                       " min=" + std::to_string(min_significance) + " split=" +
                       std::to_string(split) + (significance < min_significance ? " — inactive" : " — active"));
  if (significance < min_significance) return 0.0;
  return split;
}

double gap_valley_threshold(std::vector<double> values, double gap_factor) {
  std::vector<double> vals;
  for (const double r : values)
    if (r > 0.0 && std::isfinite(r)) vals.push_back(r);
  std::sort(vals.begin(), vals.end());
  if (vals.size() < 4) return 0.0;
  std::vector<double> logs;
  logs.reserve(vals.size());
  for (const double v : vals) logs.push_back(std::log(v));
  std::vector<double> gaps(logs.size() - 1);
  for (size_t i = 0; i + 1 < logs.size(); ++i) gaps[i] = logs[i + 1] - logs[i];
  const size_t half = std::max<size_t>(1, gaps.size() / 2);
  size_t j = 0;
  for (size_t i = 1; i < half; ++i)
    if (gaps[i] > gaps[j]) j = i;
  const double median_gap = percentile(gaps, 0.5);
  if (gaps[j] < gap_factor * median_gap) return 0.0;
  return std::exp(0.5 * (logs[j] + logs[j + 1]));
}

std::vector<double> rms_envelope_db(const AudioBuffer& buffer, size_t frame_len, size_t hop_len,
                                    size_t smooth) {
  std::vector<double> db;
  const size_t channels = buffer.num_channels();
  const size_t n = buffer.num_frames();
  if (frame_len == 0 || hop_len == 0 || n < frame_len) return db;
  db.reserve((n - frame_len) / hop_len + 1);
  for (size_t start = 0; start + frame_len <= n; start += hop_len) {
    double acc = 0.0;
    for (size_t s = start; s < start + frame_len; ++s) {
      double mono = 0.0;
      for (size_t c = 0; c < channels; ++c) mono += static_cast<double>(buffer.at(c, s));
      mono /= static_cast<double>(channels);
      acc += mono * mono;
    }
    const double rms = std::sqrt(std::max(acc / static_cast<double>(frame_len), 1e-12));
    db.push_back(20.0 * std::log10(rms));
  }
  return moving_average(db, smooth);
}

std::vector<size_t> detect_onsets(const std::vector<double>& env_db, double hop_s,
                                  const OnsetConfig& cfg) {
  if (env_db.size() < 2) return {};
  const size_t win = std::max<size_t>(1, static_cast<size_t>(std::round(cfg.lookback_s / hop_s)));
  const std::vector<double> floor = trailing_percentile(env_db, win, cfg.floor_percentile);
  const size_t hold = std::max<size_t>(1, static_cast<size_t>(std::round(cfg.hold_s / hop_s)));
  const size_t refractory =
      std::max<size_t>(1, static_cast<size_t>(std::round(cfg.refractory_s / hop_s)));
  std::vector<size_t> onsets;
  const size_t n = env_db.size();
  long last = -static_cast<long>(refractory);
  size_t i = 0;
  while (i < n) {
    if (env_db[i] > floor[i] + cfg.rise_db &&
        static_cast<long>(i) - last >= static_cast<long>(refractory)) {
      bool all_above = true;
      for (size_t j = i; j < std::min(n, i + hold); ++j)
        if (!(env_db[j] > floor[j] + cfg.rise_db)) {
          all_above = false;
          break;
        }
      if (all_above) {
        onsets.push_back(i);
        last = static_cast<long>(i);
        i += refractory;
        continue;
      }
    }
    ++i;
  }
  return onsets;
}

double local_median_floor(const std::vector<double>& env_db, size_t index, double hop_s,
                          double lookback_s, double guard_s) {
  const size_t win = static_cast<size_t>(std::round(lookback_s / hop_s));
  const size_t guard = static_cast<size_t>(std::round(guard_s / hop_s));
  const size_t lo = index > win + guard ? index - win - guard : 0;
  const size_t hi = std::max(lo + 1, index > guard ? index - guard : lo + 1);
  std::vector<double> seg(
      env_db.begin() + static_cast<std::ptrdiff_t>(lo),
      env_db.begin() + static_cast<std::ptrdiff_t>(std::min(hi, env_db.size())));
  return percentile(std::move(seg), 0.5);
}

double quiet_half_mean(const std::vector<double>& env_db) {
  if (env_db.empty()) return 0.0;
  const double median = percentile(env_db, 0.5);
  double sum = 0.0;
  size_t count = 0;
  for (const double v : env_db) {
    if (v < median) {
      sum += v;
      ++count;
    }
  }
  return count ? sum / static_cast<double>(count) : median;
}

StereoFrame stereo_window(const AudioBuffer& buffer, size_t s0, size_t s1, double max_itd_s) {
  StereoFrame out;
  if (buffer.num_channels() < 2 || s1 <= s0) return out;
  const double sr = buffer.sample_rate();
  const long maxlag = std::max<long>(1, static_cast<long>(max_itd_s * sr));
  double sL = 0.0, sR = 0.0, sLL = 0.0, sRR = 0.0, sLR = 0.0, aL = 0.0, aR = 0.0;
  for (size_t s = s0; s < s1; ++s) {
    const double L = static_cast<double>(buffer.at(0, s));
    const double R = static_cast<double>(buffer.at(1, s));
    sL += L;
    sR += R;
    sLL += L * L;
    sRR += R * R;
    sLR += L * R;
    aL += std::abs(L);
    aR += std::abs(R);
  }
  const double m = static_cast<double>(s1 - s0);
  const double cov = sLR / m - (sL / m) * (sR / m);
  const double vL = std::max(0.0, sLL / m - (sL / m) * (sL / m));
  const double vR = std::max(0.0, sRR / m - (sR / m) * (sR / m));
  const double sd = std::sqrt(vL * vR);
  out.corr = sd > 1e-12 ? std::clamp(cov / sd, -1.0, 1.0) : 1.0;
  const double side_e = 0.25 * (sLL + sRR - 2.0 * sLR);
  const double mid_e = 0.25 * (sLL + sRR + 2.0 * sLR);
  out.width = (mid_e + side_e) > 1e-12 ? std::clamp(side_e / (mid_e + side_e), 0.0, 1.0) : 0.0;
  const double rms = std::sqrt(sLL * sRR);
  double best = -2.0;
  if (rms > 1e-12) {
    const long l0 = static_cast<long>(s0);
    const long l1 = static_cast<long>(s1);
    for (long lag = -maxlag; lag <= maxlag; ++lag) {
      const long lo = std::max(l0, l0 - lag);
      const long hi = std::min(l1, l1 - lag);
      double x = 0.0;
      for (long s = lo; s < hi; ++s)
        x += static_cast<double>(buffer.at(0, static_cast<size_t>(s))) *
             static_cast<double>(buffer.at(1, static_cast<size_t>(s + lag)));
      best = std::max(best, x / rms);
    }
  } else {
    best = 1.0;
  }
  out.coherence = std::clamp(best, -1.0, 1.0);
  out.balance = (aL + aR) > 1e-12 ? aR / (aL + aR) : 0.5;
  return out;
}

std::vector<StereoFrame> stereo_frames(const AudioBuffer& buffer, std::span<const double> times_s,
                                       size_t win_samples, double max_itd_s) {
  std::vector<StereoFrame> out(times_s.size(), StereoFrame{});
  if (buffer.num_channels() < 2) return out;
  const double sr = buffer.sample_rate();
  const size_t n = buffer.num_frames();
  for (size_t i = 0; i < times_s.size(); ++i) {
    const size_t s0 = static_cast<size_t>(times_s[i] * sr);
    out[i] = stereo_window(buffer, s0, std::min(n, s0 + win_samples), max_itd_s);
  }
  return out;
}

StereoProfile extract_stereo_profile(const AudioBuffer& buffer, double max_itd_s) {
  const StereoFrame f = stereo_window(buffer, 0, buffer.num_frames(), max_itd_s);
  return StereoProfile{f.corr, f.width, f.balance, f.coherence};
}

}  // namespace sferic::analysis
