#pragma once

#include <cstddef>

namespace sferic {

// Speed of sound in air at 20°C, 1 atm (m/s).
constexpr double SPEED_OF_SOUND = 343.0;

constexpr double A4_FREQUENCY = 440.0;

constexpr double SAMPLE_RATE_44100 = 44100.0;
constexpr double SAMPLE_RATE_48000 = 48000.0;
constexpr double SAMPLE_RATE_96000 = 96000.0;

// Atmospheric absorption coefficients (dB per km) at standard conditions.
// Higher frequencies attenuate faster over distance.
// Source: ISO 9613-1 approximations at 20°C, 50% humidity, 1 atm.
struct AtmosphericAbsorption {
  double frequency_hz;
  double db_per_km;
};

constexpr AtmosphericAbsorption ABSORPTION_TABLE[] = {
    {125.0, 0.3},  {250.0, 1.1},   {500.0, 2.8},   {1000.0, 5.0},
    {2000.0, 9.0}, {4000.0, 26.0}, {8000.0, 83.0}, {16000.0, 200.0},
};
constexpr size_t ABSORPTION_TABLE_SIZE = sizeof(ABSORPTION_TABLE) / sizeof(ABSORPTION_TABLE[0]);

constexpr double PI = 3.14159265358979323846;
constexpr double TWO_PI = 2.0 * PI;

}  // namespace sferic
