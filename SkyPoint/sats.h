/**
 * sats.h - satellite tracking (SGP4 via Hopperpop's Sgp4 library, MIT).
 * Up to SAT_MAX satellites from two-line element sets; ISS is built in.
 */
#pragma once
#include <stdint.h>
class Sgp4;
Sgp4 *satCloneFor(int i);
void satFreeClone(Sgp4 *c);

#define SAT_MAX 6

struct SatNow {
    double az, el;        // degrees
    double rangeKm, altKm;
    double lat, lon;
    int16_t vis;          // -2 below horizon, -1 sky too bright, 0 in Earth's shadow, >0 sunlit (1000 = fully)
};

struct SatPass {
    bool valid;
    double jdStart, jdMax, jdStop;
    double azStart, azMax, azStop;
    double maxEl;
    bool visible;         // sunlit while the sky is dark at some point of the pass
};

void satsBegin();                                   // loads the built-in ISS elements
bool satsAddTle(const char *name, const char *l1, const char *l2); // replaces a sat with the same NORAD id
int satsLoadTleText(const char *text);              // parse a 2- or 3-line TLE file; returns sats loaded
int satsCount();
const char *satName(int i);
double satEpochJd(int i);                           // TLE epoch (Julian date)
double satTleAgeDays(int i, double jdNow);
uint32_t satNorad(int i);
void satsSetSite(double lat, double lon, double altM);
bool satCompute(int i, double jd, SatNow &out);
/* Next pass that rises above minEl, starting at jd (searches up to ~3 days). */
bool satNextPass(int i, double jd, double minEl, SatPass &out);
/* Sample the sky track (az/el) between two times, n points. */
int satTrack(int i, double jd0, double jd1, int n, float *az, float *el);
/* Thread-safe (works on a private copy of the propagator): up to maxPasses passes from jd,
 * plus the sky track of the first pass if trackAz/trackEl are given. Returns passes found. */
int satPassesSafe(int i, double jd, double minEl, SatPass *out, int maxPasses, float *trackAz, float *trackEl, int trackN);
/* Serialise all loaded TLEs (3-line format) into buf; returns length. */
int satsSerialize(char *buf, int cap);
