/* SkyPoint for Android - point your phone at the sky.
 * Same look and behaviour as the T-Watch Ultra / Waveshare C6 firmware, rebuilt for a phone:
 * Astronomy Engine for the Sun, Moon and planets, satellite.js (SGP4) for satellites, the
 * same HYG star catalogue and Stellarium constellation figures as the watch.
 * Orientation, location, GNSS and vibration come from the Java shell through window.SkyPointNative;
 * in a desktop browser it falls back to touch-drag panning so the UI can be tried anywhere. */
'use strict';

const NAT = window.SkyPointNative || null;
const A = window.Astronomy;
const SAT = window.satellite;
const CAT = window.CATALOG;
const D2R = Math.PI / 180, R2D = 180 / Math.PI;
const $ = (s) => document.querySelector(s);
const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
let TOFF = 0;   // preview-only time shift
const nowMs = () => Date.now() + TOFF;

/* ------------------------------------------------------------------ settings */
const DEF = {
  night: false, constellations: true, grid: false, labels: true, belowHorizon: true, dso: true,
  stars: true, planets: true, sunMoon: true, sats: true, art: false, rotation: 'portrait', cfg: 2,
  camera: false, touchPan: false, fov: 60, volume: 60, haptics: true, passAlerts: true,
  lat: null, lon: null, alt: 0, locName: '', manualLoc: false, azOffset: 0,
};
const S = Object.assign({}, DEF, load('sp_settings', {}));
// settings migration: from v2 every object layer starts on and the constellation drawings start off
if ((S.cfg || 0) < 2) { Object.assign(S, { stars: true, constellations: true, planets: true, sunMoon: true, sats: true, dso: true, labels: true, art: false, cfg: 2 }); }
function load(k, d) { try { const v = localStorage.getItem(k); return v ? JSON.parse(v) : d; } catch (e) { return d; } }
function save(k, v) { try { localStorage.setItem(k, JSON.stringify(v)); } catch (e) {} }
const saveS = () => save('sp_settings', S);

/* ------------------------------------------------------------------ sound + haptics */
let AC = null;
function audio() {
  if (!AC) { try { AC = new (window.AudioContext || window.webkitAudioContext)(); } catch (e) { return null; } }
  if (AC.state === 'suspended') AC.resume();
  return AC;
}
const SFX = {
  tap: [[2400, 14]], key: [[2100, 12]], page: [[1500, 18], [2100, 22]], lock: [[2600, 16], [3400, 24]], tick: [[1800, 14]],
  found: [[1600, 60], [0, 15], [2000, 60], [0, 15], [2700, 140]], align: [[1300, 70], [1950, 70], [2600, 120]],
  gps: [[1900, 60], [0, 20], [2500, 60], [0, 20], [3200, 160]], alert: [[2000, 110], [0, 60], [2000, 110], [0, 60], [2700, 260]],
  boot: [[700, 60], [1000, 60], [1400, 60], [1900, 70], [2600, 180]], error: [[520, 140], [0, 40], [380, 220]],
  ok: [[1800, 70], [2700, 130]], zoom: [[1700, 14], [2300, 18]],
};
function sfx(name) {
  if (S.volume <= 0) return;
  const ac = audio(); if (!ac) return;
  const seq = SFX[name]; if (!seq) return;
  const g0 = (S.volume * S.volume) / 10000 * 0.22;
  let t = ac.currentTime + 0.005;
  for (const [f, ms] of seq) {
    const d = ms / 1000;
    if (f > 0) {
      const o = ac.createOscillator(), g = ac.createGain();
      o.type = 'square'; o.frequency.value = f;
      const o2 = ac.createOscillator(); o2.type = 'sine'; o2.frequency.value = f * 1.5;
      g.gain.setValueAtTime(0, t); g.gain.linearRampToValueAtTime(g0, t + 0.003);
      g.gain.setValueAtTime(g0, t + Math.max(0.004, d - 0.008)); g.gain.linearRampToValueAtTime(0, t + d);
      o.connect(g); o2.connect(g); g.connect(ac.destination);
      o.start(t); o2.start(t); o.stop(t + d + 0.01); o2.stop(t + d + 0.01);
    }
    t += d;
  }
}
function buzz(ms) {
  if (!S.haptics) return;
  if (NAT && NAT.vibrate) NAT.vibrate(ms); else if (navigator.vibrate) navigator.vibrate(ms);
}

/* ------------------------------------------------------------------ toast */
let toastT = 0;
function toast(msg, color, ms) {
  if (window.SP_SHOT) return;
  const el = $('#toast');
  el.textContent = msg; el.style.setProperty('--c', color || 'var(--hud)');
  el.classList.add('on'); clearTimeout(toastT);
  toastT = setTimeout(() => el.classList.remove('on'), ms || 2200);
}

/* ------------------------------------------------------------------ location */
const LOC = { lat: S.lat, lon: S.lon, alt: S.alt || 0, acc: 0, src: S.manualLoc ? 'manual' : (S.lat != null ? 'saved' : 'none'), name: S.locName, fixMs: 0 };
let gnss = { sats: [], ok: false };
let hadFix = false;
function setLocation(lat, lon, alt, src, name, acc) {
  LOC.lat = lat; LOC.lon = lon; LOC.alt = alt || 0; LOC.src = src; LOC.name = name || ''; LOC.acc = acc || 0; LOC.fixMs = nowMs();
  S.lat = lat; S.lon = lon; S.alt = LOC.alt; S.locName = LOC.name; S.manualLoc = src === 'manual'; saveS();
  observer = new A.Observer(lat, lon, LOC.alt);
  satObs = { latitude: lat * D2R, longitude: lon * D2R, height: LOC.alt / 1000 };
  declination = NAT && NAT.declination ? +NAT.declination(lat, lon, LOC.alt) || 0 : 0;
  invalidate();
  if (typeof page !== 'undefined' && page !== 'sky' && page !== 'gps') setTimeout(renderPage, 0);
}
function pollLocation() {
  if (S.manualLoc) return;
  let fix = null;
  if (NAT && NAT.location) { try { const s = NAT.location(); if (s) fix = JSON.parse(s); } catch (e) {} }
  if (fix && fix.lat != null) {
    const moved = LOC.lat == null || Math.abs(fix.lat - LOC.lat) > 0.01 || Math.abs(fix.lon - LOC.lon) > 0.01;
    const src = fix.provider === 'gps' ? 'gps' : 'network';
    if (moved) setLocation(fix.lat, fix.lon, fix.alt, src, '', fix.acc);
    else { LOC.acc = fix.acc; LOC.src = src; LOC.fixMs = nowMs(); if (fix.alt) LOC.alt = fix.alt; }
    if (src === 'gps' && !hadFix) { hadFix = true; toast('GPS locked', 'var(--aurora)'); sfx('gps'); buzz(30); }
  }
  if (NAT && NAT.gnss) { try { const g = JSON.parse(NAT.gnss() || '{}'); if (g.sats) gnss = { sats: g.sats, ok: true }; } catch (e) {} }
}
function browserGeo() {
  if (NAT || !navigator.geolocation || S.manualLoc) return;
  navigator.geolocation.watchPosition((p) => {
    const c = p.coords;
    if (LOC.lat == null || Math.abs(c.latitude - LOC.lat) > 0.01) setLocation(c.latitude, c.longitude, c.altitude || 0, 'network', '', c.accuracy);
    LOC.acc = c.accuracy; LOC.fixMs = nowMs();
  }, () => {}, { enableHighAccuracy: true, maximumAge: 60000 });
}
let observer = LOC.lat != null ? new A.Observer(LOC.lat, LOC.lon, LOC.alt) : null;
let satObs = LOC.lat != null ? { latitude: LOC.lat * D2R, longitude: LOC.lon * D2R, height: LOC.alt / 1000 } : null;
let declination = 0;

const CITIES = [
  ['Chicago', 41.878, -87.630], ['New York', 40.713, -74.006], ['Los Angeles', 34.052, -118.244], ['Denver', 39.739, -104.990],
  ['Houston', 29.760, -95.370], ['Dallas', 32.777, -96.797], ['Atlanta', 33.749, -84.388], ['Miami', 25.762, -80.192],
  ['Seattle', 47.606, -122.332], ['Phoenix', 33.448, -112.074], ['Minneapolis', 44.978, -93.265], ['Toronto', 43.653, -79.383],
  ['Mexico City', 19.433, -99.133], ['London', 51.507, -0.128], ['Paris', 48.857, 2.352], ['Berlin', 52.520, 13.405],
  ['Madrid', 40.417, -3.704], ['Rome', 41.903, 12.496], ['Cairo', 30.044, 31.236], ['Dubai', 25.205, 55.271],
  ['Mumbai', 19.076, 72.878], ['Singapore', 1.352, 103.820], ['Tokyo', 35.676, 139.650], ['Sydney', -33.869, 151.209],
  ['Auckland', -36.848, 174.763], ['Sao Paulo', -23.551, -46.633], ['Buenos Aires', -34.604, -58.382], ['Johannesburg', -26.204, 28.047],
];

/* ------------------------------------------------------------------ orientation */
// Camera basis in true-north ENU: fwd (where the back camera looks), right, up (screen top).
const cam = { fwd: [0, 1, 0], right: [1, 0, 0], up: [0, 0, 1], ok: false, acc: 3, lastMs: 0 };
let q = null;                       // smoothed quaternion [x,y,z,w], device -> magnetic ENU
let pan = { az: 180, alt: 35 };     // manual view
let sensorSeen = false;
function slerp(a, b, t) {
  let d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
  if (d < 0) { b = [-b[0], -b[1], -b[2], -b[3]]; d = -d; }
  let r;
  if (d > 0.9995) r = a.map((v, i) => v + (b[i] - v) * t);
  else { const th = Math.acos(d), s = Math.sin(th), wa = Math.sin((1 - t) * th) / s, wb = Math.sin(t * th) / s; r = a.map((v, i) => v * wa + b[i] * wb); }
  const n = Math.hypot(r[0], r[1], r[2], r[3]); return r.map((v) => v / n);
}
function trueNorth(v) {
  const d = (declination + S.azOffset) * D2R, c = Math.cos(d), s = Math.sin(d);
  return [v[0] * c + v[1] * s, -v[0] * s + v[1] * c, v[2]];
}
function updateOrientation() {
  let raw = null;
  if (NAT && NAT.orientation) {
    const s = NAT.orientation();
    if (s) { const p = s.split(',').map(Number); raw = [p[0], p[1], p[2], p[3]]; cam.acc = p[4]; }
  } else if (webQ) raw = webQ;
  if (raw && !S.touchPan) {
    sensorSeen = true;
    if (!q) q = raw;
    else {
      // adaptive smoothing: calm when still, quick when the phone swings
      const dot = Math.abs(q[0] * raw[0] + q[1] * raw[1] + q[2] * raw[2] + q[3] * raw[3]);
      const ang = 2 * Math.acos(Math.min(1, dot)) * R2D;
      q = slerp(q, raw, clamp(0.12 + ang * 0.12, 0.12, 0.9));
    }
    const [x, y, z, w] = q;
    // rotation matrix columns = device axes expressed in world (E,N,U)
    const c0 = [1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w)];
    const c1 = [2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w)];
    const c2 = [2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)];
    // screen axes in device terms: 90 = phone turned anticlockwise (top to the left), 270 = clockwise
    const neg = (v) => [-v[0], -v[1], -v[2]];
    let sr = c0, su = c1;
    if (screenRot === 90) { sr = neg(c1); su = c0; }
    else if (screenRot === 180) { sr = neg(c0); su = neg(c1); }
    else if (screenRot === 270) { sr = c1; su = neg(c0); }
    cam.right = trueNorth(sr); cam.up = trueNorth(su); cam.fwd = trueNorth(neg(c2));
    cam.ok = true;
  } else {
    const a = pan.az * D2R, e = pan.alt * D2R;
    cam.fwd = [Math.sin(a) * Math.cos(e), Math.cos(a) * Math.cos(e), Math.sin(e)];
    cam.right = [Math.cos(a), -Math.sin(a), 0];
    cam.up = cross(cam.right, cam.fwd);
    cam.ok = false;
  }
}
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const dot3 = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const azOf = (v) => (Math.atan2(v[0], v[1]) * R2D + 360) % 360;
const altOf = (v) => Math.asin(clamp(v[2], -1, 1)) * R2D;
const enu = (az, alt) => { const a = az * D2R, e = alt * D2R; return [Math.sin(a) * Math.cos(e), Math.cos(a) * Math.cos(e), Math.sin(e)]; };

// Browser fallback (W3C deviceorientationabsolute: alpha/beta/gamma, Z-X'-Y'')
let webQ = null;
function onWebOrient(e) {
  if (e.alpha == null) return;
  const a = e.alpha * D2R / 2, b = e.beta * D2R / 2, g = e.gamma * D2R / 2;
  const ca = Math.cos(a), sa = Math.sin(a), cb = Math.cos(b), sb = Math.sin(b), cg = Math.cos(g), sg = Math.sin(g);
  // q = qz(alpha) * qx(beta) * qy(gamma)
  webQ = [ca * sb * cg - sa * cb * sg, ca * cb * sg + sa * sb * cg, sa * cb * cg + ca * sb * sg, ca * cb * cg - sa * sb * sg];
}
if (!NAT) window.addEventListener('deviceorientationabsolute', onWebOrient);

/* ------------------------------------------------------------------ sky model */
const NSTAR = CAT.stars.length;
const starJ = new Float32Array(NSTAR * 3);   // J2000 unit vectors
const starW = new Float32Array(NSTAR * 3);   // current ENU
CAT.stars.forEach((s, i) => {
  const ra = s[0] * D2R, de = s[1] * D2R;
  starJ[i * 3] = Math.cos(de) * Math.cos(ra); starJ[i * 3 + 1] = Math.cos(de) * Math.sin(ra); starJ[i * 3 + 2] = Math.sin(de);
});
const DSO = CAT.dso.map((d) => ({ m: d[0], ra: d[1], dec: d[2], mag: d[3], type: d[4], name: d[5], w: [0, 0, 1] }));
const CONS = CAT.cons.map((c) => ({ abbr: c[0], name: c[1], ra: c[2], dec: c[3], w: [0, 0, 1] }));
const conName = (i) => (CAT.cons[i] ? CAT.cons[i][1] : '');
let M = null;  // EQJ -> ENU, 3x3 row-major
function eqjToEnu(x, y, z) {
  return [M[0] * x + M[1] * y + M[2] * z, M[3] * x + M[4] * y + M[5] * z, M[6] * x + M[7] * y + M[8] * z];
}
function raDecEnu(raDeg, decDeg) {
  const ra = raDeg * D2R, de = decDeg * D2R;
  return eqjToEnu(Math.cos(de) * Math.cos(ra), Math.cos(de) * Math.sin(ra), Math.sin(de));
}
const PLANETS = [
  ['Mercury', '#c9b8a6'], ['Venus', '#fff3c4'], ['Mars', '#ff7a4f'], ['Jupiter', '#ffd9a0'],
  ['Saturn', '#f2d58a'], ['Uranus', '#9fe8f0'], ['Neptune', '#7f9cff'],
];
let bodies = [];   // {name, kind, color, w, az, alt, mag, distAU, extra}
let sunAlt = -90, moonInfo = null;
function updateSky() {
  if (!observer) return;
  const t = A.MakeTime(new Date(nowMs()));
  const R = A.Rotation_EQJ_HOR(t, observer).rot;
  // HOR: x=north, y=west, z=zenith  ->  ENU: E=-y, N=x, U=z ; RotateVector uses rot[i][j]*v[i]
  M = [-R[0][1], -R[1][1], -R[2][1], R[0][0], R[1][0], R[2][0], R[0][2], R[1][2], R[2][2]];
  for (let i = 0; i < NSTAR; i++) {
    const w = eqjToEnu(starJ[i * 3], starJ[i * 3 + 1], starJ[i * 3 + 2]);
    starW[i * 3] = w[0]; starW[i * 3 + 1] = w[1]; starW[i * 3 + 2] = w[2];
  }
  DSO.forEach((d) => { d.w = raDecEnu(d.ra, d.dec); });
  CONS.forEach((c) => { c.w = raDecEnu(c.ra, c.dec); });
  updateArtWorld();
  const out = [];
  const add = (name, kind, color) => {
    const eq = A.Equator(name, t, observer, true, true);
    const h = A.Horizon(t, observer, eq.ra, eq.dec, 'normal');
    let mag = 0, ill = null;
    try { ill = A.Illumination(name, t); mag = ill.mag; } catch (e) {}
    const b = { name, kind, color, az: h.azimuth, alt: h.altitude, w: enu(h.azimuth, h.altitude), mag, distAU: eq.dist, ill };
    out.push(b); return b;
  };
  const sun = add('Sun', 'sun', '#ffd75e'); sunAlt = sun.alt;
  const moon = add('Moon', 'moon', '#e8eef5');
  moon.phase = A.MoonPhase(t); moon.frac = moon.ill ? moon.ill.phase_fraction : 0.5; moonInfo = moon;
  PLANETS.forEach(([n, c]) => add(n, 'planet', c));
  bodies = out;
}

/* ------------------------------------------------------------------ satellites */
const ISS_TLE = ['ISS', '1 25544U 98067A   26313.85251676  .00008451  00000-0  15976-3 0  9994',
  '2 25544  51.6322 299.2609 0007289   7.6181 352.4917 15.49281775594331'];
