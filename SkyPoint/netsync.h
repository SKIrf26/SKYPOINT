/**
 * netsync.h - Wi-Fi helpers shared by both boards (Arduino only).
 */
#pragma once
#include <Arduino.h>

/* Connect with the stored credentials; returns true when associated. */
bool netConnect(const char *ssid, const char *pass, uint32_t timeoutMs, char *msg, size_t msgLen);
void netDisconnect();
/* SNTP; returns true when the system clock is set. */
bool netSyncTime(uint32_t timeoutMs);
/* Approximate position from the public IP address (ip-api.com, city level). */
bool netIpLocation(double &lat, double &lon, char *city, size_t cityLen);
/* ISS, Tiangong and Hubble element sets from CelesTrak, concatenated as TLE text. */
bool netFetchTles(String &out);
/* Blocking scan; fills up to max unique SSIDs sorted by signal. */
int netScan(char (*names)[33], int max);
