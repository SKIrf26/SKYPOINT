/**
 * astro.cpp - see astro.h. Formulae: Meeus "Astronomical Algorithms" (2nd ed.) for the Sun,
 * Moon, sidereal time and precession; E.M. Standish "Keplerian Elements for Approximate
 * Positions of the Major Planets" (JPL, table 1, 1800-2050) for the planets.
 */
#include "astro.h"
#include <string.h>

namespace astro {

static inline double fixAngle(double a)
{
    a = fmod(a, 360.0);
    return a < 0 ? a + 360.0 : a;
}
static inline double sind(double a) { return sin(a * D2R); }
static inline double cosd(double a) { return cos(a * D2R); }

Mat3 mul(const Mat3 &a, const Mat3 &b)
{
    Mat3 r;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
    return r;
}

Mat3 transpose(const Mat3 &a)
{
    Mat3 r;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) r.m[i][j] = a.m[j][i];
    return r;
}

Vec3 radecToVec(double ra, double dec)
{
    double cd = cosd(dec);
    return { cd * cosd(ra), cd * sind(ra), sind(dec) };
}

void vecToRadec(const Vec3 &v, double &ra, double &dec)
{
    double n = norm(v);
    ra = fixAngle(atan2(v.y, v.x) * R2D);
    dec = asin(v.z / n) * R2D;
}

void enuToAzAlt(const Vec3 &e, double &az, double &alt)
{
    double n = norm(e);
    az = fixAngle(atan2(e.x, e.y) * R2D);
    alt = asin(fmax(-1.0, fmin(1.0, e.z / n))) * R2D;
}

Vec3 azAltToEnu(double az, double alt)
{
    double ca = cosd(alt);
    return { ca * sind(az), ca * cosd(az), sind(alt) };
}

double julianDate(double unixSeconds) { return unixSeconds / 86400.0 + 2440587.5; }

double gmstDeg(double jd)
{
    double d = jd - 2451545.0, T = d / 36525.0;
    return fixAngle(280.46061837 + 360.98564736629 * d + 0.000387933 * T * T - T * T * T / 38710000.0);
}

Mat3 precessionFromJ2000(double jd)
{
    double T = (jd - 2451545.0) / 36525.0;
    double zeta = (2306.2181 * T + 0.30188 * T * T + 0.017998 * T * T * T) / 3600.0 * D2R;
    double z = (2306.2181 * T + 1.09468 * T * T + 0.018203 * T * T * T) / 3600.0 * D2R;
    double th = (2004.3109 * T - 0.42665 * T * T - 0.041833 * T * T * T) / 3600.0 * D2R;
    double cz = cos(zeta), sz = sin(zeta), cZ = cos(z), sZ = sin(z), ct = cos(th), st = sin(th);
    Mat3 P = { { { cz * ct * cZ - sz * sZ, -sz * ct * cZ - cz * sZ, -st * cZ },
                 { cz * ct * sZ + sz * cZ, -sz * ct * sZ + cz * cZ, -st * sZ },
                 { cz * st, -sz * st, ct } } };
    return P;
}

Mat3 equatorialToEnu(double jd, double lat, double lon)
{
    double lst = (gmstDeg(jd) + lon) * D2R;
    double cl = cos(lst), sl = sin(lst), cp = cosd(lat), sp = sind(lat);
    /* hour-angle frame: x toward meridian, y = -(toward west)... see derivation in astro.h comment */
    Mat3 Rz = { { { cl, sl, 0 }, { -sl, cl, 0 }, { 0, 0, 1 } } }; // v_h = Rz(-lst) v : x=cosd cosH, y=-cosd sinH
    Mat3 Rl = { { { 0, 1, 0 }, { -sp, 0, cp }, { cp, 0, sp } } };  // E = y_h, N = -sp x + cp z, U = cp x + sp z
    return mul(Rl, Rz);
}

const char *bodyName(Body b)
{
    static const char *n[] = { "Sun", "Moon", "Mercury", "Venus", "Mars", "Jupiter", "Saturn", "Uranus", "Neptune" };
    return b < BODY_COUNT ? n[b] : "?";
}

/* ---------------------------------------------------------------- Sun */
static double obliquityDeg(double T) { return 23.439291 - 0.0130042 * T; }

static Vec3 eclToEq(double lonDeg, double latDeg, double r, double epsDeg)
{
    double cb = cosd(latDeg);
    double x = r * cb * cosd(lonDeg), y = r * cb * sind(lonDeg), z = r * sind(latDeg);
    double ce = cosd(epsDeg), se = sind(epsDeg);
    return { x, y * ce - z * se, y * se + z * ce };
}