const TLE_IDS = [25544, 48274, 20580];
let sats = [];          // {name, rec, epochMs, now:{az,alt,w,range,height,lit}, track:[w], passes:[]}
let tleFetchedMs = load('sp_tle_ms', 0);
function tleChecksumOk(l) {
  let s = 0; for (let i = 0; i < 68; i++) { const c = l[i]; if (c >= '0' && c <= '9') s += +c; else if (c === '-') s += 1; }
  return s % 10 === +l[68];
}
function parseTles(text) {
  const lines = text.split(/\r?\n/).map((l) => l.trimEnd()).filter((l) => l.length);
  const out = [];
  for (let i = 0; i + 2 < lines.length + 1; i++) {
    const a = lines[i], b = lines[i + 1], c = lines[i + 2];
    if (b && c && b.startsWith('1 ') && c.startsWith('2 ') && b.length >= 69 && c.length >= 69 && tleChecksumOk(b) && tleChecksumOk(c)) {
      out.push([a.trim(), b, c]); i += 2;
    }
  }
  return out;
}
function niceSatName(n) {
  if (/ISS/.test(n)) return 'ISS';
  if (/CSS|TIANHE|TIANGONG/i.test(n)) return 'Tiangong';
  if (/HST|HUBBLE/i.test(n)) return 'Hubble';
  return n.replace(/\s+/g, ' ').slice(0, 18);
}
function loadSats(list) {
  const byName = {};
  list.forEach(([n, l1, l2]) => {
    try {
      const rec = SAT.twoline2satrec(l1, l2);
      const yy = +l1.slice(18, 20), day = +l1.slice(20, 32);
      const epochMs = Date.UTC(yy < 57 ? 2000 + yy : 1900 + yy, 0, 1) + (day - 1) * 86400000;
      const name = niceSatName(n);
      if (!byName[name] || byName[name].epochMs < epochMs) byName[name] = { name, rec, epochMs, now: null, track: [], passes: [] };
    } catch (e) {}
  });
  sats = Object.values(byName);
  passesAt = 0;
}
function initSats() {
  const stored = load('sp_tles', null);
  const list = stored && stored.length ? stored : [ISS_TLE];
  if (!list.some((t) => /ISS/.test(t[0]))) list.push(ISS_TLE);
  loadSats(list);
}
function updateTles(manual) {
  const urls = TLE_IDS.map((id) => `https://celestrak.org/NORAD/elements/gp.php?CATNR=${id}&FORMAT=TLE`);
  if (manual) toast('Updating orbits…');
  const done = (text) => {
    const list = parseTles(text || '');
    if (list.length) {
      const keep = load('sp_tles', []).filter((t) => !list.some((n) => niceSatName(n[0]) === niceSatName(t[0])));
      const all = list.concat(keep); save('sp_tles', all); tleFetchedMs = nowMs(); save('sp_tle_ms', tleFetchedMs);
      loadSats(all); if (manual) { toast(`Orbits updated (${list.length})`, 'var(--aurora)'); sfx('ok'); }
      renderPage();
    } else if (manual) { toast('Couldn’t update orbits. Check your connection.', 'var(--signal)', 3200); sfx('error'); }
  };
  if (NAT && NAT.fetchText) { window.SP_onFetch = (ok, text) => done(ok ? text : ''); NAT.fetchText(urls.join('\n')); }
  else Promise.all(urls.map((u) => fetch(u).then((r) => (r.ok ? r.text() : '')).catch(() => ''))).then((a) => done(a.join('\n')));
}
function sunEci(date) {
  const v = A.GeoVector('Sun', A.MakeTime(date), true);
  const n = Math.hypot(v.x, v.y, v.z); return [v.x / n, v.y / n, v.z / n];
}
function satLook(s, date, sunDir) {
  const pv = SAT.propagate(s.rec, date);
  if (!pv || !pv.position || isNaN(pv.position.x)) return null;
  const gmst = SAT.gstime(date);
  const ecf = SAT.eciToEcf(pv.position, gmst);
  const la = SAT.ecfToLookAngles(satObs, ecf);
  const az = la.azimuth * R2D, alt = la.elevation * R2D;
  const p = pv.position, r = Math.hypot(p.x, p.y, p.z);
  let lit = true;
  if (sunDir) {
    const proj = p.x * sunDir[0] + p.y * sunDir[1] + p.z * sunDir[2];
    if (proj < 0) { const px = p.x - proj * sunDir[0], py = p.y - proj * sunDir[1], pz = p.z - proj * sunDir[2]; lit = Math.hypot(px, py, pz) > 6371; }
  }
  return { az, alt, w: enu(az, alt), range: la.rangeSat, height: r - 6371, lit };
}
let sunDirCache = { ms: 0, v: [1, 0, 0] };
function sunDirNow() { const t = nowMs(); if (t - sunDirCache.ms > 60000) sunDirCache = { ms: t, v: sunEci(new Date(t)) }; return sunDirCache.v; }
let trackAt = 0;
function updateSats(t) {
  if (!satObs) return;
  const d = new Date(t), sd = sunDirNow();
  for (const s of sats) s.now = satLook(s, d, sd);
  if (t - trackAt > 3000) {
    trackAt = t;
    for (const s of sats) {
      s.track = [];
      for (let k = -6; k <= 9; k += 0.5) { const l = satLook(s, new Date(t + k * 60000), null); if (l) s.track.push([k, l.w]); }
    }
  }
}
// Pass finder: 60 s scan, bisection for rise/set, golden-section for culmination.
let passesAt = 0;
function elevAt(s, ms) { const l = satLook(s, new Date(ms), null); return l ? l.alt : -90; }
function findPasses(s, start, hours) {
  const out = [], end = start + hours * 3600000, step = 60000;
  let prevT = start, prevE = elevAt(s, start);
  let aos = prevE > 0 ? start : null;
  for (let t = start + step; t <= end; t += step) {
    const e = elevAt(s, t);
    if (prevE <= 0 && e > 0) { let a = prevT, b = t; for (let i = 0; i < 12; i++) { const m = (a + b) / 2; if (elevAt(s, m) > 0) b = m; else a = m; } aos = b; }
    if (prevE > 0 && e <= 0 && aos != null) {
      let a = prevT, b = t; for (let i = 0; i < 12; i++) { const m = (a + b) / 2; if (elevAt(s, m) > 0) a = m; else b = m; }
      const los = a;
      let lo = aos, hi = los; const g = 0.381966;
      for (let i = 0; i < 24; i++) { const m1 = lo + (hi - lo) * g, m2 = hi - (hi - lo) * g; if (elevAt(s, m1) < elevAt(s, m2)) lo = m1; else hi = m2; }
      const tmax = (lo + hi) / 2;
      const maxEl = elevAt(s, tmax);
      const ld = new Date(tmax), look = satLook(s, ld, sunEci(ld));
      let sAlt = 0; try { sAlt = sunAltAt(ld); } catch (e) {}
      const startLook = satLook(s, new Date(aos), null), endLook = satLook(s, new Date(los), null);
      out.push({ sat: s.name, aos, los, tmax, maxEl, visible: look && look.lit && sAlt < -6, fromAz: startLook ? startLook.az : 0, toAz: endLook ? endLook.az : 0 });
      aos = null;
    }
    prevT = t; prevE = e;
  }
  return out;
}
function sunAltAt(date) {
  const t = A.MakeTime(date), eq = A.Equator('Sun', t, observer, true, true);
  return A.Horizon(t, observer, eq.ra, eq.dec, 'normal').altitude;
}
function updatePasses(force) {
  if (!satObs || !sats.length) return;
  const t = nowMs();
  if (!force && t - passesAt < 10 * 60000) return;
  passesAt = t;
  for (const s of sats) s.passes = findPasses(s, t - 15 * 60000, 36).filter((p) => p.los > t);
}
const alerted = new Set();
function checkAlerts() {
  if (!S.passAlerts) return;
  const t = nowMs();
  for (const s of sats) for (const p of s.passes) {
    const key = s.name + Math.round(p.aos / 60000);
    if (p.visible && p.aos - t < 120000 && p.aos - t > 0 && !alerted.has(key)) {
      alerted.add(key); toast(`${s.name} rises in ${Math.max(1, Math.round((p.aos - t) / 60000))} min, look ${compass(p.fromAz)}`, 'var(--aurora)', 5000); sfx('alert'); buzz(120);
    }
  }
}
const compass = (az) => ['N', 'NNE', 'NE', 'ENE', 'E', 'ESE', 'SE', 'SSE', 'S', 'SSW', 'SW', 'WSW', 'W', 'WNW', 'NW', 'NNW'][Math.round(((az % 360) + 360) % 360 / 22.5) % 16];

/* ------------------------------------------------------------------ canvas + projection */
const cv = $('#sky'), cx2 = cv.getContext('2d');
let W = 0, H = 0, DPR = 1, VH = 0, CX = 0, CY = 0, F = 1, NAV = 64;
function resize() {
  DPR = Math.min(2, window.devicePixelRatio || 1);
  W = window.innerWidth; H = window.innerHeight;
  NAV = ($('#tabs').getBoundingClientRect().height || 64) + 28;
  cv.width = Math.round(W * DPR); cv.height = Math.round(H * DPR);
  VH = H - NAV; CX = W / 2; CY = VH / 2 + 10;
}
window.addEventListener('resize', () => { readScreenRot(); resize(); setTimeout(() => { readScreenRot(); resize(); }, 250); });
function setF() { F = (VH / 2) / Math.tan(S.fov * D2R / 2); }
// world ENU -> screen; returns null behind the viewer
function proj(w, out) { return projXYZ(w[0], w[1], w[2], out); }
function projXYZ(a, b, c, out) {
  const f = cam.fwd, z = a * f[0] + b * f[1] + c * f[2];
  if (z < 0.02) return null;
  const r = cam.right, u = cam.up;
  const x = a * r[0] + b * r[1] + c * r[2], y = a * u[0] + b * u[1] + c * u[2];
  out = out || [0, 0];
  out[0] = CX + (x / z) * F; out[1] = CY - (y / z) * F; return out;
}
const onScreen = (p, m) => p && p[0] > -m && p[0] < W + m && p[1] > -m && p[1] < VH + m;

/* ------------------------------------------------------------------ labels (no overlaps) */
let boxes = [];
function labelFits(x, y, w, h) {
  for (const b of boxes) if (x < b[2] && x + w > b[0] && y < b[3] && y + h > b[1]) return false;
  boxes.push([x, y, x + w, y + h]); return true;
}
let x0OfLabel = 0;   // the object's x, set by callers that want the left-side fallback
function label(txt, x, y, color, font, align) {
  const c = cx2; c.font = font || '500 14.0px Saira';
  const w = c.measureText(txt).width;
  const lx = align === 'center' ? x - w / 2 : x;
  let fx = lx;
  if (!labelFits(lx - 2, y - 12, w + 4, 16)) {
    if (align === 'center') return false;
    fx = 2 * x0OfLabel - lx - w;   // try the mirror position on the other side of the object
    if (!labelFits(fx - 2, y - 12, w + 4, 16)) return false;
  }
  c.fillStyle = color; c.textAlign = 'left'; c.fillText(txt, fx, y); return true;
}

/* ------------------------------------------------------------------ star colours */
function starColor(ci) {
  if (ci < -0.1) return '#a9c4ff'; if (ci < 0.15) return '#d4e2ff'; if (ci < 0.45) return '#f4f6ff';
  if (ci < 0.75) return '#fff6dc'; if (ci < 1.1) return '#ffe2b0'; if (ci < 1.45) return '#ffc98a'; return '#ffab70';
}
const SC = CAT.stars.map((s) => starColor(s[3]));

/* ------------------------------------------------------------------ targeting */
let target = null, targetSince = 0, locked = false, lockedKey = '';
let find = null, foundDone = false;   // find mode target {kind, key}
function keyOf(o) { return o.kind + ':' + o.name; }
function objectFor(k) {
  if (!k) return null;
  const [kind, name] = [k.slice(0, k.indexOf(':')), k.slice(k.indexOf(':') + 1)];
  if (kind === 'sat') { const s = sats.find((x) => x.name === name); return s && s.now ? { kind, name, w: s.now.w, sat: s } : null; }
  if (kind === 'star') { const i = CAT.stars.findIndex((s) => CAT.names[s[4]] === name); return i >= 0 ? starObj(i) : null; }
  if (kind === 'dso') { const d = DSO.find((x) => dsoName(x) === name); return d ? { kind, name, w: d.w, dso: d } : null; }
  const b = bodies.find((x) => x.name === name); return b ? { kind: b.kind, name, w: b.w, body: b } : null;
}
function starObj(i) {
  const s = CAT.stars[i];
  const nm = CAT.names[s[4]] || (s[6] ? s[6] + ' ' + (CAT.cons[s[5]] ? CAT.cons[s[5]][0] : '') : 'Star');
  return { kind: 'star', name: nm, w: [starW[i * 3], starW[i * 3 + 1], starW[i * 3 + 2]], star: s, idx: i };
}
const dsoName = (d) => 'M' + d.m + (d.name ? ' ' + d.name : '');
function pickNear(dir, maxDeg) {
  const cmin = Math.cos(maxDeg * D2R);
  let best = null, bestScore = -1;
  const consider = (o, w, bias) => {
    const c = dot3(dir, w); if (c < cmin) return;
    const score = c + bias; if (score > bestScore) { bestScore = score; best = o; }
  };
  for (const b of bodies) { if (b.kind === 'sun' && sunAlt < -2) continue; if (b.kind === 'planet' ? !S.planets : !S.sunMoon) continue; consider({ kind: b.kind, name: b.name, w: b.w, body: b }, b.w, 0.004); }
  if (S.sats) for (const s of sats) if (s.now && s.now.alt > -5) consider({ kind: 'sat', name: s.name, w: s.now.w, sat: s }, s.now.w, 0.004);
  for (let i = 0; i < NSTAR; i++) {
    const s = CAT.stars[i]; if (s[2] > 4.2 || !S.stars) continue;
    const c = dir[0] * starW[i * 3] + dir[1] * starW[i * 3 + 1] + dir[2] * starW[i * 3 + 2]; if (c < cmin) continue;
    const score = c + (s[4] ? 0.001 : 0) - s[2] * 0.0002;
    if (score > bestScore) { bestScore = score; best = starObj(i); }
  }
  if (S.dso) for (const d of DSO) consider({ kind: 'dso', name: dsoName(d), w: d.w, dso: d }, d.w, -0.0005);
  return best;
}


/* ------------------------------------------------------------------ constellation art */
// Each drawing is pinned to the sky by three anchor stars: project them, solve the affine map
// from image pixels to screen, and draw the image through it. Images load lazily the first time
// their constellation comes into view.
const ARTS = (window.ART || []).map(([abbr, file, w, h, an]) => ({ abbr, file, w, h, an: an.map(([x, y, ra, dec]) => ({ x, y, ra, dec, w: [0, 0, 1] })), img: null, ok: false, center: [0, 0, 1] }));
function updateArtWorld() {
  for (const a of ARTS) {
    let cx = 0, cy = 0, cz = 0;
    for (const p of a.an) { p.w = raDecEnu(p.ra, p.dec); cx += p.w[0]; cy += p.w[1]; cz += p.w[2]; }
    const n = Math.hypot(cx, cy, cz) || 1; a.center = [cx / n, cy / n, cz / n];
  }
}
function artImg(a) {
  if (!a.img) { a.img = new Image(); a.img.onload = () => { a.ok = true; }; a.img.src = a.file; }
  return a.ok ? a.img : null;
}
function affine(P, Q) {
  // solve u = a*x + c*y + e, v = b*x + d*y + f for three point pairs
  const [p0, p1, p2] = P;
  const det = p0[0] * (p1[1] - p2[1]) - p0[1] * (p1[0] - p2[0]) + (p1[0] * p2[1] - p2[0] * p1[1]);
  if (Math.abs(det) < 1e-9) return null;
  const solve = (r0, r1, r2) => [
    (r0 * (p1[1] - p2[1]) - p0[1] * (r1 - r2) + (r1 * p2[1] - r2 * p1[1])) / det,
    (p0[0] * (r1 - r2) - r0 * (p1[0] - p2[0]) + (p1[0] * r2 - p2[0] * r1)) / det,
    (p0[0] * (p1[1] * r2 - p2[1] * r1) - p0[1] * (p1[0] * r2 - p2[0] * r1) + r0 * (p1[0] * p2[1] - p2[0] * p1[1])) / det,
  ];
  const [a, c, e] = solve(Q[0][0], Q[1][0], Q[2][0]);
  const [b, d, f] = solve(Q[0][1], Q[1][1], Q[2][1]);
  return [a, b, c, d, e, f];
}
function drawArt() {
  if (!ARTS.length) return;
  const c = cx2, focus = target && target.kind === 'star' ? CAT.cons[target.star[5]] && CAT.cons[target.star[5]][0] : null;
  const cosView = Math.cos(Math.min(89, S.fov * 0.9) * D2R);
  const fade = clamp((115 - S.fov) / 40, 0.35, 1);
  c.save();
  c.globalCompositeOperation = 'lighter';
  for (const a of ARTS) {
    if (dot3(a.center, cam.fwd) < cosView - 0.35) continue;        // well outside the view
    if (!S.belowHorizon && a.center[2] < -0.15) continue;
    const Q = [];
    for (const p of a.an) { if (dot3(p.w, cam.fwd) < 0.25) break; const r = proj(p.w, [0, 0]); if (!r) break; Q.push(r); }
    if (Q.length < 3) continue;                                     // an anchor is too far off-axis: the map would smear
    const img = artImg(a); if (!img) continue;
    const m = affine(a.an.map((p) => [p.x, p.y]), Q); if (!m) continue;
    const scale = Math.sqrt(Math.abs(m[0] * m[3] - m[1] * m[2]));
    if (scale * a.w > 6 * Math.max(W, VH)) continue;               // degenerate near the edge of the view
    const sx = Math.hypot(m[0], m[1]), sy = Math.hypot(m[2], m[3]);
    if (Math.max(sx, sy) / Math.max(1e-6, Math.min(sx, sy)) > 3) continue;   // badly stretched
    let alpha = (focus === a.abbr ? 0.8 : 0.36) * fade;
    if (a.center[2] < 0) alpha *= 0.45;
    c.globalAlpha = alpha;
    c.setTransform(DPR * m[0], DPR * m[1], DPR * m[2], DPR * m[3], DPR * m[4], DPR * m[5]);
    c.drawImage(img, 0, 0, a.w, a.h);
  }
  c.restore();
  c.setTransform(DPR, 0, 0, DPR, 0, 0);
}

