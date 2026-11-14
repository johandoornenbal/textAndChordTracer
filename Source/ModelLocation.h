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
inline juce::File resolve(const juce::File& requested,const juce::File& standard,const juce::File& legacy={}) {
    if(requested.existsAsFile()) return requested;
    for(const auto& folder:{standard,legacy}) {
        if(folder==juce::File{}) continue;
        for(const char* name:{"ggml-base.bin","ggml-small.bin","ggml-tiny.bin"}) {
            auto file=folder.getChildFile(name); if(file.existsAsFile()) return file;
        }
    }
    return standard.getChildFile("ggml-base.bin");
}
inline juce::File find(const juce::File& requested={}) { return resolve(requested,directory(),legacyDirectory()); }
inline juce::File pickerDirectory(const juce::File& selected,const juce::File& standard) {
    return selected.existsAsFile()?selected.getParentDirectory():standard;
}
inline juce::String signature(const juce::File& file) {
    return file.getFullPathName()+"|"+juce::String(file.getSize())+"|"+juce::String(file.getLastModificationTime().toMilliseconds());
}
}
