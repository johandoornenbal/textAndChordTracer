#include "MusicAnalysis.h"
#include "TapTempo.h"
#include <iostream>
#include <stdexcept>
void check(bool b,const char* m) { if(!b) throw std::runtime_error(m); }
void tapTests() {
    TapTempo taps;
    for(int i=0;i<4;++i) taps.tap(i*500.,4,4);
    check(taps.ready(4,4) && std::abs(taps.quarterBpm()-120.)<1.e-6,"4/4 tap estimate failed");
    taps.reset(); for(int i=0;i<5;++i) taps.tap(i*500.,6,8);
    check(!taps.ready(6,8),"6/8 accepted fewer than six taps"); taps.tap(2500.,6,8);
    check(taps.ready(6,8) && std::abs(taps.quarterBpm()-60.)<1.e-6,"6/8 did not treat taps as eighth notes");
    taps.reset(); for(int i=0;i<3;++i) taps.tap(i*2000.,3,4);
    check(taps.ready(3,4) && std::abs(taps.quarterBpm()-30.)<1.e-6,"3/4 tap count or slow estimate failed");
    taps.reset(); for(int i=0;i<12;++i) taps.tap(i*400.,12,16);
    check(taps.ready(12,16) && std::abs(taps.quarterBpm()-37.5)<1.e-6,"Sixteenth-note meter taps failed");
    taps.reset(); for(int i=0;i<4;++i) taps.tap(i*2500.,4,4);
    check(taps.ready(4,4) && std::abs(taps.quarterBpm()-24.)<1.e-6,"Sub-40 BPM tapping failed");
    taps.reset(); taps.tap(0,4,4); taps.tap(70,4,4); taps.tap(500,4,4); taps.tap(1010,4,4); taps.tap(1500,4,4);
    check(taps.count()==4 && std::abs(taps.quarterBpm()-120.)<.1,"Tap jitter/double-click handling failed");
    taps.tap(1800,6,8); check(taps.count()==1 && !taps.ready(6,8),"Changing meter reused old tap intervals");
    TapCaptureGate gate;
    check(gate.permit(true,0,.01),"Normal playback was blocked"); gate.begin();
    check(!gate.permit(true,.01,.01),"Captured while tapping"); gate.finish();
    check(!gate.permit(true,.02,.01),"Captured before playback restart");
    check(!gate.permit(false,.03,.01),"Captured stopped transport");
    check(gate.permit(true,.04,.01),"Did not rearm after stop/play");
    gate.begin(); gate.permit(true,4,.01); gate.finish();
    check(!gate.permit(true,4.01,.01) && gate.permit(true,0,.01),"Seek-back replay did not rearm capture");
    gate.begin(); gate.finish(); gate.hostWentIdle(); check(gate.permit(true,.01,.01),"Host idle/restart did not rearm");
    gate.begin(); gate.recordNow(); check(gate.permit(true,.02,.01),"Manual record-now override failed");
}
int main() try {
    tapTests();
    music::ChordDetector detector;
    for(int root=0;root<12;++root) for(bool minor:{false,true}) {
        std::array<float,music::fftSize> l{},r{};
        const int notes[]{48+root,48+root+(minor?3:4),55+root};
        for(int i=0;i<music::fftSize;++i) for(int note:notes) l[static_cast<size_t>(i)]+=static_cast<float>(.12*std::sin(2*music::pi*440.*std::pow(2.,(note-69)/12.)*i/music::analysisRate));
        for(int i=0;i<music::fftSize;++i) r[static_cast<size_t>(i)]=-l[static_cast<size_t>(i)];
        const char* names[]{"C","C#","D","Eb","E","F","F#","G","Ab","A","Bb","B"};
        auto chord=detector.analyse(l,r);
        check(chord.name==std::string(names[root])+(minor?"m":""),"Major/minor chord recognition failed");
    }
    std::array<float,music::fftSize> silence{}; check(detector.analyse(silence,silence).name.empty(),"Silence generated a chord");
    music::Grid grid; grid.numerator=6; grid.denominator=8; grid.quarter=4.6;
    check(music::barNumber(grid)==1 && music::beatNumber(grid)==3,"6/8 measure placement failed");
    music::TempoEstimator tempo;
    for(int i=0;i<512;++i) tempo.push(i%24==0?1.:0.);
    check(std::abs(tempo.estimate()-117.1875)<1.,"Tempo estimate failed");
    for(int beats:{3,4}) {
        music::TempoEstimator accents;
        for(int i=0;i<512;++i) accents.push(i%24==0?(i/24%beats==0?1.:.15):0.);
        check(accents.meter(117.1875)==beats,"Accent meter recognition failed");
    }
    music::TempoEstimator slow;
    for(int i=0;i<1800;++i) slow.push(i%117==0?1.:0.);
    check(std::abs(slow.estimate(24.)-24.03846)<.2,"Sub-40 BPM audio estimation failed");
    check(tempo.meter(117.1875)==0,"Unaccented pulses invented a confident meter");
    grid.originBar=60; grid.originQuarter=240.; grid.quarter=241.; grid.numerator=4; grid.denominator=4;
    check(music::barNumber(grid)==60 && music::beatNumber(grid)==1,"Host measure anchor failed");
    std::cout<<"24 stereo/antiphase chord fixtures, silence, 6/8 grid and pulse tempo passed\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
