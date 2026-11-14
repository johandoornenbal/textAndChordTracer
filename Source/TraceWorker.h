#pragma once
#include "TraceModel.h"
#include "SpeechEngine.h"
#include <juce_audio_basics/juce_audio_basics.h>
template<class T,int Capacity> class CaptureQueue {
public:
    bool push(const T& value) {
        const int w=write.load(std::memory_order_relaxed),next=(w+1)%Capacity;
        if(next==read.load(std::memory_order_acquire)) return false;
        items[static_cast<size_t>(w)]=value; write.store(next,std::memory_order_release); return true;
    }
    bool pop(T& value) {
        const auto r=read.load(std::memory_order_relaxed);
        if(r==write.load(std::memory_order_acquire)) return false;
        value=items[static_cast<size_t>(r)]; read.store((r+1)%Capacity,std::memory_order_release); return true;
    }
    bool empty() const { return read.load(std::memory_order_acquire)==write.load(std::memory_order_acquire); }
private:
    std::array<T,static_cast<size_t>(Capacity)> items{};
    std::atomic<int> read{0},write{0};
};
struct AudioCapture {
    std::array<float,512> left{},right{},vocal{};
    int count=0,mode=0;
    double rate=48000,seconds=0,hint=120;
    music::Grid host;
    bool playing=false;
    unsigned generation=0;
};
struct SpeechStamp { int sample=0; double seconds=0; music::Grid grid; };
struct SpeechJob {
    std::array<float,128000> samples{};
    std::array<SpeechStamp,96> stamps{};
    int count=0,stampCount=0,skip=0;
    unsigned generation=0,pass=0;
};
class TraceWorker final : public juce::Thread {
public:
    TraceWorker(); ~TraceWorker() override;
    void newTake() { const auto value=++generation; document.clear(value); detectedBpm=0; detectedNumerator=0; dropped=0; finishRequested=false; }
    void requestModel(const juce::String& path);
    juce::String status() const { const std::lock_guard<std::mutex> guard(statusLock); return message; }
    bool pending() const { return speechBusy.load() || !jobs.empty(); }
    CaptureQueue<AudioCapture,128> audio;
    TraceDocument document;
    std::atomic<unsigned> generation{1},dropped{0};
    std::atomic<double> detectedBpm{0};
    std::atomic<int> detectedNumerator{0},currentBar{0};
    std::atomic<bool> usingHost{false};
    std::atomic<bool> finishRequested{false};
    std::atomic<bool> modelReady{false},speechBusy{false},capturing{false};
private:
    class SpeechThread final : public juce::Thread {
    public: explicit SpeechThread(TraceWorker& w):Thread("Local lyric transcription"),owner(w) {} void run() override;
    private: TraceWorker& owner;
    } speechThread;
    void run() override;
    void setStatus(const juce::String& text) { const std::lock_guard<std::mutex> guard(statusLock); message=text; }
    CaptureQueue<SpeechJob,6> jobs;
    std::atomic<bool> cancelled{false},modelRequested{false};
    mutable std::mutex statusLock,modelLock;
    juce::String message="Choose a Whisper model to enable local lyrics",requestedModel;
};