/* ------------------------------------------------------------------ drawing */
let lastSky = 0, frame = 0, fps = 0, fpsT = 0, fpsN = 0;
function drawSky(t) {
  const c = cx2;
  c.setTransform(DPR, 0, 0, DPR, 0, 0);
  boxes = [];
  reserveHud();
  // background: transparent over the camera, otherwise a sky tinted by the Sun's altitude
  if (S.camera && camOn) { c.clearRect(0, 0, W, H); c.fillStyle = 'rgba(0,0,0,.25)'; c.fillRect(0, 0, W, H); }
  else {
    const day = clamp((sunAlt + 12) / 18, 0, 1);
    const g = c.createLinearGradient(0, 0, 0, VH);
    g.addColorStop(0, `rgb(${2 + 6 * day},${4 + 22 * day},${10 + 46 * day})`);
    g.addColorStop(1, `rgb(${4 + 18 * day},${10 + 34 * day},${18 + 56 * day})`);
    c.fillStyle = g; c.fillRect(0, 0, W, H);
  }
  if (!observer) { drawNoLocation(); return; }
  drawGround();
  if (S.grid) drawGrid();
  drawHorizon();
  if (S.art) drawArt();
  if (S.constellations) drawConstellations();
  if (S.dso) drawDso();
  if (S.stars) drawStars();
  drawBodies();
  if (S.sats) drawSats(t);
  drawStarLabels();
  drawHud(t);
}
function polyline(pts, close) {
  const c = cx2, p = [0, 0]; let pen = false;
  c.beginPath();
  for (let i = 0; i < pts.length + (close ? 1 : 0); i++) {
    const r = proj(pts[i % pts.length], p);
    if (r && Math.abs(r[0] - CX) < 4 * W && Math.abs(r[1] - CY) < 4 * H) { if (pen) c.lineTo(r[0], r[1]); else { c.moveTo(r[0], r[1]); pen = true; } }
    else pen = false;
  }
  c.stroke();
}
const HORIZON = []; for (let a = 0; a < 360; a += 3) HORIZON.push(enu(a, 0));
function drawGround() {
  // shade below the horizon (not over the camera picture)
  if (S.camera && camOn) return;
  const c = cx2, falt = altOf(cam.fwd);
  c.fillStyle = 'rgba(6,24,20,.55)';
  if (falt < -55) { c.fillRect(0, 0, W, VH); return; }
  if (falt > 55) return;
  const pts = [];
  for (const h of HORIZON) { const r = proj(h, [0, 0]); if (r) pts.push(r); }
  if (pts.length < 2) return;
  // screen direction of "down" (towards the nadir)
  const dn = [-cam.right[2], cam.up[2]];
  const n = Math.hypot(dn[0], dn[1]) || 1; dn[0] /= n; dn[1] /= n;
  const along = (p) => (p[0] - CX) * -dn[1] + (p[1] - CY) * dn[0];
  pts.sort((a, b) => along(a) - along(b));
  const L = 5000;
  c.beginPath(); c.moveTo(pts[0][0] - dn[1] * -L * 0, pts[0][1]);
  for (const p of pts) c.lineTo(p[0], p[1]);
  const e = pts[pts.length - 1], s0 = pts[0];
  c.lineTo(e[0] + dn[0] * L, e[1] + dn[1] * L); c.lineTo(s0[0] + dn[0] * L, s0[1] + dn[1] * L);
  c.closePath(); c.fill();
}
function drawHorizon() {
  const c = cx2;
  c.strokeStyle = 'rgba(125,226,238,.5)'; c.lineWidth = 1; polyline(HORIZON, true);
  const p = [0, 0];
  [['N', 0], ['NE', 45], ['E', 90], ['SE', 135], ['S', 180], ['SW', 225], ['W', 270], ['NW', 315]].forEach(([n, a]) => {
    const r = proj(enu(a, 0), p); if (!onScreen(r, 20)) return;
    const big = n.length === 1;
    c.font = big ? '600 18.4px Saira' : '500 13.0px Saira'; c.textAlign = 'center';
    c.fillStyle = n === 'N' ? '#ff6f86' : 'rgba(238,243,246,.8)'; c.fillText(n, r[0], r[1] - 8);
    c.fillRect(r[0] - 0.75, r[1] - 4, 1.5, 8);
  });
}
function drawGrid() {
  const c = cx2; c.strokeStyle = 'rgba(125,226,238,.13)'; c.lineWidth = 1;
  for (const e of [15, 30, 45, 60, 75]) { const pts = []; for (let a = 0; a < 360; a += 4) pts.push(enu(a, e)); polyline(pts, true); }
  for (let a = 0; a < 360; a += 30) { const pts = []; for (let e = -10; e <= 88; e += 4) pts.push(enu(a, e)); polyline(pts, false); }
}
// Same size rule as drawStars, so a joint and the star on top of it always match.
function starRadius(mag) {
  const zoom = clamp(60 / S.fov, 0.6, 3), lim = clamp(4.2 + Math.log2(zoom) * 1.4, 3.6, 6.5);
  return Math.max(0.9, (lim + 1.4 - mag) * 0.78 * Math.sqrt(zoom));
}
const JOINTS = [...new Set(CAT.segs)];
function drawConstellations() {
  const c = cx2, sg = CAT.segs, pa = [0, 0], pb = [0, 0];
  // every star the figures connect gets drawn at its true scale (size by brightness, colour by
  // temperature), even when it's fainter than the current star limit or the Stars layer is off,
  // so lines always meet a star instead of stopping at an empty gap
  const scr = new Map();
  for (const i of JOINTS) {
    const z = starW[i * 3 + 2];
    if (!S.belowHorizon && z < 0) continue;
    const r = projXYZ(starW[i * 3], starW[i * 3 + 1], z, [0, 0]); if (!r || !onScreen(r, 300)) continue;
    scr.set(i, [r[0], r[1], Math.max(1.7, starRadius(CAT.stars[i][2]))]);
  }
  c.strokeStyle = 'rgba(125,226,238,.26)'; c.lineWidth = 1;
  c.beginPath();
  for (let i = 0; i < sg.length; i += 2) {
    const A1 = scr.get(sg[i]), B1 = scr.get(sg[i + 1]); if (!A1 || !B1) continue;
    const dx = B1[0] - A1[0], dy = B1[1] - A1[1], L = Math.hypot(dx, dy);
    const ga = A1[2] + 3, gb = B1[2] + 3;   // stop just short of each star's disc
    if (L < ga + gb + 2) continue;
    c.moveTo(A1[0] + dx * ga / L, A1[1] + dy * ga / L); c.lineTo(B1[0] - dx * gb / L, B1[1] - dy * gb / L);
  }
  c.stroke();
  const day = clamp((sunAlt + 8) / 10, 0, 1);
  for (const [i, [x, y, rad]] of scr) {
    if (!onScreen([x, y], 6)) continue;
    const below = starW[i * 3 + 2] < 0;
    const base = (below ? 0.35 : 1) * (1 - day * 0.55);
    // faint ring in the figure colour marks it as part of the constellation
    c.globalAlpha = base * 0.5; c.strokeStyle = 'rgba(125,226,238,.9)'; c.lineWidth = 0.8;
    c.beginPath(); c.arc(x, y, rad + 2.2, 0, 2 * Math.PI); c.stroke();
    if (CAT.stars[i][2] < 1.6) { c.globalAlpha = base * 0.18; c.fillStyle = SC[i]; c.beginPath(); c.arc(x, y, rad * 2.6, 0, 2 * Math.PI); c.fill(); }
    c.globalAlpha = base; c.fillStyle = SC[i]; c.beginPath(); c.arc(x, y, rad, 0, 2 * Math.PI); c.fill();
  }
  c.globalAlpha = 1;
  if (S.labels && S.fov < 100) {
    const p = [0, 0];
    for (const k of CONS) {
      if (!S.belowHorizon && k.w[2] < 0) continue;
      const r = proj(k.w, p); if (!onScreen(r, 0)) continue;
      label(k.name, r[0], r[1], 'rgba(125,226,238,.42)', '400 13.0px Saira', 'center');
    }
  }
}
function drawDso() {
  const c = cx2, p = [0, 0], lim = S.fov < 40 ? 9.5 : S.fov < 70 ? 7.5 : 6;
  for (const d of DSO) {
    if (d.mag > lim) continue;
    if (!S.belowHorizon && d.w[2] < 0) continue;
    const r = proj(d.w, p); if (!onScreen(r, 10)) continue;
    const below = d.w[2] < 0;
    c.globalAlpha = below ? 0.3 : 0.8; c.strokeStyle = '#b98cff'; c.lineWidth = 1.2;
    c.beginPath();
    if (d.type === 'Galaxy') c.ellipse(r[0], r[1], 7, 3.5, -0.5, 0, 2 * Math.PI);
    else if (d.type === 'GC') { c.arc(r[0], r[1], 5, 0, 2 * Math.PI); c.moveTo(r[0] - 5, r[1]); c.lineTo(r[0] + 5, r[1]); c.moveTo(r[0], r[1] - 5); c.lineTo(r[0], r[1] + 5); }
    else c.arc(r[0], r[1], 5, 0, 2 * Math.PI);
    c.stroke();
    x0OfLabel = r[0];
    if (S.labels && (d.mag < 6 || S.fov < 50)) label('M' + d.m, r[0] + 8, r[1] + 4, 'rgba(185,140,255,.85)', '400 12.1px Saira');
    c.globalAlpha = 1;
  }
}
let starLabels = [];
function drawStarLabels() {
  // after planets + satellites so those always win the label space
  for (const [n, x, y, rad] of starLabels) { x0OfLabel = x; label(n, x + rad + 5, y + 4, 'rgba(238,243,246,.78)', '500 14.0px Saira'); }
  starLabels = [];
}
function drawStars() {
  const c = cx2, p = [0, 0], st = CAT.stars;
  const zoom = clamp(60 / S.fov, 0.6, 3);
  const lim = clamp(4.2 + Math.log2(zoom) * 1.4, 3.6, 6.5);
  const day = clamp((sunAlt + 8) / 10, 0, 1);   // stars fade into daylight
  const dayLim = lim - day * 4.5;
  for (let i = 0; i < NSTAR; i++) {
    const s = st[i], mag = s[2]; if (mag > dayLim) continue;
    const z = starW[i * 3 + 2];
    if (z < 0 && !S.belowHorizon) continue;
    const r = projXYZ(starW[i * 3], starW[i * 3 + 1], z, p); if (!onScreen(r, 6)) continue;
    const rad = starRadius(mag);
    c.globalAlpha = (z < 0 ? 0.3 : 1) * clamp((dayLim + 1 - mag) / 1.2, 0.45, 1);
    if (mag < 1.6) { c.fillStyle = SC[i]; c.globalAlpha *= 0.18; c.beginPath(); c.arc(r[0], r[1], rad * 2.6, 0, 2 * Math.PI); c.fill(); c.globalAlpha /= 0.18; }
    c.fillStyle = SC[i]; c.beginPath(); c.arc(r[0], r[1], rad, 0, 2 * Math.PI); c.fill();
    if (S.labels && s[4] && mag < (S.fov < 45 ? 3.5 : 2.2)) starLabels.push([CAT.names[s[4]], r[0], r[1], rad]);
  }
  c.globalAlpha = 1;
}
function drawMoon(x, y, r, frac, phase) {
  const c = cx2;
  c.fillStyle = '#1b2530'; c.beginPath(); c.arc(x, y, r, 0, 2 * Math.PI); c.fill();
  c.fillStyle = '#e8eef5'; c.beginPath();
  const waxing = phase < 180, k = 1 - 2 * frac;  // terminator ellipse x-scale
  c.arc(x, y, r, -Math.PI / 2, Math.PI / 2, !waxing);
  c.ellipse(x, y, Math.abs(k) * r, r, 0, Math.PI / 2, -Math.PI / 2, waxing ? k > 0 : k < 0);
  c.fill();
}
function drawBodies() {
  const c = cx2, p = [0, 0], zoom = clamp(60 / S.fov, 0.6, 4);
  for (const b of bodies) {
    if (b.kind === 'planet' ? !S.planets : !S.sunMoon) continue;
    if (b.alt < 0 && !S.belowHorizon) continue;
    const r = proj(b.w, p); if (!onScreen(r, 40)) continue;
    c.globalAlpha = b.alt < 0 ? 0.4 : 1;
    if (b.kind === 'sun') {
      const R = 13 * Math.sqrt(zoom);
      const g = c.createRadialGradient(r[0], r[1], 0, r[0], r[1], R * 3); g.addColorStop(0, 'rgba(255,215,94,.55)'); g.addColorStop(1, 'rgba(255,215,94,0)');
      c.fillStyle = g; c.beginPath(); c.arc(r[0], r[1], R * 3, 0, 2 * Math.PI); c.fill();
      c.fillStyle = '#ffd75e'; c.beginPath(); c.arc(r[0], r[1], R, 0, 2 * Math.PI); c.fill();
    } else if (b.kind === 'moon') {
      const R = 12 * Math.sqrt(zoom);
      c.fillStyle = 'rgba(232,238,245,.12)'; c.beginPath(); c.arc(r[0], r[1], R * 2.2, 0, 2 * Math.PI); c.fill();
      drawMoon(r[0], r[1], R, b.frac, b.phase);
    } else {
      const R = clamp(5.2 - b.mag * 0.5, 2.8, 7) * Math.sqrt(zoom);
      c.fillStyle = b.color; c.globalAlpha *= 0.22; c.beginPath(); c.arc(r[0], r[1], R * 2.4, 0, 2 * Math.PI); c.fill(); c.globalAlpha = b.alt < 0 ? 0.4 : 1;
      c.beginPath(); c.arc(r[0], r[1], R, 0, 2 * Math.PI); c.fill();
      if (b.name === 'Saturn') { c.strokeStyle = b.color; c.lineWidth = 1.3; c.beginPath(); c.ellipse(r[0], r[1], R * 2.1, R * 0.7, -0.35, 0, 2 * Math.PI); c.stroke(); }
    }
    x0OfLabel = r[0];
    if (S.labels || b.kind !== 'planet') label(b.name, r[0] + 14, r[1] + 5, b.kind === 'planet' ? b.color : '#eef3f6', '600 15.1px Saira');
    c.globalAlpha = 1;
  }
}
function drawSats(t) {
  const c = cx2, p = [0, 0];
  for (const s of sats) {
    if (!s.now) continue;
    if (s.now.alt < -10 && !(find && find === 'sat:' + s.name)) continue;
    // track: past solid, future dashed
    c.lineWidth = 1.4;
    const past = s.track.filter((x) => x[0] <= 0).map((x) => x[1]), fut = s.track.filter((x) => x[0] >= 0).map((x) => x[1]);
    past.push(s.now.w); fut.unshift(s.now.w);
    c.strokeStyle = 'rgba(108,240,195,.35)'; c.setLineDash([]); polyline(past, false);
    c.strokeStyle = 'rgba(108,240,195,.7)'; c.setLineDash([5, 5]); polyline(fut, false); c.setLineDash([]);
    const r = proj(s.now.w, p); if (!onScreen(r, 20)) continue;
    const pulse = 1 + 0.25 * Math.sin(t / 220);
    c.globalAlpha = s.now.alt < 0 ? 0.45 : 1;
    c.strokeStyle = s.now.lit ? '#6cf0c3' : '#5b7480'; c.lineWidth = 1.6;
    c.beginPath(); c.arc(r[0], r[1], 7 * pulse, 0, 2 * Math.PI); c.stroke();
    c.fillStyle = s.now.lit ? '#6cf0c3' : '#5b7480'; c.beginPath(); c.arc(r[0], r[1], 2.6, 0, 2 * Math.PI); c.fill();
    x0OfLabel = r[0];
    label(s.name, r[0] + 12, r[1] - 8, s.now.lit ? '#6cf0c3' : '#8b98a3', '600 15.1px Saira');
    c.globalAlpha = 1;
  }
}
function drawNoLocation() {
  const c = cx2;
  c.textAlign = 'center'; c.fillStyle = '#eef3f6'; c.font = '600 23.8px Saira'; c.fillText('Waiting for your location', CX, CY - 8);
  c.fillStyle = '#8b98a3'; c.font = '400 16.2px Saira'; c.fillText('Allow location, or choose a city on the Location page', CX, CY + 20);
}
function roundRect(x, y, w, h, r) { const c = cx2; c.beginPath(); c.moveTo(x + r, y); c.arcTo(x + w, y, x + w, y + h, r); c.arcTo(x + w, y + h, x, y + h, r); c.arcTo(x, y + h, x, y, r); c.arcTo(x, y, x + w, y, r); c.closePath(); }
function reserveHud() {
  const top = HUD_TOP();
  boxes.push([CX - 170, top, CX + 170, top + 92]);
  const lh = Math.min(240, VH * 0.4);
  boxes.push([W - 64, CY - lh / 2 - 10, W, CY + lh / 2 + 30]);
  boxes.push([CX - 46, CY - 46, CX + 46, CY + 46]);
  if (target) boxes.push([CX - 190, VH - 132, CX + 190, VH]);
}
const HUD_TOP = () => (window.SP_INSET_TOP || 0) + 14;
const COL = { accent: '#7de2ee', ember: '#ffb35c', aurora: '#6cf0c3', signal: '#ff6f86', text: '#eef3f6', text2: '#8b98a3', text3: '#55616b', warn: '#ffd166' };
const F_SANS = (w, s) => `${w} ${Math.round(s * 1.08 * 10) / 10}px Saira`, F_MONO = (w, s) => `${Math.max(500, w)} ${Math.round(s * 1.12 * 10) / 10}px Saira`;
let fovShownAt = 0, lockAnimAt = 0;
function pill(txt, x, y, color, bg) {
  const c = cx2; c.font = F_SANS(500, 13);
  const w = c.measureText(txt).width + 30, h = 28;
  c.fillStyle = bg || 'rgba(14,19,24,.78)'; roundRect(x - w / 2, y - h / 2, w, h, 14); c.fill();
  c.strokeStyle = 'rgba(255,255,255,.1)'; c.lineWidth = 1; roundRect(x - w / 2 + .5, y - h / 2 + .5, w - 1, h - 1, 13.5); c.stroke();
  c.fillStyle = color; c.beginPath(); c.arc(x - w / 2 + 14, y, 3.5, 0, 2 * Math.PI); c.fill();
  c.fillStyle = COL.text; c.textAlign = 'left'; c.fillText(txt, x - w / 2 + 23, y + 4.5);
}
function drawHud(t) {
  const c = cx2;
  const az = azOf(cam.fwd), alt = altOf(cam.fwd);
  const top = HUD_TOP();
  // soft scrims keep the HUD legible over the camera or a bright sky
  let g = c.createLinearGradient(0, 0, 0, top + 120); g.addColorStop(0, 'rgba(0,0,0,.55)'); g.addColorStop(1, 'rgba(0,0,0,0)');
  c.fillStyle = g; c.fillRect(0, 0, W, top + 120);
  g = c.createLinearGradient(0, VH - 170, 0, H); g.addColorStop(0, 'rgba(0,0,0,0)'); g.addColorStop(1, 'rgba(0,0,0,.6)');
  c.fillStyle = g; c.fillRect(0, VH - 170, W, H - VH + 170);

  // heading readout + hairline compass tape
  c.textAlign = 'center';
  c.font = F_MONO(500, 30); c.fillStyle = COL.text;
  const hd = Math.round(az) % 360, hdTxt = hd.toString().padStart(3, '0') + '°';
  c.fillText(hdTxt, CX - 14, top + 30);
  const hw = c.measureText(hdTxt).width;
  c.font = F_SANS(500, 15); c.fillStyle = COL.accent; c.textAlign = 'left';
  c.fillText(compass(az), CX - 14 + hw / 2 + 6, top + 30);
  const tw = Math.min(W - 48, 340), ty = top + 54, pxPerDeg = tw / 100;
  c.save();
  const mask = c.createLinearGradient(CX - tw / 2, 0, CX + tw / 2, 0);
  mask.addColorStop(0, 'rgba(125,226,238,0)'); mask.addColorStop(.2, 'rgba(125,226,238,.55)'); mask.addColorStop(.8, 'rgba(125,226,238,.55)'); mask.addColorStop(1, 'rgba(125,226,238,0)');
  for (let d = Math.floor(az - 50); d <= az + 50; d++) {
    const dd = ((d % 360) + 360) % 360, x = CX + (d - az) * pxPerDeg;
    if (dd % 5) continue;
    const fade = 1 - Math.min(1, Math.abs(x - CX) / (tw / 2));
    const major = dd % 45 === 0, mid = dd % 15 === 0;
    c.globalAlpha = Math.pow(fade, 0.7) * (major ? 1 : mid ? .7 : .4);
    c.fillStyle = dd === 0 ? COL.signal : major ? COL.text : COL.accent;
    c.fillRect(x - .5, ty - (major ? 7 : mid ? 5 : 3), 1, major ? 7 : mid ? 5 : 3);
    if (major) { c.font = F_SANS(600, 11); c.textAlign = 'center'; c.fillText(compass(dd), x, ty + 15); }
  }
  c.globalAlpha = 1; c.restore();
  c.fillStyle = COL.accent; c.beginPath(); c.moveTo(CX, ty - 10); c.lineTo(CX - 4, ty - 16); c.lineTo(CX + 4, ty - 16); c.fill();

  // status pill (only when it matters)
  let st = null;
  if (find) { const o = objectFor(find); if (o) st = [`Finding ${o.name} · ${Math.round(Math.acos(clamp(dot3(o.w, cam.fwd), -1, 1)) * R2D)}°`, COL.ember]; }
  else if (!cam.ok) st = [S.touchPan ? 'Touch pan · drag to look' : 'No motion sensor · drag to look', COL.warn];
  else if (cam.acc < 2) st = ['Compass unsure · wave in a figure-8', COL.warn];
  else if (nowMs() - fovShownAt < 1600) st = [`Field of view ${Math.round(S.fov)}°`, COL.accent];
  if (st) pill(st[0], CX, top + 88, st[1]);

  // altitude scale (right edge), true projection spacing
  const lx = W - 30, lh = Math.min(240, VH * 0.4), ly = CY - lh / 2;
  const stepDeg = S.fov < 35 ? 1 : 5, labDeg = S.fov < 35 ? 5 : 10;
  for (let e = Math.ceil((alt - 80) / stepDeg) * stepDeg; e <= alt + 80; e += stepDeg) {
    if (e < -90 || e > 90) continue;
    const dE = (e - alt) * D2R; if (Math.abs(dE) > 1.4) continue;
    const y = CY - Math.tan(dE) * F;
    if (y < ly || y > ly + lh) continue;
    const fade = 1 - Math.abs(y - CY) / (lh / 2);
    const major = e % labDeg === 0;
    c.globalAlpha = (0.3 + 0.7 * Math.max(0, fade)) * (major ? .95 : .5);
    c.fillStyle = e === 0 ? COL.ember : COL.accent;
    c.fillRect(lx - (major ? 8 : 4), y - .5, major ? 8 : 4, 1);
    if (major) { c.font = F_MONO(400, 10.5); c.textAlign = 'right'; c.fillStyle = COL.text2; c.fillText(e, lx - 12, y + 3.5); }
  }
  c.globalAlpha = 1;
  c.fillStyle = COL.accent; c.beginPath(); c.moveTo(lx + 2, CY); c.lineTo(lx + 8, CY - 4); c.lineTo(lx + 8, CY + 4); c.fill();
  c.font = F_MONO(500, 13); c.textAlign = 'right'; c.fillStyle = COL.text;
  c.fillText(`${alt >= 0 ? '+' : '−'}${Math.abs(alt).toFixed(0)}°`, W - 14, ly + lh + 22);

  // reticle: hairline ring with four gaps; tightens and turns ember on lock
  const tgt = target, lockedNow = locked && tgt;
  const k = lockedNow ? Math.min(1, (t - lockAnimAt) / 260) : 0, ease = 1 - Math.pow(1 - k, 3);
  const rr = 30 - 6 * ease, col = lockedNow ? COL.ember : 'rgba(238,243,246,.85)';
  c.strokeStyle = col; c.lineWidth = 1.2;
  for (let q = 0; q < 4; q++) { const a0 = q * Math.PI / 2 + 0.22; c.beginPath(); c.arc(CX, CY, rr, a0, a0 + Math.PI / 2 - 0.44); c.stroke(); }
  c.beginPath();
  for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) { c.moveTo(CX + dx * (rr + 5), CY + dy * (rr + 5)); c.lineTo(CX + dx * (rr + 13), CY + dy * (rr + 13)); }
  c.stroke();
  c.fillStyle = col; c.beginPath(); c.arc(CX, CY, 1.5, 0, 2 * Math.PI); c.fill();
  if (tgt) {
    const p = proj(tgt.w, [0, 0]);
    if (p) {
      const s = 12, L = 5; c.strokeStyle = col; c.lineWidth = 1.4; c.beginPath();
      for (const [sx, sy] of [[-1, -1], [1, -1], [1, 1], [-1, 1]]) { const x = p[0] + sx * s, y = p[1] + sy * s; c.moveTo(x, y - sy * L); c.lineTo(x, y); c.lineTo(x - sx * L, y); }
      c.stroke();
    }
  }
  if (find) drawFind(t);
  drawCard(tgt);
}
function drawFind(t) {
  const o = objectFor(find); if (!o) return;
  const c = cx2;
  const ang = Math.acos(clamp(dot3(o.w, cam.fwd), -1, 1)) * R2D;
  const x = dot3(o.w, cam.right), y = dot3(o.w, cam.up);
  const p = proj(o.w, [0, 0]);
  const inView = onScreen(p, -30);
  if (ang < 2.5) {
    if (!foundDone) { foundDone = true; sfx('found'); buzz(60); toast(`Found ${o.name}`, 'var(--aurora)'); }
  } else if (ang > 6) foundDone = false;
  if (!inView) {
    const a = Math.atan2(-y, x), R = Math.min(W, VH) * 0.3;
    const bob = Math.sin(t / 180) * 3;
    c.save(); c.translate(CX + Math.cos(a) * (R + bob), CY + Math.sin(a) * (R + bob)); c.rotate(a);
    c.fillStyle = COL.ember; c.shadowColor = COL.ember; c.shadowBlur = 14;
    c.beginPath(); c.moveTo(16, 0); c.lineTo(-8, -11); c.lineTo(-3, 0); c.lineTo(-8, 11); c.closePath(); c.fill();
    c.restore();
  } else if (p) {
    c.strokeStyle = COL.ember; c.lineWidth = 1.4; const s = 18 + Math.sin(t / 160) * 2.5;
    c.beginPath(); c.arc(p[0], p[1], s, 0, 2 * Math.PI); c.stroke();
  }
}
function fmtDist(o) {
  if (o.kind === 'star') { const ly = o.star[7] * 3.2616; return ly > 0 && ly < 1e5 ? `${ly < 100 ? ly.toFixed(1) : Math.round(ly).toLocaleString()} ly` : '—'; }
  if (o.kind === 'moon') return `${Math.round(o.body.distAU * 149597.8707).toLocaleString()}k km`;
  if (o.body) return `${o.body.distAU.toFixed(2)} AU`;
  if (o.kind === 'sat') return `${Math.round(o.sat.now.range).toLocaleString()} km`;
  return '—';
}
function kindLabel(o) {
  if (o.kind === 'star') { const cn = conName(o.star[5]); return cn ? `Star in ${cn}` : 'Star'; }
  if (o.kind === 'planet') return 'Planet';
  if (o.kind === 'moon') return `Moon · ${Math.round(o.body.frac * 100)}% lit`;
  if (o.kind === 'sun') return 'Our star';
  if (o.kind === 'sat') return o.sat.now.lit ? 'Satellite · sunlit' : 'Satellite · in shadow';
  if (o.kind === 'dso') return `${DSO_KIND[o.dso.type] || o.dso.type} · Messier ${o.dso.m}`;
  return '';
}
let cardRect = null, closeRect = null;
function drawCard(o) {
  const c = cx2;
  cardRect = closeRect = null;
  if (!o) return;
  const w = Math.min(W - 32, 370), h = 118, x = CX - w / 2, y = VH - h - 6;
  const col = locked ? COL.ember : COL.accent;
  c.fillStyle = 'rgba(14,19,24,.84)'; roundRect(x, y, w, h, 24); c.fill();
  c.strokeStyle = locked ? 'rgba(255,179,92,.45)' : 'rgba(255,255,255,.1)'; c.lineWidth = 1; roundRect(x + .5, y + .5, w - 1, h - 1, 23.5); c.stroke();
  // kind line with status dot, centred
  c.font = F_SANS(500, 12.5); const kl = kindLabel(o), kw = c.measureText(kl).width;
  c.fillStyle = col; c.beginPath(); c.arc(CX - kw / 2 - 8, y + 21, 3, 0, 2 * Math.PI); c.fill();
  c.fillStyle = COL.text2; c.textAlign = 'left'; c.fillText(kl, CX - kw / 2 + 2, y + 25.5);
  // name
  c.font = F_SANS(600, 25); c.fillStyle = COL.text; c.textAlign = 'center';
  let nm = o.name; while (c.measureText(nm).width > w - 70 && nm.length > 4) nm = nm.slice(0, -2) + '…';
  c.fillText(nm, CX, y + 55);
  // four readouts split by hairlines
  const az = azOf(o.w), alt = altOf(o.w);
  let mag = '—';
  if (o.star) mag = o.star[2].toFixed(1); else if (o.body) mag = o.body.mag.toFixed(1); else if (o.dso) mag = o.dso.mag.toFixed(1);
  else if (o.sat) mag = Math.round(o.sat.now.height) + ' km';
  const cols = [['Azimuth', `${Math.round(az)}°`], ['Altitude', `${alt >= 0 ? '+' : '−'}${Math.abs(alt).toFixed(1)}°`], [o.sat ? 'Height' : 'Magnitude', mag], [o.sat ? 'Range' : 'Distance', fmtDist(o)]];
  const cw = (w - 16) / 4;
  cols.forEach(([kk, v], i) => {
    const cx = x + 8 + cw * (i + .5);
    if (i) { c.fillStyle = 'rgba(255,255,255,.08)'; c.fillRect(x + 8 + cw * i, y + 72, 1, 30); }
    c.textAlign = 'center';
    c.font = F_SANS(400, 11); c.fillStyle = COL.text2; c.fillText(kk, cx, y + 82);
    c.font = F_MONO(500, 14); c.fillStyle = COL.text; c.fillText(v, cx, y + 101);
  });
  // info button (top right): the whole card opens the info sheet, this makes it obvious
  const ix = x + w - 30, iy = y + 26;
  c.fillStyle = 'rgba(125,226,238,.14)'; c.beginPath(); c.arc(ix, iy, 13, 0, 2 * Math.PI); c.fill();
  c.fillStyle = COL.accent; c.font = 'italic 700 16px Saira'; c.textAlign = 'center'; c.fillText('i', ix, iy + 5.5);
  if (find) {
    const bx = x + 30, by = y + 26;
    c.fillStyle = 'rgba(255,255,255,.1)'; c.beginPath(); c.arc(bx, by, 13, 0, 2 * Math.PI); c.fill();
    c.strokeStyle = COL.text; c.lineWidth = 1.6; c.beginPath(); c.moveTo(bx - 4.5, by - 4.5); c.lineTo(bx + 4.5, by + 4.5); c.moveTo(bx + 4.5, by - 4.5); c.lineTo(bx - 4.5, by + 4.5); c.stroke();
    closeRect = [bx - 22, by - 22, 44, 44];
  }
  cardRect = [x, y, w, h];
}

