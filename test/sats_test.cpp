#include "../SkyPoint/sats.h"
#include "../SkyPoint/astro.h"
#include <stdio.h>
int main(){ satsBegin(); satsSetSite(41.88,-87.63,180);
  double jd0=astro::julianDate(1794340000.0);
  for(int k=0;k<6;k++){ double jd=jd0+k*0.0137; SatNow o; satCompute(0,jd,o); printf("POS %.6f %.4f %.4f %.2f %d\n",jd,o.az,o.el,o.rangeKm,o.vis);}
  double jd=jd0; for(int p=0;p<4;p++){ SatPass ps; if(!satNextPass(0,jd,10,ps)) break; printf("PASS %.6f %.6f %.6f %.1f %d\n",ps.jdStart,ps.jdMax,ps.jdStop,ps.maxEl,ps.visible); jd=ps.jdStop+0.001;}
  printf("AGE %.2f %s\n", satTleAgeDays(0,jd0), satName(0));
}
