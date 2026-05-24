#include "spatial/stereo.h"

#include <algorithm>
#include <cmath>

#include "core/constants.h"

namespace sferic {
namespace spatial {

namespace {

// Read a SampleBuffer at a fractional sample index using linear interpolation.
// Returns 0 for indices outside the buffer (acts as silence padding at both ends).
inline float read_fractional(const SampleBuffer& buf, double idx) {
  if (idx < 0.0) return 0.0f;
  if (idx >= static_cast<double>(buf.size()) - 1.0) return 0.0f;
  size_t i0 = static_cast<size_t>(idx);
  double frac = idx - static_cast<double>(i0);
  return static_cast<float>(buf[i0] * (1.0 - frac) + buf[i0 + 1] * frac);
}

// One-pole lowpass for the head-shadow model. y[n] = (1-a) x[n] + a y[n-1].
struct OnePoleLP {
  double y = 0.0;
  double a = 0.0;  // 0 = bypass

  void set_cutoff(double fc_hz, double sr) {
    if (fc_hz <= 0.0 || fc_hz >= sr * 0.5) {
      a = 0.0;
      return;
    }
    a = std::exp(-TWO_PI * fc_hz / sr);
  }

  inline float process(float x) {
    if (a == 0.0) return x;
    y = (1.0 - a) * static_cast<double>(x) + a * y;
    return static_cast<float>(y);
  }
};

}  // namespace

StereoProcessor::StereoProcessor(const StereoConfig& config) : config_(config) {}

StereoFrame StereoProcessor::compute_frame(const Position& source) const {
  StereoFrame frame{};

  const double half_w = 0.5 * config_.head_width_m;
  const double dxL = source.x_m + half_w;  // left ear at -half_w
  const double dxR = source.x_m - half_w;  // right ear at +half_w
  const double y2 = source.y_m * source.y_m;
  const double z2 = source.z_m * source.z_m;

  frame.left_distance_m = std::sqrt(dxL * dxL + y2 + z2);
  frame.right_distance_m = std::sqrt(dxR * dxR + y2 + z2);

  const double v = config_.speed_of_sound_ms;
  frame.left_delay_s = frame.left_distance_m / v;
  frame.right_delay_s = frame.right_distance_m / v;

  // Inverse-distance gain. Clamp to a small minimum to avoid divide-by-zero
  // when the source is co-located with an ear.
  const double dL = std::max(frame.left_distance_m, 0.01);
  const double dR = std::max(frame.right_distance_m, 0.01);
  frame.left_gain = 1.0 / dL;
  frame.right_gain = 1.0 / dR;

  // Azimuth in the horizontal plane: 0 = ahead, +π/2 = right, -π/2 = left.
  frame.azimuth_rad = std::atan2(source.x_m, source.y_m);

  // Stereo separation: blend toward the centred (mono) response.
  // The centre uses the source's actual range (not the inter-ear midpoint),
  // so the unblended ITD is exactly zero and gain is symmetrical.
  const double s = std::clamp(config_.separation, 0.0, 1.0);
  if (s < 1.0) {
    const double ctr_dist =
        std::max(std::sqrt(source.x_m * source.x_m + y2 + z2), 0.01);
    const double ctr_delay = ctr_dist / v;
    const double ctr_gain = 1.0 / ctr_dist;

    frame.left_delay_s = ctr_delay + s * (frame.left_delay_s - ctr_delay);
    frame.right_delay_s = ctr_delay + s * (frame.right_delay_s - ctr_delay);
    frame.left_gain = ctr_gain + s * (frame.left_gain - ctr_gain);
    frame.right_gain = ctr_gain + s * (frame.right_gain - ctr_gain);
  }

  // Distance-normalisation: scale so the louder ear is unity.
  if (config_.normalise_gain) {
    const double max_g = std::max(frame.left_gain, frame.right_gain);
    if (max_g > 0.0) {
      frame.left_gain /= max_g;
      frame.right_gain /= max_g;
    }
  }

  frame.itd_s = frame.right_delay_s - frame.left_delay_s;
  return frame;
}

AudioBuffer StereoProcessor::render(const SampleBuffer& mono,
                                    const Position& source,
                                    double sample_rate,
                                    size_t output_frames) const {
  const StereoFrame frame = compute_frame(source);
  const double max_delay_s = std::max(frame.left_delay_s, frame.right_delay_s);
  const size_t pad =
      static_cast<size_t>(std::ceil(max_delay_s * sample_rate)) + 2;
  const size_t out_n = output_frames ? output_frames : mono.size() + pad;

  AudioBuffer out(2, out_n, sample_rate);
  render_into(mono, source, out, 0);
  return out;
}

void StereoProcessor::render_into(const SampleBuffer& mono,
                                  const Position& source,
                                  AudioBuffer& stereo_out,
                                  size_t frame_offset) const {
  if (stereo_out.num_channels() != 2 || mono.empty()) return;
  if (frame_offset >= stereo_out.num_frames()) return;

  const StereoFrame frame = compute_frame(source);
  const double sr = stereo_out.sample_rate();

  const double l_delay_samples = frame.left_delay_s * sr;
  const double r_delay_samples = frame.right_delay_s * sr;
  const double l_gain = frame.left_gain;
  const double r_gain = frame.right_gain;

  // Head shadow: the far ear receives a 1st-order lowpass.
  // Lateralisation = |sin(azimuth)|: 0 at front/back, 1 at the sides.
  OnePoleLP lp_l;
  OnePoleLP lp_r;
  if (config_.head_shadow) {
    const double lat = std::abs(std::sin(frame.azimuth_rad));
    const double fc_far =
        config_.head_shadow_cutoff_hz +
        (sr * 0.5 - config_.head_shadow_cutoff_hz) * (1.0 - lat);
    if (frame.azimuth_rad > 0.0) {
      lp_l.set_cutoff(fc_far, sr);  // source on the right → left ear is far
    } else if (frame.azimuth_rad < 0.0) {
      lp_r.set_cutoff(fc_far, sr);
    }
  }

  // Loop bound: the contribution lasts mono.size() + max delay samples.
  const size_t max_delay_int = static_cast<size_t>(std::ceil(
                                   std::max(l_delay_samples, r_delay_samples))) +
                               2;
  const size_t available = stereo_out.num_frames() - frame_offset;
  const size_t loop_end = std::min(available, mono.size() + max_delay_int);

  for (size_t k = 0; k < loop_end; ++k) {
    const double idx_l = static_cast<double>(k) - l_delay_samples;
    const double idx_r = static_cast<double>(k) - r_delay_samples;

    float s_l = read_fractional(mono, idx_l);
    float s_r = read_fractional(mono, idx_r);

    s_l = lp_l.process(s_l);
    s_r = lp_r.process(s_r);

    const size_t out_idx = k + frame_offset;
    stereo_out.at(0, out_idx) += static_cast<Sample>(s_l * l_gain);
    stereo_out.at(1, out_idx) += static_cast<Sample>(s_r * r_gain);
  }
}

AudioBuffer StereoProcessor::constant_power_pan(const SampleBuffer& mono,
                                                double pan,
                                                double sample_rate) {
  pan = std::clamp(pan, -1.0, 1.0);
  // Map pan ∈ [-1, 1] → θ ∈ [0, π/2], with L = cos θ, R = sin θ.
  const double theta = (pan + 1.0) * 0.25 * PI;
  const double g_l = std::cos(theta);
  const double g_r = std::sin(theta);

  AudioBuffer out(2, mono.size(), sample_rate);
  for (size_t i = 0; i < mono.size(); ++i) {
    out.at(0, i) = static_cast<Sample>(mono[i] * g_l);
    out.at(1, i) = static_cast<Sample>(mono[i] * g_r);
  }
  return out;
}

}  // namespace spatial
}  // namespace sferic
