(() => { const qs = new URLSearchParams(location.search); const az = +(qs.get('saz')||0), alt = +(qs.get('salt')||30);
 const h=-az*Math.PI/360, p=(90+alt)*Math.PI/360; const z1=Math.sin(h), w1=Math.cos(h), x2=Math.sin(p), w2=Math.cos(p);
 const q=[w1*x2, z1*x2, z1*w2, w1*w2].join(',')+',3';
 window.SkyPointNative={orientation:()=>q, location:()=>JSON.stringify({lat:41.88,lon:-87.63,alt:180,acc:4,provider:'gps'}),
 gnss:()=>JSON.stringify({sats:[[45,60,42,1,1],[200,20,30,1,6],[300,40,25,0,3],[120,10,18,0,5],[80,35,38,1,1],[250,70,40,1,6],[160,50,33,1,5],[20,15,22,0,1],[330,25,29,1,3]]}),
 declination:()=>-3.4, vibrate:()=>{}, version:()=>'1.0 Beta 2', hasCamera:()=>false, requestCamera:()=>{}, requestLocation:()=>{}, fetchText:()=>{}};})();
