#pragma once
#include "TraceModel.h"
#include <juce_core/juce_core.h>
namespace documentExport {
enum class Style { basic, detailed };
inline juce::String xml(const juce::String& text) {
    return text.replace("&","&amp;").replace("<","&lt;").replace(">","&gt;").replace("\"","&quot;").replace("'","&apos;");
}
inline juce::String paragraph(const juce::String& text,bool bold=false) {
    return "<w:p><w:r>"+juce::String(bold?"<w:rPr><w:b/><w:color w:val=\"816027\"/></w:rPr>":"")+"<w:t xml:space=\"preserve\">"+xml(text)+"</w:t></w:r></w:p>";
}
struct PoemCell { juce::String chord,lyric; };
inline juce::String table(const std::vector<PoemCell>& cells,bool detailed=false) {
    if(cells.empty()) return {};
    std::vector<int> weights; int total=0;
    for(const auto& cell:cells) { const int width=detailed?1:juce::jmax(3,juce::jmax(cell.chord.length(),cell.lyric.length())+2); weights.push_back(width); total+=width; }
    juce::String result="<w:tbl><w:tblPr><w:tblW w:w=\"9000\" w:type=\"dxa\"/><w:tblLayout w:type=\"fixed\"/></w:tblPr><w:tblGrid>";
    for(int width:weights) result+="<w:gridCol w:w=\""+juce::String(9000*width/total)+"\"/>";
    result+="</w:tblGrid>";
    for(bool chords:{true,false}) {
        result+="<w:tr>";
        for(size_t i=0;i<cells.size();++i) result+="<w:tc><w:tcPr><w:tcW w:w=\""+juce::String(9000*weights[i]/total)+"\" w:type=\"dxa\"/></w:tcPr>"+paragraph(chords?cells[i].chord:cells[i].lyric,chords)+"</w:tc>";
        result+="</w:tr>";
    }
    return result+"</w:tbl>";
}
inline juce::String poem(const std::vector<LeadBar>& bars) {
    juce::String result; std::vector<PoemCell> line; int width=0,words=0;
    auto flush=[&] { if(!line.empty()) { result+=table(line)+paragraph(""); line.clear(); width=words=0; } };
    auto add=[&](const PoemCell& cell) {
        const auto tokens=juce::StringArray::fromTokens(cell.lyric," \t\n\r","");
        const int count=tokens.size(),length=juce::jmax(cell.chord.length(),cell.lyric.length())+2;
        if(!line.empty() && (width+length>70 || words+count>14)) flush();
        line.push_back(cell); width+=length; words+=count;
        if(cell.lyric.endsWithChar('.') || cell.lyric.endsWithChar('!') || cell.lyric.endsWithChar('?') || (words>=8 && cell.lyric.endsWithChar(','))) flush();
    };
    for(const auto& bar:bars) {
        juce::StringArray chordNames; for(const auto& chord:bar.chords) if(chord.isNotEmpty()) chordNames.add(chord);
        if(bar.lyrics.isEmpty() && chordNames.isEmpty()) { flush(); continue; }
        auto barWords=juce::StringArray::fromTokens(bar.lyrics," \t\n\r",""); barWords.removeEmptyStrings();
        if(words>=6 && words+barWords.size()>14) flush();
        std::vector<PoemCell> cells;
        if(bar.hasBeatLyrics()) {
            for(size_t i=0;i<bar.words.size();++i) if(bar.words[i].isNotEmpty() || bar.chords[i].isNotEmpty()) cells.push_back({bar.chords[i],bar.words[i]});
        } else cells.push_back({chordNames.joinIntoString("  "),bar.lyrics});
        int phraseWidth=0; for(const auto& cell:cells) phraseWidth+=juce::jmax(cell.chord.length(),cell.lyric.length())+2;
        if(!line.empty() && phraseWidth<=70 && (width+phraseWidth>70 || words+barWords.size()>14)) flush();
        for(const auto& cell:cells) {
            // Long corrected phrases wrap by words; retain the chord on the first piece.
            const auto lines=juce::StringArray::fromLines(cell.lyric); bool first=true;
            if(lines.isEmpty()) { add(cell); continue; }
            for(const auto& text:lines) {
                auto tokens=juce::StringArray::fromTokens(text," \t",""); tokens.removeEmptyStrings(); juce::String phrase;
                if(tokens.isEmpty() && first) { add({cell.chord,{}}); first=false; }
                for(const auto& word:tokens) {
                    if(phrase.isNotEmpty() && phrase.length()+word.length()>52) { add({first?cell.chord:juce::String{},phrase}); first=false; flush(); phrase.clear(); }
                    phrase+=(phrase.isEmpty()?"":" ")+word;
                }
                if(phrase.isNotEmpty()) { add({first?cell.chord:juce::String{},phrase}); first=false; }
                if(lines.size()>1) flush();
            }
        }
    }
    flush(); return result;
}
inline juce::String makeDocument(const std::vector<LeadBar>& bars,const juce::String& title,Style style=Style::detailed) {
    juce::String body;
    if(style==Style::basic) body=poem(bars);
    else {
        body=paragraph(title,true)+paragraph("Text and Chord Tracer | Editable draft - review sung words, chords and bar placement.");
        for(const auto& bar:bars) {
            body+=paragraph("Bar "+juce::String(bar.number+1)+"   "+juce::String(bar.numerator)+"/"+juce::String(bar.denominator)+"   ~"+juce::String(bar.bpm,1)+" BPM",true);
            if(bar.hasBeatLyrics()) {
                std::vector<PoemCell> cells; for(size_t i=0;i<bar.chords.size();++i) cells.push_back({bar.chords[i],bar.words[i]});
                body+=table(cells,true);
            } else {
                // The corrected phrase spans the measure; all chord cells remain separate.
                body+="<w:tbl><w:tblPr><w:tblW w:w=\"9000\" w:type=\"dxa\"/></w:tblPr><w:tblGrid>";
                for(int beat=0;beat<bar.numerator;++beat) body+="<w:gridCol w:w=\""+juce::String(9000/bar.numerator)+"\"/>";
                body+="</w:tblGrid><w:tr>";
                for(const auto& chord:bar.chords) body+="<w:tc><w:tcPr><w:tcW w:w=\""+juce::String(9000/bar.numerator)+"\" w:type=\"dxa\"/></w:tcPr>"+paragraph(chord,true)+"</w:tc>";
                body+="</w:tr><w:tr><w:tc><w:tcPr><w:gridSpan w:val=\""+juce::String(bar.numerator)+"\"/></w:tcPr>"+paragraph(bar.lyrics)+"</w:tc></w:tr></w:tbl>";
            }
            body+=paragraph("");
        }
    }
    return "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body>"+body+"<w:sectPr><w:pgSz w:w=\"11906\" w:h=\"16838\"/><w:pgMar w:top=\"1080\" w:right=\"1080\" w:bottom=\"1080\" w:left=\"1080\"/></w:sectPr></w:body></w:document>";
}
inline bool writeDocx(const juce::File& destination,const std::vector<LeadBar>& bars,const juce::String& title,Style style=Style::detailed) {
    juce::ZipFile::Builder zip;
    auto add=[&](const char* name,const juce::String& content) {
        zip.addEntry(new juce::MemoryInputStream(content.toRawUTF8(),content.getNumBytesAsUTF8(),true),6,name,juce::Time::getCurrentTime());
    };
    add("[Content_Types].xml","<?xml version=\"1.0\" encoding=\"UTF-8\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\" ContentType=\"application/xml\"/><Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/></Types>");
    add("_rels/.rels","<?xml version=\"1.0\" encoding=\"UTF-8\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/></Relationships>");
    add("word/document.xml",makeDocument(bars,title,style));
    juce::TemporaryFile temporary(destination); auto stream=temporary.getFile().createOutputStream();
    if(!stream || !zip.writeToStream(*stream,nullptr)) return false;
    stream->flush(); if(stream->getStatus().failed()) return false; stream.reset();
    return temporary.overwriteTargetFileWithTemporary();
}
}
