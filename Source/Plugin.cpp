#include <JuceHeader.h>
#include "TraceWorker.h"
#include "DocumentExport.h"
#include "ModelLocation.h"
#include "TapTempo.h"
class TracerProcessor final : public juce::AudioProcessor,private juce::Timer {
public:
    TracerProcessor():AudioProcessor(BusesProperties().withInput("Music",juce::AudioChannelSet::stereo(),true)
        .withInput("Clean vocal (optional)",juce::AudioChannelSet::stereo(),false).withOutput("Muted output",juce::AudioChannelSet::stereo(),true)),state(*this,nullptr,"TextAndChordTracer",layout()) {
        enabled=state.getRawParameterValue("trace"); mode=state.getRawParameterValue("timingMode");
        bpm=state.getRawParameterValue("bpmHint"); numerator=state.getRawParameterValue("beatsHint"); denominator=state.getRawParameterValue("unitHint"); language=state.getRawParameterValue("language");
        englishModelPath=modelLocation::find().getFullPathName(); dutchModelPath=modelLocation::find({},true).getFullPathName();
        modelPath=englishModelPath;
        if(juce::File(modelPath).existsAsFile()) chooseModel(juce::File(modelPath));
        startTimerHz(2);
    }
    ~TracerProcessor() override { stopTimer(); }
    static juce::File defaultModel() {
        return modelLocation::directory().getChildFile("ggml-base.en.bin");
    }
    static juce::AudioProcessorValueTreeState::ParameterLayout layout() {
        juce::AudioProcessorValueTreeState::ParameterLayout p;
        p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"trace",1},"Trace playback",true));
        p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"timingMode",1},"Timing mode",juce::StringArray{"Detect","Hint + follow","DAW"},0));
        p.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"bpmHint",1},"Approximate tempo",juce::NormalisableRange<float>(10,300,.1f),120));
        p.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{"beatsHint",1},"Beats per bar hint",2,12,4));
        p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"unitHint",1},"Beat unit hint",juce::StringArray{"4","8","16"},0));
        p.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"language",1},"Lyric language",juce::StringArray{"English","Dutch"},0));
        return p;
    }
    bool isBusesLayoutSupported(const BusesLayout& layout) const override {
        return layout.inputBuses.size()==2 && layout.outputBuses.size()==1 && layout.getMainInputChannelSet()==juce::AudioChannelSet::stereo() && layout.getMainOutputChannelSet()==juce::AudioChannelSet::stereo() && (layout.inputBuses[1].isDisabled() || layout.inputBuses[1]==juce::AudioChannelSet::stereo());
    }
    void prepareToPlay(double sampleRate,int maximumBlockSize) override { rate=sampleRate; fallbackSeconds=0; tapGate.resetTransport(); callbackTimeout=juce::jmax(.5,2.*maximumBlockSize/sampleRate); }
    void releaseResources() override { stopCapture(); }
    void stopCapture() { worker.finishRequested=true; }
    template<class T> void capture(juce::AudioBuffer<T>& buffer) {
        lastCallback.store(juce::Time::getMillisecondCounter(),std::memory_order_relaxed);
        auto main=getBusBuffer(buffer,true,0); const bool vocalEnabled=getBus(true,1)->isEnabled(); auto vocal=getBusBuffer(buffer,true,1);
        music::Grid grid; grid.bpm=bpm->load(); grid.numerator=juce::roundToInt(numerator->load()); grid.denominator=4<<juce::roundToInt(denominator->load());
        bool playing=true,hostClock=false; double seconds=fallbackSeconds;
        if(auto* head=getPlayHead()) if(auto position=head->getPosition()) {
            playing=position->getIsPlaying();
            if(auto value=position->getTimeInSeconds()) { seconds=*value; hostClock=true; } else if(auto samples=position->getTimeInSamples()) { seconds=*samples/rate; hostClock=true; }
            if(auto value=position->getBpm()) if(std::isfinite(*value) && *value>0) grid.bpm=*value;
            if(auto value=position->getTimeSignature()) { grid.numerator=juce::jlimit(1,12,value->numerator); grid.denominator=value->denominator>0?value->denominator:4; }
            if(auto value=position->getPpqPosition()) { grid.quarter=*value; grid.fromHost=true; }
            if(auto start=position->getPpqPositionOfLastBarStart()) if(auto bar=position->getBarCount()) { grid.originQuarter=*start; grid.originBar=*bar; }
        }
        const int timing=juce::roundToInt(mode->load());
        if(timing!=2) { grid.numerator=juce::roundToInt(numerator->load()); grid.denominator=4<<juce::roundToInt(denominator->load()); }
        transportSequence.fetch_add(1,std::memory_order_acq_rel);
        transportSeconds.store(seconds,std::memory_order_relaxed); transportMillis.store(juce::Time::getMillisecondCounterHiRes(),std::memory_order_relaxed);
        transportPlaying.store(playing,std::memory_order_relaxed); transportValid.store(hostClock,std::memory_order_relaxed);
        transportSequence.fetch_add(1,std::memory_order_release);
        playing=tapGate.permit(playing,seconds,main.getNumSamples()/rate) && enabled->load()>.5f;
        // Copy analysis inputs first; output is silenced only after capture completes.
        for(int offset=0;offset<main.getNumSamples();offset+=512) {
            AudioCapture packet; packet.count=juce::jmin(512,main.getNumSamples()-offset); packet.rate=rate;
            packet.playing=playing; packet.generation=worker.generation.load(); packet.mode=timing; packet.hint=bpm->load();
            packet.language=juce::roundToInt(language->load()); packet.hintRevision=hintRevision.load(); packet.seconds=seconds+offset/rate; packet.host=grid; packet.host.quarter+=offset/rate*grid.bpm/60;
            if(playing) for(int i=0;i<packet.count;++i) {
                auto index=static_cast<size_t>(i); auto l=main.getSample(0,offset+i),r=main.getSample(1,offset+i);
                packet.left[index]=static_cast<float>(l); packet.right[index]=static_cast<float>(r);
                packet.vocal[index]=static_cast<float>(vocalEnabled?(vocal.getSample(0,offset+i)+vocal.getSample(1,offset+i))*.5:(l+r)*.5);
            }
            if(!worker.audio.push(packet)) ++worker.dropped;
        }
        fallbackSeconds=seconds+main.getNumSamples()/rate;
        auto output=getBusBuffer(buffer,false,0); output.clear(); // Mute every output, preserving read-only sidechain inputs.
    }
    void processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&) override { capture(b); }
    void processBlock(juce::AudioBuffer<double>& b,juce::MidiBuffer&) override { capture(b); }
    void processBlockBypassed(juce::AudioBuffer<float>& b,juce::MidiBuffer&) override { stopCapture(); auto output=getBusBuffer(b,false,0); output.clear(); }
    void processBlockBypassed(juce::AudioBuffer<double>& b,juce::MidiBuffer&) override { stopCapture(); auto output=getBusBuffer(b,false,0); output.clear(); }
    bool supportsDoublePrecisionProcessing() const override { return true; }
    const juce::String getName() const override { return "Text and Chord Tracer"; }
    bool acceptsMidi() const override { return false; } bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; } int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {} const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int,const juce::String&) override {}
    bool hasEditor() const override { return true; } juce::AudioProcessorEditor* createEditor() override;
    void getStateInformation(juce::MemoryBlock& destination) override {
        auto saved=state.copyState(); saved.setProperty("uiSize",uiSize,nullptr); saved.setProperty("title",title,nullptr); saved.setProperty("modelPath",modelPath,nullptr); saved.setProperty("englishModelPath",englishModelPath,nullptr); saved.setProperty("dutchModelPath",dutchModelPath,nullptr);
        saved.addChild(worker.document.save(),-1,nullptr); if(auto xml=saved.createXml()) copyXmlToBinary(*xml,destination);
    }
    void setStateInformation(const void* data,int length) override {
        if(auto xml=getXmlFromBinary(data,length);xml && xml->hasTagName("TextAndChordTracer")) {
            auto saved=juce::ValueTree::fromXml(*xml); tapGate.recordNow(); tapTempo.reset(); worker.newTake(); worker.document.restore(saved.getChildWithName("LeadSheet"));
            uiSize=juce::jlimit(1,3,static_cast<int>(saved.getProperty("uiSize",2))); title=saved.getProperty("title","Untitled song").toString().substring(0,200);
            const auto oldPath=saved.getProperty("modelPath",defaultModel().getFullPathName()).toString();
            englishModelPath=saved.getProperty("englishModelPath",oldPath==modelLocation::directory().getChildFile("ggml-base.bin").getFullPathName()?modelLocation::find().getFullPathName():oldPath).toString();
            dutchModelPath=saved.getProperty("dutchModelPath",modelLocation::find({},true).getFullPathName()).toString();
            saved.removeChild(saved.getChildWithName("LeadSheet"),nullptr);
            bool hasLanguage=false; for(const auto& node:saved) if(node.getProperty("id").toString()=="language") hasLanguage=true;
            if(!hasLanguage) { juce::ValueTree value("PARAM"); value.setProperty("id","language",nullptr); value.setProperty("value",0.f,nullptr); saved.addChild(value,-1,nullptr); }
            state.replaceState(saved);
            loadedLanguage=-1; syncLanguage();
        }
    }
    double projectTime(double milliseconds) const {
        for(int attempt=0;attempt<4;++attempt) {
            const auto before=transportSequence.load(std::memory_order_acquire); if(before%2) continue;
            const auto seconds=transportSeconds.load(std::memory_order_relaxed),stamp=transportMillis.load(std::memory_order_relaxed);
            const bool valid=transportValid.load(std::memory_order_relaxed) && transportPlaying.load(std::memory_order_relaxed);
            if(before!=transportSequence.load(std::memory_order_acquire)) continue;
            const double age=(milliseconds-stamp)*.001;
            if(valid && age>=0 && age<callbackTimeout) return seconds+age;
        }
        return std::numeric_limits<double>::quiet_NaN();
    }
    void tap(double milliseconds,bool downbeat) {
        if(!tapGate.isTapping()) {
            if(!downbeat) { tapFeedback="Begin with Bar start, then mark the other beats"; return; }
            tapTempo.reset(); tapGate.begin(); worker.finishRequested=true; tapFeedback.clear();
        }
        const auto seconds=projectTime(milliseconds);
        if(downbeat) tapTempo.downbeat(milliseconds,seconds,juce::roundToInt(numerator->load()),4<<juce::roundToInt(denominator->load()));
        else if(!tapTempo.other(milliseconds,seconds)) tapFeedback="Next count should be Bar start (or Stop to finish)";
    }
    void cancelTapping() {
        auto* parameter=state.getParameter("trace"); parameter->beginChangeGesture(); parameter->setValueNotifyingHost(0); parameter->endChangeGesture();
        tapGate.recordNow(); tapTempo.reset(); worker.finishRequested=true;
    }
    bool stopTapping(double milliseconds) {
        if(!tapTempo.ready()) { cancelTapping(); tapFeedback="Stopped - mark at least one full bar to estimate tempo"; return false; }
        auto set=[this](const char* id,float value) {
            auto* parameter=state.getParameter(id); parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value)); parameter->endChangeGesture();
        };
        auto base=worker.document.positionAt(tapTempo.firstSeconds());
        auto first=base?*base:worker.document.continuation();
        const auto firstBar=music::barNumber(first);
        const double firstQuarter=first.originQuarter+(firstBar-first.originBar)*first.barLength();
        auto guide=tapTempo.guide(firstBar,firstQuarter,milliseconds,projectTime(milliseconds));
        set("bpmHint",static_cast<float>(tapTempo.quarterBpm())); set("timingMode",1); set("trace",1);
        if(!guide.empty()) worker.installTapGuide(guide);
        tapFeedback=guide.empty()?"Tempo hint set. Run playback while tapping to mark project bar positions":"Marked bar timing saved - replay to trace; manual corrections kept";
        ++hintRevision; tapGate.finish(); return true;
    }
    void syncLanguage() {
        const int selected=juce::roundToInt(language->load()); if(selected==loadedLanguage) return;
        loadedLanguage=selected;
        modelPath=modelLocation::find(juce::File(selected==1?dutchModelPath:englishModelPath),selected==1).getFullPathName();
        lastModelAttempt.clear();
        if(juce::File(modelPath).existsAsFile()) chooseModel(juce::File(modelPath)); else worker.requestModel(modelPath,selected);
    }
    void chooseModel(const juce::File& file) {
        loadedLanguage=juce::roundToInt(language->load()); modelPath=file.getFullPathName();
        (loadedLanguage==1?dutchModelPath:englishModelPath)=modelPath;
        lastModelAttempt=modelLocation::signature(file); worker.requestModel(modelPath,loadedLanguage); updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true)); }
    juce::AudioProcessorValueTreeState state;
    TraceWorker worker;
    BarTapSession tapTempo; TapCaptureGate tapGate;
    juce::String tapFeedback;
    int uiSize=2; juce::String title="Untitled song",modelPath,englishModelPath,dutchModelPath;