static void sunEcliptic(double T, double &lon, double &R)
{
    double L0 = 280.46646 + 36000.76983 * T + 0.0003032 * T * T;
    double M = 357.52911 + 35999.05029 * T - 0.0001537 * T * T;
    double e = 0.016708634 - 0.000042037 * T;
    double C = (1.914602 - 0.004817 * T - 0.000014 * T * T) * sind(M) + (0.019993 - 0.000101 * T) * sind(2 * M) +
               0.000289 * sind(3 * M);
    double nu = M + C;
    R = 1.000001018 * (1 - e * e) / (1 + e * cosd(nu));
    double omega = 125.04 - 1934.136 * T;
    lon = fixAngle(L0 + C - 0.00569 - 0.00478 * sind(omega));
}

/* ---------------------------------------------------------------- Moon (Meeus ch. 47, main terms) */
struct MoonTermLR { int8_t d, m, mp, f; int32_t l, r; };
static const MoonTermLR kMoonLR[] = {
    { 0, 0, 1, 0, 6288774, -20905355 }, { 2, 0, -1, 0, 1274027, -3699111 }, { 2, 0, 0, 0, 658314, -2955968 },
    { 0, 0, 2, 0, 213618, -569925 },    { 0, 1, 0, 0, -185116, 48888 },     { 0, 0, 0, 2, -114332, -3149 },
    { 2, 0, -2, 0, 58793, 246158 },     { 2, -1, -1, 0, 57066, -152138 },   { 2, 0, 1, 0, 53322, -170733 },
    { 2, -1, 0, 0, 45758, -204586 },    { 0, 1, -1, 0, -40923, -129620 },   { 1, 0, 0, 0, -34720, 108743 },
    { 0, 1, 1, 0, -30383, 104755 },     { 2, 0, 0, -2, 15327, 10321 },      { 0, 0, 1, 2, -12528, 0 },
    { 0, 0, 1, -2, 10980, 79661 },      { 4, 0, -1, 0, 10675, -34782 },     { 0, 0, 3, 0, 10034, -23210 },
    { 4, 0, -2, 0, 8548, -21636 },      { 2, 1, -1, 0, -7888, 24208 },      { 2, 1, 0, 0, -6766, 30824 },
    { 1, 0, -1, 0, -5163, -8379 },      { 1, 1, 0, 0, 4987, -16675 },       { 2, -1, 1, 0, 4036, -12831 },
    { 2, 0, 2, 0, 3994, -10445 },       { 4, 0, 0, 0, 3861, -11650 },       { 2, 0, -3, 0, 3665, 14403 },
    { 0, 1, -2, 0, -2689, -7003 },      { 2, 0, -1, 2, -2602, 0 },          { 2, -1, -2, 0, 2390, 10056 },
    { 1, 0, 1, 0, -2348, 6322 },        { 2, -2, 0, 0, 2236, -9884 },       { 0, 1, 2, 0, -2120, 5751 },
    { 0, 2, 0, 0, -2069, 0 },           { 2, -2, -1, 0, 2048, -4950 },      { 2, 0, 1, -2, -1773, 4130 },
    { 2, 0, 0, 2, -1595, 0 },           { 4, -1, -1, 0, 1215, -3958 },      { 0, 0, 2, 2, -1110, 0 },
    { 3, 0, -1, 0, -892, 3258 },        { 2, 1, 1, 0, -810, 2616 },         { 4, -1, -2, 0, 759, -1897 },
    { 0, 2, -1, 0, -713, -2117 },       { 2, 2, -1, 0, -700, 2354 },        { 2, 1, -2, 0, 691, 0 },
    { 2, -1, 0, -2, 596, 0 },           { 4, 0, 1, 0, 549, -1423 },         { 0, 0, 4, 0, 537, -1117 },
    { 4, -1, 0, 0, 520, -1571 },        { 1, 0, -2, 0, -487, -1739 },       { 2, 1, 0, -2, -399, 0 },
    { 0, 0, 2, -2, -381, -4421 },       { 1, 1, 1, 0, 351, 0 },             { 3, 0, -2, 0, -340, 0 },
    { 4, 0, -3, 0, 330, 0 },            { 2, -1, 2, 0, 327, 0 },            { 0, 2, 1, 0, -323, 1165 },
    { 1, 1, -1, 0, 299, 0 },            { 2, 0, 3, 0, 294, 0 },
};
struct MoonTermB { int8_t d, m, mp, f; int32_t b; };
static const MoonTermB kMoonB[] = {
    { 0, 0, 0, 1, 5128122 }, { 0, 0, 1, 1, 280602 }, { 0, 0, 1, -1, 277693 }, { 2, 0, 0, -1, 173237 },
    { 2, 0, -1, 1, 55413 },  { 2, 0, -1, -1, 46271 }, { 2, 0, 0, 1, 32573 },   { 0, 0, 2, 1, 17198 },
    { 2, 0, 1, -1, 9266 },   { 0, 0, 2, -1, 8822 },   { 2, -1, 0, -1, 8216 },  { 2, 0, -2, -1, 4324 },
    { 2, 0, 1, 1, 4200 },    { 2, 1, 0, -1, -3359 },  { 2, -1, -1, 1, 2463 },  { 2, -1, 0, 1, 2211 },
    { 2, -1, -1, -1, 2065 }, { 0, 1, -1, -1, -1870 }, { 4, 0, -1, -1, 1828 },  { 0, 1, 0, 1, -1794 },
    { 0, 0, 0, 3, -1749 },   { 0, 1, -1, 1, -1565 },  { 1, 0, 0, 1, -1491 },   { 0, 1, 1, 1, -1475 },
    { 0, 1, 1, -1, -1410 },  { 0, 1, 0, -1, -1344 },  { 1, 0, 0, -1, -1335 },  { 0, 0, 3, 1, 1107 },
    { 4, 0, 0, -1, 1021 },   { 4, 0, -1, 1, 833 },
};

