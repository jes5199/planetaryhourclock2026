#include "sun.h"

#include <cmath>

namespace planetary {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;
constexpr double kZenith = 90.833;  // refraction + solar disc radius

struct SolarParams {
  double declRad;
  double eqTimeMin;
};

// Solar declination and equation of time at Julian day `jd`.
SolarParams solarParams(double jd) {
  const double T = (jd - 2451545.0) / 36525.0;
  double L0 = std::fmod(280.46646 + T * (36000.76983 + T * 0.0003032), 360.0);
  if (L0 < 0) L0 += 360.0;
  const double M = 357.52911 + T * (35999.05029 - 0.0001537 * T);
  const double e = 0.016708634 - T * (0.000042037 + 0.0000001267 * T);
  const double Mr = M * kDeg;
  const double C = std::sin(Mr) * (1.914602 - T * (0.004817 + 0.000014 * T)) +
                   std::sin(2 * Mr) * (0.019993 - 0.000101 * T) +
                   std::sin(3 * Mr) * 0.000289;
  const double trueLong = L0 + C;
  const double omega = 125.04 - 1934.136 * T;
  const double lambda = trueLong - 0.00569 - 0.00478 * std::sin(omega * kDeg);
  const double eps0 =
      23.0 + (26.0 + (21.448 - T * (46.815 + T * (0.00059 - T * 0.001813))) / 60.0) / 60.0;
  const double eps = eps0 + 0.00256 * std::cos(omega * kDeg);
  const double decl = std::asin(std::sin(eps * kDeg) * std::sin(lambda * kDeg));

  const double y = std::pow(std::tan(eps * kDeg / 2.0), 2);
  const double L0r = L0 * kDeg;
  const double eqTime =
      4.0 / kDeg *
      (y * std::sin(2 * L0r) - 2 * e * std::sin(Mr) + 4 * e * y * std::sin(Mr) * std::cos(2 * L0r) -
       0.5 * y * y * std::sin(4 * L0r) - 1.25 * e * e * std::sin(2 * Mr));
  return {decl, eqTime};
}

// cos of the sunrise hour angle; outside [-1, 1] means polar day/night.
double cosHourAngle(double latDeg, double declRad) {
  const double lat = latDeg * kDeg;
  return std::cos(kZenith * kDeg) / (std::cos(lat) * std::cos(declRad)) -
         std::tan(lat) * std::tan(declRad);
}

double jdFromUnix(double unixSec) { return unixSec / 86400.0 + 2440587.5; }

}  // namespace

int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  // Howard Hinnant's algorithm.
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

SunTimes sunTimes(int64_t epochDay, double latDeg, double lonDeg) {
  const double dayStart = static_cast<double>(epochDay) * 86400.0;  // 00:00 UTC of that date
  // First guess: solar noon at the location.
  double noonUtcMin = 720.0 - 4.0 * lonDeg;

  SunTimes out{};
  double events[2];
  for (int which = 0; which < 2; ++which) {
    const double sign = which == 0 ? -1.0 : 1.0;
    double t = dayStart + noonUtcMin * 60.0;
    // Two refinement passes evaluating the sun at the event time itself.
    for (int pass = 0; pass < 3; ++pass) {
      const SolarParams p = solarParams(jdFromUnix(t));
      const double noon = 720.0 - 4.0 * lonDeg - p.eqTimeMin;
      double c = cosHourAngle(latDeg, p.declRad);
      if (c < -1.0) {
        out.polarDay = true;
        c = -1.0;
      } else if (c > 1.0) {
        out.polarNight = true;
        c = 1.0;
      } else {
        out.polarDay = out.polarNight = false;
      }
      const double haDeg = std::acos(c) / kDeg;
      t = dayStart + (noon + sign * 4.0 * haDeg) * 60.0;
    }
    events[which] = t;
  }
  out.sunrise = static_cast<int64_t>(std::llround(events[0]));
  out.sunset = static_cast<int64_t>(std::llround(events[1]));
  return out;
}

}  // namespace planetary
