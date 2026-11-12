#include "../SkyPoint/sky.h"
#include "../SkyPoint/sats.h"
#include <chrono>
#include <stdio.h>
using namespace std::chrono;
int main(){ satsBegin(); skyBegin(); Observer o; o.lat=41.88;o.lon=-87.63;o.valid=true;
 auto t0=steady_clock::now(); skyUpdate(1794706200,o); auto t1=steady_clock::now();
 SatPass p; satNextPass(0,skyJd(),10,p); auto t2=steady_clock::now();
 ObjRef r; r.kind=OK_PLANET; r.idx=astro::JUPITER; skyRiseTime(r); auto t3=steady_clock::now();
 skyUpdate(1794706201,o); auto t4=steady_clock::now();
 printf("skyUpdate first %.2f ms, passsearch %.2f ms, planet rise %.2f ms, skyUpdate %.3f ms\n",
  duration<double,std::milli>(t1-t0).count(),duration<double,std::milli>(t2-t1).count(),duration<double,std::milli>(t3-t2).count(),duration<double,std::milli>(t4-t3).count());}