static void moonEcliptic(double T, double &lon, double &lat, double &distKm)
{
    double Lp = 218.3164477 + 481267.88123421 * T - 0.0015786 * T * T;
    double D = 297.8501921 + 445267.1114034 * T - 0.0018819 * T * T;
    double M = 357.5291092 + 35999.0502909 * T - 0.0001536 * T * T;
    double Mp = 134.9633964 + 477198.8675055 * T + 0.0087414 * T * T;
    double F = 93.2720950 + 483202.0175233 * T - 0.0036539 * T * T;
    double A1 = 119.75 + 131.849 * T, A2 = 53.09 + 479264.290 * T, A3 = 313.45 + 481266.484 * T;
    double E = 1 - 0.002516 * T - 0.0000074 * T * T;
    double sl = 0, sr = 0, sb = 0;
    for (const auto &t : kMoonLR) {
        double arg = t.d * D + t.m * M + t.mp * Mp + t.f * F;
        double e = t.m == 0 ? 1 : (abs(t.m) == 1 ? E : E * E);
        sl += e * t.l * sind(arg);
        sr += e * t.r * cosd(arg);
    }
    for (const auto &t : kMoonB) {
        double arg = t.d * D + t.m * M + t.mp * Mp + t.f * F;
        double e = t.m == 0 ? 1 : (abs(t.m) == 1 ? E : E * E);
        sb += e * t.b * sind(arg);
    }
    sl += 3958 * sind(A1) + 1962 * sind(Lp - F) + 318 * sind(A2);
    sb += -2235 * sind(Lp) + 382 * sind(A3) + 175 * sind(A1 - F) + 175 * sind(A1 + F) + 127 * sind(Lp - Mp) -
          115 * sind(Lp + Mp);
    double omega = 125.04452 - 1934.136261 * T;
    lon = fixAngle(Lp + sl / 1e6 - 0.00478 * sind(omega)); // + nutation in longitude (main term)
    lat = sb / 1e6;
    distKm = 385000.56 + sr / 1000.0;
}

