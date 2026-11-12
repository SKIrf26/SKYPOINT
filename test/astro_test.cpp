// Host test: prints alt/az for every body + some stars; compared against PyEphem by astro_check.py
#include "../SkyPoint/astro.h"
#include <stdio.h>
#include <stdlib.h>
using namespace astro;
int main(int argc, char **argv){
  double lat=atof(argv[1]), lon=atof(argv[2]);
  for(int i=3;i<argc;i++){
    double unix_t=atof(argv[i]); double jd=julianDate(unix_t);
    Mat3 E=equatorialToEnu(jd,lat,lon);
    for(int b=0;b<BODY_COUNT;b++){
      BodyState s=computeBody((Body)b,jd,lat,lon,0);
      double az,alt; enuToAzAlt(mul(E,radecToVec(s.ra,s.dec)),az,alt);
      printf("%s %.0f %.5f %.5f %.5f %.5f %.3f %.4f %.6f\n",bodyName((Body)b),unix_t,s.ra,s.dec,az,alt,s.mag,s.phase,s.distAu);
    }
    // Sirius J2000 101.28715533 -16.71611586 ; Vega 279.23473479 38.78368896
    double st[][2]={{101.28715533,-16.71611586},{279.23473479,38.78368896}};
    const char*nm[]={"Sirius","Vega"};
    for(int k=0;k<2;k++){ Vec3 v=mul(precessionFromJ2000(jd),radecToVec(st[k][0],st[k][1])); double az,alt; enuToAzAlt(mul(E,v),az,alt);
      printf("%s %.0f 0 0 %.5f %.5f 0 0 0\n",nm[k],unix_t,az,alt);}
    double sr=nextCrossing(SUN,jd,1.0,lat,lon,-0.833,true), ss=nextCrossing(SUN,jd,1.0,lat,lon,-0.833,false);
    double mr=nextCrossing(MOON,jd,1.0,lat,lon,0.125,true);
    printf("RISESET %.0f %.6f %.6f %.6f %s %.2f\n",unix_t,sr,ss,mr,moonPhaseName(jd),moonAgeDays(jd));
  }
}
