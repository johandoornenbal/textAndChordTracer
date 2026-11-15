#include "MusicAnalysis.h"
namespace music {
Chord ChordDetector::analyse(const std::array<float,fftSize>& left,const std::array<float,fftSize>& right) {
    power.fill(0);
    double energy=0;
    for(const auto* input:{&left,&right}) {
        for(int i=0;i<fftSize;++i) {
            const double x=std::isfinite((*input)[static_cast<size_t>(i)])?(*input)[static_cast<size_t>(i)]:0;
            energy+=x*x;
            fft[static_cast<size_t>(i)]=x*(.5-.5*std::cos(2*pi*i/(fftSize-1)));
        }
        for(int i=1,j=0;i<fftSize;++i) {
            int bit=fftSize>>1;
            for(;j&bit;bit>>=1) j^=bit;
            j^=bit; if(i<j) std::swap(fft[static_cast<size_t>(i)],fft[static_cast<size_t>(j)]);
        }
        for(int length=2;length<=fftSize;length<<=1) {
            const auto rotation=std::polar(1.,-2*pi/length);
            for(int i=0;i<fftSize;i+=length) {
                std::complex<double> w{1,0};
                for(int j=0;j<length/2;++j) {
                    auto a=fft[static_cast<size_t>(i+j)],b=fft[static_cast<size_t>(i+j+length/2)]*w;
                    fft[static_cast<size_t>(i+j)]=a+b; fft[static_cast<size_t>(i+j+length/2)]=a-b; w*=rotation;
                }
            }
        }
        for(int i=1;i<fftSize/2;++i) power[static_cast<size_t>(i)]+=std::norm(fft[static_cast<size_t>(i)]);
    }
    if(energy/(2*fftSize)<1.e-7) return {};
    std::array<double,12> chroma{};
    const auto maximum=*std::max_element(power.begin(),power.end());
    int peaks=0;
    for(int i=20;i<700;++i) {
        const auto p=power[static_cast<size_t>(i)];
        if(p<maximum*.012 || p<power[static_cast<size_t>(i-1)] || p<power[static_cast<size_t>(i+1)]) continue;
        const double a=std::log(std::max(1.e-20,power[static_cast<size_t>(i-1)])),b=std::log(std::max(1.e-20,p)),c=std::log(std::max(1.e-20,power[static_cast<size_t>(i+1)]));
        const auto correction=std::clamp(.5*(a-c)/(a-2*b+c),-.5,.5);
        const double hz=(i+correction)*analysisRate/fftSize;
        const double midi=69+12*std::log2(hz/440.);
        const int note=static_cast<int>(std::round(midi));
        if(std::abs(midi-note)>.4) continue;
        chroma[static_cast<size_t>((note%12+12)%12)]+=std::sqrt(p)/std::sqrt(hz);
        ++peaks;
    }
    if(peaks<2) return {};
    double norm=0; for(auto& value:chroma) { value=std::sqrt(value); norm+=value*value; }
    if(norm<1.e-12) return {};
    for(auto& value:chroma) value/=std::sqrt(norm);
    struct Shape { const char* suffix; std::array<int,4> intervals; int count; };
    static const Shape shapes[]{ {"",{0,4,7,0},3},{"m",{0,3,7,0},3},{"7",{0,4,7,10},4},{"maj7",{0,4,7,11},4},{"m7",{0,3,7,10},4},{"sus2",{0,2,7,0},3},{"sus4",{0,5,7,0},3},{"dim",{0,3,6,0},3},{"aug",{0,4,8,0},3} };
    static const char* roots[]{"C","C#","D","Eb","E","F","F#","G","Ab","A","Bb","B"};
    double best=0,second=0; Chord result;
    for(int root=0;root<12;++root) for(const auto& shape:shapes) {
        double score=0; for(int n=0;n<shape.count;++n) score+=chroma[static_cast<size_t>((root+shape.intervals[static_cast<size_t>(n)])%12)];
        score/=std::sqrt(shape.count);
        if(score>best) { second=best; best=score; result.name=std::string(roots[root])+shape.suffix; }
        else second=std::max(second,score);
    }
    result.confidence=static_cast<float>(std::clamp((best-.65)/.35,0.,1.)*std::clamp((best-second)/.10,0.,1.));
    if(best<.82 || result.confidence<.12f) return {};
    return result;
}
void TempoEstimator::push(double energy) {
    novelty[static_cast<size_t>(head)]=std::max(0.,energy-previous);
    previous=energy; head=(head+1)%static_cast<int>(novelty.size()); count=std::min(count+1,static_cast<int>(novelty.size()));
}
double TempoEstimator::estimate(double hint) const {
    if(count<300) return 0;
    double best=0,bpm=0,strength=0;
    for(int lag=9;lag<=282;++lag) {
        if(count<3*lag) continue;
        const int window=std::min(count,std::max(300,8*lag));
        double sum=0,a=0,b=0;
        for(int i=lag;i<window;++i) {
            const double x=novelty[static_cast<size_t>((head-1-i+historySize*2)%historySize)],y=novelty[static_cast<size_t>((head-1-i+lag+historySize*2)%historySize)];
            sum+=x*y; a+=x*x; b+=y*y;
        }
        const auto correlation=sum/std::sqrt(std::max(1.e-20,a*b));
        const auto candidate=60.*analysisRate/(256.*lag);
        const auto score=correlation*std::exp(-.5*std::pow(std::log2(candidate/std::max(10.,hint))/.7,2.));
        if(score>best) { best=score; bpm=candidate; strength=correlation; }
    }
    return strength>.25?std::clamp(bpm,10.,300.):0;
}
}

int music::TempoEstimator::meter(double bpm) const {
    if(count<300 || bpm<=0) return 0;
    const int beatLag=static_cast<int>(std::round(60.*analysisRate/(256.*bpm)));
    if(count<8*beatLag) return 0;
    const int window=std::min(count,12*beatLag);
    double best=0,second=0; int beats=0;
    for(int n:{3,4}) {
        const int lag=static_cast<int>(std::round(n*60.*analysisRate/(256.*bpm)));
        if(lag>=window/2) continue;
        double sum=0,a=0,b=0;
        for(int i=lag;i<window;++i) {
            const auto x=novelty[static_cast<size_t>((head-1-i+historySize*2)%historySize)],y=novelty[static_cast<size_t>((head-1-i+lag+historySize*2)%historySize)];
            sum+=x*y; a+=x*x; b+=y*y;
        }
        const double score=sum/std::sqrt(std::max(1.e-20,a*b));
        if(score>best) { second=best; best=score; beats=n; } else second=std::max(second,score);
    }
    return best>.70 && best-second>.06?beats:0;
}