/* ---------------------------------------------------------------- Planets (JPL approx. elements) */
struct Elem { double a, e, I, L, wbar, node; };
static const Elem kEl0[] = {
    { 0.38709927, 0.20563593, 7.00497902, 252.25032350, 77.45779628, 48.33076593 },
    { 0.72333566, 0.00677672, 3.39467605, 181.97909950, 131.60246718, 76.67984255 },
    { 1.00000261, 0.01671123, -0.00001531, 100.46457166, 102.93768193, 0.0 },
    { 1.52371034, 0.09339410, 1.84969142, -4.55343205, -23.94362959, 49.55953891 },
    { 5.20288700, 0.04838624, 1.30439695, 34.39644051, 14.72847983, 100.47390909 },
    { 9.53667594, 0.05386179, 2.48599187, 49.95424423, 92.59887831, 113.66242448 },
    { 19.18916464, 0.04725744, 0.77263783, 313.23810451, 170.95427630, 74.01692503 },
    { 30.06992276, 0.00859048, 1.77004347, -55.12002969, 44.96476227, 131.78422574 },
};
static const Elem kElR[] = {
    { 0.00000037, 0.00001906, -0.00594749, 149472.67411175, 0.16047689, -0.12534081 },
    { 0.00000390, -0.00004107, -0.00078890, 58517.81538729, 0.00268329, -0.27769418 },
    { 0.00000562, -0.00004392, -0.01294668, 35999.37244981, 0.32327364, 0.0 },
    { 0.00001847, 0.00007882, -0.00813131, 19140.30268499, 0.44441088, -0.29257343 },
    { -0.00011607, -0.00013253, -0.00183714, 3034.74612775, 0.21252668, 0.20469106 },
    { -0.00125060, -0.00050991, 0.00193609, 1222.49362201, -0.41897216, -0.28867794 },
    { -0.00196176, -0.00004397, -0.00242939, 428.48202785, 0.40805281, 0.04240589 },
    { 0.00026291, 0.00005105, 0.00035372, 218.45945325, -0.32241464, -0.00508664 },
};

/* idx: 0 Mercury,1 Venus,2 Earth-Moon bary,3 Mars ... 7 Neptune. Heliocentric J2000 ecliptic, AU. */
static Vec3 helio(int idx, double T)
{
    const Elem &e0 = kEl0[idx], &r = kElR[idx];
    double a = e0.a + r.a * T, e = e0.e + r.e * T, I = e0.I + r.I * T, L = e0.L + r.L * T;
    double wb = e0.wbar + r.wbar * T, node = e0.node + r.node * T;
    double w = wb - node;
    double M = fmod(L - wb, 360.0);
    if (M > 180) M -= 360;
    if (M < -180) M += 360;
    double Mr = M * D2R, E = Mr + e * sin(Mr);
    for (int i = 0; i < 8; i++) {
        double dE = (Mr - (E - e * sin(E))) / (1 - e * cos(E));
        E += dE;
        if (fabs(dE) < 1e-12) break;
    }
    double xp = a * (cos(E) - e), yp = a * sqrt(1 - e * e) * sin(E);
    double cw = cosd(w), sw = sind(w), cO = cosd(node), sO = sind(node), cI = cosd(I), sI = sind(I);
    return { (cw * cO - sw * sO * cI) * xp + (-sw * cO - cw * sO * cI) * yp,
             (cw * sO + sw * cO * cI) * xp + (-sw * sO + cw * cO * cI) * yp,
             (sw * sI) * xp + (cw * sI) * yp };
}

static Vec3 eclJ2000ToEqJ2000(const Vec3 &v)
{
    const double ce = cos(23.43928 * D2R), se = sin(23.43928 * D2R);
    return { v.x, v.y * ce - v.z * se, v.y * se + v.z * ce };
}

/* ---------------------------------------------------------------- body dispatcher */
static void topocentricShift(Vec3 &geoKm, double jd, double lat, double lon, double h)
{
    double lst = (gmstDeg(jd) + lon) * D2R;
    const double Re = 6378.14, f = 1 / 298.257;
    double u = atan((1 - f) * tan(lat * D2R));
    double rcos = cos(u) + h / 1000.0 / Re * cos(lat * D2R);
    double rsin = (1 - f) * sin(u) + h / 1000.0 / Re * sin(lat * D2R);
    geoKm.x -= Re * rcos * cos(lst);
    geoKm.y -= Re * rcos * sin(lst);
    geoKm.z -= Re * rsin;
}