/* ------------------------------------------------------------------ main loop */
let page = 'sky', skyAt = 0, satsAt = 0, locAt = 0, lastTarget = '', pageAt = 0;
function invalidate() { skyAt = 0; trackAt = 0; passesAt = 0; }
function tick(t) {
  requestAnimationFrame(tick);
  const ms = nowMs();
  if (ms - locAt > 1000) { locAt = ms; pollLocation(); checkAlerts(); if (page === 'gps' && !$('#sheet').classList.contains('on')) renderGps(); }
  if (ms - skyAt > 1000) { skyAt = ms; updateSky(); }
  if (ms - satsAt > 100) { satsAt = ms; updateSats(ms); }
  updatePasses(false);
  if (page === 'gps') drawGpsPlot(gnss.sats || []);
  if ((page === 'orbit' && ms - pageAt > 10000) || (page === 'tonight' && ms - pageAt > 60000)) { pageAt = ms; if (!$('#sheet').classList.contains('on')) renderPage(); }
  if (page !== 'sky') return;
  updateOrientation();
  setF();
  // auto targeting near the reticle (or follow the find target)
  const near = observer ? pickNear(cam.fwd, clamp(S.fov * 0.07, 1.2, 6)) : null;
  const k = near ? keyOf(near) : '';
  if (k !== lastTarget) { lastTarget = k; targetSince = ms; locked = false; if (near) sfx('tick'); }
  target = near;
  if (near && !locked && ms - targetSince > 650) { locked = true; lockAnimAt = t; if (lockedKey !== k) { lockedKey = k; sfx('lock'); buzz(18); } }
  if (!near) lockedKey = '';
  drawSky(t);
  fpsN++; if (ms - fpsT > 1000) { fps = fpsN; fpsN = 0; fpsT = ms; }
}

