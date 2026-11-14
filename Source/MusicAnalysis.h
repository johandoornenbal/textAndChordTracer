#pragma once
#include <array>
#include <complex>
#include <string>
#include <cmath>
#include <algorithm>
namespace music {
constexpr double pi=3.14159265358979323846;
constexpr int fftSize=4096,analysisRate=12000;
struct Chord { std::string name; float confidence=0; };
class ChordDetector {
public:
    Chord analyse(const std::array<float,fftSize>& left,const std::array<float,fftSize>& right);
private:
    std::array<std::complex<double>,fftSize> fft{};
    std::array<double,fftSize/2> power{};
};
class TempoEstimator {
public:
    void push(double energy);
    double estimate(double hint=115) const;
    int meter(double bpm) const;
    void reset() { *this=TempoEstimator{}; }
private:
    std::array<double,512> novelty{};
    int head=0,count=0;
    double previous=0;
};
struct Grid {
    double quarter=0,bpm=120;
    int numerator=4,denominator=4;
    bool fromHost=false;
    double originQuarter=0;
    long long originBar=0;
    double beatLength() const { return 4./denominator; }
    double barLength() const { return numerator*beatLength(); }
};
inline long long barNumber(const Grid& grid) { return static_cast<long long>(std::floor((grid.quarter-grid.originQuarter)/grid.barLength())+grid.originBar); }
inline int beatNumber(const Grid& grid) { return std::clamp(static_cast<int>(std::floor((grid.quarter-grid.originQuarter-(barNumber(grid)-grid.originBar)*grid.barLength())/grid.beatLength()+.0001)),0,grid.numerator-1); }
}