private:
    void timerCallback() override {
        syncLanguage();
        if(!worker.modelReady.load() && !worker.speechBusy.load()) {
            const auto file=modelLocation::find(juce::File(modelPath),juce::roundToInt(language->load())==1);
            if(file.existsAsFile() && modelLocation::signature(file)!=lastModelAttempt) chooseModel(file);
        }
        if((juce::Time::getMillisecondCounter()-lastCallback.load())*.001>callbackTimeout) tapGate.hostWentIdle();
        if(worker.capturing.load() && (enabled->load()<.5f || (juce::Time::getMillisecondCounter()-lastCallback.load())*.001>callbackTimeout)) worker.finishRequested=true; const auto value=worker.document.revision.load(); if(value!=lastRevision) { lastRevision=value; updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true)); } }
    std::atomic<float>* enabled=nullptr; std::atomic<float>* mode=nullptr; std::atomic<float>* bpm=nullptr;
    std::atomic<float>* numerator=nullptr; std::atomic<float>* denominator=nullptr; std::atomic<float>* language=nullptr;
    double rate=48000,fallbackSeconds=0,callbackTimeout=.5; unsigned lastRevision=0;
    std::atomic<juce::uint32> lastCallback{0};
    std::atomic<unsigned> hintRevision{0};
    juce::String lastModelAttempt; int loadedLanguage=-1;
    std::atomic<unsigned> transportSequence{0};
    std::atomic<double> transportSeconds{0},transportMillis{0};
    std::atomic<bool> transportPlaying{false},transportValid{false};
};
namespace theme { const juce::Colour cream{0xfff4eddb},ink{0xff252b29},gold{0xffd7ac61},muted{0xff99a192}; }
class LeadSheet final : public juce::Component {
public:
    void setBars(std::vector<LeadBar> value,int active) { bars=std::move(value); current=active; setSize(824,juce::jmax(390,static_cast<int>(bars.size())*142+30)); repaint(); }
    int yForBar(int number) const {
        for(size_t row=0;row<bars.size();++row) if(bars[row].number==number) return static_cast<int>(row)*142;
        return 0;
    }
    void paint(juce::Graphics& g) override {
        using namespace theme; g.fillAll(juce::Colour(0xff222d26));
        if(bars.empty()) { g.setColour(muted); g.setFont(juce::FontOptions(17.f)); g.drawText("Play the song to start tracing",getLocalBounds(),juce::Justification::centred); return; }
        for(size_t row=0;row<bars.size();++row) {
            const auto& bar=bars[row]; const int y=static_cast<int>(row)*142+12;
            if(bar.number==current) { g.setColour(gold.withAlpha(.07f)); g.fillRoundedRectangle(8.f,static_cast<float>(y),808.f,128.f,8.f); }
            g.setColour(muted); g.setFont(juce::FontOptions(11.f));
            g.drawText("BAR "+juce::String(bar.number+1)+"    "+juce::String(bar.numerator)+"/"+juce::String(bar.denominator)+"    ~"+juce::String(bar.bpm,0)+" BPM",24,y+4,760,22,juce::Justification::left);
            const int width=768/bar.numerator;
            for(int beat=0;beat<bar.numerator;++beat) {
                const int x=24+beat*width;
                g.setColour(cream.withAlpha(.12f)); g.drawVerticalLine(x,static_cast<float>(y+34),static_cast<float>(y+111));
                g.setColour(muted); g.setFont(juce::FontOptions(9.f)); g.drawText(juce::String(beat+1),x+8,y+28,30,14,juce::Justification::left);
                g.setColour(gold); g.setFont(juce::FontOptions(24.f)); g.drawFittedText(bar.chords[static_cast<size_t>(beat)],x+8,y+45,width-12,34,juce::Justification::left,1);
                if(bar.hasBeatLyrics()) { g.setColour(cream); g.setFont(juce::FontOptions(15.f)); g.drawFittedText(bar.words[static_cast<size_t>(beat)],x+8,y+84,width-12,31,juce::Justification::topLeft,2); }
            }
            g.setColour(cream); if(!bar.hasBeatLyrics()) { g.setFont(juce::FontOptions(16.f)); g.drawFittedText(bar.lyrics,32,y+84,752,34,juce::Justification::topLeft,2); }
            g.setColour(cream.withAlpha(.08f)); g.drawHorizontalLine(y+133,24,792);
        }
    }
    void mouseDoubleClick(const juce::MouseEvent& event) override {
        const int row=(event.y-12)/142; if(row>=0 && row<static_cast<int>(bars.size()) && edit) edit(bars[static_cast<size_t>(row)]);
    }
    std::function<void(const LeadBar&)> edit;
private:
    std::vector<LeadBar> bars; int current=-1;
};
class TracerPanel final : public juce::Component,private juce::Timer {
public:
    explicit TracerPanel(TracerProcessor& p):processor(p) {
        using namespace theme;
        for(auto* button:{&trace,&newTake,&model,&exportWord,&follow,&tap,&otherTap,&useTap}) {
            addAndMakeVisible(*button); button->setColour(juce::TextButton::buttonColourId,ink.brighter(.12f)); button->setColour(juce::TextButton::textColourOffId,cream); button->setColour(juce::TextButton::textColourOnId,gold);
        }
        trace.setComponentID("trace"); trace.setClickingTogglesState(true); trace.setButtonText("Trace playback");
        trace.onClick=[this] { if(processor.tapGate.isTapping() || processor.tapGate.isWaiting()) processor.cancelTapping(); else if(!trace.getToggleState()) processor.worker.finishRequested=true; };
        traceAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.state,"trace",trace);
        newTake.setButtonText("New take"); newTake.onClick=[this] { processor.worker.newTake(); };
        model.setComponentID("speechModel"); model.setButtonText("Speech model..."); model.setTooltip("Choose a model from "+modelLocation::directory().getFullPathName()); model.onClick=[this] { selectModel(); };
        exportWord.setButtonText("Export Word / Docs..."); exportWord.setComponentID("export"); exportWord.onClick=[this] { exportDocument(); };
        exportWord.setTooltip("Choose Basic (chords/lyrics only) or Detailed (bar numbers/meter/tempo), then save a Word .docx. For Google Docs, upload the file to Drive and open it with Google Docs.");
        tap.setComponentID("tapTempo"); tap.setButtonText("Bar start");
        tap.setTooltip("Left: mark count 1 of each bar. Keep marking a series of bars while playback runs.");
        tap.onClick=[this] { processor.tap(juce::Time::getMillisecondCounterHiRes(),true); refresh(); };
        otherTap.setComponentID("otherTapTempo"); otherTap.setButtonText("Other beat");
        otherTap.setTooltip("Middle: mark the remaining counts in the bar. In 6/8, mark five other eighth-note counts after Bar start.");
        otherTap.onClick=[this] { processor.tap(juce::Time::getMillisecondCounterHiRes(),false); refresh(); };
        useTap.setComponentID("stopTapTempo"); useTap.setButtonText("Stop");
        useTap.setTooltip("Right: finish the entire tapping pass and save its bar timing. Capture waits for replay.");
        useTap.onClick=[this] { processor.stopTapping(juce::Time::getMillisecondCounterHiRes()); refresh(); };
        follow.setButtonText("Follow: ON"); follow.setClickingTogglesState(true); follow.setToggleState(true,juce::dontSendNotification);
        follow.onClick=[this] { follow.setButtonText(follow.getToggleState()?"Follow: ON":"Follow: OFF"); };
        addAndMakeVisible(timing); timing.addItemList({"Detect","Hint + follow","DAW"},1); timing.setComponentID("timingMode");
        timingAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor.state,"timingMode",timing);
        for(auto* control:{&tempo,&beats}) { addAndMakeVisible(*control); control->setSliderStyle(juce::Slider::LinearHorizontal); control->setTextBoxStyle(juce::Slider::TextBoxRight,false,65,24); control->setColour(juce::Slider::trackColourId,gold); control->setColour(juce::Slider::thumbColourId,gold); }
        tempo.setTooltip("Quarter-note BPM hint. Hint + follow starts here and continues estimating tempo as the performance changes.");
        tempoAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.state,"bpmHint",tempo);
        beats.setSliderStyle(juce::Slider::IncDecButtons); beats.setTextBoxStyle(juce::Slider::TextBoxLeft,false,36,26);
        beatsAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.state,"beatsHint",beats);
        addAndMakeVisible(unit); unit.addItemList({"4","8","16"},1);
        unitAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor.state,"unitHint",unit);
        addAndMakeVisible(languageChoice); languageChoice.setComponentID("language"); languageChoice.addItemList({"English","Dutch"},1);
        languageAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor.state,"language",languageChoice);
        languageChoice.onChange=[this] { processor.syncLanguage(); };
        for(auto* combo:{&timing,&unit,&sizes,&languageChoice}) { combo->setColour(juce::ComboBox::backgroundColourId,ink); combo->setColour(juce::ComboBox::textColourId,gold); }
        addAndMakeVisible(sizes); sizes.addItemList({"Small (75%)","Medium (100%)","Large (125%)"},1); sizes.setSelectedId(processor.uiSize,juce::dontSendNotification); sizes.setComponentID("uiSize");
        sizes.onChange=[this] { processor.uiSize=sizes.getSelectedId(); processor.updateHostDisplay(juce::AudioProcessor::ChangeDetails{}.withNonParameterStateChanged(true)); if(sizeChanged) sizeChanged(); };
        addAndMakeVisible(title); title.setText(processor.title,false); title.setColour(juce::TextEditor::backgroundColourId,ink); title.setColour(juce::TextEditor::textColourId,cream);
        title.onTextChange=[this] { processor.title=title.getText().substring(0,200); processor.updateHostDisplay(juce::AudioProcessor::ChangeDetails{}.withNonParameterStateChanged(true)); };
        for(auto* label:{&status,&timingStatus}) { addAndMakeVisible(*label); label->setColour(juce::Label::textColourId,muted); label->setFont(juce::FontOptions(12.f)); }
        addAndMakeVisible(viewport); viewport.setViewedComponent(&sheet,false); viewport.setScrollBarsShown(true,false); viewport.getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId,gold.withAlpha(.5f));
        sheet.edit=[this](const LeadBar& bar) { editBar(bar); };
        setSize(900,830); startTimerHz(20); refresh();
    }
    ~TracerPanel() override { stopTimer(); viewport.setViewedComponent(nullptr,false); }
    std::function<void()> sizeChanged;
    void resized() override {
        sizes.setBounds(680,15,184,28); title.setBounds(32,100,366,30); languageChoice.setBounds(410,100,122,30); model.setBounds(548,100,146,30); exportWord.setBounds(706,100,158,30);
        trace.setBounds(32,158,140,30); newTake.setBounds(184,158,104,30); timing.setBounds(310,158,152,30);
        tempo.setBounds(514,158,178,30); beats.setBounds(716,158,78,30); unit.setBounds(802,158,62,30);
        timingStatus.setBounds(32,199,430,27); tap.setBounds(480,199,82,27); otherTap.setBounds(568,199,82,27); useTap.setBounds(656,199,72,27); follow.setBounds(736,199,128,27); viewport.setBounds(32,242,836,470); status.setBounds(32,728,832,52);
    }
    void paint(juce::Graphics& g) override {
        using namespace theme; g.fillAll(cream); g.setColour(ink); g.setFont(juce::FontOptions(11.f));
        g.drawText("YODO AUDIO    /    LIVE SONG NOTEBOOK    /    OUTPUT MUTED",32,14,610,25,juce::Justification::left);
        g.setFont(juce::FontOptions(33.f)); g.drawText("Text and Chord Tracer",30,45,700,44,juce::Justification::left);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff343d36),0,144,juce::Colour(0xff1e2722),0,786,false)); g.fillRoundedRectangle(16,144,868,642,16);
        g.setColour(gold); g.setFont(juce::FontOptions(10.f)); g.drawText("Q BPM",470,158,44,30,juce::Justification::left); g.drawText("METER",716,146,80,12,juce::Justification::left); g.drawText("/",794,158,8,30,juce::Justification::centred);
        g.setColour(ink.withAlpha(.7f)); g.setFont(juce::FontOptions(10.f));
        g.drawText("ANALYSIS ONLY  /  OUTPUT MUTED  /  LOCAL TRANSCRIPTION",32,800,600,20,juce::Justification::left);
        g.drawText("DOUBLE-CLICK A BAR TO EDIT",600,800,264,20,juce::Justification::right);
    }
    void refresh() {
        if(title.getText()!=processor.title && !title.hasKeyboardFocus(false)) title.setText(processor.title,false);
        if(sizes.getSelectedId()!=processor.uiSize) { sizes.setSelectedId(processor.uiSize,juce::dontSendNotification); if(sizeChanged) sizeChanged(); }
        const auto revision=processor.worker.document.revision.load();
        if(revision!=lastRevision || lastBar!=processor.worker.currentBar.load()) {
            auto bars=processor.worker.document.snapshot(); haveBars=!bars.empty();
            lastRevision=revision; lastBar=processor.worker.currentBar.load(); sheet.setBars(std::move(bars),lastBar);
        }
        if(follow.getToggleState() && (processor.worker.capturing.load() || processor.worker.pending()) && !dragging()) {
            const int maximum=juce::jmax(0,sheet.getHeight()-viewport.getHeight());
            const int target=juce::jlimit(0,maximum,sheet.yForBar(lastBar)-viewport.getHeight()/3);
            const auto focus=processor.worker.focusRevision.load();
            if(focus!=lastFocus) { lastFocus=focus; viewport.setViewPosition(0,target); }
            else {
                const int difference=target-viewport.getViewPositionY();
                if(difference!=0) viewport.setViewPosition(0,viewport.getViewPositionY()+(difference>0?1:-1)*juce::jmax(1,std::abs(difference)/14));
            }
        }
        const auto bpm=processor.worker.detectedBpm.load();
        timingStatus.setText((processor.worker.capturing.load()?"TRACING":"READY")+juce::String("   /   ")+(timing.getSelectedId()==3 && processor.worker.usingHost.load()?"DAW grid":bpm>0?"~"+juce::String(bpm,1)+" BPM":"Listening for a steady pulse")+"   /   Bar grid is provisional",juce::dontSendNotification);
        const bool tapping=processor.tapGate.isTapping(),waiting=processor.tapGate.isWaiting();
        trace.setButtonText(tapping || waiting?"Cancel tap":"Trace playback");
        otherTap.setEnabled(tapping); useTap.setEnabled(tapping);
        if(tapping) {
            auto text="TAPPING - bar "+juce::String(processor.tapTempo.barCount())+", count "+juce::String(processor.tapTempo.count())+"/"+juce::String(processor.tapTempo.beats())+" - capture paused";
            if(processor.tapTempo.quarterBpm()>0) text+="  /  ~"+juce::String(processor.tapTempo.quarterBpm(),1)+" Q BPM";
            timingStatus.setText(text,juce::dontSendNotification);
        } else if(waiting) timingStatus.setText("Bar timing set - restart playback to trace",juce::dontSendNotification);
        else if(processor.tapFeedback.isNotEmpty()) timingStatus.setText(processor.tapFeedback,juce::dontSendNotification);
        auto message=processor.worker.status();
        if(!processor.worker.modelReady.load() && !juce::File(processor.modelPath).existsAsFile())
            message="Speech model missing. Run scripts/download-model.sh "+juce::String(languageChoice.getSelectedId()==2?"base":"base.en")+". Folder: "+modelLocation::directory().getFullPathName();
        else if(processor.worker.modelReady.load()) message+=" | "+juce::File(processor.modelPath).getFileName();
        if(processor.worker.dropped.load()>0) message+=" | Capture backlog: some audio skipped";
        status.setText(message,juce::dontSendNotification);
        exportWord.setEnabled(!processor.worker.capturing.load() && !processor.worker.pending() && haveBars);
    }
