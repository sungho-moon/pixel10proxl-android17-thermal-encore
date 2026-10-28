#include "fas-rs-protocol.h"
#include "fas-rs-frequency.h"
#include <cassert>
#include <iostream>
int main() {
    assert(pixel_quantize_floor({600,100,500,200},450,600)==500);
    assert(pixel_quantize_floor({100,200,500,600},550,500)==500);
    assert(pixel_quantize_floor({100,200,500,600},450,450)==200);
    assert(pixel_quantize_floor({100,200},500,50)==0);
    FasRsSnapshot s;
    const std::string valid = "FASRS1 100 200 10000 1 120 118000 1 2 0 1000000 2 1800000";
    assert(parse_fas_rs(valid,100,200,10001,0,s) && s.cpu.at(2)==1800000);
    assert(!parse_fas_rs(valid,100,200,12000,0,s)); // stale request
    assert(!parse_fas_rs(valid,101,200,10001,0,s)); // other PID
    assert(!parse_fas_rs(valid,100,201,10001,0,s)); // PID reuse
    assert(!parse_fas_rs(valid,100,200,10001,1,s)); // replay
    assert(!parse_fas_rs(valid,100,200,9999,0,s)); // future timestamp
    assert(!parse_fas_rs(valid+" trailing",100,200,10001,0,s));
    assert(!parse_fas_rs("FASRS1 100 200 10000 2 120 118000 1 2 0 1 0 2",100,200,10001,0,s));
    assert(!parse_fas_rs("FASRS1 100 200 10000 2 120 118000 1 1 0 -1",100,200,10001,0,s));
    assert(parse_fas_rs("FASRS1 100 200 10000 2 0 0 0 0",100,200,10001,0,s) && !s.ready && s.cpu.empty());
    std::cout << "FASRS_PROTOCOL PASS\n";
}
