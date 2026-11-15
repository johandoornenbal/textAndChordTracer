#include "../Source/TraceWorker.h"
#include <iostream>
#include <stdexcept>
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
int main() try {
    auto worker=std::make_unique<TraceWorker>();
    const int total=12000*28;
    for(int offset=0;offset<total;offset+=512) {
        AudioCapture packet; packet.count=std::min(512,total-offset); packet.rate=12000; packet.seconds=offset/12000.; packet.generation=worker->generation.load(); packet.playing=true; packet.mode=0;
        for(int i=0;i<packet.count;++i) {
            const int sample=offset+i; const bool slower=sample>12000*14;
            const int interval=slower?7168:6144,beat=sample/interval; const double phase=static_cast<double>(sample%interval)/interval;
            const double envelope=phase<.65?(beat%4==0?.9:.35):.015;
            const int root=sample<12000*14?48:53; double value=0;
            for(int note:{root,root+4,root+7}) value+=.13*std::sin(2*music::pi*440*std::pow(2.,(note-69)/12.)*sample/12000.)*envelope;
            packet.left[static_cast<size_t>(i)]=static_cast<float>(value); packet.right[static_cast<size_t>(i)]=static_cast<float>(-value);
        }
        int attempts=0; while(!worker->audio.push(packet)) { juce::Thread::sleep(2); check(++attempts<2000,"Analysis worker stalled"); }
    }
    AudioCapture stop; stop.generation=worker->generation.load(); while(!worker->audio.push(stop)) juce::Thread::sleep(2);
    for(int retry=0;retry<1000 && (!worker->audio.empty() || worker->capturing.load());++retry) juce::Thread::sleep(5);
    auto bars=worker->document.snapshot(); check(bars.size()>6,"Audio did not produce measures");
    bool c=false,f=false; for(const auto& bar:bars) for(const auto& chord:bar.chords) { c=c || chord=="C"; f=f || chord=="F"; }
    check(c && f,"Actual worker failed to capture chord changes");
    std::cout<<"Final moving tempo: "<<worker->detectedBpm.load()<<" BPM\n";
    check(worker->detectedBpm.load()>90 && worker->detectedBpm.load()<112,"Moving tempo estimate failed");
    check(std::abs(bars.front().bpm-bars.back().bpm)>5.,"Measure tempos did not follow the performance");
    const auto barCount=bars.size(); const auto focus=worker->focusRevision.load();
    auto replay=[&](double start) {
        for(int block=0;block<24;++block) {
            AudioCapture packet; packet.count=512; packet.rate=12000; packet.seconds=start+block*512./12000.; packet.generation=worker->generation.load(); packet.playing=true; packet.mode=0;
            for(int i=0;i<512;++i) {
                double value=0; for(int note:{48,52,55}) value+=.1*std::sin(2*music::pi*440*std::pow(2.,(note-69)/12.)*(block*512+i)/12000.);
                packet.left[static_cast<size_t>(i)]=static_cast<float>(value); packet.right[static_cast<size_t>(i)]=static_cast<float>(-value);
            }
            while(!worker->audio.push(packet)) juce::Thread::sleep(2);
        }
        AudioCapture end; end.generation=worker->generation.load(); while(!worker->audio.push(end)) juce::Thread::sleep(2);
        for(int retry=0;retry<1000 && (!worker->audio.empty() || worker->capturing.load());++retry) juce::Thread::sleep(5);
    };
    const auto backward=worker->document.positionAt(2.+24*512./12000.);
    check(backward.has_value(),"Original audio did not record a resume map");
    replay(2.);
    check(worker->currentBar.load()==music::barNumber(*backward),"Backward restart resumed in the wrong bar");
    check(worker->document.snapshot().size()==barCount && worker->focusRevision.load()>focus,"Replay duplicated bars or failed to refocus");
    const auto forward=worker->document.positionAt(22.+24*512./12000.); check(forward.has_value(),"Forward resume position missing");
    replay(22.);
    check(worker->currentBar.load()==music::barNumber(*forward) && worker->document.snapshot().size()==barCount,"Forward seek did not reuse previously reached timing");
    check(!worker->document.positionAt(35.),"Unvisited timeline section was mapped");
    const auto stale=worker->generation.load(); worker->newTake(); music::Grid grid; worker->document.word(grid,0,"stale",stale);
    check(worker->document.snapshot().empty(),"New take accepted stale transcription");
    std::cout<<"Actual audio queue, chord changes, measure creation, drifting tempo, flush and stale-take rejection passed\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
