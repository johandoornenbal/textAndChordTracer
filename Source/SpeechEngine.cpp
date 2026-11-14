#include "SpeechEngine.h"
#if TRACER_WITH_WHISPER
#include <whisper.h>
#endif
SpeechEngine::~SpeechEngine() {
#if TRACER_WITH_WHISPER
    if(context) whisper_free(static_cast<whisper_context*>(context));
#endif
}
bool SpeechEngine::load(const juce::String& path) {
#if TRACER_WITH_WHISPER
    if(context) { whisper_free(static_cast<whisper_context*>(context)); context=nullptr; }
    if(!juce::File(path).existsAsFile()) return false;
    auto params=whisper_context_default_params();
    context=whisper_init_from_file_with_params(path.toRawUTF8(),params);
    return context!=nullptr;
#else
    juce::ignoreUnused(path); return false;
#endif
}
std::vector<RecognisedWord> SpeechEngine::transcribe(const float* samples,int count,std::atomic<bool>& cancelled,const juce::String& language) {
    std::vector<RecognisedWord> words;
#if TRACER_WITH_WHISPER
    if(!context || count<16000) return words;
    auto* ctx=static_cast<whisper_context*>(context);
    auto params=whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.n_threads=juce::jlimit(1,4,juce::SystemStats::getNumCpus()/2);
    params.translate=false; params.no_context=true; params.print_realtime=false;
    params.print_progress=false; params.print_timestamps=false; params.print_special=false;
    params.language=language.toRawUTF8(); params.token_timestamps=true;
    params.abort_callback=[](void* data) { return static_cast<std::atomic<bool>*>(data)->load(); };
    params.abort_callback_user_data=&cancelled;
    if(whisper_full(ctx,params,samples,count)!=0 || cancelled.load()) return words;
    for(int segment=0;segment<whisper_full_n_segments(ctx);++segment) {
        if(whisper_full_get_segment_no_speech_prob(ctx,segment)>.65f) continue;
        const auto text=juce::String::fromUTF8(whisper_full_get_segment_text(ctx,segment)).trim();
        if(text.startsWithChar('[') || text.startsWithChar('(')) continue;
        auto tokens=juce::StringArray::fromTokens(text," \t\r\n",""); tokens.removeEmptyStrings();
        const double start=whisper_full_get_segment_t0(ctx,segment)*.01,end=whisper_full_get_segment_t1(ctx,segment)*.01;
        for(int i=0;i<tokens.size();++i) words.push_back({start+(end-start)*(i+.5)/juce::jmax(1,tokens.size()),tokens[i]});
    }
#else
    juce::ignoreUnused(samples,count,cancelled,language);
#endif
    return words;
}