/* ------------------------------------------------------------------ touch: tap, pinch, drag */
const ptrs = new Map();
let pinch0 = 0, fov0 = 60, downAt = 0, downXY = [0, 0], moved = false;
cv.addEventListener('pointerdown', (e) => {
  audio(); try { cv.setPointerCapture(e.pointerId); } catch (err) {}
  ptrs.set(e.pointerId, [e.clientX, e.clientY]);
  if (ptrs.size === 2) { const [a, b] = [...ptrs.values()]; pinch0 = Math.hypot(a[0] - b[0], a[1] - b[1]); fov0 = S.fov; }
  downAt = nowMs(); downXY = [e.clientX, e.clientY]; moved = false;
});
cv.addEventListener('pointermove', (e) => {
  if (!ptrs.has(e.pointerId)) return;
  const prev = ptrs.get(e.pointerId); ptrs.set(e.pointerId, [e.clientX, e.clientY]);
  if (Math.hypot(e.clientX - downXY[0], e.clientY - downXY[1]) > 10) moved = true;
  if (ptrs.size === 2) {
    const [a, b] = [...ptrs.values()]; const d = Math.hypot(a[0] - b[0], a[1] - b[1]);
    const nf = clamp(fov0 * pinch0 / d, 12, 110);
    if (Math.round(nf / 10) !== Math.round(S.fov / 10)) sfx('zoom');
    S.fov = nf; fovShownAt = nowMs();
  } else if (!cam.ok) {
    const k = S.fov / VH;
    pan.az = (pan.az - (e.clientX - prev[0]) * k + 360) % 360;
    pan.alt = clamp(pan.alt + (e.clientY - prev[1]) * k, -89, 89);
  }
});
const endPtr = (e) => {
  ptrs.delete(e.pointerId);
  if (ptrs.size === 0) saveS();
  if (moved || nowMs() - downAt > 500 || e.type !== 'pointerup') return;
  const x = e.clientX, y = e.clientY;
  if (closeRect && x > closeRect[0] && x < closeRect[0] + closeRect[2] && y > closeRect[1] && y < closeRect[1] + closeRect[3]) { find = null; endAlign(); sfx('tap'); toast('Stopped finding'); return; }
  if (cardRect && x > cardRect[0] && x < cardRect[0] + cardRect[2] && y > cardRect[1] && y < cardRect[1] + cardRect[3]) {
    if (target) { sfx('page'); buzz(8); infoSheet(target); }
    return;
  }
  // tap the sky: aim at the tapped point
  const dir = [cam.fwd[0] * F + cam.right[0] * (x - CX) - cam.up[0] * (y - CY),
    cam.fwd[1] * F + cam.right[1] * (x - CX) - cam.up[1] * (y - CY),
    cam.fwd[2] * F + cam.right[2] * (x - CX) - cam.up[2] * (y - CY)];
  const n = Math.hypot(...dir); dir[0] /= n; dir[1] /= n; dir[2] /= n;
  const o = observer && pickNear(dir, clamp(S.fov * 0.05, 1, 4));
  if (o) { find = keyOf(o); foundDone = false; sfx('ok'); toast(`Finding ${o.name}`, 'var(--ember)'); }
  else sfx('tap');
};
// Stop the browser's follow-up click: a tap on the card opens a sheet right under the finger,
// and that click would otherwise land on the sheet (or its backdrop) and close it again.
cv.addEventListener('touchend', (e) => { if (e.cancelable) e.preventDefault(); }, { passive: false });
cv.addEventListener('pointerup', endPtr); cv.addEventListener('pointercancel', endPtr);
cv.addEventListener('wheel', (e) => { S.fov = clamp(S.fov * (e.deltaY > 0 ? 1.1 : 0.9), 12, 110); fovShownAt = nowMs(); saveS(); }, { passive: true });

/* ------------------------------------------------------------------ camera (AR) */
let camOn = false, camStream = null;
async function setCamera(on) {
  const v = $('#cam');
  if (!on) { if (camStream) camStream.getTracks().forEach((t) => t.stop()); camStream = null; camOn = false; v.classList.remove('on'); return; }
  try {
    if (NAT && NAT.requestCamera && !NAT.hasCamera()) { NAT.requestCamera(); return; }  // SP_onPerm will call back
    camStream = await navigator.mediaDevices.getUserMedia({ video: { facingMode: 'environment', width: { ideal: 1280 } }, audio: false });
    v.srcObject = camStream; camOn = true; v.classList.add('on');
  } catch (e) { S.camera = false; saveS(); toast('Camera isn’t available', 'var(--signal)'); renderPage(); }
}
window.SP_onPerm = (what, ok) => {
  if (what === 'camera') { if (ok && S.camera) setCamera(true); else if (!ok) { S.camera = false; saveS(); renderPage(); } }
  if (what === 'location' && !ok) toast('No location permission. Choose a city instead.', 'var(--warn)', 3500);
};

/* ------------------------------------------------------------------ layers */
const LAYERS = [
  ['stars', 'Stars', 'Named and unnamed, by colour', '<circle cx="12" cy="12" r="2.6" fill="currentColor"/><path d="M12 3v4M12 17v4M3 12h4M17 12h4"/>'],
  ['constellations', 'Constellations', 'Figures and names', '<path d="M4 17l5-6 4 3 7-8"/><circle cx="4" cy="17" r="1.4" fill="currentColor"/><circle cx="9" cy="11" r="1.4" fill="currentColor"/><circle cx="13" cy="14" r="1.4" fill="currentColor"/><circle cx="20" cy="6" r="1.4" fill="currentColor"/>'],
  ['art', 'Constellation art', 'The dippers, zodiac, Orion and more', '<path d="M5 18c2-6 5-9 9-10M14 8c1-2 3-3 5-3M14 8l2 3M9 12l-3-2"/><circle cx="19" cy="5" r="1.3" fill="currentColor"/><circle cx="5" cy="18" r="1.3" fill="currentColor"/>'],
  ['planets', 'Planets', 'Mercury to Neptune', '<circle cx="12" cy="12" r="4.5"/><ellipse cx="12" cy="12" rx="9.5" ry="3" transform="rotate(-20 12 12)"/>'],
  ['sunMoon', 'Sun and Moon', 'With the real phase', '<path d="M17 15.5A6.5 6.5 0 0 1 8.5 7a6.5 6.5 0 1 0 8.5 8.5z"/>'],
  ['sats', 'Satellites', 'ISS, Tiangong, Hubble', '<rect x="9.5" y="9.5" width="5" height="5" rx="1"/><path d="M3 10h5v4H3zM16 10h5v4h-5zM8 12h1.5M14.5 12H16"/>'],
  ['dso', 'Deep sky', 'Galaxies, clusters, nebulae', '<ellipse cx="12" cy="12" rx="9" ry="4" transform="rotate(-25 12 12)"/><circle cx="12" cy="12" r="1.6" fill="currentColor"/>'],
  ['labels', 'Labels', 'Names next to objects', '<path d="M4 6h10l5 6-5 6H4z"/>'],
  ['grid', 'Altitude grid', 'Lines every 15°', '<path d="M3 8h18M3 16h18M8 3v18M16 3v18"/>'],
];
const PRESETS = [
  ['Default', { stars: 1, constellations: 1, art: 0, planets: 1, sunMoon: 1, sats: 1, dso: 1, labels: 1, grid: 0 }],
  ['Everything', { stars: 1, constellations: 1, art: 1, planets: 1, sunMoon: 1, sats: 1, dso: 1, labels: 1, grid: 1 }],
  ['Naked eye', { stars: 1, constellations: 1, art: 0, planets: 1, sunMoon: 1, sats: 0, dso: 0, labels: 1, grid: 0 }],
  ['Planets', { stars: 0, constellations: 0, art: 0, planets: 1, sunMoon: 1, sats: 0, dso: 0, labels: 1, grid: 0 }],
  ['Satellites', { stars: 0, constellations: 0, art: 0, planets: 0, sunMoon: 0, sats: 1, dso: 0, labels: 1, grid: 0 }],
  ['Deep sky', { stars: 1, constellations: 0, art: 0, planets: 0, sunMoon: 0, sats: 0, dso: 1, labels: 1, grid: 0 }],
  ['Constellations', { stars: 1, constellations: 1, art: 1, planets: 0, sunMoon: 0, sats: 0, dso: 0, labels: 1, grid: 0 }],
];
const OBJ_LAYERS = ['stars', 'constellations', 'art', 'planets', 'sunMoon', 'sats', 'dso'];
// the drawings are an optional extra, so leaving them off doesn't count as filtering the sky
const hiddenCount = () => OBJ_LAYERS.filter((k) => k !== 'art' && !S[k]).length;
function applyPreset(p) { for (const k in p) S[k] = !!p[k]; saveS(); updateLayersBtn(); }
function layersBody() {
  const preset = PRESETS.find(([, p]) => Object.keys(p).every((k) => !!p[k] === !!S[k]));
  let h = `<div class="presets">${PRESETS.map(([n, p]) => `<button class="chip${preset && preset[0] === n ? ' on' : ''}" data-act="${act(() => { applyPreset(p); sfx('tap'); buzz(6); fillLayers(); })}">${n === 'Default' || n === 'Everything' || n === 'Naked eye' ? n : n + ' only'}</button>`).join('')}</div>`;
  h += `<div class="tiles">${LAYERS.map(([k, n, sub, ic]) => `<button class="tile${S[k] ? ' on' : ''}" data-layer="${k}" data-act="${act(() => { S[k] = !S[k]; saveS(); sfx('tap'); buzz(6); updateLayersBtn(); fillLayers(); })}">` +
    `<svg viewBox="0 0 24 24">${ic}</svg><span class="tn">${n}</span><span class="ts">${sub}</span><span class="tk"></span></button>`).join('')}</div>`;
  h += `<p class="hint">Hold a tile to show only that layer.</p>`;
  return h;
}
function fillLayers() {
  const box = $('#layersBox'); if (!box) return;
  box.innerHTML = layersBody(); bindActs(box);
  // long-press a tile: solo that layer (labels stay as they are)
  box.querySelectorAll('.tile').forEach((t) => {
    let timer = 0;
    t.onpointerdown = () => { timer = setTimeout(() => {
      const k = t.dataset.layer; timer = -1;
      for (const o of OBJ_LAYERS) S[o] = o === k || ((k === 'constellations' || k === 'art') && (o === 'stars' || o === 'constellations' || o === 'art'));
      if (k === 'labels' || k === 'grid') S[k] = true;
      saveS(); sfx('ok'); buzz(20); updateLayersBtn(); fillLayers();
      toast(`Showing only ${LAYERS.find((l) => l[0] === k)[1].toLowerCase()}`);
    }, 520); };
    t.onpointerup = t.onpointercancel = t.onpointerleave = () => { if (timer > 0) clearTimeout(timer); };
    const click = t.onclick; t.onclick = (e) => { if (timer === -1) { timer = 0; e.stopPropagation(); return; } click(e); };
  });
}
function layersSheet() {
  openSheet('Layers', 'Choose what’s drawn on the sky.', '<div id="layersBox"></div>');
  fillLayers();
}
function updateLayersBtn() {
  const b = $('#layersBtn'); if (!b) return;
  const n = hiddenCount();
  b.classList.toggle('filtered', n > 0);
}

/* ------------------------------------------------------------------ screen rotation */
// S.rotation: 'portrait' | 'auto' | 'landscape'. Java applies it; JS remaps the camera axes to the screen.
let screenRot = 0;
function readScreenRot() {
  let r = 0;
  if (NAT && NAT.rotation) r = +NAT.rotation() || 0;
  else if (screen.orientation && typeof screen.orientation.angle === 'number') r = screen.orientation.angle;
  screenRot = ((r % 360) + 360) % 360;
}
function applyRotation() { if (NAT && NAT.setRotation) NAT.setRotation(S.rotation || 'portrait'); setTimeout(() => { readScreenRot(); resize(); }, 300); }

/* ------------------------------------------------------------------ pages */
const fmtTime = (ms) => new Date(ms).toLocaleTimeString([], { hour: 'numeric', minute: '2-digit' });
const fmtDay = (ms) => { const d = new Date(ms), n = new Date(nowMs()); const tm = new Date(n); tm.setDate(n.getDate() + 1);
  if (d.toDateString() === n.toDateString()) return 'Today'; if (d.toDateString() === tm.toDateString()) return 'Tomorrow';
  return d.toLocaleDateString([], { weekday: 'short' }); };
