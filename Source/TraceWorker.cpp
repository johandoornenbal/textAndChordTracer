#include "TraceWorker.h"
TraceWorker::TraceWorker():Thread("Chord and tempo analysis"),speechThread(*this) { startThread(); speechThread.startThread(); }
TraceWorker::~TraceWorker() { cancelled=true; signalThreadShouldExit(); speechThread.signalThreadShouldExit(); waitForThreadToExit(-1); speechThread.waitForThreadToExit(-1); }
void TraceWorker::requestModel(const juce::String& path) {
    { const std::lock_guard<std::mutex> guard(modelLock); requestedModel=path; }
    modelReady=false; modelRequested=true;
}
void TraceWorker::SpeechThread::run() {
    SpeechEngine engine; auto job=std::make_unique<SpeechJob>();
    while(!threadShouldExit()) {
        if(owner.modelRequested.exchange(false)) {
            juce::String path; { const std::lock_guard<std::mutex> guard(owner.modelLock); path=owner.requestedModel; }
            owner.speechBusy=true; owner.setStatus("Loading local transcription model...");
            const bool ready=engine.load(path); owner.modelReady=ready; owner.speechBusy=false;
            owner.setStatus(ready?"Local lyrics ready - best results with a clean vocal input":"Model unavailable - choose a valid ggml Whisper model");
        }
        if(!owner.jobs.pop(*job)) { wait(15); continue; }
        if(job->generation!=owner.generation.load() || !engine.ready()) continue;
        double energy=0; for(int i=job->skip;i<job->count;++i) energy+=job->samples[static_cast<size_t>(i)]*job->samples[static_cast<size_t>(i)];
        if(energy/juce::jmax(1,job->count-job->skip)<1.e-6) continue;
        owner.speechBusy=true; owner.setStatus("Transcribing locally - sung words may need correction");
        const auto words=engine.transcribe(job->samples.data(),job->count,owner.cancelled,"auto");
        if(job->generation==owner.generation.load()) {
            for(const auto& word:words) {
                const int sample=juce::jlimit(0,job->count-1,static_cast<int>(word.seconds*16000));
                if(sample<job->skip || job->stampCount==0) continue;
                int stamp=0; while(stamp+1<job->stampCount && job->stamps[static_cast<size_t>(stamp+1)].sample<=sample) ++stamp;
                const auto& location=job->stamps[static_cast<size_t>(stamp)]; auto grid=location.grid;
                const double delta=(sample-location.sample)/16000.; grid.quarter+=delta*grid.bpm/60.;
                owner.document.word(grid,location.seconds+delta,word.text,job->generation,job->pass);
            }
        }
        owner.speechBusy=false; owner.setStatus("Local lyrics ready - review words and chord guesses before export");
    }
}
void TraceWorker::run() {
    auto packet=std::make_unique<AudioCapture>(); auto speech=std::make_unique<SpeechJob>();
    std::array<float,music::fftSize> left{},right{};
    music::ChordDetector detector; music::TempoEstimator tempo;
    int filled=0,hop=0,energySamples=0,stable=0;
    double analysisPhase=0,speechPhase=0,energy=0,quarter=0,bpm=120,lastEnd=-1;
    double filterL=0,filterR=0,filterV=0,lastHint=120;
    music::Grid grid;
    unsigned activeGeneration=0,pass=0;
    std::string previousChord;
    bool wasPlaying=false,heardAudio=false;
    double previousEnergy=0;
    auto flush=[&](bool retain) {
        if(speech->count>speech->skip && speech->count>=16000) {
            speech->generation=activeGeneration; speech->pass=pass;
            if(!jobs.push(*speech)) ++dropped;
        }
        if(retain) {
            const int keep=32000,offset=speech->count-keep;
            std::move(speech->samples.begin()+offset,speech->samples.begin()+speech->count,speech->samples.begin());
            int stamps=0;
            for(int i=0;i<speech->stampCount;++i) if(speech->stamps[static_cast<size_t>(i)].sample>=offset) {
                auto s=speech->stamps[static_cast<size_t>(i)]; s.sample-=offset; speech->stamps[static_cast<size_t>(stamps++)]=s;
            }
            speech->stampCount=stamps; speech->count=keep; speech->skip=keep;
        } else { speech->count=0; speech->stampCount=0; speech->skip=0; }
    };
    while(!threadShouldExit()) {
        if(!audio.pop(*packet)) {
            if(finishRequested.exchange(false)) { if(wasPlaying) flush(false); wasPlaying=false; capturing=false; }
            wait(5); continue;
        }
        capturing=packet->playing;
        usingHost=packet->mode==2 && packet->host.fromHost;
        if(packet->mode==2 && !packet->host.fromHost) { packet->mode=1; packet->hint=packet->host.bpm; }
        if(packet->generation!=generation.load()) continue;
        if(packet->generation!=activeGeneration) {
            activeGeneration=packet->generation; speech->count=speech->skip=speech->stampCount=0;
            quarter=0; bpm=packet->hint; lastEnd=-1; filled=hop=stable=energySamples=0; energy=0; previousChord.clear(); tempo.reset();
            analysisPhase=speechPhase=filterL=filterR=filterV=0; grid={}; heardAudio=false; previousEnergy=0;
        }
        if(!packet->playing) { if(wasPlaying) flush(false); wasPlaying=false; continue; }
        const bool seek=lastEnd>=0 && std::abs(packet->seconds-lastEnd)>.15;
        if(seek) { flush(false); filled=hop=stable=0; previousChord.clear(); quarter=packet->seconds*bpm/60.; }
        if(!wasPlaying || seek) { ++pass; speech->count=speech->skip=speech->stampCount=0; }
        wasPlaying=true;
        if(packet->mode==2 && packet->host.fromHost) grid=packet->host;
        else {
            grid.quarter=quarter; grid.bpm=bpm; grid.fromHost=false;
            const int desiredBeats=packet->mode==1 || detectedNumerator.load()==0?packet->host.numerator:detectedNumerator.load();
            const int desiredUnit=packet->mode==1 || detectedNumerator.load()==0?packet->host.denominator:4;
            if((desiredBeats!=grid.numerator || desiredUnit!=grid.denominator) && music::beatNumber(grid)==0) {
                const auto number=music::barNumber(grid);
                grid.originQuarter=grid.originQuarter+(number-grid.originBar)*grid.barLength(); grid.originBar=number;
                grid.numerator=desiredBeats; grid.denominator=desiredUnit;
            }
        }
        if(packet->mode==1 && std::abs(packet->hint-lastHint)>.1) { bpm=packet->hint; grid.bpm=bpm; }
        lastHint=packet->hint;
        if(!heardAudio && packet->mode!=2) {
            double level=0; for(int i=0;i<packet->count;++i) level+=packet->left[static_cast<size_t>(i)]*packet->left[static_cast<size_t>(i)]+packet->right[static_cast<size_t>(i)]*packet->right[static_cast<size_t>(i)]+packet->vocal[static_cast<size_t>(i)]*packet->vocal[static_cast<size_t>(i)];
            if(level/juce::jmax(1,packet->count)<1.e-7) { lastEnd=packet->seconds+packet->count/packet->rate; continue; }
            heardAudio=true;
        }
        document.touch(grid,packet->seconds,activeGeneration);
        const double coefficient=1.-std::exp(-2*music::pi*4500./packet->rate);
        auto finite=[](float value) { return std::isfinite(value)?value:0.f; };
        for(int i=0;i<packet->count;++i) {
            const double seconds=packet->seconds+i/packet->rate;
            filterL+=coefficient*(finite(packet->left[static_cast<size_t>(i)])-filterL);
            filterR+=coefficient*(finite(packet->right[static_cast<size_t>(i)])-filterR);
            filterV+=coefficient*(finite(packet->vocal[static_cast<size_t>(i)])-filterV);
            speechPhase+=16000./packet->rate;
            while(speechPhase>=1.) {
                speechPhase-=1.;
                if(speech->count%1600==0 && speech->stampCount<96) speech->stamps[static_cast<size_t>(speech->stampCount++)]={speech->count,seconds,grid};
                speech->samples[static_cast<size_t>(speech->count++)]=static_cast<float>(filterV);
                if(speech->count==128000) flush(true);
            }
            analysisPhase+=music::analysisRate/packet->rate;
            while(analysisPhase>=1.) {
                analysisPhase-=1.; left[static_cast<size_t>(filled)]=static_cast<float>(filterL); right[static_cast<size_t>(filled)]=static_cast<float>(filterR); ++filled;
                energy+=(filterL*filterL+filterR*filterR)*.5;
                if(++energySamples==256) {
                    const double currentEnergy=energy/256;
                    tempo.push(currentEnergy);
                    if(packet->mode!=2 && currentEnergy>1.e-6 && currentEnergy>previousEnergy*1.8) {
                        const double beat=(grid.quarter-grid.originQuarter)/grid.beatLength();
                        const double error=std::round(beat)-beat;
                        if(std::abs(error)<.2) grid.quarter+=error*grid.beatLength()*.15;
                    }
                    previousEnergy=currentEnergy; energy=0; energySamples=0;
                }
                if(filled==music::fftSize) {
                    auto chord=detector.analyse(left,right);
                    if(chord.name==previousChord && !chord.name.empty()) ++stable; else { previousChord=chord.name; stable=1; }
                    auto centre=grid; centre.quarter-=music::fftSize*.5/music::analysisRate*grid.bpm/60.;
                    if(stable>=2 && centre.quarter>=0) document.chord(centre,seconds-music::fftSize*.5/music::analysisRate,juce::String(chord.name),activeGeneration);
                    std::move(left.begin()+1024,left.end(),left.begin()); std::move(right.begin()+1024,right.end(),right.begin()); filled=music::fftSize-1024;
                    if(++hop%12==0 && packet->mode!=2) {
                        const double candidate=tempo.estimate(packet->mode==1?packet->hint:bpm);
                        if(candidate>0) { bpm=bpm*.8+candidate*.2; detectedBpm=bpm; }
                        if(packet->mode==0) {
                            const int beats=tempo.meter(bpm);
                            // Adopt a changed meter at a measure boundary, preserving bar numbering.
                            if(beats>0 && music::beatNumber(grid)==0) {
                                const auto number=music::barNumber(grid);
                                grid.originQuarter=grid.originQuarter+(number-grid.originBar)*grid.barLength(); grid.originBar=number;
                                grid.numerator=beats; grid.denominator=4; detectedNumerator=beats;
                            }
                        }
                    }
                }
            }
            grid.quarter+=grid.bpm/(60.*packet->rate);
            quarter=grid.quarter;
        }
        currentBar=static_cast<int>(music::barNumber(grid));
        lastEnd=packet->seconds+packet->count/packet->rate;
    }
}
