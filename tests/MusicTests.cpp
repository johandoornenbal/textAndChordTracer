#include "MusicAnalysis.h"
#include <iostream>
#include <stdexcept>
void check(bool b,const char* m) { if(!b) throw std::runtime_error(m); }
int main() try {
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
    check(tempo.meter(117.1875)==0,"Unaccented pulses invented a confident meter");
    grid.originBar=60; grid.originQuarter=240.; grid.quarter=241.; grid.numerator=4; grid.denominator=4;
    check(music::barNumber(grid)==60 && music::beatNumber(grid)==1,"Host measure anchor failed");
    std::cout<<"24 stereo/antiphase chord fixtures, silence, 6/8 grid and pulse tempo passed\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
