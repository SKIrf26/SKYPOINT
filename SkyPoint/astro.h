/**
 * astro.h - compact positional astronomy for SkyPoint (pure C++, host-testable).
 *
 * Accuracy targets (good enough for pointing a watch at the sky):
 *   stars  - J2000 catalogue precessed to date            (< 1')
 *   Sun    - low-precision solar theory                   (~ 1')
 *   Moon   - truncated ELP (Meeus ch.47 main terms) + topocentric parallax (~ 5')
 *   planets- JPL Keplerian elements, 1800-2050            (~ 1-10')
 */
#pragma once
#include <stdint.h>
#include <math.h>

namespace astro {

constexpr double A_PI = 3.14159265358979323846;
constexpr double D2R = A_PI / 180.0;
constexpr double R2D = 180.0 / A_PI;

struct Vec3 { double x, y, z; };
struct Mat3 { double m[3][3]; };

inline Vec3 mul(const Mat3 &a, const Vec3 &v)
{
    return { a.m[0][0] * v.x + a.m[0][1] * v.y + a.m[0][2] * v.z,
             a.m[1][0] * v.x + a.m[1][1] * v.y + a.m[1][2] * v.z,
             a.m[2][0] * v.x + a.m[2][1] * v.y + a.m[2][2] * v.z };
}
Mat3 mul(const Mat3 &a, const Mat3 &b);
Mat3 transpose(const Mat3 &a);
inline double dot(const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3 &a, const Vec3 &b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
inline double norm(const Vec3 &a) { return sqrt(dot(a, a)); }
inline Vec3 normalize(const Vec3 &a) { double n = norm(a); return n > 0 ? Vec3{ a.x / n, a.y / n, a.z / n } : a; }

/* RA/Dec (deg) -> unit vector in the equatorial frame. */
Vec3 radecToVec(double raDeg, double decDeg);
void vecToRadec(const Vec3 &v, double &raDeg, double &decDeg);

/* Unit vector in ENU (x=East, y=North, z=Up) -> azimuth (deg from N, clockwise) / altitude (deg). */
void enuToAzAlt(const Vec3 &enu, double &azDeg, double &altDeg);
Vec3 azAltToEnu(double azDeg, double altDeg);

double julianDate(double unixSeconds);
double gmstDeg(double jdUt);                    // Greenwich mean sidereal time, degrees
Mat3 precessionFromJ2000(double jd);            // J2000 equatorial -> mean equator of date
/* Equatorial (of date) -> local ENU, for latitude/longitude in degrees. */
Mat3 equatorialToEnu(double jdUt, double latDeg, double lonDeg);

enum Body : uint8_t { SUN = 0, MOON, MERCURY, VENUS, MARS, JUPITER, SATURN, URANUS, NEPTUNE, BODY_COUNT };
const char *bodyName(Body b);

struct BodyState {
    double ra, dec;          // apparent geocentric (topocentric for the Moon), equator of date, degrees
    double distAu;           // distance from observer (AU); Moon: also see distKm
    double distKm;
    double mag;              // visual magnitude estimate
    double phase;            // illuminated fraction 0..1
    double elongation;       // degrees from the Sun
    double angDiamArcsec;    // apparent diameter
    double brightLimbDeg;    // position angle of the bright limb (Moon), degrees
};

/* Compute a body for observer lat/lon (deg) and height (m). */
BodyState computeBody(Body b, double jdUt, double latDeg, double lonDeg, double heightM);

/* Altitude (deg) of a body for the observer at time jd. */
double bodyAltitude(Body b, double jdUt, double latDeg, double lonDeg);

/* Search for the next time (after jdStart, within spanDays) the body's altitude crosses
 * `alt` in the given direction (rising=true: upward). Returns 0 if none. */
double nextCrossing(Body b, double jdStart, double spanDays, double latDeg, double lonDeg, double alt, bool rising);

/* Same for a fixed RA/Dec (J2000) point. */
double nextCrossingFixed(double ra2000, double dec2000, double jdStart, double spanDays, double latDeg, double lonDeg,
                         double alt, bool rising);

/* Moon phase name from age fraction. */
const char *moonPhaseName(double jdUt);
double moonAgeDays(double jdUt);

/* B-V colour index -> RGB (0..255) approximation of star colour. */
void bvToRgb(double bv, uint8_t &r, uint8_t &g, uint8_t &b);

/* 16-point compass name for an azimuth. */
const char *compassPoint(double azDeg);

/* Unix seconds <-> calendar (UTC). */
void unixToCivil(int64_t t, int &y, int &mo, int &d, int &h, int &mi, int &s);
int64_t civilToUnix(int y, int mo, int d, int h, int mi, int s);

} // namespace astro
