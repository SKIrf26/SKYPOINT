#include "orient.h"
#include <math.h>

using namespace astro;

static double g_q[4] = { 1, 0, 0, 0 };   // smoothed
static bool g_have = false;
static int g_vote = 0;                   // >0: q maps device->world, <0: world->device
static double g_heading = 0;             // radians, rotation about Up applied after the IMU frame
static bool g_cal = false;
static double g_speed = 0;
static uint32_t g_alignSec = 0;

void orientReset()
{
    g_have = false; g_vote = 0; g_heading = 0; g_cal = false; g_speed = 0;
}

static Mat3 quatToMat(const double q[4])
{
    double w = q[0], x = q[1], y = q[2], z = q[3];
    Mat3 R = { { { 1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y) },
                 { 2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x) },
                 { 2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y) } } };
    return R;
}

void orientFeed(const float qin[4], float dt)
{
    double q[4] = { qin[0], qin[1], qin[2], qin[3] };
    double n = sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (n < 1e-6) return;
    for (int i = 0; i < 4; i++) q[i] /= n;
    if (!g_have) {
        for (int i = 0; i < 4; i++) g_q[i] = q[i];
        g_have = true;
        return;
    }
    double d = g_q[0] * q[0] + g_q[1] * q[1] + g_q[2] * q[2] + g_q[3] * q[3];
    if (d < 0) { for (int i = 0; i < 4; i++) q[i] = -q[i]; d = -d; }
    double ang = 2 * acos(fmin(1.0, d)) * R2D;           // degrees moved since last sample
    if (dt > 0) g_speed = g_speed * 0.8 + 0.2 * (ang / dt);
    /* adaptive low-pass: glassy-smooth when still, instant when swinging */
    double a = 0.10 + ang / 6.0;
    if (a > 1) a = 1;
    for (int i = 0; i < 4; i++) g_q[i] += (q[i] - g_q[i]) * a;
    n = sqrt(g_q[0] * g_q[0] + g_q[1] * g_q[1] + g_q[2] * g_q[2] + g_q[3] * g_q[3]);
    for (int i = 0; i < 4; i++) g_q[i] /= n;
}

void orientFeedAccel(const float a[3])
{
    if (!g_have) return;
    Vec3 v = normalize({ a[0], a[1], a[2] });
    Mat3 R = quatToMat(g_q);
    double z1 = mul(R, v).z, z2 = mul(transpose(R), v).z;
    if (fabs(fabs(z1) - fabs(z2)) > 0.25) {
        g_vote += fabs(z1) > fabs(z2) ? 1 : -1;
        if (g_vote > 60) g_vote = 60;
        if (g_vote < -60) g_vote = -60;
    }
}

bool orientReady() { return g_have; }
bool orientInverted() { return g_vote < -8; }

/* sensor -> ENU (before/after heading) */
static Mat3 worldFromSensor()
{
    Mat3 R = quatToMat(g_q);
    if (orientInverted()) R = transpose(R);
    double c = cos(g_heading), s = sin(g_heading);
    Mat3 Rz = { { { c, -s, 0 }, { s, c, 0 }, { 0, 0, 1 } } };
    return mul(Rz, R);
}

static Vec3 axisFix(const Vec3 &v, uint8_t k)
{
    switch (k & 3) {
    case 1: return { -v.y, v.x, v.z };
    case 2: return { -v.x, -v.y, v.z };
    case 3: return { v.y, -v.x, v.z };
    default: return v;
    }
}

static void appAxes(uint8_t mode, Vec3 &b, Vec3 &u, Vec3 &r)
{
    if (mode == 1) { b = { 0, 1, 0 }; u = { 0, 0, 1 }; r = { 1, 0, 0 }; }   // pointer: top edge forward
    else { b = { 0, 0, -1 }; u = { 0, 1, 0 }; r = { 1, 0, 0 }; }           // viewfinder: out of the back
}

Camera orientCamera(uint8_t mode, uint8_t k, double fov)
{
    Camera c;
    c.setFov(fov);
    Vec3 b, u, r;
    appAxes(mode, b, u, r);
    Mat3 W = worldFromSensor();
    c.fwd = normalize(mul(W, axisFix(b, k)));
    c.up = normalize(mul(W, axisFix(u, k)));
    c.right = normalize(mul(W, axisFix(r, k)));
    return c;
}

double orientBoresightAlt(uint8_t mode, uint8_t k)
{
    Camera c = orientCamera(mode, k, 60);
    double az, alt;
    enuToAzAlt(c.fwd, az, alt);
    return alt;
}

void orientAlignTo(const Vec3 &target, uint8_t mode, uint8_t k)
{
    double az, alt;
    enuToAzAlt(target, az, alt);
    orientAlignAzimuth(az, mode, k);
}

void orientAlignAzimuth(double azT, uint8_t mode, uint8_t k)
{
    Camera c = orientCamera(mode, k, 60);
    /* If the boresight is near the zenith the azimuth is undefined: use the screen-up vector instead. */
    Vec3 f = c.fwd;
    if (fabs(f.z) > 0.97) f = c.up;
    double azG = atan2(f.x, f.y);
    g_heading += azG - azT * D2R;
    g_heading = fmod(g_heading, 2 * A_PI);
    g_cal = true;
}

bool orientCalibrated() { return g_cal; }
double orientHeadingOffsetDeg() { return g_heading * R2D; }
double orientAngularSpeed() { return g_speed; }
void orientMarkAlignTime(uint32_t s) { g_alignSec = s; }
uint32_t orientAlignAgeSec(uint32_t s) { return g_cal ? s - g_alignSec : 0; }
