import ephem, subprocess, math, sys, datetime
lat, lon = 41.88, -87.63
times = [1793000000 + k*86400*37.3 for k in range(12)] + [1794340000]
out = subprocess.check_output(['./astro_test', str(lat), str(lon)] + [str(int(t)) for t in times]).decode().split('\n')
obs = ephem.Observer(); obs.lat = str(lat); obs.lon = str(lon); obs.elevation = 0; obs.pressure = 0
bodies = {'Sun':ephem.Sun(),'Moon':ephem.Moon(),'Mercury':ephem.Mercury(),'Venus':ephem.Venus(),'Mars':ephem.Mars(),
          'Jupiter':ephem.Jupiter(),'Saturn':ephem.Saturn(),'Uranus':ephem.Uranus(),'Neptune':ephem.Neptune(),
          'Sirius':ephem.star('Sirius'),'Vega':ephem.star('Vega')}
worst = {}
def sep(az1,al1,az2,al2):
    a=math.radians
    c=math.sin(a(al1))*math.sin(a(al2))+math.cos(a(al1))*math.cos(a(al2))*math.cos(a(az1-az2))
    return math.degrees(math.acos(max(-1,min(1,c))))
for line in out:
    p=line.split()
    if not p: continue
    if p[0]=='RISESET':
        t=float(p[1]); obs.date=datetime.datetime.utcfromtimestamp(t); 
        obs.horizon='-0:50'; s=ephem.Sun()
        er=obs.next_rising(s, use_center=False); es=obs.next_setting(s)
        def jd2unix(j): return (j-2440587.5)*86400
        def e2unix(d): return (ephem.julian_date(d)-2440587.5)*86400
        obs.horizon='0'; emr=obs.next_rising(ephem.Moon())
        print('rise/set err (s): sunrise %.0f sunset %.0f moonrise %.0f  %s age %s'%(jd2unix(float(p[2]))-e2unix(er), jd2unix(float(p[3]))-e2unix(es), jd2unix(float(p[4]))-e2unix(emr), p[5:-1], p[-1]))
        continue
    name=p[0]; t=float(p[1]); az=float(p[4]); alt=float(p[5])
    obs.date=datetime.datetime.utcfromtimestamp(t); b=bodies[name]; b.compute(obs)
    d=sep(az,alt,math.degrees(b.az),math.degrees(b.alt))*60
    worst[name]=max(worst.get(name,0),d)
    if name not in ('Sirius','Vega'):
        dm=float(p[6])-b.mag
        worst[name+'_mag']=max(worst.get(name+'_mag',0),abs(dm))
        if hasattr(b,'phase'): worst[name+'_phase']=max(worst.get(name+'_phase',0),abs(float(p[7])*100-b.phase))
for k,v in worst.items(): print('%-14s worst %.2f %s'%(k,v,'arcmin' if '_' not in k else ''))
