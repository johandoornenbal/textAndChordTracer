#pragma once
#include <array>
#include <algorithm>
#include <atomic>
#include <cmath>
class TapTempo {
public:
    void reset() { *this=TapTempo{}; }
    void tap(double milliseconds,int beats,int unit) {
        if(!std::isfinite(milliseconds)) return;
        if(taps==0 || beats!=numerator || unit!=denominator) {
            reset(); numerator=beats; denominator=unit; previous=milliseconds; taps=1; return;
        }
        const double interval=milliseconds-previous,beatLength=4./unit;
        if(interval<=0 || interval>60000.*beatLength/10.) {
            reset(); numerator=beats; denominator=unit; previous=milliseconds; taps=1; return;
        }
        // Ignore accidental double clicks without shifting the previous valid tap.
        if(interval<60000.*beatLength/300.) return;
        intervals[static_cast<size_t>(head)]=interval; head=(head+1)%24;
        stored=std::min(stored+1,24); previous=milliseconds; ++taps;
    }
    double quarterBpm() const {
        if(stored==0) return 0;
        auto values=intervals; std::sort(values.begin(),values.begin()+stored);
        const double median=stored%2?values[static_cast<size_t>(stored/2)]:(values[static_cast<size_t>(stored/2-1)]+values[static_cast<size_t>(stored/2)])*.5;
        return 60000./median*4./denominator;
    }
    bool matchesMeter(int beats,int unit) const { return taps==0 || (numerator==beats && denominator==unit); }
    bool ready(int beats,int unit) const { return numerator==beats && denominator==unit && taps>=std::max(2,beats) && quarterBpm()>=10. && quarterBpm()<=300.; }
    int count() const { return taps; }
private:
    std::array<double,24> intervals{};
    double previous=0;
    int head=0,stored=0,taps=0,numerator=4,denominator=4;
};
// UI writes atomic state; only the audio thread owns the transport history.
class TapCaptureGate {
public:
    void begin() { tapping=true; waiting=false; stopped=false; }
    void finish() { stopped=false; ++generation; waiting=true; tapping=false; }
    void recordNow() { tapping=false; waiting=false; stopped=false; }
    bool isTapping() const { return tapping.load(); }
    bool isWaiting() const { return waiting.load(); }
    void hostWentIdle() { if(waiting.load()) stopped=true; }
    void resetTransport() { havePosition=false; lastPlaying=false; }
    bool permit(bool playing,double seconds,double duration) {
        const bool seek=havePosition && std::isfinite(seconds) && std::abs(seconds-lastEnd)>.05;
        if(waiting.load()) {
            const auto current=generation.load();
            if(current!=seenGeneration) { seenGeneration=current; if(!lastPlaying) stopped=true; }
            if(!playing) stopped=true;
            if(playing && (stopped.load() || seek)) { waiting=false; stopped=false; }
        }
        lastPlaying=playing; lastEnd=seconds+duration; havePosition=true;
        return playing && !tapping.load() && !waiting.load();
    }
private:
    std::atomic<bool> tapping{false},waiting{false},stopped{false};
    std::atomic<unsigned> generation{0};
    unsigned seenGeneration=0;
    bool havePosition=false,lastPlaying=false;
    double lastEnd=0;
};
