#pragma once
#include <juce_core/juce_core.h>
namespace modelLocation {
inline juce::File directoryFor(const juce::File& applicationData,bool mac) {
    return (mac?applicationData.getChildFile("Application Support"):applicationData)
        .getChildFile("Doornenbal Audio/TextAndChordTracer/models");
}
inline juce::File directory() {
    #if JUCE_MAC
    return directoryFor(juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory),true);
#else
    return directoryFor(juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory),false);
#endif
}
inline juce::File legacyDirectory() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Doornenbal Audio/TextAndChordTracer/models");
}
inline bool englishOnlyName(const juce::File& file) { return file.getFileName().containsIgnoreCase(".en"); }
inline juce::File resolve(const juce::File& requested,const juce::File& standard,const juce::File& legacy={},bool dutch=false) {
    if(requested.existsAsFile() && (!dutch || !englishOnlyName(requested))) return requested;
    const juce::StringArray names=dutch?juce::StringArray{"ggml-base.bin","ggml-small.bin","ggml-tiny.bin"}
        :juce::StringArray{"ggml-base.en.bin","ggml-base.bin","ggml-small.en.bin","ggml-small.bin","ggml-tiny.en.bin","ggml-tiny.bin"};
    for(const auto& folder:{standard,legacy}) {
        if(folder==juce::File{}) continue;
        for(const auto& name:names) { auto file=folder.getChildFile(name); if(file.existsAsFile()) return file; }
    }
    return standard.getChildFile(dutch?"ggml-base.bin":"ggml-base.en.bin");
}
inline juce::File find(const juce::File& requested={},bool dutch=false) { return resolve(requested,directory(),legacyDirectory(),dutch); }
inline juce::File pickerDirectory(const juce::File& selected,const juce::File& standard) {
    return selected.existsAsFile()?selected.getParentDirectory():standard;
}
inline juce::String signature(const juce::File& file) {
    return file.getFullPathName()+"|"+juce::String(file.getSize())+"|"+juce::String(file.getLastModificationTime().toMilliseconds());
}
}
