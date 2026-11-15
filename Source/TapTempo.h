#pragma once
#include <array>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>
#include <limits>
#include "MusicAnalysis.h"
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
// A pass may contain many explicitly marked bars. Incomplete middle counts
// fall back to the selected meter between downbeats; complete bars use every tap.
class BarTapSession {
public:
    struct Beat { double milliseconds=0,seconds=std::numeric_limits<double>::quiet_NaN(); };
    struct Bar { std::vector<Beat> counts; int beats=4,unit=4; Beat end; bool closed=false; };
    void reset() { bars.clear(); total=0; }
    bool downbeat(double milliseconds,double seconds,int beats,int unit) {
        if(!std::isfinite(milliseconds) || total>=4096) return false;
        if(!bars.empty()) {
            const auto& last=bars.back().counts.back();
            if(milliseconds-last.milliseconds<60000.*4./bars.back().unit/300.) return false;
            bars.back().end={milliseconds,seconds}; bars.back().closed=true;
        }
        Bar bar; bar.beats=std::clamp(beats,2,12); bar.unit=unit; bar.counts.push_back({milliseconds,seconds}); bars.push_back(std::move(bar)); ++total; return true;
    }
    bool other(double milliseconds,double seconds) {
        if(bars.empty() || total>=4096 || static_cast<int>(bars.back().counts.size())>=bars.back().beats) return false;
        const auto& last=bars.back().counts.back();
        if(!std::isfinite(milliseconds) || milliseconds-last.milliseconds<60000.*4./bars.back().unit/300.) return false;
        bars.back().counts.push_back({milliseconds,seconds}); ++total; return true;
    }
    double quarterBpm() const {
        std::vector<double> durations;
        for(const auto& bar:bars) {
            const double beatLength=4./bar.unit;
            if(bar.closed && static_cast<int>(bar.counts.size())!=bar.beats) {
                durations.push_back((bar.end.milliseconds-bar.counts.front().milliseconds)/(bar.beats*beatLength));
            } else {
                for(size_t i=1;i<bar.counts.size();++i) durations.push_back((bar.counts[i].milliseconds-bar.counts[i-1].milliseconds)/beatLength);
                if(bar.closed) durations.push_back((bar.end.milliseconds-bar.counts.back().milliseconds)/beatLength);
            }
        }
        durations.erase(std::remove_if(durations.begin(),durations.end(),[](double value) { return value<200. || value>6000.; }),durations.end());
        if(durations.empty()) return 0;
        std::sort(durations.begin(),durations.end()); const auto n=durations.size();
        const double median=n%2?durations[n/2]:(durations[n/2-1]+durations[n/2])*.5;
        return 60000./median;
    }
    int completed() const {
        int count=0; for(const auto& bar:bars) if(bar.closed || static_cast<int>(bar.counts.size())==bar.beats) ++count; return count;
    }
    bool ready() const { return completed()>0 && quarterBpm()>=10. && quarterBpm()<=300.; }
    int barCount() const { return static_cast<int>(bars.size()); }
    int count() const { return bars.empty()?0:static_cast<int>(bars.back().counts.size()); }
    int beats() const { return bars.empty()?4:bars.back().beats; }
    int unit() const { return bars.empty()?4:bars.back().unit; }
    double firstSeconds() const { return bars.empty()?std::numeric_limits<double>::quiet_NaN():bars.front().counts.front().seconds; }
    std::vector<music::TimedGrid> guide(long long firstBar,double firstQuarter,double stopMilliseconds,double stopSeconds) const {
        std::vector<music::TimedGrid> result; double quarter=firstQuarter; const double estimate=quarterBpm();
        for(size_t barIndex=0;barIndex<bars.size();++barIndex) {
            const auto& bar=bars[barIndex]; const double beatLength=4./bar.unit;
            auto end=bar.end;
            if(!bar.closed) {
                if(static_cast<int>(bar.counts.size())!=bar.beats || estimate<=0) break;
                const auto& last=bar.counts.back(); const double beatMillis=60000.*beatLength/estimate;
                end.milliseconds=std::min(stopMilliseconds,last.milliseconds+beatMillis);
                end.seconds=std::min(stopSeconds,last.seconds+beatMillis*.001);
            }
            std::vector<Beat> anchors=bar.counts;
            const bool complete=static_cast<int>(anchors.size())==bar.beats;
            if(!complete) anchors.resize(1);
            anchors.push_back(end);
            bool valid=std::isfinite(anchors.front().seconds) && (result.empty() || anchors.front().seconds>=result.back().end);
            for(size_t i=1;i<anchors.size();++i) valid=valid && std::isfinite(anchors[i].seconds) && anchors[i].seconds>anchors[i-1].seconds;
            if(valid) for(size_t i=0;i+1<anchors.size();++i) {
                music::Grid grid; grid.originBar=firstBar+static_cast<long long>(barIndex); grid.originQuarter=quarter;
                grid.numerator=bar.beats; grid.denominator=bar.unit; grid.quarter=quarter+(complete?static_cast<double>(i)*beatLength:0.);
                const double quarters=complete?beatLength:bar.beats*beatLength;
                // Stop is not an extra beat; the final marked beat may be partial.
                const double fullMillis=bar.closed || i+2<anchors.size()?anchors[i+1].milliseconds-anchors[i].milliseconds:60000.*beatLength/estimate;
                grid.bpm=std::clamp(60000.*quarters/fullMillis,10.,300.);
                result.push_back({anchors[i].seconds,anchors[i+1].seconds,grid});
            }
            quarter+=bar.beats*beatLength;
        }
        return result;
    }
private:
    std::vector<Bar> bars;
    int total=0;
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
