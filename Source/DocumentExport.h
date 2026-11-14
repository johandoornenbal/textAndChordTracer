#pragma once
#include "TraceModel.h"
#include <juce_core/juce_core.h>
namespace documentExport {
inline juce::String xml(const juce::String& text) {
    return text.replace("&","&amp;").replace("<","&lt;").replace(">","&gt;").replace("\"","&quot;").replace("'","&apos;");
}
inline juce::String paragraph(const juce::String& text,bool bold=false) {
    return "<w:p><w:r>"+juce::String(bold?"<w:rPr><w:b/><w:color w:val=\"816027\"/></w:rPr>":"")+"<w:t xml:space=\"preserve\">"+xml(text)+"</w:t></w:r></w:p>";
}
inline juce::String makeDocument(const std::vector<LeadBar>& bars,const juce::String& title) {
    juce::String body=paragraph(title,true)+paragraph("Text and Chord Tracer | Editable draft - review sung words, chords and bar placement.");
    for(const auto& bar:bars) {
        body+=paragraph("Bar "+juce::String(bar.number+1)+"   "+juce::String(bar.numerator)+"/"+juce::String(bar.denominator)+"   ~"+juce::String(bar.bpm,1)+" BPM",true);
        body+="<w:tbl><w:tblPr><w:tblW w:w=\"9000\" w:type=\"dxa\"/><w:tblBorders><w:bottom w:val=\"single\" w:sz=\"4\" w:color=\"CCCCCC\"/></w:tblBorders></w:tblPr><w:tblGrid>";
        for(int beat=0;beat<bar.numerator;++beat) body+="<w:gridCol w:w=\""+juce::String(9000/bar.numerator)+"\"/>";
        body+="</w:tblGrid>";
        for(bool chordRow:{true,false}) {
            body+="<w:tr>";
            if(!chordRow && bar.lyricsEdited) {
                body+="<w:tc><w:tcPr><w:tcW w:w=\"9000\" w:type=\"dxa\"/><w:gridSpan w:val=\""+juce::String(bar.numerator)+"\"/></w:tcPr>"+paragraph(bar.lyrics)+"</w:tc></w:tr>";
                continue;
            }
            for(int beat=0;beat<bar.numerator;++beat) {
                auto text=chordRow?bar.chords[static_cast<size_t>(beat)]:bar.lyricsEdited?(beat==0?bar.lyrics:juce::String{}):bar.words[static_cast<size_t>(beat)];
                body+="<w:tc><w:tcPr><w:tcW w:w=\""+juce::String(9000/bar.numerator)+"\" w:type=\"dxa\"/></w:tcPr>"+paragraph(text,chordRow)+"</w:tc>";
            }
            body+="</w:tr>";
        }
        body+="</w:tbl>"+paragraph("");
    }
    return "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body>"+body+"<w:sectPr><w:pgSz w:w=\"11906\" w:h=\"16838\"/><w:pgMar w:top=\"1080\" w:right=\"1080\" w:bottom=\"1080\" w:left=\"1080\"/></w:sectPr></w:body></w:document>";
}
inline bool writeDocx(const juce::File& destination,const std::vector<LeadBar>& bars,const juce::String& title) {
    juce::ZipFile::Builder zip;
    auto add=[&](const char* name,const juce::String& content) {
        zip.addEntry(new juce::MemoryInputStream(content.toRawUTF8(),content.getNumBytesAsUTF8(),true),6,name,juce::Time::getCurrentTime());
    };
    add("[Content_Types].xml","<?xml version=\"1.0\" encoding=\"UTF-8\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/><Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/></Types>");
    add("_rels/.rels","<?xml version=\"1.0\" encoding=\"UTF-8\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/></Relationships>");
    add("word/document.xml",makeDocument(bars,title));
    juce::TemporaryFile temporary(destination); auto stream=temporary.getFile().createOutputStream();
    if(!stream || !zip.writeToStream(*stream,nullptr)) return false;
    stream->flush(); if(stream->getStatus().failed()) return false; stream.reset();
    return temporary.overwriteTargetFileWithTemporary();
}
}
