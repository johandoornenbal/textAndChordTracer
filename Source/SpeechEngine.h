#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
struct RecognisedWord { double seconds=0; juce::String text; };
class SpeechEngine {
public:
    ~SpeechEngine();
    bool load(const juce::String& path);
    std::vector<RecognisedWord> transcribe(const float* samples,int count,std::atomic<bool>& cancelled,const juce::String& language);
    bool supportsLanguage(const juce::String& language) const;
    bool ready() const { return context!=nullptr; }
private:
    void* context=nullptr;
};
