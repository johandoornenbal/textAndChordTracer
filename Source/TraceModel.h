#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include "MusicAnalysis.h"
#include <map>
#include <mutex>
#include <optional>
struct LeadBar {
    long long number=0;
    double quarter=0,seconds=0,bpm=120;
    int numerator=4,denominator=4;
    std::vector<juce::String> chords,words;
    juce::String lyrics;
    bool lyricsEdited=false,chordsEdited=false,lyricsBeatAligned=false;
    unsigned transcriptPass=0;
    bool hasBeatLyrics() const { return !lyricsEdited || lyricsBeatAligned; }
    juce::String lyricsForEditor() const {
        if(!hasBeatLyrics()) return lyrics;
        juce::StringArray cells; for(const auto& cell:words) cells.add(cell);
        return cells.joinIntoString(" | ");
    }
};
class TraceDocument {
public:
    std::vector<LeadBar> snapshot() const {
        const std::lock_guard<std::mutex> guard(lock); std::vector<LeadBar> result;
        for(const auto& entry:bars) result.push_back(entry.second);
        return result;
    }
    void clear(unsigned next=0) { const std::lock_guard<std::mutex> guard(lock); if(next) epoch=next; bars.clear(); positions.clear(); ++revision; }
    void touch(const music::Grid& grid,double seconds,unsigned expected=0,double duration=0) {
        const std::lock_guard<std::mutex> guard(lock); if(expected && expected!=epoch) return; ensure(grid,seconds); recordPosition(grid,seconds,duration);
    }
    void chord(const music::Grid& grid,double seconds,const juce::String& name,unsigned expected=0) {
        const std::lock_guard<std::mutex> guard(lock); if(expected && expected!=epoch) return; auto* bar=ensure(grid,seconds); if(!bar || bar->chordsEdited) return;
        const auto beat=std::min(bar->chords.size()-1,static_cast<size_t>(music::beatNumber(grid)));
        if(beat>0 && bar->chords[beat-1]==name && bar->chords[beat].isEmpty()) return;
        if(bar->chords[beat]!=name) { bar->chords[beat]=name; ++revision; }
    }
    void word(const music::Grid& grid,double seconds,const juce::String& text,unsigned expected=0,unsigned pass=0) {
        const std::lock_guard<std::mutex> guard(lock); if(expected && expected!=epoch) return; auto* bar=ensure(grid,seconds); if(!bar || bar->lyricsEdited) return;
        if(pass && bar->lyrics.isNotEmpty() && bar->transcriptPass!=pass) return;
        if(pass) bar->transcriptPass=pass;
        const auto beat=std::min(bar->chords.size()-1,static_cast<size_t>(music::beatNumber(grid)));
        bar->words[beat]+=(bar->words[beat].isEmpty()?"":" ")+text;
        bar->lyrics+=(bar->lyrics.isEmpty()?"":" ")+text; ++revision;
    }
    bool edit(long long number,const juce::String& chords,const juce::String& lyrics) {
        const std::lock_guard<std::mutex> guard(lock); auto found=bars.find(number); if(found==bars.end()) return false;
        auto& bar=found->second; auto tokens=juce::StringArray::fromTokens(chords,"|","");
        auto lyricCells=juce::StringArray::fromTokens(lyrics,"|","");
        if(tokens.size()>bar.numerator || lyricCells.size()>bar.numerator) return false;
        for(size_t i=0;i<bar.chords.size();++i) bar.chords[i]=i<static_cast<size_t>(tokens.size())?tokens[static_cast<int>(i)].trim():juce::String{};
        bar.lyricsBeatAligned=lyrics.containsChar('|');
        bar.words.assign(bar.chords.size(),juce::String{});
        if(bar.lyricsBeatAligned) {
            juce::StringArray phrase;
            for(int i=0;i<lyricCells.size();++i) {
                const auto text=lyricCells[i].trim(); bar.words[static_cast<size_t>(i)]=text;
                if(text.isNotEmpty()) phrase.add(text);
            }
            bar.lyrics=phrase.joinIntoString(" ");
        } else bar.lyrics=lyrics;
        bar.lyricsEdited=true; bar.chordsEdited=true; ++revision;
        return true;
    }
    std::optional<music::Grid> positionAt(double seconds) const {
        const std::lock_guard<std::mutex> guard(lock);
        auto next=positions.upper_bound(seconds+1.e-7);
        if(next==positions.begin()) return std::nullopt;
        auto current=std::prev(next);
        if(seconds<current->first-1.e-7 || seconds>current->second.end+1.e-7) return std::nullopt;
        auto grid=current->second.grid;
        const double delta=std::max(0.,seconds-current->first);
        if(next!=positions.end() && next->first<=current->second.end+1.e-5 && next->first>current->first) {
            const double fraction=std::clamp(delta/(next->first-current->first),0.,1.);
            grid.quarter+=(next->second.grid.quarter-grid.quarter)*fraction;
            grid.bpm+=(next->second.grid.bpm-grid.bpm)*fraction;
        } else grid.quarter+=delta*grid.bpm/60.;
        return grid;
    }
    music::Grid continuation() const {
        const std::lock_guard<std::mutex> guard(lock); music::Grid grid;
        if(!bars.empty()) {
            const auto& last=bars.rbegin()->second;
            grid.bpm=last.bpm; grid.numerator=last.numerator; grid.denominator=last.denominator;
            grid.originBar=last.number; grid.originQuarter=last.quarter;
            grid.quarter=last.quarter+grid.barLength();
        }
        return grid;
    }
    juce::ValueTree save() const {
        auto root=juce::ValueTree("LeadSheet");
        std::vector<LeadBar> savedBars; std::map<double,RecordedPosition> savedPositions;
        { const std::lock_guard<std::mutex> guard(lock); for(const auto& entry:bars) savedBars.push_back(entry.second); savedPositions=positions; }
        for(const auto& bar:savedBars) {
            juce::ValueTree node("Bar"); node.setProperty("number",juce::int64(bar.number),nullptr);
            node.setProperty("quarter",bar.quarter,nullptr); node.setProperty("seconds",bar.seconds,nullptr); node.setProperty("bpm",bar.bpm,nullptr);
            node.setProperty("numerator",bar.numerator,nullptr); node.setProperty("denominator",bar.denominator,nullptr);
            node.setProperty("lyrics",bar.lyrics,nullptr); node.setProperty("edited",bar.lyricsEdited,nullptr); node.setProperty("chordsEdited",bar.chordsEdited,nullptr); node.setProperty("lyricBeats",bar.lyricsBeatAligned,nullptr);
            for(size_t i=0;i<bar.chords.size();++i) { node.setProperty("chord"+juce::String(static_cast<int>(i)),bar.chords[i],nullptr); node.setProperty("word"+juce::String(static_cast<int>(i)),bar.words[i],nullptr); }
            root.addChild(node,-1,nullptr);
        }
        juce::ValueTree timeline("Positions");
        for(const auto& entry:savedPositions) {
            const auto& grid=entry.second.grid; juce::ValueTree point("Point");
            point.setProperty("seconds",entry.first,nullptr); point.setProperty("end",entry.second.end,nullptr);
            point.setProperty("quarter",grid.quarter,nullptr); point.setProperty("bpm",grid.bpm,nullptr);
            point.setProperty("numerator",grid.numerator,nullptr); point.setProperty("denominator",grid.denominator,nullptr);
            point.setProperty("originQuarter",grid.originQuarter,nullptr); point.setProperty("originBar",juce::int64(grid.originBar),nullptr);
            timeline.addChild(point,-1,nullptr);
        }
        root.addChild(timeline,-1,nullptr);
        return root;
    }
    void restore(const juce::ValueTree& root) {
        const std::lock_guard<std::mutex> guard(lock); bars.clear(); positions.clear();
        for(const auto& node:root) {
            if(!node.hasType("Bar")) continue;
            if(bars.size()>=2048) break;
            LeadBar bar; bar.number=static_cast<juce::int64>(node.getProperty("number",0));
            auto finite=[](double value,double fallback) { return std::isfinite(value)?value:fallback; };
            bar.quarter=finite(node.getProperty("quarter",0.),0.); bar.seconds=finite(node.getProperty("seconds",0.),0.);
            bar.bpm=std::clamp(finite(node.getProperty("bpm",120.),120.),30.,300.);
            bar.numerator=juce::jlimit(1,12,static_cast<int>(node.getProperty("numerator",4)));
            bar.denominator=static_cast<int>(node.getProperty("denominator",4)); if(bar.denominator!=4 && bar.denominator!=8 && bar.denominator!=16) bar.denominator=4;
            bar.lyrics=node.getProperty("lyrics").toString().substring(0,4096); bar.lyricsEdited=node.getProperty("edited",false); bar.chordsEdited=node.getProperty("chordsEdited",false); bar.lyricsBeatAligned=node.getProperty("lyricBeats",false);
            for(int i=0;i<bar.numerator;++i) { bar.chords.push_back(node.getProperty("chord"+juce::String(i)).toString().substring(0,64)); bar.words.push_back(node.getProperty("word"+juce::String(i)).toString().substring(0,1024)); }
            bars[bar.number]=bar;
        }
        for(const auto& point:root.getChildWithName("Positions")) {
            if(positions.size()>=36000) break;
            const double seconds=point.getProperty("seconds",0.),end=point.getProperty("end",0.);
            music::Grid grid; grid.quarter=point.getProperty("quarter",0.); grid.bpm=point.getProperty("bpm",120.);
            grid.numerator=juce::jlimit(1,12,static_cast<int>(point.getProperty("numerator",4)));
            grid.denominator=static_cast<int>(point.getProperty("denominator",4));
            grid.originQuarter=point.getProperty("originQuarter",0.); grid.originBar=static_cast<juce::int64>(point.getProperty("originBar",0));
            if(!std::isfinite(seconds) || !std::isfinite(end) || end<=seconds || end-seconds>120. || !std::isfinite(grid.quarter) || !std::isfinite(grid.originQuarter) || !std::isfinite(grid.bpm) || grid.bpm<30. || grid.bpm>300.) continue;
            if(grid.denominator!=1 && grid.denominator!=2 && grid.denominator!=4 && grid.denominator!=8 && grid.denominator!=16 && grid.denominator!=32) continue;
            positions[seconds]={end,grid};
        }
        // Older sessions have only bar anchors. Interpolate between adjacent
        // known bars, without claiming that unseen gaps or the last full bar played.
        if(positions.empty()) for(auto entry=bars.begin();entry!=bars.end();++entry) {
            const auto& bar=entry->second; music::Grid grid;
            grid.quarter=grid.originQuarter=bar.quarter; grid.originBar=bar.number;
            grid.bpm=bar.bpm; grid.numerator=bar.numerator; grid.denominator=bar.denominator;
            auto next=std::next(entry);
            double end=bar.seconds+1.e-6;
            if(next!=bars.end() && next->first==entry->first+1 && next->second.seconds>bar.seconds) end=next->second.seconds;
            positions[bar.seconds]={end,grid};
        }
        ++revision;
    }
    std::atomic<unsigned> revision{0};
private:
    struct RecordedPosition { double end=0; music::Grid grid; };
    void recordPosition(const music::Grid& grid,double seconds,double duration) {
        if(duration<=0 || !std::isfinite(seconds)) return;
        auto upper=positions.upper_bound(seconds);
        if(upper!=positions.begin()) {
            auto previous=std::prev(upper);
            if(seconds>=previous->first && seconds-previous->first<.1 && seconds<=previous->second.end+1.e-6
                && grid.numerator==previous->second.grid.numerator && grid.denominator==previous->second.grid.denominator) {
                previous->second.end=std::max(previous->second.end,seconds+duration); return;
            }
        }
        if(positions.size()>=36000) return;
        positions[seconds]={seconds+duration,grid}; ++revision;
    }
    LeadBar* ensure(const music::Grid& grid,double seconds) {
        const auto number=music::barNumber(grid); auto found=bars.find(number); if(found!=bars.end()) return &found->second;
        if(bars.size()>=2048) return nullptr;
        LeadBar bar; bar.number=number; bar.quarter=grid.originQuarter+(number-grid.originBar)*grid.barLength();
        bar.seconds=seconds-(grid.quarter-bar.quarter)*60/grid.bpm; bar.bpm=grid.bpm;
        bar.numerator=grid.numerator; bar.denominator=grid.denominator;
        bar.chords.resize(static_cast<size_t>(grid.numerator)); bar.words.resize(bar.chords.size());
        ++revision; return &bars.emplace(number,std::move(bar)).first->second;
    }
    unsigned epoch=1;
    mutable std::mutex lock;
    std::map<long long,LeadBar> bars;
    std::map<double,RecordedPosition> positions;
};