private:
    bool dragging() { return viewport.getVerticalScrollBar().isMouseButtonDown(); }
    void timerCallback() override { refresh(); }
    void selectModel() {
        const auto folder=modelLocation::pickerDirectory(juce::File(processor.modelPath),modelLocation::directory());
        if(folder.createDirectory().failed()) {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"Model folder unavailable",folder.getFullPathName());
            return;
        }
        chooser=std::make_unique<juce::FileChooser>("Choose a multilingual ggml Whisper model",folder,"*.bin");
        chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe=juce::Component::SafePointer<TracerPanel>(this)](const juce::FileChooser& selected) {
            if(safe && selected.getResult().existsAsFile()) safe->processor.chooseModel(selected.getResult());
        });
    }
    void exportDocument() {
        juce::PopupMenu menu;
        menu.addItem(1,"Basic - chords and lyrics only");
        menu.addItem(2,"Detailed - bar numbers, meter and tempo");
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&exportWord),[safe=juce::Component::SafePointer<TracerPanel>(this)](int result) {
            if(safe && result!=0) safe->saveDocument(result==1?documentExport::Style::basic:documentExport::Style::detailed);
        });
    }
    void saveDocument(documentExport::Style style) {
        chooser=std::make_unique<juce::FileChooser>((style==documentExport::Style::basic?"Export basic chords and lyrics":"Export detailed lead sheet"),juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(juce::File::createLegalFileName(processor.title)+(style==documentExport::Style::basic?"-basic.docx":".docx")),"*.docx");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,[safe=juce::Component::SafePointer<TracerPanel>(this),style](const juce::FileChooser& selected) {
            if(!safe || selected.getResult()==juce::File{}) return;
            const auto file=selected.getResult().withFileExtension("docx");
            const bool ok=documentExport::writeDocx(file,safe->processor.worker.document.snapshot(),safe->processor.title,style);
            safe->status.setText(ok?"Exported "+file.getFullPathName()+" - upload to Drive to open in Google Docs":"Export failed - choose another location",juce::dontSendNotification);
        });
    }
    void editBar(const LeadBar& bar) {
        auto* window=new juce::AlertWindow("Edit bar "+juce::String(bar.number+1),"Use | in either row to separate beats; an empty field leaves that beat blank. Without |, lyrics span the bar.",juce::MessageBoxIconType::NoIcon);
        juce::StringArray chords; for(const auto& value:bar.chords) chords.add(value);
        window->addTextEditor("chords",chords.joinIntoString(" | "),"Chords"); window->addTextEditor("lyrics",bar.lyricsForEditor(),"Lyrics");
        window->addButton("Save",1); window->addButton("Cancel",0);
        auto validate=[window,beats=bar.numerator] {
            const bool valid=juce::StringArray::fromTokens(window->getTextEditorContents("chords"),"|","").size()<=beats
                && juce::StringArray::fromTokens(window->getTextEditorContents("lyrics"),"|","").size()<=beats;
            window->getButton(0)->setEnabled(valid);
            window->setMessage(valid?"Use | in either row to separate beats; an empty field leaves that beat blank. Without |, lyrics span the bar."
                :"This bar has "+juce::String(beats)+" beats. Remove extra | separators before saving.");
        };
        window->getTextEditor("chords")->onTextChange=validate;
        window->getTextEditor("lyrics")->onTextChange=validate;
        validate();
        window->enterModalState(true,juce::ModalCallbackFunction::create([safe=juce::Component::SafePointer<TracerPanel>(this),window,number=bar.number](int result) {
            if(safe && result==1) safe->processor.worker.document.edit(number,window->getTextEditorContents("chords"),window->getTextEditorContents("lyrics"));
        }),true);
    }
    TracerProcessor& processor;
    juce::TextButton trace,newTake,model,exportWord,follow,tap,otherTap,useTap;
    juce::ComboBox timing,unit,sizes,languageChoice; juce::Slider tempo,beats;
    juce::TextEditor title; juce::Label status,timingStatus;
    LeadSheet sheet; juce::Viewport viewport;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> traceAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> timingAttachment,unitAttachment,languageAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tempoAttachment,beatsAttachment;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltip{this,600};
    unsigned lastRevision=~0u,lastFocus=0; int lastBar=-1; bool haveBars=false;
};
class TracerEditor final : public juce::AudioProcessorEditor {
public:
    explicit TracerEditor(TracerProcessor& p):AudioProcessorEditor(p),processor(p),panel(p) { addAndMakeVisible(panel); setResizable(false,false); panel.sizeChanged=[this] { resize(); }; resize(); }
private:
    void resize() { const float scale=processor.uiSize==1?.75f:processor.uiSize==3?1.25f:1.f; panel.setTransform(juce::AffineTransform::scale(scale)); panel.setBounds(0,0,900,830); setSize(juce::roundToInt(900*scale),juce::roundToInt(830*scale)); }
    TracerProcessor& processor; TracerPanel panel;
};
juce::AudioProcessorEditor* TracerProcessor::createEditor() { return new TracerEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TracerProcessor; }
