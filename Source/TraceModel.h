#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include "MusicAnalysis.h"
#include <map>
#include <mutex>
struct LeadBar {
    long long number=0;
    double quarter=0,seconds=0,bpm=120;
    int numerator=4,denominator=4;
    std::vector<juce::String> chords,words;
    juce::String lyrics;
    bool lyricsEdited=false,chordsEdited=false;
    unsigned transcriptPass=0;
};
class TraceDocument {
public:
    std::vector<LeadBar> snapshot() const {
        const std::lock_guard<std::mutex> guard(lock); std::vector<LeadBar> result;
        for(const auto& entry:bars) result.push_back(entry.second);
        return result;
    }
    void clear(unsigned next=0) { const std::lock_guard<std::mutex> guard(lock); if(next) epoch=next; bars.clear(); ++revision; }
    void touch(const music::Grid& grid,double seconds,unsigned expected=0) {
        const std::lock_guard<std::mutex> guard(lock); if(expected && expected!=epoch) return; ensure(grid,seconds);
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
    void edit(long long number,const juce::String& chords,const juce::String& lyrics) {
        const std::lock_guard<std::mutex> guard(lock); auto found=bars.find(number); if(found==bars.end()) return;
        auto& bar=found->second; auto tokens=juce::StringArray::fromTokens(chords,"|","");
        for(size_t i=0;i<bar.chords.size();++i) bar.chords[i]=i<static_cast<size_t>(tokens.size())?tokens[static_cast<int>(i)].trim():juce::String{};
        bar.lyrics=lyrics; bar.lyricsEdited=true; bar.chordsEdited=true; ++revision;
    }
    juce::ValueTree save() const {
        auto root=juce::ValueTree("LeadSheet");
        for(const auto& bar:snapshot()) {
            juce::ValueTree node("Bar"); node.setProperty("number",juce::int64(bar.number),nullptr);
            node.setProperty("quarter",bar.quarter,nullptr); node.setProperty("seconds",bar.seconds,nullptr); node.setProperty("bpm",bar.bpm,nullptr);
            node.setProperty("numerator",bar.numerator,nullptr); node.setProperty("denominator",bar.denominator,nullptr);
            node.setProperty("lyrics",bar.lyrics,nullptr); node.setProperty("edited",bar.lyricsEdited,nullptr); node.setProperty("chordsEdited",bar.chordsEdited,nullptr);
            for(size_t i=0;i<bar.chords.size();++i) { node.setProperty("chord"+juce::String(static_cast<int>(i)),bar.chords[i],nullptr); node.setProperty("word"+juce::String(static_cast<int>(i)),bar.words[i],nullptr); }
            root.addChild(node,-1,nullptr);
        }
        return root;
    }
    void restore(const juce::ValueTree& root) {
        const std::lock_guard<std::mutex> guard(lock); bars.clear();
        for(const auto& node:root) {
            if(bars.size()>=2048) break;
            LeadBar bar; bar.number=static_cast<juce::int64>(node.getProperty("number",0));
            auto finite=[](double value,double fallback) { return std::isfinite(value)?value:fallback; };
            bar.quarter=finite(node.getProperty("quarter",0.),0.); bar.seconds=finite(node.getProperty("seconds",0.),0.);
            bar.bpm=std::clamp(finite(node.getProperty("bpm",120.),120.),30.,300.);
            bar.numerator=juce::jlimit(1,12,static_cast<int>(node.getProperty("numerator",4)));
            bar.denominator=static_cast<int>(node.getProperty("denominator",4)); if(bar.denominator!=4 && bar.denominator!=8 && bar.denominator!=16) bar.denominator=4;
            bar.lyrics=node.getProperty("lyrics").toString().substring(0,4096); bar.lyricsEdited=node.getProperty("edited",false); bar.chordsEdited=node.getProperty("chordsEdited",false);
            for(int i=0;i<bar.numerator;++i) { bar.chords.push_back(node.getProperty("chord"+juce::String(i)).toString().substring(0,64)); bar.words.push_back(node.getProperty("word"+juce::String(i)).toString().substring(0,1024)); }
            bars[bar.number]=bar;
        }
        ++revision;
    }
    std::atomic<unsigned> revision{0};
private:
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
};
