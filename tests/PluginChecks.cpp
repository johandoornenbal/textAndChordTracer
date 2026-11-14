#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/TraceModel.h"
#include "../Source/DocumentExport.h"
#include <iostream>
#include <stdexcept>
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
void silentOutput() {
    for(bool doubles:{false,true}) for(bool vocal:{false,true}) {
        std::unique_ptr<juce::AudioProcessor> p(createPluginFilter());
        if(vocal) { auto buses=p->getBusesLayout(); buses.inputBuses.set(1,juce::AudioChannelSet::stereo()); check(p->setBusesLayout(buses),"Optional vocal bus unavailable"); }
        const int channels=vocal?4:2;
        p->setProcessingPrecision(doubles?juce::AudioProcessor::doublePrecision:juce::AudioProcessor::singlePrecision);
        p->prepareToPlay(48000,1025); juce::MidiBuffer midi;
        auto run=[&](auto type) {
            using T=decltype(type);
            for(int count:{0,1,257,1025}) {
                juce::AudioBuffer<T> audio(channels,count);
                for(int ch=0;ch<channels;++ch) for(int i=0;i<count;++i) audio.setSample(ch,i,static_cast<T>(.2*std::sin(i*.1+ch)));
                juce::AudioBuffer<T> before; before.makeCopyOf(audio);
                p->getParameters()[0]->setValueNotifyingHost(count==257?0.f:1.f);
                p->processBlock(audio,midi);
                for(int ch=0;ch<channels;++ch) for(int i=0;i<count;++i) check(ch<2?audio.getSample(ch,i)==T{}:audio.getSample(ch,i)==before.getSample(ch,i),"Output leaked or vocal input was overwritten");
                audio.makeCopyOf(before);
                p->processBlockBypassed(audio,midi);
                for(int ch=0;ch<channels;++ch) for(int i=0;i<count;++i) check(ch<2?audio.getSample(ch,i)==T{}:audio.getSample(ch,i)==before.getSample(ch,i),"Bypass output leaked or vocal input was overwritten");
            }
        };
        if(doubles) run(double{}); else run(float{});
        check(p->getLatencySamples()==0,"Tracer added latency"); p->releaseResources();
    }
}
void captureBeforeMute() {
    std::unique_ptr<juce::AudioProcessor> processor(createPluginFilter()); processor->prepareToPlay(48000,512);
    juce::AudioBuffer<float> audio(2,512); juce::MidiBuffer midi;
    for(int block=0;block<180;++block) {
        for(int i=0;i<512;++i) {
            double value=0; for(int note:{48,52,55}) value+=.12*std::sin(2*music::pi*440*std::pow(2.,(note-69)/12.)*(block*512+i)/48000.);
            audio.setSample(0,i,static_cast<float>(value)); audio.setSample(1,i,static_cast<float>(-value));
        }
        processor->processBlock(audio,midi); check(audio.getMagnitude(0,512)==0.f,"Main output leaked analysis audio");
        if(block%8==0) juce::Thread::sleep(2);
    }
    processor->releaseResources();
    bool found=false;
    for(int retry=0;retry<300 && !found;++retry) {
        juce::MemoryBlock state; processor->getStateInformation(state);
        auto xml=juce::AudioProcessor::getXmlFromBinary(state.getData(),static_cast<int>(state.getSize()));
        if(xml) {
            auto bars=juce::ValueTree::fromXml(*xml).getChildWithName("LeadSheet");
            for(const auto& bar:bars) for(int beat=0;beat<4;++beat) found=found || bar.getProperty("chord"+juce::String(beat)).toString()=="C";
        }
        if(!found) juce::Thread::sleep(5);
    }
    check(found,"Silencing happened before the chord analysis tap");
}
int main() try {
    juce::ScopedJuceInitialiser_GUI init; silentOutput(); captureBeforeMute();
    TraceDocument document; music::Grid grid; grid.bpm=112; grid.numerator=4;
    for(int bar=0;bar<6;++bar) {
        grid.quarter=bar*4.; document.touch(grid,bar*2.);
        document.chord(grid,bar*2.,std::array<const char*,6>{"C","Am","F","G","C","G7"}[static_cast<size_t>(bar)]);
        document.word(grid,bar*2.,"We"); grid.quarter+=1.; document.word(grid,bar*2.+.5,"follow"); grid.quarter+=1.; document.word(grid,bar*2.+1,"the"); grid.quarter+=1.; document.word(grid,bar*2.+1.5,"song");
    }
    document.edit(1,"Am | | F |","Let the rhythm find its way");
    music::Grid corrected; corrected.quarter=4.; document.chord(corrected,2.,"Bdim");
    check(document.snapshot()[1].chords[0]=="Am","Analysis overwrote a manual chord correction");
    TraceDocument passes; passes.word(corrected,2.,"one",0,1); passes.word(corrected,2.1,"two",0,1); passes.word(corrected,2.,"duplicate",0,2);
    check(passes.snapshot()[0].lyrics=="one two","Repeated playback duplicated lyrics");
    auto stored=document.save(); TraceDocument recalled; recalled.restore(stored);
    check(recalled.snapshot().size()==6 && recalled.snapshot()[1].lyrics=="Let the rhythm find its way","Lead-sheet recall failed");
    std::unique_ptr<juce::AudioProcessor> p(createPluginFilter());
    juce::MemoryBlock bytes; p->getStateInformation(bytes);
    auto xml=juce::AudioProcessor::getXmlFromBinary(bytes.getData(),static_cast<int>(bytes.getSize()));
    auto state=juce::ValueTree::fromXml(*xml); state.removeChild(state.getChildWithName("LeadSheet"),nullptr); state.addChild(stored,-1,nullptr);
    state.setProperty("title","A song that follows you",nullptr);
    auto stateXml=state.createXml(); juce::AudioProcessor::copyXmlToBinary(*stateXml,bytes); p->setStateInformation(bytes.getData(),static_cast<int>(bytes.getSize()));
    std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor()); auto* panel=editor->getChildComponent(0);
    auto* sizes=dynamic_cast<juce::ComboBox*>(panel->findChildWithID("uiSize")); check(sizes!=nullptr,"UI sizing missing");
    auto* mode=dynamic_cast<juce::ComboBox*>(panel->findChildWithID("timingMode")); check(mode && mode->getNumItems()==3,"Detection/hint/DAW modes missing");
    for(int id:{1,3,2}) {
        sizes->setSelectedId(id,juce::sendNotificationSync); const double scale=id==1?.75:id==3?1.25:1.;
        check(editor->getWidth()==juce::roundToInt(900*scale) && editor->getHeight()==juce::roundToInt(830*scale),"UI scale incorrect");
        for(auto* child:panel->getChildren()) check(panel->getLocalBounds().contains(child->getBounds()),"Control outside panel");
    }
    const auto file=juce::File::getCurrentWorkingDirectory().getChildFile("lead-sheet.docx");
    check(documentExport::writeDocx(file,recalled.snapshot(),"A song <&> lyrics"),"DOCX export failed");
    juce::ZipFile zip(file); check(zip.getNumEntries()==3,"DOCX package incomplete");
    std::unique_ptr<juce::InputStream> input(zip.createStreamForEntry(zip.getIndexOfFileName("word/document.xml")));
    check(input!=nullptr,"Word document missing"); auto text=input->readEntireStreamAsString();
    check(juce::parseXML(text)!=nullptr && text.contains("Let the rhythm") && text.contains("&lt;&amp;&gt;"),"DOCX content or XML escaping failed");
    auto image=editor->createComponentSnapshot(editor->getLocalBounds()); auto stream=juce::File::getCurrentWorkingDirectory().getChildFile("ui-preview.png").createOutputStream();
    check(stream!=nullptr,"Cannot write preview"); stream->setPosition(0); stream->truncate(); check(juce::PNGImageFormat().writeImageToStream(image,*stream),"Cannot render editor");
    std::cout<<"Float/double silence, capture before mute, silent bypass callbacks, lead-sheet edits/recall, timing modes, UI scales, DOCX XML and actual editor preview passed\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
