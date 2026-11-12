/**
 * orient.h - turns the BHI260AP game-rotation-vector into a sky camera.
 *
 * The watch has no magnetometer, so the IMU knows "down" exactly but not "north".
 * Altitude is absolute; azimuth is fixed by aligning once on a known object (the Moon,
 * a planet, a bright star...) or by facing north. Gyro drift is small and the alignment
 * can be refreshed at any time with a long-press on a locked target.
 */
#pragma once
#include "astro.h"
#include "sky.h"

void orientReset();
/* Feed a new raw quaternion (w,x,y,z). Smoothing is applied internally. */
void orientFeed(const float q[4], float dtSec);
/* Feed an accelerometer sample to auto-detect the quaternion convention. */
void orientFeedAccel(const float a[3]);
bool orientReady();

/* Build the camera for the current pose. pointMode: 0 viewfinder, 1 pointer. */
Camera orientCamera(uint8_t pointMode, uint8_t axisRot, double fovDeg);

/* Heading alignment */
void orientAlignTo(const astro::Vec3 &targetEnu, uint8_t pointMode, uint8_t axisRot);
void orientAlignAzimuth(double azDeg, uint8_t pointMode, uint8_t axisRot);
bool orientCalibrated();
double orientHeadingOffsetDeg();
uint32_t orientAlignAgeSec(uint32_t nowSec);
void orientMarkAlignTime(uint32_t nowSec);

/* Raw pitch of the boresight (deg) - works without calibration. */
double orientBoresightAlt(uint8_t pointMode, uint8_t axisRot);
/* Angular speed (deg/s) of the boresight, smoothed. */
double orientAngularSpeed();
bool orientInverted();