const fmtWhen = (ms) => { const d = fmtDay(ms); return (d === 'Today' ? '' : d === 'Tomorrow' ? 'tomorrow ' : d + ' ') + fmtTime(ms); };
const inMin = (ms) => { const m = Math.max(0, Math.round((ms - nowMs()) / 60000)); if (m < 60) return `in ${m} min`; return `in ${Math.floor(m / 60)} h ${m % 60} m`; };
const esc = (s) => String(s).replace(/[&<>"]/g, (ch) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[ch]));
function showInfo(k, name) { const o = objectFor(k); if (o) { sfx('page'); infoSheet(o); } else goFind(k, name); }
function goFind(k, name) {
  find = k; foundDone = false; showPage('sky'); toast(`Finding ${name}`, 'var(--ember)'); sfx('ok');
}
function phaseName(p) {
  const a = p / 360;
  if (a < 0.0339 || a > 0.9661) return 'New Moon'; if (a < 0.2161) return 'Waxing crescent'; if (a < 0.2839) return 'First quarter';
  if (a < 0.4661) return 'Waxing gibbous'; if (a < 0.5339) return 'Full Moon'; if (a < 0.7161) return 'Waning gibbous';
  if (a < 0.7839) return 'Last quarter'; return 'Waning crescent';
}
function riseSet(body, dir, startMs, limitDays) {
  try { const r = A.SearchRiseSet(body, observer, dir, A.MakeTime(new Date(startMs)), limitDays || 1.2); return r ? r.date.getTime() : null; } catch (e) { return null; }
}
function twilight(dir, alt) {
  try { const r = A.SearchAltitude('Sun', observer, dir, A.MakeTime(new Date(nowMs())), 1.2, alt); return r ? r.date.getTime() : null; } catch (e) { return null; }
}

/* Click handlers: each render registers closures, bindActs attaches them to the elements it just
   created and forgets them. Nothing is shared between renders, so a page refreshing in the
   background can never re-point a button in the open sheet (the old city-picker bug). */
let actSeq = 0;
const ACTS = new Map();
function act(fn) { const id = 'a' + (++actSeq); ACTS.set(id, fn); return id; }
function bindActs(root) {
  root.querySelectorAll('[data-act]').forEach((el) => {
    const fn = ACTS.get(el.dataset.act); ACTS.delete(el.dataset.act);
    if (fn) el.onclick = (e) => { e.stopPropagation(); audio(); fn(el); };
  });
}
function setHtml(el, h) { if (el._h !== h) { el._h = h; el.innerHTML = h; bindActs(el); } else ACTS.clear(); }

const chev = '<span class="chev">›</span>';
function item(o) {
  const dot = o.dot ? `<span class="dot${o.dotOff ? ' off' : ''}"></span>` : '';
  const right = o.r != null ? `<span class="r${o.hot ? ' hot' : ''}">${o.r}</span>` : '';
  return `<div class="item${o.cls ? ' ' + o.cls : ''}" style="${o.c ? '--c:' + o.c : ''}"${o.act ? ` data-act="${o.act}"` : ''}${o.name ? ` data-name="${esc(o.name)}"` : ''}>` +
    `${dot}<div class="body"><div class="t">${o.t}</div>${o.s ? `<div class="s">${o.s}</div>` : ''}</div>${right}${o.tail || (o.act && !o.noChev ? chev : '')}</div>`;
}
const group = (title, inner) => `<div class="group">${title ? `<div class="gh">${title}</div>` : ''}<div class="list">${inner}</div></div>`;
const strip = (cells) => `<div class="strip">${cells.map(([k, v, c]) => `<div><div class="k">${k}</div><div class="v"${c ? ` style="color:${c}"` : ''}>${v}</div></div>`).join('')}</div>`;
const placeName = () => LOC.src === 'manual' && LOC.name ? LOC.name : LOC.lat != null ? `${Math.abs(LOC.lat).toFixed(2)}°${LOC.lat >= 0 ? 'N' : 'S'}, ${Math.abs(LOC.lon).toFixed(2)}°${LOC.lon >= 0 ? 'E' : 'W'}` : '';
const needLoc = () => `<h1>${page === 'orbit' ? 'Orbit' : 'Tonight'}</h1><p class="sub">Needs to know where you are.</p>` +
  `<div class="btns"><button class="btn primary" data-act="${act(() => showPage('gps'))}">Set location</button></div>`;

function moonSvg(frac, phase, r) {
  const waxing = phase < 180, k = 1 - 2 * frac, rx = Math.abs(k) * r;
  const sweepOuter = waxing ? 1 : 0, sweepIn = waxing ? (k > 0 ? 0 : 1) : (k > 0 ? 1 : 0);
  return `<svg width="${2 * r + 40}" height="${2 * r + 40}" viewBox="${-r - 20} ${-r - 20} ${2 * r + 40} ${2 * r + 40}">` +
    `<defs><radialGradient id="mg"><stop offset="60%" stop-color="rgba(232,238,245,.10)"/><stop offset="100%" stop-color="rgba(232,238,245,0)"/></radialGradient></defs>` +
    `<circle r="${r + 20}" fill="url(#mg)"/><circle r="${r}" fill="#141b22"/>` +
    `<path d="M0 ${-r} A${r} ${r} 0 0 ${sweepOuter} 0 ${r} A${rx} ${r} 0 0 ${sweepIn} 0 ${-r}" fill="#e9eef3"/>` +
    `<circle r="${r}" fill="none" stroke="rgba(255,255,255,.12)"/></svg>`;
}

function renderTonight() {
  const el = $('#tonight');
  if (!observer) { setHtml(el, needLoc()); return; }
  updateSky();
  const t = nowMs();
  const sunset = riseSet('Sun', -1, t), sunrise = riseSet('Sun', +1, t), dusk = twilight(-1, -18), dawn = twilight(+1, -18);
  const dark = sunAlt < -6;
  const today = new Date(t).toLocaleDateString([], { weekday: 'long', month: 'long', day: 'numeric' });
  let h = `<h1>Tonight</h1><p class="sub">${today}${placeName() ? ' · ' + esc(placeName()) : ''}</p>`;
  const m = moonInfo;
  if (m) {
    const mr = riseSet('Moon', +1, t), ms = riseSet('Moon', -1, t);
    h += `<div class="hero" data-act="${act(() => showInfo('moon:Moon', 'Moon'))}">${moonSvg(m.frac, m.phase, 46)}` +
      `<div class="title">${phaseName(m.phase)}</div><div class="cap">${Math.round(m.frac * 100)}% lit · ${m.alt > 0 ? 'up now, sets ' + (ms ? fmtWhen(ms) : '—') : 'rises ' + (mr ? fmtWhen(mr) : '—')}</div></div>`;
  }
  const cells = sunAlt > -0.8
    ? [['Sunset', sunset ? fmtTime(sunset) : '—'], ['Full dark', dusk ? fmtTime(dusk) : '—'], ['Sunrise', sunrise ? fmtTime(sunrise) : '—']]
    : [['Sun', `${Math.round(sunAlt)}°`], [sunAlt < -18 ? 'Dark until' : 'Full dark', sunAlt < -18 ? (dawn ? fmtTime(dawn) : '—') : (dusk ? fmtTime(dusk) : '—')], ['Sunrise', sunrise ? fmtTime(sunrise) : '—']];
  h += strip(cells);

  const pl = bodies.filter((b) => b.kind === 'planet' && b.name !== 'Neptune' && b.name !== 'Uranus').sort((a, b) => b.alt - a.alt);
  let ph = '';
  for (const b of pl) {
    const up = b.alt > 0;
    let s;
    if (up) { const st = riseSet(b.name, -1, t); s = `Up now · ${compass(b.az)} ${Math.round(b.alt)}° · sets ${st ? fmtWhen(st) : '—'}`; }
    else { const r = riseSet(b.name, +1, t); s = `Rises ${r ? fmtWhen(r) : '—'}`; }
    ph += item({ dot: true, dotOff: !up, c: b.color, t: b.name, s, r: b.mag.toFixed(1), act: up ? act(() => showInfo('planet:' + b.name, b.name)) : null });
  }
  h += group('Planets', ph);

  const iss = sats.find((s) => s.name === 'ISS');
  updatePasses(false);
  const vp = iss && iss.passes.find((p) => p.visible && p.los > t);
  if (vp) h += group('Space station', item({ dot: true, c: 'var(--aurora)', t: `${fmtDay(vp.aos)} at ${fmtTime(vp.aos)}`,
    s: `${compass(vp.fromAz)} to ${compass(vp.toAz)} · up to ${Math.round(vp.maxEl)}° · ${Math.round((vp.los - vp.aos) / 60000)} min`, r: vp.aos < t ? 'now' : inMin(vp.aos), hot: true, act: act(() => showPage('orbit')) }));

  const best = [];
  for (let i = 0; i < NSTAR; i++) { const s = CAT.stars[i]; if (s[4] && s[2] < 1.6 && starW[i * 3 + 2] > 0.17) best.push(starObj(i)); }
  best.sort((a, b) => a.star[2] - b.star[2]);
  if (best.length) h += group('Bright stars up now', best.slice(0, 6).map((o) => item({ dot: true, c: SC[o.idx], t: esc(o.name),
    s: `${conName(o.star[5])} · ${compass(azOf(o.w))} ${Math.round(altOf(o.w))}°`, r: o.star[2].toFixed(1), act: act(() => showInfo('star:' + o.name, o.name)) })).join(''));
  const deep = DSO.filter((d) => d.mag < 7 && d.w[2] > 0.26).sort((a, b) => a.mag - b.mag).slice(0, 5);
  if (deep.length) h += group(dark ? 'Deep sky' : 'Deep sky, once it’s dark', deep.map((d) => item({ dot: true, c: '#b98cff', t: esc(dsoName(d)),
    s: `${kindLabel({ kind: 'dso', dso: d })} · ${compass(azOf(d.w))} ${Math.round(altOf(d.w))}°`, r: d.mag.toFixed(1), act: act(() => showInfo('dso:' + dsoName(d), dsoName(d))) })).join(''));
  setHtml(el, h);
}

function renderOrbit() {
  const el = $('#orbit');
  if (!observer) { setHtml(el, needLoc()); return; }
  updatePasses(false);
  const t = nowMs();
  let h = `<h1>Orbit</h1><p class="sub">Satellites over ${esc(placeName() || 'you')}</p>`;
  const upNow = sats.filter((s) => s.now && s.now.alt > 0).sort((a, b) => b.now.alt - a.now.alt)[0];
  const all = [].concat(...sats.map((s) => s.passes)).filter((p) => p.los > t && p.maxEl > 10).sort((a, b) => a.aos - b.aos);
  const nextVis = all.find((p) => p.visible && p.aos > t);
  if (upNow) {
    h += `<div class="hero" data-act="${act(() => showInfo('sat:' + upNow.name, upNow.name))}"><div class="big" style="color:var(--aurora)">+${Math.round(upNow.now.alt)}°</div>` +
      `<div class="title">${esc(upNow.name)} is overhead</div><div class="cap">${compass(upNow.now.az)} · ${upNow.now.lit ? 'sunlit, look for it' : 'in Earth’s shadow'} · tap to find</div></div>`;
  } else if (nextVis) {
    const mins = Math.max(0, Math.round((nextVis.aos - t) / 60000));
    const big = mins < 60 ? `${mins}<span style="font-size:.5em;color:var(--text-2)"> min</span>` : `${Math.floor(mins / 60)}<span style="font-size:.5em;color:var(--text-2)"> h </span>${mins % 60}<span style="font-size:.5em;color:var(--text-2)"> m</span>`;
    h += `<div class="hero"><div class="big">${big}</div><div class="title">${esc(nextVis.sat)} · ${fmtDay(nextVis.aos)} at ${fmtTime(nextVis.aos)}</div>` +
      `<div class="cap">Next visible pass · ${compass(nextVis.fromAz)} to ${compass(nextVis.toAz)}, up to ${Math.round(nextVis.maxEl)}°</div></div>`;
  } else h += `<div class="hero"><div class="title">No visible passes in the next 36 hours</div><div class="cap">Satellites are only visible when they’re sunlit and your sky is dark.</div></div>`;

  let sh = '';
  for (const s of sats) {
    const n = s.now;
    let sub;
    if (n && n.alt > 0) sub = `Up now · ${compass(n.az)} ${Math.round(n.alt)}° · ${n.lit ? 'sunlit' : 'in shadow'}`;
    else { const p = s.passes.find((x) => x.aos > t && x.maxEl > 10); sub = p ? `Next ${fmtWhen(p.aos)} · up to ${Math.round(p.maxEl)}°${p.visible ? ' · visible' : ''}` : 'No pass in the next 36 hours'; }
    sh += item({ dot: true, dotOff: !(n && n.alt > 0), c: 'var(--aurora)', t: esc(s.name), s: sub, r: n ? `${Math.round(n.height)} km` : '', act: act(() => showInfo('sat:' + s.name, s.name)) });
  }
  h += group('Tracking', sh);
  const ph = all.slice(0, 10).map((p) => item({ dot: true, dotOff: !p.visible, c: 'var(--aurora)', t: `${esc(p.sat)} · ${fmtWhen(p.aos)}`,
    s: `${compass(p.fromAz)} to ${compass(p.toAz)} · up to ${Math.round(p.maxEl)}° · ${Math.round((p.los - p.aos) / 60000)} min${p.visible ? ' · visible' : ''}`,
    r: p.aos < t ? 'now' : inMin(p.aos), hot: p.visible })).join('');
  h += group('Passes above 10°', ph || '<div class="empty">None in the next 36 hours.</div>');
  const ageD = tleFetchedMs ? (t - tleFetchedMs) / 86400000 : null;
  const epochAge = sats.length ? (t - Math.max(...sats.map((s) => s.epochMs))) / 86400000 : 99;
  h += strip([['Orbits updated', ageD == null ? 'Built-in' : ageD < 1 ? 'Today' : `${Math.floor(ageD)} d ago`], ['Data age', `${epochAge.toFixed(1)} d`, epochAge > 10 ? 'var(--warn)' : null]]);
  h += `<div class="btns"><button class="btn" data-act="${act(() => updateTles(true))}">Update orbits</button></div>`;
  setHtml(el, h);
}

const GNSS = { 1: ['GPS', '#7de2ee'], 3: ['GLONASS', '#ffb35c'], 5: ['BeiDou', '#b98cff'], 6: ['Galileo', '#6cf0c3'], 4: ['QZSS', '#ffd166'], 7: ['NavIC', '#ff6f86'], 2: ['SBAS', '#8b98a3'] };
function gpsState() {
  if (LOC.src === 'manual') return ['Using a chosen city', 'var(--ember)', false];
  if (LOC.src === 'gps' && nowMs() - LOC.fixMs < 15000) return ['GPS locked', 'var(--aurora)', true];
  if (LOC.src === 'network' && nowMs() - LOC.fixMs < 120000) return ['Network location', 'var(--accent)', true];
  if (LOC.lat != null) return ['Last known position', 'var(--warn)', false];
  return ['Searching for satellites', 'var(--signal)', true];
}
function renderGps() {
  const el = $('#gps');
  const ss = gnss.sats || [];
  const used = ss.filter((s) => s[3]).length;
  const [state, col, live] = gpsState();
  const age = LOC.fixMs ? Math.round((nowMs() - LOC.fixMs) / 1000) : null;
  const best = ss.length ? Math.max(...ss.map((s) => s[2])) : 0;
  let h = `<h1>Location</h1><p class="sub">${LOC.src === 'manual' ? esc(LOC.name || 'Chosen city') : ss.length ? `${used} of ${ss.length} satellites in use` : NAT ? 'Listening for satellites' : 'Browser location'}</p>`;
  h += `<div class="hero" style="padding-bottom:6px"><canvas class="plot" id="gpsPlot" width="560" height="560" style="width:280px;height:280px"></canvas>` +
    `<span class="state${live ? ' live' : ''}" style="--c:${col}"><i></i>${state}</span></div>`;
  const sys = [...new Set(ss.map((s) => s[4]))].filter((k) => GNSS[k]);
  h += sys.length ? `<div class="legend">${sys.map((k) => `<span style="--c:${GNSS[k][1]}">${GNSS[k][0]}</span>`).join('')}</div>` : '<div style="height:18px"></div>';
  h += strip([['Accuracy', LOC.acc && LOC.src !== 'manual' ? `±${Math.round(LOC.acc)} m` : '—'], ['Best signal', best ? `${Math.round(best)} dB` : '—'],
    ['Last fix', age == null || LOC.src === 'manual' ? '—' : age < 60 ? `${age} s` : `${Math.round(age / 60)} min`]]);
  const coord = LOC.lat != null ? `${Math.abs(LOC.lat).toFixed(4)}° ${LOC.lat >= 0 ? 'N' : 'S'}<br>${Math.abs(LOC.lon).toFixed(4)}° ${LOC.lon >= 0 ? 'E' : 'W'}` : '—';
  h += group('', item({ t: 'Coordinates', r: `<span style="color:var(--text)">${coord}</span>` }) +
    item({ t: 'Altitude', r: LOC.lat != null ? `${Math.round(LOC.alt)} m` : '—' }) +
    item({ t: 'Compass correction', s: 'Magnetic to true north', r: `${declination >= 0 ? '+' : '−'}${Math.abs(declination).toFixed(1)}°` }));
  h += `<div class="btns"><button class="btn wide ${LOC.src === 'manual' ? 'primary' : ''}" data-act="${act(useGps)}">Use GPS</button>` +
    `<button class="btn wide ${LOC.src === 'manual' ? '' : ''}" data-act="${act(citySheet)}">Choose city</button></div>`;
  setHtml(el, h);
  drawGpsPlot(ss);
}
function drawGpsPlot(ss) {
  const cvp = $('#gpsPlot'); if (!cvp) return;
  const c = cvp.getContext('2d'); c.setTransform(2, 0, 0, 2, 0, 0);
  const R = 120, X = 140, Y = 140, t = performance.now();
  c.clearRect(0, 0, 280, 280);
  c.strokeStyle = 'rgba(255,255,255,.09)'; c.lineWidth = 1;
  for (const f of [1, 2 / 3, 1 / 3]) { c.beginPath(); c.arc(X, Y, R * f, 0, 2 * Math.PI); c.stroke(); }
  c.beginPath(); c.moveTo(X - R, Y); c.lineTo(X + R, Y); c.moveTo(X, Y - R); c.lineTo(X, Y + R); c.stroke();
  // fine ticks around the rim
  for (let a = 0; a < 360; a += 10) { const r0 = a % 90 ? R - 4 : R - 8, s = Math.sin(a * D2R), k = Math.cos(a * D2R); c.beginPath(); c.moveTo(X + s * r0, Y - k * r0); c.lineTo(X + s * R, Y - k * R); c.stroke(); }
  c.font = '600 11.9px Saira'; c.textAlign = 'center'; c.fillStyle = '#ff6f86'; c.fillText('N', X, Y - R - 7);
  c.fillStyle = '#55616b'; c.fillText('E', X + R + 9, Y + 4); c.fillText('S', X, Y + R + 15); c.fillText('W', X - R - 9, Y + 4);
  c.font = '400 10.5px Saira'; c.fillText('30°', X + 4, Y - R * 2 / 3 + 11); c.fillText('60°', X + 4, Y - R / 3 + 11);
  const sweep = (t / 2400) % (2 * Math.PI);
  if (c.createConicGradient) {
    const g = c.createConicGradient(sweep - Math.PI / 2, X, Y);
    g.addColorStop(0, 'rgba(125,226,238,.18)'); g.addColorStop(0.1, 'rgba(125,226,238,0)'); g.addColorStop(1, 'rgba(125,226,238,0)');
    c.fillStyle = g; c.beginPath(); c.arc(X, Y, R, 0, 2 * Math.PI); c.fill();
  }
  for (const s of ss) {
    const [az, el, cn0, usedF, sys] = s; if (el < 0) continue;
    const r = R * (1 - el / 90), a = az * D2R;
    const x = X + Math.sin(a) * r, y = Y - Math.cos(a) * r, rad = clamp(cn0 / 10, 2.4, 5);
    const col = (GNSS[sys] || [0, '#eef3f6'])[1];
    c.globalAlpha = usedF ? 1 : 0.35; c.fillStyle = col;
    c.beginPath(); c.arc(x, y, rad, 0, 2 * Math.PI); c.fill();
    if (usedF) { c.strokeStyle = col; c.globalAlpha = .35; c.beginPath(); c.arc(x, y, rad + 4, 0, 2 * Math.PI); c.stroke(); }
  }
  c.globalAlpha = 1;
  c.fillStyle = '#eef3f6'; c.beginPath(); c.arc(X, Y, 2.5, 0, 2 * Math.PI); c.fill();
  if (!ss.length) { c.fillStyle = '#8b98a3'; c.font = '400 14.0px Saira'; c.fillText(NAT ? 'Step outside for a clear view of the sky' : 'The satellite view needs the Android app', X, Y + 44); }
}
function useGps() {
  S.manualLoc = false; saveS(); LOC.src = LOC.lat != null ? 'saved' : 'none'; hadFix = false;
  if (NAT && NAT.requestLocation) NAT.requestLocation(); else browserGeo();
  toast('Using GPS'); sfx('ok'); renderGps();
}

/* ------------------------------------------------------------------ bottom sheet */
let sheetOnClose = null;
const ICON_X = '<svg viewBox="0 0 16 16"><path d="M3 3l10 10M13 3L3 13"/></svg>';
const ICON_SEARCH = '<svg viewBox="0 0 20 20"><circle cx="8.5" cy="8.5" r="6"/><path d="M13 13l5 5"/></svg>';
function openSheet(title, intro, body, opts) {
  const sh = $('#sheet');
  sh.innerHTML = `<div class="grab"></div><div class="head"><span></span><h2>${title}</h2><button class="close" aria-label="Close" data-act="${act(closeSheet)}">${ICON_X}</button></div>` +
    (intro ? `<p class="intro">${intro}</p>` : '') + (opts && opts.search ? `<div class="search">${ICON_SEARCH}<input type="search" placeholder="${opts.search}" autocomplete="off"></div>` : '') +
    `<div class="scroll">${body}</div>`;
  bindActs(sh);
  sheetOnClose = opts && opts.onClose;
  sheetOpenedAt = performance.now();
  $('#scrim').classList.add('on'); sh.classList.add('on');
  sh.querySelector('.scroll').scrollTop = 0;
  const inp = sh.querySelector('.search input');
  if (inp) inp.oninput = () => {
    const q2 = inp.value.trim().toLowerCase();
    sh.querySelectorAll('.item[data-name]').forEach((it) => { it.style.display = !q2 || it.dataset.name.toLowerCase().includes(q2) ? '' : 'none'; });
  };
  // drag the grab handle down to dismiss
  let y0 = null;
  sh.querySelector('.head').onpointerdown = (e) => { y0 = e.clientY; sh.style.transition = 'none'; };
  sh.onpointermove = (e) => { if (y0 != null) sh.style.transform = `translateY(${Math.max(0, e.clientY - y0)}px)`; };
  sh.onpointerup = sh.onpointercancel = (e) => {
    if (y0 == null) return; const d = e.clientY - y0; y0 = null; sh.style.transition = ''; sh.style.transform = '';
    if (d > 90) closeSheet();
  };
}
function closeSheet() {
  const sh = $('#sheet');
  if (!sh.classList.contains('on')) return false;
  sh.classList.remove('on', 'tall'); $('#scrim').classList.remove('on');
  const inp = sh.querySelector('input'); if (inp) inp.blur();
  const cb = sheetOnClose; sheetOnClose = null; if (cb) cb();
  return true;
}
let sheetOpenedAt = 0;
$('#scrim').onclick = () => { if (performance.now() - sheetOpenedAt > 450) closeSheet(); };
function citySheet() {
  let h = '';
  for (const [n, la, lo] of CITIES) {
    const sel = LOC.src === 'manual' && LOC.name === n;
    h += item({ name: n, cls: sel ? 'sel' : '', t: n, s: `${Math.abs(la).toFixed(2)}° ${la >= 0 ? 'N' : 'S'}, ${Math.abs(lo).toFixed(2)}° ${lo >= 0 ? 'E' : 'W'}`,
      tail: sel ? '<span class="check">✓</span>' : '', noChev: true,
      act: act(() => { setLocation(la, lo, 0, 'manual', n, 0); closeSheet(); toast(`Location set to ${n}`, 'var(--aurora)'); sfx('ok'); renderPage(); }) });
  }
  openSheet('Choose a city', 'Close enough for the sky. GPS is more precise.', `<div class="list">${h}</div>`, { search: 'Search cities' });
}
function alignSheet() {
  if (!observer) { toast('Set your location first', 'var(--warn)'); return; }
  updateSky();
  const opts = bodies.filter((b) => b.alt > 3 && b.kind !== 'sun' && (b.kind === 'moon' || b.mag < 1.5)).map((b) => ({ kind: b.kind, name: b.name, w: b.w, color: b.color }));
  for (let i = 0; i < NSTAR; i++) { const s = CAT.stars[i]; if (s[4] && s[2] < 1.3 && starW[i * 3 + 2] > 0.1) { const o = starObj(i); o.color = SC[i]; opts.push(o); } }
  if (sunAlt > 3) opts.unshift({ kind: 'sun', name: 'Sun', w: bodies[0].w, color: '#ffd75e' });
  let h = opts.slice(0, 10).map((o) => item({ dot: true, c: o.color, t: esc(o.name), s: `${compass(azOf(o.w))} · ${Math.round(altOf(o.w))}° up`, act: act(() => { closeSheet(); startAlign(o); }) })).join('');
  if (!h) h = '<div class="empty">Nothing bright enough is up right now.</div>';
  h = `<div class="list">${h}</div><div class="btns"><button class="btn" data-act="${act(() => { S.azOffset = 0; saveS(); closeSheet(); toast('Compass correction cleared'); sfx('tap'); renderPage(); })}">Clear correction</button></div>`;
  openSheet('Align compass', 'Pick something you can see, centre it in the reticle, then tap Lock it.', h);
}
function startAlign(o) {
  showPage('sky'); find = keyOf(o); foundDone = true;
  const bar = $('#alignBar');
  bar.innerHTML = `<button class="btn" data-act="${act(endAlign)}">Cancel</button><button class="btn primary" data-act="${act(() => {
    const cur = objectFor(keyOf(o)) || o;
    const err = ((azOf(cur.w) - azOf(cam.fwd) + 540) % 360) - 180;
    S.azOffset = ((S.azOffset + err + 540) % 360) - 180; saveS();
    endAlign(); sfx('align'); buzz(50); toast(`Compass aligned (${err >= 0 ? '+' : '−'}${Math.abs(err).toFixed(1)}°)`, 'var(--aurora)');
  })}">Lock it on ${esc(o.name)}</button>`;
  bindActs(bar); bar.hidden = false;
}
function endAlign() { const bar = $('#alignBar'); if (bar.hidden) return false; bar.hidden = true; bar.innerHTML = ''; find = null; return true; }

function renderSetup() {
  const el = $('#setup');
  const sw = (key, title, sub, after) => item({ t: title, s: sub, noChev: true, tail: `<span class="sw ${S[key] ? 'on' : ''}"></span>`,
    act: act((r) => { S[key] = !S[key]; saveS(); sfx('tap'); buzz(6); r.querySelector('.sw').classList.toggle('on', S[key]); el._h = ''; if (after) after(); }) });
  let h = `<h1>Settings</h1><p class="sub">SkyPoint ${NAT && NAT.version ? esc(NAT.version()) : 'preview'}</p>`;
  h += group('View',
    sw('camera', 'Camera view', 'Draw the sky over the live camera', () => setCamera(S.camera)) +
    sw('night', 'Night mode', 'Red only, keeps your dark vision', applyNight) +
    item({ t: 'Layers', s: hiddenCount() ? `${OBJ_LAYERS.length - 1 - hiddenCount()} of ${OBJ_LAYERS.length - 1} object layers shown` : S.art ? 'Everything shown, with drawings' : 'All objects shown', act: act(layersSheet) }) +
    sw('belowHorizon', 'Below the horizon', 'Show what’s under your feet, dimmed') +
    sw('touchPan', 'Drag to look', 'Ignore the sensors and pan by touch', () => { q = null; }));
  h += group('Pointing',
    `<div class="item col2"><div class="rowtop"><div class="t">Screen rotation</div></div><div class="s" style="margin:2px 0 10px">${S.rotation === 'auto' ? 'Follows the phone. Lock it if it flips while you point straight up.' : S.rotation === 'landscape' ? 'Landscape, either way round' : 'Upright only'}</div>` +
    `<div class="seg">${[['portrait', 'Portrait'], ['auto', 'Auto'], ['landscape', 'Landscape']].map(([k, n]) => `<button class="${(S.rotation || 'portrait') === k ? 'on' : ''}" data-act="${act(() => { S.rotation = k; saveS(); sfx('tap'); buzz(6); applyRotation(); renderSetup(); })}">${n}</button>`).join('')}</div></div>` +
    item({ t: 'Field of view', s: 'Or pinch on the sky', noChev: true, tail: `<span class="stepper"><button data-act="${act(() => { S.fov = clamp(Math.round(S.fov / 10) * 10 + 10, 20, 110); saveS(); sfx('zoom'); renderSetup(); })}" aria-label="Wider">−</button><span>${Math.round(S.fov)}°</span><button data-act="${act(() => { S.fov = clamp(Math.round(S.fov / 10) * 10 - 10, 20, 110); saveS(); sfx('zoom'); renderSetup(); })}" aria-label="Closer">+</button></span>` }) +
    item({ t: 'Align compass', s: S.azOffset ? `Correction ${S.azOffset >= 0 ? '+' : '−'}${Math.abs(S.azOffset).toFixed(1)}°` : 'Fine-tune north on a star or planet', act: act(alignSheet) }));
  h += group('Sound and feel',
    `<div class="item col2"><div class="rowtop"><div class="t">Volume</div><span class="r" id="volv">${S.volume ? S.volume + '%' : 'Off'}</span></div><input type="range" min="0" max="100" step="5" value="${S.volume}" id="vol" style="--p:${S.volume}%" aria-label="Volume"></div>` +
    sw('haptics', 'Vibration', 'On lock and when you find something') +
    sw('passAlerts', 'Pass alerts', 'Two minutes before a visible pass'));
  h += group('Data',
    item({ t: 'Location', s: LOC.src === 'manual' ? esc(LOC.name || 'Chosen city') : 'GPS and network', act: act(() => showPage('gps')) }) +
    item({ t: 'Update orbits', s: 'ISS, Tiangong and Hubble from CelesTrak', act: act(() => updateTles(true)) }));
  h += `<div class="wordmark">SKYPOINT</div><p class="about">Also on the LilyGO T-Watch Ultra and Waveshare ESP32-C6 AMOLED.<br>` +
    `Stars from <b>HYG v4.1</b> (CC BY-SA) · figures from <b>Stellarium</b> · constellation art by <b>Johan Meuris</b> (Free Art License) · <b>Astronomy Engine</b> and <b>satellite.js</b> (MIT) · orbits from <b>CelesTrak</b>.</p>`;
  el._h = ''; setHtml(el, h);
  const v = $('#vol');
  v.oninput = () => { S.volume = +v.value; v.style.setProperty('--p', S.volume + '%'); $('#volv').textContent = S.volume ? S.volume + '%' : 'Off'; };
  v.onchange = () => { saveS(); sfx('tick'); };
}
function applyNight() { document.body.classList.toggle('night', S.night); }
function renderPage() {
  if (page === 'tonight') renderTonight(); else if (page === 'orbit') renderOrbit();
  else if (page === 'gps') renderGps(); else if (page === 'setup') renderSetup();
}
function showPage(p) {
  if (p === page) { if (p !== 'sky') renderPage(); return; }
  page = p;
  document.querySelectorAll('#tabs button').forEach((b) => b.classList.toggle('on', b.dataset.page === p));
  document.querySelectorAll('.page').forEach((s) => {
    const on = s.id === 'page-' + p; s.hidden = !on;
    if (on) { s.classList.remove('enter'); void s.offsetWidth; s.classList.add('enter'); s.scrollTop = 0; s.firstElementChild._h = ''; }
  });
  if (p !== 'sky') endAlign();
  $('#layersBtn').hidden = p !== 'sky';
  pageAt = nowMs();
  renderPage();
}
document.querySelectorAll('#tabs button').forEach((b) => (b.onclick = () => { audio(); if (b.dataset.page !== page) { sfx('page'); buzz(8); } showPage(b.dataset.page); }));
// Android back button: sheet -> align -> find -> pages -> exit
window.SP_back = () => {
  if (closeSheet()) return true;
  if (endAlign()) return true;
  if (find) { find = null; return true; }
  if (page !== 'sky') { showPage('sky'); return true; }
  return false;
};

/* ------------------------------------------------------------------ object info sheet */
const PLANET_FACTS = {
  Mercury: { d: '4,879 km', day: '58.6 days', year: '88 days', moons: 'None', what: 'Smallest planet, closest to the Sun', wiki: 'Mercury (planet)' },
  Venus: { d: '12,104 km', day: '243 days, backwards', year: '225 days', moons: 'None', what: 'Rocky planet under thick, bright clouds' },
  Mars: { d: '6,779 km', day: '24.6 hours', year: '687 days', moons: '2', what: 'Cold desert planet, coloured by iron oxide' },
  Jupiter: { d: '139,820 km', day: '9.9 hours', year: '11.9 years', moons: '95+', what: 'Largest planet, a gas giant' },
  Saturn: { d: '116,460 km', day: '10.7 hours', year: '29.4 years', moons: '270+', what: 'Gas giant with the brightest rings' },
  Uranus: { d: '50,724 km', day: '17.2 hours', year: '84 years', moons: '28+', what: 'Ice giant tipped on its side' },
  Neptune: { d: '49,244 km', day: '16.1 hours', year: '165 years', moons: '16', what: 'Outermost planet, an ice giant' },
};
const SAT_FACTS = {
  ISS: { wiki: 'International Space Station', what: 'Crewed laboratory run by NASA, Roscosmos, ESA, JAXA and CSA. Continuously occupied since November 2000.', since: '1998' },
  Tiangong: { wiki: 'Tiangong space station', what: 'China’s crewed space station, built from three modules.', since: '2021' },
  Hubble: { wiki: 'Hubble Space Telescope', what: 'Space telescope with a 2.4 m mirror, run by NASA and ESA.', since: '1990' },
};
const GREEK = { 'α': 'Alpha', 'β': 'Beta', 'γ': 'Gamma', 'δ': 'Delta', 'ε': 'Epsilon', 'ζ': 'Zeta', 'η': 'Eta', 'θ': 'Theta', 'ι': 'Iota', 'κ': 'Kappa', 'λ': 'Lambda', 'μ': 'Mu',
  'ν': 'Nu', 'ξ': 'Xi', 'ο': 'Omicron', 'π': 'Pi', 'ρ': 'Rho', 'σ': 'Sigma', 'τ': 'Tau', 'υ': 'Upsilon', 'φ': 'Phi', 'χ': 'Chi', 'ψ': 'Psi', 'ω': 'Omega' };
const DSO_KIND = { Galaxy: 'Galaxy', GC: 'Globular cluster', OC: 'Open cluster', 'Open cluster': 'Open cluster', 'OC+Neb': 'Cluster with nebula', Nebula: 'Nebula',
  PN: 'Planetary nebula', 'Galaxy cloud': 'Star cloud', 'Double star': 'Double star', Asterism: 'Asterism' };

function openLink(url) { if (NAT && NAT.openUrl) NAT.openUrl(url); else window.open(url, '_blank', 'noopener'); }
const fmtDeg = (v, plus) => `${plus && v >= 0 ? '+' : v < 0 ? '−' : ''}${Math.abs(v).toFixed(1)}°`;
function fmtRa(deg) { const h = ((deg / 15) % 24 + 24) % 24, hh = Math.floor(h), mm = Math.floor((h - hh) * 60), ss = Math.round(((h - hh) * 60 - mm) * 60); return `${hh}h ${String(mm).padStart(2, '0')}m ${String(ss % 60).padStart(2, '0')}s`; }
function fmtDec(deg) { const a = Math.abs(deg), d = Math.floor(a), m = Math.round((a - d) * 60); return `${deg < 0 ? '−' : '+'}${d}° ${String(m % 60).padStart(2, '0')}′`; }
function starTemp(bv) { return 4600 * (1 / (0.92 * bv + 1.7) + 1 / (0.92 * bv + 0.62)); }
function starClass(T) {
  if (T > 30000) return ['O', 'blue']; if (T > 10000) return ['B', 'blue-white']; if (T > 7500) return ['A', 'white']; if (T > 6000) return ['F', 'yellow-white'];
  if (T > 5200) return ['G', 'yellow, like the Sun']; if (T > 3700) return ['K', 'orange']; return ['M', 'red'];
}
// rise, transit and set for anything with a J2000 RA/Dec (or a solar-system body)
function skyTimes(body) {
  const t0 = A.MakeTime(new Date(nowMs()));
  const out = {};
  try { const r = A.SearchRiseSet(body, observer, +1, t0, 1.1); out.rise = r ? r.date.getTime() : null; } catch (e) {}
  try { const r = A.SearchRiseSet(body, observer, -1, t0, 1.1); out.set = r ? r.date.getTime() : null; } catch (e) {}
  try { const h = A.SearchHourAngle(body, observer, 0, t0, +1); out.transit = h.time.date.getTime(); out.maxAlt = h.hor.altitude; } catch (e) {}
  if (out.rise == null && out.set == null) out.always = out.maxAlt != null && out.maxAlt > 0;
  return out;
}
function constellationOf(raDeg, decDeg) { try { return A.Constellation(raDeg / 15, decDeg).name; } catch (e) { return ''; } }

function infoVisual(o) {
  const S = 120, c = S / 2;
  let inner = '';
  if (o.kind === 'star' || o.kind === 'sun') {
    const col = o.kind === 'sun' ? '#ffd75e' : SC[o.idx];
    inner = `<defs><radialGradient id="ig"><stop offset="0" stop-color="#fff"/><stop offset=".22" stop-color="${col}"/><stop offset=".5" stop-color="${col}" stop-opacity=".25"/><stop offset="1" stop-color="${col}" stop-opacity="0"/></radialGradient></defs>` +
      `<circle cx="${c}" cy="${c}" r="${c}" fill="url(#ig)"/><path d="M${c} 8V${S - 8}M8 ${c}H${S - 8}" stroke="${col}" stroke-opacity=".5" stroke-width="1"/>`;
  } else if (o.kind === 'planet') {
    const col = o.body.color;
    inner = `<defs><radialGradient id="ig" cx=".35" cy=".35" r=".75"><stop offset="0" stop-color="#fff" stop-opacity=".9"/><stop offset=".25" stop-color="${col}"/><stop offset="1" stop-color="#05080b"/></radialGradient></defs>` +
      `<circle cx="${c}" cy="${c}" r="34" fill="url(#ig)"/>` +
      (o.name === 'Saturn' ? `<ellipse cx="${c}" cy="${c}" rx="56" ry="15" transform="rotate(-18 ${c} ${c})" fill="none" stroke="${col}" stroke-opacity=".75" stroke-width="5"/>` : '');
  } else if (o.kind === 'moon') return moonSvg(o.body.frac, o.body.phase, 40);
  else if (o.kind === 'sat') {
    inner = `<g stroke="#6cf0c3" stroke-width="1.6" fill="none" stroke-linejoin="round"><rect x="50" y="50" width="20" height="20" rx="3"/>` +
      `<rect x="14" y="54" width="30" height="12"/><rect x="76" y="54" width="30" height="12"/><path d="M44 60h6M70 60h6M29 54v12M91 54v12M60 50v-12M54 38h12"/></g>` +
      `<circle cx="${c}" cy="${c}" r="56" fill="none" stroke="#6cf0c3" stroke-opacity=".15" stroke-dasharray="3 5"/>`;
  } else if (o.kind === 'dso') {
    inner = `<defs><radialGradient id="ig"><stop offset="0" stop-color="#e6d6ff"/><stop offset=".3" stop-color="#b98cff" stop-opacity=".55"/><stop offset="1" stop-color="#b98cff" stop-opacity="0"/></radialGradient></defs>` +
      (o.dso.type === 'Galaxy' ? `<ellipse cx="${c}" cy="${c}" rx="56" ry="22" transform="rotate(-25 ${c} ${c})" fill="url(#ig)"/>` : `<circle cx="${c}" cy="${c}" r="50" fill="url(#ig)"/>`);
  }
  return `<svg width="${S}" height="${S}" viewBox="0 0 ${S} ${S}">${inner}</svg>`;
}

function buildInfo(o) {
  const t = nowMs(), tm = A.MakeTime(new Date(t));
  const az = azOf(o.w), alt = altOf(o.w);
  const r = { kicker: kindLabel(o), stats: [], groups: [], wiki: [], blurb: '' };
  const where = [item({ t: 'Direction', r: `${compass(az)} · ${Math.round(az)}°` }), item({ t: 'Height above horizon', r: fmtDeg(alt, true) })];
  const timesItems = (tt) => {
    const a = [];
    if (tt.always) a.push(item({ t: 'Never sets from here', s: 'It circles the pole all night' }));
    else if (tt.rise == null && tt.set == null) a.push(item({ t: 'Doesn’t rise from here', s: 'Too far south (or north) for your latitude' }));
    else {
      const ev = [];
      if (tt.rise) ev.push([tt.rise, item({ t: 'Rises', r: fmtWhen(tt.rise) })]);
      if (tt.transit) ev.push([tt.transit, item({ t: 'Highest', s: tt.maxAlt != null ? `${Math.round(tt.maxAlt)}° up` : '', r: fmtWhen(tt.transit) })]);
      if (tt.set) ev.push([tt.set, item({ t: 'Sets', r: fmtWhen(tt.set) })]);
      ev.sort((x, y) => x[0] - y[0]).forEach((e) => a.push(e[1]));
    }
    return a.join('');
  };
  if (o.kind === 'star') {
    const s = o.star, ly = s[7] * 3.2616, T = starTemp(s[3]), [cls, colour] = starClass(T);
    const absMag = s[7] > 0 ? s[2] - 5 * Math.log10(s[7]) + 5 : null;
    const lum = absMag != null ? Math.pow(10, (4.83 - absMag) / 2.5) : null;
    const con = conName(s[5]);
    const bayer = s[6] ? `${GREEK[s[6]] || s[6]} ${con}` : '';
    r.stats = [['Brightness', `mag ${s[2].toFixed(2)}`], ['Distance', ly > 0 && ly < 1e5 ? `${ly < 100 ? ly.toFixed(1) : Math.round(ly).toLocaleString()} ly` : '—'], ['Surface', `${Math.round(T / 100) * 100} K`]];
    r.blurb = ly > 0 && ly < 3000 ? `The light reaching you tonight left ${o.name} around ${ly < 1 ? 'this year' : Math.round(new Date(t).getFullYear() - ly) < 0 ? Math.round(ly - new Date(t).getFullYear()) + ' BC' : Math.round(new Date(t).getFullYear() - ly)}.` : '';
    r.groups.push(['The star', [
      item({ t: 'Constellation', r: esc(con) }),
      bayer && CAT.names[s[4]] ? item({ t: 'Also known as', r: esc(`${s[6]} ${CAT.cons[s[5]] ? CAT.cons[s[5]][0] : ''}`) }) : '',
      item({ t: 'Colour', s: `Spectral class ${cls}, estimated from its colour`, r: colour.split(',')[0] }),
      lum ? item({ t: 'Brightness vs the Sun', s: 'Visible light, estimated', r: lum >= 10 ? `${Math.round(lum).toLocaleString()}×` : `${lum.toFixed(lum < 1 ? 2 : 1)}×` }) : '',
      item({ t: 'Position (J2000)', r: `${fmtRa(s[0])}<br>${fmtDec(s[1])}` }),
    ].join('')]);
    try { A.DefineStar('Star1', s[0] / 15, s[1], Math.max(1, ly)); r.groups.push(['Tonight', timesItems(skyTimes('Star1'))]); } catch (e) {}
    r.groups.unshift(['Where to look', where.join('')]);
    if (CAT.names[s[4]]) r.wiki = [`${o.name} (star)`, o.name];
    else if (s[6]) r.wiki = [{ search: `${GREEK[s[6]] || ''} ${con} star` }];
  } else if (o.kind === 'planet' || o.kind === 'moon' || o.kind === 'sun') {
    const b = o.body, eqj = A.Equator(b.name, tm, observer, false, true);
    const lightMin = b.distAU * 499.005 / 60;
    if (o.kind === 'planet') {
      const f = PLANET_FACTS[b.name] || {};
      let el = null; try { el = A.Elongation(b.name, tm); } catch (e) {}
      r.stats = [['Brightness', `mag ${b.mag.toFixed(1)}`], ['Distance', `${b.distAU.toFixed(2)} AU`], ['Light takes', lightMin < 60 ? `${lightMin.toFixed(1)} min` : `${(lightMin / 60).toFixed(1)} h`]];
      r.blurb = f.what ? f.what + '.' : '';
      r.groups.push(['Where to look', where.join('') + item({ t: 'Constellation', r: esc(constellationOf(eqj.ra * 15, eqj.dec)) }) +
        (el ? item({ t: 'Angle from the Sun', s: el.visibility === 'morning' ? 'Morning sky' : 'Evening sky', r: `${Math.round(el.elongation)}°` }) : '') +
        (b.ill ? item({ t: 'Lit side facing us', r: `${Math.round(b.ill.phase_fraction * 100)}%` }) : '')]);
      r.groups.push(['Tonight', timesItems(skyTimes(b.name))]);
      if (f.d) r.groups.push(['The planet', item({ t: 'Diameter', r: f.d }) + item({ t: 'One day', r: f.day }) + item({ t: 'One year', r: f.year }) + item({ t: 'Moons', r: f.moons })]);
      r.wiki = [f.wiki || b.name];
    } else if (o.kind === 'moon') {
      const age = (b.phase / 360) * 29.53;
      let full = null, nw = null; try { full = A.SearchMoonPhase(180, tm, 40).date.getTime(); nw = A.SearchMoonPhase(0, tm, 40).date.getTime(); } catch (e) {}
      const km = b.distAU * 149597870.7;
      r.stats = [['Lit', `${Math.round(b.frac * 100)}%`], ['Age', `${age.toFixed(1)} days`], ['Distance', `${Math.round(km / 1000).toLocaleString()}k km`]];
      r.blurb = `${phaseName(b.phase)}. ${km < 370000 ? 'Closer than average, so a little bigger and brighter.' : km > 400000 ? 'Farther than average, so a little smaller.' : ''}`;
      r.groups.push(['Where to look', where.join('') + item({ t: 'Constellation', r: esc(constellationOf(eqj.ra * 15, eqj.dec)) })]);
      r.groups.push(['Tonight', timesItems(skyTimes('Moon'))]);
      r.groups.push(['Phases', (full ? item({ t: 'Next full Moon', r: new Date(full).toLocaleDateString([], { weekday: 'short', month: 'short', day: 'numeric' }) }) : '') +
        (nw ? item({ t: 'Next new Moon', r: new Date(nw).toLocaleDateString([], { weekday: 'short', month: 'short', day: 'numeric' }) }) : '') +
        item({ t: 'Diameter', r: '3,474 km' })]);
      r.wiki = ['Moon'];
    } else {
      r.stats = [['Distance', `${b.distAU.toFixed(3)} AU`], ['Light takes', `${lightMin.toFixed(1)} min`], ['Surface', '5,772 K']];
      r.blurb = 'Never look at the Sun directly or through binoculars or a camera.';
      r.groups.push(['Where to look', where.join('')]);
      r.groups.push(['Today', timesItems(skyTimes('Sun'))]);
      r.wiki = ['Sun'];
    }
  } else if (o.kind === 'sat') {
    const s = o.sat, n = s.now, f = SAT_FACTS[s.name] || {};
    const pv = SAT.propagate(s.rec, new Date(t));
    const v = pv && pv.velocity ? Math.hypot(pv.velocity.x, pv.velocity.y, pv.velocity.z) : 0;
    const period = (2 * Math.PI) / s.rec.no;
    r.stats = [['Height', `${Math.round(n.height)} km`], ['Speed', `${(v * 3600).toLocaleString(undefined, { maximumFractionDigits: 0 })} km/h`], ['One orbit', `${Math.round(period)} min`]];
    r.blurb = f.what || '';
    r.groups.push(['Where to look', where.join('') + item({ t: 'Distance from you', r: `${Math.round(n.range).toLocaleString()} km` }) +
      item({ t: 'Sunlight', s: n.lit ? 'Visible when your sky is dark' : 'In Earth’s shadow, not visible', r: n.lit ? 'Sunlit' : 'Shadow' })]);
    const next = s.passes.filter((p) => p.los > t && p.maxEl > 10).slice(0, 3);
    if (next.length) r.groups.push(['Next passes', next.map((p) => item({ t: fmtWhen(p.aos), s: `${compass(p.fromAz)} to ${compass(p.toAz)} · up to ${Math.round(p.maxEl)}°${p.visible ? ' · visible' : ''}`, r: p.aos < t ? 'now' : inMin(p.aos), hot: p.visible, c: 'var(--aurora)' })).join('')]);
    const epochAge = (t - s.epochMs) / 86400000;
    r.groups.push(['Orbit', item({ t: 'Inclination', r: `${(s.rec.inclo * R2D).toFixed(1)}°` }) + item({ t: 'Catalogue number', r: `NORAD ${s.rec.satnum}` }) +
      (f.since ? item({ t: 'In orbit since', r: f.since }) : '') + item({ t: 'Orbit data age', r: `${epochAge.toFixed(1)} days` })]);
    r.wiki = f.wiki ? [f.wiki] : [{ search: s.name + ' satellite' }];
  } else if (o.kind === 'dso') {
    const d = o.dso, con = constellationOf(d.ra, d.dec);
    r.stats = [['Brightness', `mag ${d.mag.toFixed(1)}`], ['Catalogue', `M${d.m}`], ['Type', (DSO_KIND[d.type] || d.type).split(' ')[0]]];
    r.blurb = d.mag < 5 ? 'Visible to the naked eye from a dark site.' : d.mag < 8 ? 'An easy target for binoculars.' : 'Best with a small telescope and a dark sky.';
    r.groups.push(['Where to look', where.join('') + item({ t: 'Constellation', r: esc(con) }) + item({ t: 'Position (J2000)', r: `${fmtRa(d.ra)}<br>${fmtDec(d.dec)}` })]);
    try { A.DefineStar('Star1', d.ra / 15, d.dec, 1000); r.groups.push(['Tonight', timesItems(skyTimes('Star1'))]); } catch (e) {}
    r.wiki = [`Messier ${d.m}`];
  }
  return r;
}

/* Wikipedia summaries: fetched on request, cached, shown as plain text (no remote HTML) */
const WIKI_API = 'https://en.wikipedia.org/api/rest_v1/page/summary/';
const wikiCache = load('sp_wiki', {});
async function wikiSummary(cands, kind) {
  const key = JSON.stringify(cands);
  if (wikiCache[key] && nowMs() - wikiCache[key].at < 30 * 86400000) return wikiCache[key].v;
  const fits = (j) => j && j.type === 'standard' && j.extract &&
    (kind !== 'star' || /star|binary|system|giant|dwarf/i.test((j.description || '') + ' ' + j.extract.slice(0, 200)));
  let found = null;
  for (const c of cands) {
    try {
      let title = c;
      if (typeof c === 'object') {
        const q2 = await fetch(`https://en.wikipedia.org/w/api.php?action=query&list=search&srlimit=1&format=json&origin=*&srsearch=${encodeURIComponent(c.search)}`).then((x) => x.json());
        const hit = q2.query && q2.query.search[0]; if (!hit) continue; title = hit.title;
      }
      const j = await fetch(WIKI_API + encodeURIComponent(title.replace(/ /g, '_')) + '?redirect=true').then((x) => (x.ok ? x.json() : null));
      if (fits(j)) { found = { title: j.title, desc: j.description || '', extract: j.extract, thumb: j.thumbnail && j.thumbnail.source, url: (j.content_urls && (j.content_urls.mobile || j.content_urls.desktop).page) || '' }; break; }
    } catch (e) { throw e; }
  }
  wikiCache[key] = { at: nowMs(), v: found }; save('sp_wiki', wikiCache);
  return found;
}
function wikiSearchUrl(o) { return `https://en.m.wikipedia.org/w/index.php?search=${encodeURIComponent(o.name + (o.kind === 'star' ? ' star' : ''))}`; }

let infoKey = null;
function infoSheet(o) {
  if (!o || !observer) return;
  o = objectFor(keyOf(o)) || o;
  infoKey = keyOf(o);
  const r = buildInfo(o);
  const finding = find === keyOf(o);
  let h = `<div class="info-hero"><div class="vis">${infoVisual(o)}</div><div class="kick">${esc(r.kicker)}</div>` +
    (r.blurb ? `<p class="blurb">${esc(r.blurb)}</p>` : '') + `</div>`;
  if (r.stats.length) h += strip(r.stats);
  h += `<div class="btns"><button class="btn primary wide" data-act="${act(() => { if (find === keyOf(o)) { find = null; toast('Stopped finding'); } else { find = keyOf(o); foundDone = altOf(o.w) > -90 && Math.acos(clamp(dot3(o.w, cam.fwd), -1, 1)) * R2D < 2.5; toast(`Finding ${o.name}`, 'var(--ember)'); } sfx('ok'); closeSheet(); if (page !== 'sky') showPage('sky'); })}">${finding ? 'Stop finding' : 'Find in sky'}</button></div>`;
  for (const [title, inner] of r.groups) if (inner) h += group(title, inner);
  h += `<div class="group"><div class="gh">From Wikipedia</div><div class="list wiki" id="wikiBox"><div class="wiki-load"><span></span><span></span><span></span></div></div></div>`;
  openSheet(esc(o.name), '', h, { onClose: () => { infoKey = null; } });
  $('#sheet').classList.add('tall');
  const box = $('#wikiBox');
  const fallback = (msg) => {
    box.innerHTML = `<div class="empty">${msg}</div><div class="btns" style="margin:0 0 16px"><button class="btn" data-act="${act(() => openLink(wikiSearchUrl(o)))}">Search Wikipedia</button></div>`;
    bindActs(box);
  };
  if (!r.wiki.length) { fallback('No Wikipedia article is linked to this object.'); return; }
  wikiSummary(r.wiki, o.kind).then((w) => {
    if (infoKey !== keyOf(o) || !document.body.contains(box)) return;
    if (!w) { fallback('Wikipedia doesn’t have a clear match for this one.'); return; }
    box.innerHTML = `<div class="wiki-body">${w.thumb ? `<img src="${esc(w.thumb)}" alt="" onerror="this.remove()">` : ''}` +
      `<div class="wiki-title">${esc(w.title)}</div>${w.desc ? `<div class="wiki-desc">${esc(w.desc)}</div>` : ''}<p>${esc(w.extract)}</p></div>` +
      `<div class="btns" style="margin:0 0 16px"><button class="btn" data-act="${act(() => openLink(w.url || wikiSearchUrl(o)))}">Read on Wikipedia</button></div>` +
      `<div class="wiki-lic">Text from Wikipedia, CC BY-SA 4.0</div>`;
    bindActs(box);
  }).catch(() => { if (infoKey === keyOf(o)) fallback('Wikipedia isn’t reachable. Connect to the internet to read more.'); });
}

/* ------------------------------------------------------------------ intro warp */
function intro() {
  const box = $('#intro'), c = $('#introCanvas'), g = c.getContext('2d');
  c.width = W * DPR; c.height = H * DPR; g.scale(DPR, DPR);
  const stars = Array.from({ length: 260 }, () => ({ x: (Math.random() - 0.5) * W, y: (Math.random() - 0.5) * H, z: Math.random() * W }));
  const t0 = performance.now();
  sfx('boot');
  const step = (t) => {
    const e = (t - t0) / 1000;
    g.fillStyle = 'rgba(0,0,0,.35)'; g.fillRect(0, 0, W, H);
    const speed = 6 + e * 40;
    for (const s of stars) {
      const pz = s.z; s.z -= speed; if (s.z < 1) { s.z = W; s.x = (Math.random() - 0.5) * W; s.y = (Math.random() - 0.5) * H; continue; }
      const k = 260 / s.z, pk = 260 / pz;
      g.strokeStyle = `rgba(${180 + 60 * (1 - s.z / W)},236,${244},${1 - s.z / W})`; g.lineWidth = 1.6 * (1 - s.z / W) + 0.3;
      g.beginPath(); g.moveTo(W / 2 + s.x * pk, H / 2 + s.y * pk); g.lineTo(W / 2 + s.x * k, H / 2 + s.y * k); g.stroke();
    }
    if (e < 1.7) requestAnimationFrame(step); else { box.classList.add('out'); setTimeout(() => box.remove(), 450); }
  };
  requestAnimationFrame(step);
  box.onclick = () => audio();
}

/* ------------------------------------------------------------------ boot */
function boot() {
  readScreenRot(); resize(); setF(); applyNight();
  if (S.rotation && S.rotation !== 'portrait') applyRotation();
  $('#layersBtn').onclick = () => { audio(); sfx('page'); buzz(8); layersSheet(); };
  updateLayersBtn();
  if (observer) declination = NAT && NAT.declination ? +NAT.declination(LOC.lat, LOC.lon, LOC.alt) || 0 : 0;
  initSats();
  if (NAT && NAT.requestLocation && !S.manualLoc) NAT.requestLocation();
  browserGeo();
  if (S.camera) setCamera(true);
  if (!tleFetchedMs || nowMs() - tleFetchedMs > 2 * 86400000) setTimeout(() => updateTles(false), 2500);
  setTimeout(() => { if (!sensorSeen && !S.touchPan && page === 'sky') toast('No motion sensor. Drag to look around.', 'var(--warn)', 3500); }, 2500);
  // desktop preview: ?lat=41.88&lon=-87.63&az=120&alt=30&fov=70&page=tonight&nointro
  const qs = new URLSearchParams(location.search);
  if (qs.has('lat')) setLocation(+qs.get('lat'), +qs.get('lon'), 0, 'manual', qs.get('name') || 'Preview', 0);
  if (qs.has('shot')) window.SP_SHOT = true;
  if (qs.has('utc')) TOFF = Date.parse(qs.get('utc')) - Date.now();
  if (qs.has('az')) { pan.az = +qs.get('az'); pan.alt = +qs.get('alt'); }
  if (qs.has('fov')) S.fov = +qs.get('fov');
  if (qs.has('find')) { find = qs.get('find'); foundDone = true; }
  if (qs.has('nointro')) $('#intro').remove(); else intro();
  if (qs.has('page')) setTimeout(() => showPage(qs.get('page')), 50);
  requestAnimationFrame(tick);
  window.SP_DEBUG = { cardRect: () => cardRect, S, LOC, pan, sats, bodies: () => bodies, showPage, setLocation, cam, fps: () => fps };
}
document.fonts && document.fonts.load ? Promise.all(['400 16px Saira', '500 16px Saira', '600 16px Saira', '700 16px Saira'].map((f) => document.fonts.load(f))).finally(boot) : boot();