BodyState computeBody(Body b, double jd, double lat, double lon, double h)
{
    BodyState s;
    memset(&s, 0, sizeof(s));
    double T = (jd - 2451545.0) / 36525.0;
    double eps = obliquityDeg(T);
    double sunLon, sunR;
    sunEcliptic(T, sunLon, sunR);
    Vec3 sunEq = eclToEq(sunLon, 0, sunR, eps); // geocentric Sun, equator of date, AU
    double sunRa, sunDec;
    vecToRadec(sunEq, sunRa, sunDec);

    if (b == SUN) {
        s.ra = sunRa; s.dec = sunDec; s.distAu = sunR; s.distKm = sunR * 149597870.7;
        s.mag = -26.74; s.phase = 1; s.elongation = 0; s.angDiamArcsec = 1919.26 / sunR;
        return s;
    }

    if (b == MOON) {
        double ml, mb, mr;
        moonEcliptic(T, ml, mb, mr);
        Vec3 geo = eclToEq(ml, mb, mr, eps);
        topocentricShift(geo, jd, lat, lon, h);
        vecToRadec(geo, s.ra, s.dec);
        s.distKm = norm(geo);
        s.distAu = s.distKm / 149597870.7;
        Vec3 mu = normalize(geo), su = normalize(sunEq);
        double psi = acos(fmax(-1.0, fmin(1.0, dot(mu, su)))); // elongation
        s.elongation = psi * R2D;
        double R = sunR * 149597870.7;
        double i = atan2(R * sin(psi), s.distKm - R * cos(psi));
        s.phase = (1 + cos(i)) / 2;
        double ideg = fabs(i * R2D);
        s.mag = -12.73 + 0.026 * ideg + 4e-9 * pow(ideg, 4);
        s.angDiamArcsec = 2 * atan(1737.4 / s.distKm) * R2D * 3600;
        double dra = (sunRa - s.ra) * D2R;
        s.brightLimbDeg = fixAngle(atan2(cosd(sunDec) * sin(dra),
                                         sind(sunDec) * cosd(s.dec) - cosd(sunDec) * sind(s.dec) * cos(dra)) * R2D);
        return s;
    }

    int idx = (int)b - (int)MERCURY;           // 0..6 -> table index (skip Earth at 2)
    int tab = idx >= 2 ? idx + 1 : idx;
    Vec3 earth = helio(2, T);
    Vec3 p = helio(tab, T), g;
    double delta = 0;
    for (int it = 0; it < 2; it++) {          // light-time correction
        g = { p.x - earth.x, p.y - earth.y, p.z - earth.z };
        delta = norm(g);
        p = helio(tab, T - delta / 173.1446 / 36525.0);
    }
    g = { p.x - earth.x, p.y - earth.y, p.z - earth.z };
    delta = norm(g);
    Vec3 eq = mul(precessionFromJ2000(jd), eclJ2000ToEqJ2000(g));
    vecToRadec(eq, s.ra, s.dec);
    s.distAu = delta;
    s.distKm = delta * 149597870.7;
    double r = norm(p), R = norm(earth);
    double ci = (r * r + delta * delta - R * R) / (2 * r * delta);
    double i = acos(fmax(-1.0, fmin(1.0, ci))) * R2D;
    s.phase = (1 + cosd(i)) / 2;
    Vec3 su = normalize(sunEq), pu = normalize(eq);
    s.elongation = acos(fmax(-1.0, fmin(1.0, dot(su, pu)))) * R2D;
    double m5 = 5 * log10(r * delta);
    static const double diam[] = { 6.74, 16.92, 9.36, 196.94, 165.6, 65.8, 62.2 };
    s.angDiamArcsec = diam[idx] / delta;
    switch (b) {
    case MERCURY: s.mag = -0.42 + m5 + 0.0380 * i - 0.000273 * i * i + 0.000002 * i * i * i; break;
    case VENUS:   s.mag = -4.40 + m5 + 0.0009 * i + 0.000239 * i * i - 0.00000065 * i * i * i; break;
    case MARS:    s.mag = -1.52 + m5 + 0.016 * i; break;
    case JUPITER: s.mag = -9.40 + m5 + 0.005 * i; break;
    case SATURN:  s.mag = -8.88 + m5 + 0.044 * i; break;
    case URANUS:  s.mag = -7.19 + m5; break;
    case NEPTUNE: s.mag = -6.87 + m5; break;
    default: break;
    }
    return s;
}

double bodyAltitude(Body b, double jd, double lat, double lon)
{
    BodyState s = computeBody(b, jd, lat, lon, 0);
    Vec3 enu = mul(equatorialToEnu(jd, lat, lon), radecToVec(s.ra, s.dec));
    double az, alt;
    enuToAzAlt(enu, az, alt);
    return alt;
}

template <typename F>
static double crossing(F altFn, double jd0, double span, double target, bool rising)
{
    const double step = 10.0 / 1440.0;
    double t0 = jd0, a0 = altFn(t0) - target;
    for (double t1 = jd0 + step; t1 <= jd0 + span + 1e-9; t1 += step) {
        double a1 = altFn(t1) - target;
        bool hit = rising ? (a0 < 0 && a1 >= 0) : (a0 >= 0 && a1 < 0);
        if (hit) {
            double lo = t0, hi = t1, alo = a0;
            for (int i = 0; i < 18; i++) {
                double mid = (lo + hi) / 2, am = altFn(mid) - target;
                if ((am < 0) == (alo < 0)) { lo = mid; alo = am; } else hi = mid;
            }
            return (lo + hi) / 2;
        }
        t0 = t1; a0 = a1;
    }
    return 0;
}

