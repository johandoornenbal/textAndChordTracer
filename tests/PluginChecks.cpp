#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/TraceModel.h"
#include "../Source/DocumentExport.h"
#include "../Source/ModelLocation.h"
#include "../Source/TapTempo.h"
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
void modelPaths() {
    const auto root=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("tracer-model-paths-"+juce::Uuid().toString());
    struct Cleanup { juce::File folder; ~Cleanup() { folder.deleteRecursively(); } } cleanup{root};
    const auto mac=modelLocation::directoryFor(root.getChildFile("Library"),true);
    check(mac==root.getChildFile("Library/Application Support/Doornenbal Audio/TextAndChordTracer/models"),"Mac model directory does not match installer");
#if JUCE_MAC
    check(modelLocation::directory()==juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile("Library/Application Support/Doornenbal Audio/TextAndChordTracer/models"),"Native Mac default path is wrong");
#endif
    const auto legacy=root.getChildFile("legacy"),custom=root.getChildFile("custom");
    check(mac.createDirectory().wasOk() && legacy.createDirectory().wasOk() && custom.createDirectory().wasOk(),"Cannot create model test folders");
    const auto base=mac.getChildFile("ggml-base.bin"),small=mac.getChildFile("ggml-small.bin"),old=legacy.getChildFile("ggml-base.bin");
    const auto missing=custom.getChildFile("missing.bin"),chosen=custom.getChildFile("chosen.bin");
    check(modelLocation::pickerDirectory(missing,mac)==mac,"Missing model did not open the standard folder");
    check(modelLocation::resolve(missing,mac,legacy)==mac.getChildFile("ggml-base.en.bin"),"Missing model default incorrect");
    check(old.replaceWithText("fixture"),"Cannot write legacy fixture");
    check(modelLocation::resolve(missing,mac,legacy)==old,"Existing legacy model not recovered");
    check(small.replaceWithText("fixture"),"Cannot write small fixture");
    check(modelLocation::resolve(missing,mac,legacy)==small,"Installed small model not discovered");
    check(base.replaceWithText("fixture"),"Cannot write base fixture");
    check(modelLocation::resolve(old.getSiblingFile("absent.bin"),mac,legacy)==base,"Missing saved path did not recover installed base model");
    const auto english=mac.getChildFile("ggml-base.en.bin");
    check(english.replaceWithText("fixture"),"Cannot write English fixture");
    check(modelLocation::resolve(missing,mac,legacy)==english,"English-only default not preferred");
    check(modelLocation::resolve(english,mac,legacy,true)==base,"Dutch selected an English-only model");
    check(chosen.replaceWithText("fixture"),"Cannot write custom fixture");
    check(modelLocation::resolve(chosen,mac,legacy)==chosen && modelLocation::pickerDirectory(chosen,mac)==custom,"Custom model selection was lost");
    const auto signature=modelLocation::signature(base); base.appendText(" changed");
    check(signature!=modelLocation::signature(base),"Model replacement did not change retry signature");
}
void tapCaptureIntegration() {
    struct Transport : juce::AudioPlayHead {
        PositionInfo info;
        juce::Optional<PositionInfo> getPosition() const override { return info; }
    } transport;
    transport.info.setIsPlaying(true);
    std::unique_ptr<juce::AudioProcessor> processor(createPluginFilter()); processor->setPlayHead(&transport); processor->prepareToPlay(48000,512);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor()); auto* panel=editor->getChildComponent(0);
    auto* tap=dynamic_cast<juce::TextButton*>(panel->findChildWithID("tapTempo"));
    auto* use=dynamic_cast<juce::TextButton*>(panel->findChildWithID("stopTapTempo"));
    auto* other=dynamic_cast<juce::TextButton*>(panel->findChildWithID("otherTapTempo"));
    check(tap && use && other,"Tap controls missing");
    juce::AudioBuffer<float> audio(2,512); juce::MidiBuffer midi; double seconds=0;
    auto feed=[&] {
        transport.info.setTimeInSeconds(seconds);
        for(int ch=0;ch<2;++ch) for(int i=0;i<512;++i) audio.setSample(ch,i,.1f);
        processor->processBlock(audio,midi); check(audio.getMagnitude(0,512)==0.f,"Tap mode leaked audio"); seconds+=512./48000.;
    };
    auto bars=[&] {
        juce::MemoryBlock bytes; processor->getStateInformation(bytes);
        auto xml=juce::AudioProcessor::getXmlFromBinary(bytes.getData(),static_cast<int>(bytes.getSize()));
        auto root=juce::ValueTree::fromXml(*xml).getChildWithName("LeadSheet");
        int count=0; for(const auto& node:root) if(node.hasType("Bar")) ++count; return count;
    };
    TapTempo expectedTaps; expectedTaps.tap(juce::Time::getMillisecondCounterHiRes(),4,4); tap->onClick(); feed();
    for(int i=0;i<3;++i) { juce::Thread::sleep(250); expectedTaps.tap(juce::Time::getMillisecondCounterHiRes(),4,4); other->onClick(); feed(); }
    check(bars()==0,"Recording continued while tapping"); use->onClick();
    for(int i=0;i<4;++i) feed(); juce::Thread::sleep(30);
    check(bars()==0,"Recording started before replay");
    auto* bpm=dynamic_cast<juce::RangedAudioParameter*>(processor->getParameters()[2]);
    check(bpm && std::abs(bpm->convertFrom0to1(bpm->getValue())-expectedTaps.quarterBpm())<2.,"Tap estimate did not reach the tempo parameter");
    transport.info.setIsPlaying(false); feed(); transport.info.setIsPlaying(true); seconds=0; feed();
    for(int retry=0;retry<100 && bars()==0;++retry) juce::Thread::sleep(5);
    check(bars()>0,"Recording did not resume after replay");
    processor->releaseResources(); processor->setPlayHead(nullptr);
}
void recordedTimeline() {
    TraceDocument tapped; music::Grid marked; tapped.touch(marked,10.); tapped.edit(0,"C | G | Am | F","One | two | three | four");
    BarTapSession pass; pass.downbeat(0,10,3,4); pass.other(500,10.5); pass.other(1100,11.1); pass.downbeat(1800,11.8,3,4);
    tapped.installTapGuide(pass.guide(0,0,1800,11.8),2);
    auto mapped=tapped.positionAt(10.7); auto kept=tapped.snapshot();
    check(mapped && music::beatNumber(*mapped)==1,"Tapped beat map was not installed");
    check(kept.size()==1 && kept[0].numerator==3 && kept[0].words[2]=="three four" && kept[0].chords[2]=="Am F","Tap remapping lost manual corrections");
    check(std::abs(kept[0].seconds-10.)<.001,"Rubato changed the saved bar start");

    TraceDocument document; music::Grid grid;
    document.touch(grid,10.,0,.125);
    grid.quarter=.25; grid.bpm=90; document.touch(grid,10.125,0,.125);
    auto known=document.positionAt(10.2);
    check(known && std::abs(known->quarter-.3625)<1.e-6,"Recorded local tempo was not used on resume");
    check(!document.positionAt(10.3) && !document.positionAt(15.),"Unvisited timeline position was guessed");
    grid.quarter=grid.originQuarter=8.; grid.originBar=5; grid.numerator=3;
    document.touch(grid,20.,0,.125);
    known=document.positionAt(20.05);
    check(known && music::barNumber(*known)==5 && known->numerator==3,"Resume lost measure anchor or meter");
    TraceDocument restored; restored.restore(document.save());
    known=restored.positionAt(10.2);
    check(known && std::abs(known->quarter-.3625)<1.e-6,"Timeline map was lost on session recall");
    check(!restored.positionAt(15.),"Recall filled an unseen gap");
    TraceDocument legacy; music::Grid old; legacy.touch(old,0.); old.quarter=4.; legacy.touch(old,2.);
    TraceDocument migrated; migrated.restore(legacy.save()); known=migrated.positionAt(1.);
    check(known && std::abs(known->quarter-2.)<1.e-6 && !migrated.positionAt(2.1),"Old bar-anchor migration failed");
    restored.clear(); check(!restored.positionAt(10.2),"New take retained the old timeline map");
    TraceDocument slow; music::Grid slowGrid; slowGrid.bpm=24.; slow.touch(slowGrid,0.,0,.125);
    restored.restore(slow.save()); const auto slowPosition=restored.positionAt(.05);
    check(restored.snapshot()[0].bpm==24. && slowPosition && slowPosition->bpm==24.,"Session recall clamped a slow tempo");
}
int main() try {
    juce::ScopedJuceInitialiser_GUI init; tapCaptureIntegration(); recordedTimeline(); modelPaths(); silentOutput(); captureBeforeMute();
    TraceDocument document; music::Grid grid; grid.bpm=112; grid.numerator=4;
    for(int bar=0;bar<6;++bar) {
        grid.quarter=bar*4.; document.touch(grid,bar*2.);
        document.chord(grid,bar*2.,std::array<const char*,6>{"C","Am","F","G","C","G7"}[static_cast<size_t>(bar)]);
        document.word(grid,bar*2.,"We"); grid.quarter+=1.; document.word(grid,bar*2.+.5,"follow"); grid.quarter+=1.; document.word(grid,bar*2.+1,"the"); grid.quarter+=1.; document.word(grid,bar*2.+1.5,"song");
    }
    check(document.edit(0,"| Abmaj7 | Bbmaj7 |","| My body | is | a"),"Beat edit rejected");
    auto positioned=document.snapshot()[0];
    check(positioned.hasBeatLyrics() && positioned.words[0].isEmpty() && positioned.words[1]=="My body" && positioned.words[2]=="is" && positioned.words[3]=="a","Lyrics did not move to requested beats");
    check(positioned.lyricsForEditor().trim()=="| My body | is | a","Reopened editor lost beat separators");
    check(!document.edit(0,"C | | |","one | two | three | four | five"),"Extra lyric fields were silently discarded");
    check(document.snapshot()[0].words==positioned.words,"Rejected edit changed the lyric placement");
    document.edit(1,"Am | | F |","Let the rhythm find its way");
    music::Grid corrected; corrected.quarter=4.; document.chord(corrected,2.,"Bdim");
    check(document.snapshot()[1].chords[0]=="Am","Analysis overwrote a manual chord correction");
    TraceDocument passes; passes.word(corrected,2.,"one",0,1); passes.word(corrected,2.1,"two",0,1); passes.word(corrected,2.,"duplicate",0,2);
    check(passes.snapshot()[0].lyrics=="one two","Repeated playback duplicated lyrics");
    auto stored=document.save(); TraceDocument recalled; recalled.restore(stored);
    check(recalled.snapshot()[0].words==positioned.words && recalled.snapshot()[0].lyricsBeatAligned,"Beat placement was lost on session recall");
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
    auto documentXml=juce::parseXML(text); auto* body=documentXml->getChildByName("w:body");
    check(body!=nullptr,"Word body missing"); auto* table=body->getChildByName("w:tbl");
    check(table!=nullptr,"Word table missing"); auto* chordRow=table->getChildByName("w:tr");
    auto* lyricRow=chordRow?chordRow->getNextElementWithTagName("w:tr"):nullptr;
    check(lyricRow && lyricRow->getNumChildElements()==4,"Beat-aligned lyric row was merged in Word export");
    const char* expected[]{"","My body","is","a"};
    for(int beat=0;beat<4;++beat) check(lyricRow->getChildElement(beat)->getAllSubText()==expected[beat],"Word export misplaced lyric beat");
    const auto basicFile=file.getSiblingFile("lead-sheet-basic.docx");
    check(documentExport::writeDocx(basicFile,recalled.snapshot(),"Should not be printed",documentExport::Style::basic),"Basic export failed");
    juce::ZipFile basicZip(basicFile);
    std::unique_ptr<juce::InputStream> basicInput(basicZip.createStreamForEntry(basicZip.getIndexOfFileName("word/document.xml")));
    check(basicInput!=nullptr,"Basic Word document missing"); const auto basicText=basicInput->readEntireStreamAsString();
    auto basicXml=juce::parseXML(basicText); check(basicXml!=nullptr,"Basic Word XML invalid");
    check(!basicText.contains("BPM") && !basicText.contains("Bar ") && !basicText.contains("Editable draft") && !basicText.contains("Should not be printed"),"Basic export included metadata");
    auto* basicTable=basicXml->getChildByName("w:body")->getChildByName("w:tbl");
    auto* basicLyrics=basicTable->getChildByName("w:tr")->getNextElementWithTagName("w:tr");
    check(basicLyrics && basicLyrics->getNumChildElements()>=3,"Basic export lost lyrics");
    check(basicLyrics->getChildElement(0)->getAllSubText()=="My body","Basic export retained empty beat spacing");
    LeadBar first; first.numerator=4; first.words={"We","walk","through","the"}; first.chords={"C","","G",""}; first.lyrics="We walk through the";
    LeadBar second=first; second.words={"quiet","night.","",""}; second.chords={"Am","F","",""}; second.lyrics="quiet night.";
    const auto flowing=documentExport::makeDocument({first,second},"",documentExport::Style::basic);
    auto flowXml=juce::parseXML(flowing); auto* flowBody=flowXml->getChildByName("w:body");
    int lines=0; for(auto* child:flowBody->getChildIterator()) if(child->hasTagName("w:tbl")) ++lines;
    check(lines==1 && flowing.contains("quiet") && flowing.contains("Am"),"Basic export did not join a phrase across bars");
    auto image=editor->createComponentSnapshot(editor->getLocalBounds()); auto stream=juce::File::getCurrentWorkingDirectory().getChildFile("ui-preview.png").createOutputStream();
    check(stream!=nullptr,"Cannot write preview"); stream->setPosition(0); stream->truncate(); check(juce::PNGImageFormat().writeImageToStream(image,*stream),"Cannot render editor");
    std::cout<<"Float/double silence, capture before mute, silent bypass callbacks, lead-sheet edits/recall, timing modes, UI scales, DOCX XML and actual editor preview passed\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
