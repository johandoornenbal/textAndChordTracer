#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/SpeechEngine.h"
#include "../Source/TraceWorker.h"
#include "../Source/ModelLocation.h"
#include <iostream>
int main(int argc,char** argv) {
    const auto explicitModel=juce::SystemStats::getEnvironmentVariable("TRACER_TEST_MODEL","");
    const auto model=explicitModel.isNotEmpty()?explicitModel:modelLocation::find().getFullPathName();
    if(argc<2) { std::cerr<<"Speech fixture is not configured\n"; return 1; }
    if(explicitModel.isEmpty() && !juce::File(model).existsAsFile()) {
        std::cout<<"No speech model installed at "<<model<<". Install one or set TRACER_TEST_MODEL.\n"; return 77;
    }
    SpeechEngine engine; if(!engine.load(model)) { std::cerr<<"Model failed to load\n"; return 1; }
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(juce::File(argv[1])));
    if(!reader || reader->sampleRate!=16000) return 1;
    juce::AudioBuffer<float> audio(1,static_cast<int>(reader->lengthInSamples)); reader->read(&audio,0,audio.getNumSamples(),0,true,false);
    std::atomic<bool> cancelled{false}; const auto words=engine.transcribe(audio.getReadPointer(0),audio.getNumSamples(),cancelled,"en");
    juce::String text; for(const auto& word:words) text+=word.text+" ";
    std::cout<<text<<'\n'; if(!text.containsIgnoreCase("Americans")) return 1;
    auto worker=std::make_unique<TraceWorker>(); worker->requestModel(model);
    for(int retry=0;retry<1200 && !worker->modelReady.load();++retry) juce::Thread::sleep(25);
    if(!worker->modelReady.load()) { std::cerr<<worker->status()<<'\n'; return 1; }
    for(int offset=0;offset<audio.getNumSamples();offset+=512) {
        AudioCapture capture; capture.playing=true; capture.mode=1; capture.rate=16000; capture.seconds=offset/16000.; capture.generation=worker->generation.load(); capture.count=std::min(512,audio.getNumSamples()-offset);
        for(int i=0;i<capture.count;++i) capture.left[static_cast<size_t>(i)]=capture.right[static_cast<size_t>(i)]=capture.vocal[static_cast<size_t>(i)]=audio.getSample(0,offset+i);
        int tries=0; while(!worker->audio.push(capture)) { juce::Thread::sleep(5); if(++tries>12000) return 1; }
    }
    AudioCapture stop; stop.generation=worker->generation.load(); while(!worker->audio.push(stop)) juce::Thread::sleep(5);
    juce::String transcript;
    for(int retry=0;retry<2400;++retry) {
        transcript.clear(); for(const auto& bar:worker->document.snapshot()) transcript+=bar.lyrics+" ";
        if(transcript.containsIgnoreCase("Americans")) break;
        juce::Thread::sleep(25);
    }
    std::cout<<"Actual capture/ASR/lead-sheet pipeline: "<<transcript<<'\n';
    return transcript.containsIgnoreCase("Americans")?0:1;
}