double nextCrossing(Body b, double jd0, double span, double lat, double lon, double alt, bool rising)
{
    return crossing([&](double t) { return bodyAltitude(b, t, lat, lon); }, jd0, span, alt, rising);
}

double nextCrossingFixed(double ra, double dec, double jd0, double span, double lat, double lon, double alt, bool rising)
{
    Vec3 v2000 = radecToVec(ra, dec);
    Vec3 v = mul(precessionFromJ2000(jd0), v2000);
    return crossing([&](double t) {
        double az, a;
        enuToAzAlt(mul(equatorialToEnu(t, lat, lon), v), az, a);
        return a;
    }, jd0, span, alt, rising);
}

double moonAgeDays(double jd)
{
    double T = (jd - 2451545.0) / 36525.0, ml, mb, mr, sl, sr;
    moonEcliptic(T, ml, mb, mr);
    sunEcliptic(T, sl, sr);
    return fixAngle(ml - sl) / 360.0 * 29.530589;
}

const char *moonPhaseName(double jd)
{
    double a = moonAgeDays(jd) / 29.530589;
    if (a < 0.0339 || a > 0.9661) return "New Moon";
    if (a < 0.2161) return "Waxing Crescent";
    if (a < 0.2839) return "First Quarter";
    if (a < 0.4661) return "Waxing Gibbous";
    if (a < 0.5339) return "Full Moon";
    if (a < 0.7161) return "Waning Gibbous";
    if (a < 0.7839) return "Last Quarter";
    return "Waning Crescent";
}

void bvToRgb(double bv, uint8_t &r, uint8_t &g, uint8_t &b)
{
    if (bv < -0.4) bv = -0.4;
    if (bv > 2.0) bv = 2.0;
    double t = 4600.0 * (1.0 / (0.92 * bv + 1.7) + 1.0 / (0.92 * bv + 0.62)) / 100.0;
    double R, G, B;
    if (t <= 66) { R = 255; G = 99.4708025861 * log(t) - 161.1195681661; }
    else { R = 329.698727446 * pow(t - 60, -0.1332047592); G = 288.1221695283 * pow(t - 60, -0.0755148492); }
    if (t >= 66) B = 255;
    else if (t <= 19) B = 0;
    else B = 138.5177312231 * log(t - 10) - 305.0447927307;
    /* push saturation a little so colours read on the AMOLED */
    double avg = (R + G + B) / 3;
    R = avg + (R - avg) * 1.6; G = avg + (G - avg) * 1.6; B = avg + (B - avg) * 1.6;
    double mx = fmax(R, fmax(G, B));
    if (mx > 255) { R = R * 255 / mx; G = G * 255 / mx; B = B * 255 / mx; }
    r = (uint8_t)fmax(0, fmin(255, R)); g = (uint8_t)fmax(0, fmin(255, G)); b = (uint8_t)fmax(0, fmin(255, B));
}

const char *compassPoint(double az)
{
    static const char *p[] = { "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
                               "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW" };
    return p[(int)floor(fixAngle(az + 11.25) / 22.5) % 16];
}

/* Howard Hinnant's civil-from-days / days-from-civil. */
void unixToCivil(int64_t t, int &y, int &mo, int &d, int &h, int &mi, int &s)
{
    int64_t days = t >= 0 ? t / 86400 : (t - 86399) / 86400;
    int64_t rem = t - days * 86400;
    h = (int)(rem / 3600); mi = (int)(rem % 3600 / 60); s = (int)(rem % 60);
    days += 719468;
    int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    unsigned doe = (unsigned)(days - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t yy = (int64_t)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    d = (int)(doy - (153 * mp + 2) / 5 + 1);
    mo = (int)(mp < 10 ? mp + 3 : mp - 9);
    y = (int)(yy + (mo <= 2));
}

int64_t civilToUnix(int y, int mo, int d, int h, int mi, int s)
{
    y -= mo <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = era * 146097 + (int64_t)doe - 719468;
    return days * 86400 + h * 3600 + mi * 60 + s;
}

} // namespace astro
