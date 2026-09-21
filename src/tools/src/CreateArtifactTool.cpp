#include "jarvis/tools/CreateArtifactTool.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QtEndian>
#include <windows.h>
#include <vector>
namespace jarvis::tools {
namespace {
QString xml(QString s) { s.replace('&',"&amp;"); s.replace('<',"&lt;"); s.replace('>',"&gt;"); s.replace('"',"&quot;"); return s; }
QString runs(const QString& text) {
    QString result; const auto chunks=text.split("**");
    for (qsizetype i=0;i<chunks.size();++i) result += "<w:r>" + QString(i%2 ? "<w:rPr><w:b/></w:rPr>" : "") + "<w:t xml:space=\"preserve\">" + xml(chunks[i]) + "</w:t></w:r>";
    return result;
}
void u16(QByteArray& b, quint16 v) { const auto n=qToLittleEndian(v); b.append(reinterpret_cast<const char*>(&n),2); }
void u32(QByteArray& b, quint32 v) { const auto n=qToLittleEndian(v); b.append(reinterpret_cast<const char*>(&n),4); }
quint32 crc(const QByteArray& bytes) { quint32 c=0xffffffff; for (unsigned char b : bytes) { c^=b; for(int i=0;i<8;++i) c=(c>>1)^((c&1)?0xedb88320:0); } return c^0xffffffff; }
QByteArray zip(const std::vector<std::pair<QByteArray,QByteArray>>& entries) {
    QByteArray data,central;
    for(const auto& [name,body] : entries) {
        const auto offset=static_cast<quint32>(data.size()), size=static_cast<quint32>(body.size()), checksum=crc(body);
        u32(data,0x04034b50); u16(data,20); u16(data,0); u16(data,0); u16(data,0); u16(data,33);
        u32(data,checksum); u32(data,size); u32(data,size); u16(data,static_cast<quint16>(name.size())); u16(data,0); data+=name; data+=body;
        u32(central,0x02014b50); u16(central,20); u16(central,20); u16(central,0); u16(central,0); u16(central,0); u16(central,33);
        u32(central,checksum); u32(central,size); u32(central,size); u16(central,static_cast<quint16>(name.size()));
        u16(central,0); u16(central,0); u16(central,0); u16(central,0); u32(central,0); u32(central,offset); central+=name;
    }
    const auto offset=static_cast<quint32>(data.size()); data+=central;
    u32(data,0x06054b50); u16(data,0); u16(data,0); u16(data,static_cast<quint16>(entries.size())); u16(data,static_cast<quint16>(entries.size()));
    u32(data,static_cast<quint32>(central.size())); u32(data,offset); u16(data,0); return data;
}
bool reparse(const QString& path) { const DWORD a=GetFileAttributesW(reinterpret_cast<LPCWSTR>(path.utf16())); return a!=INVALID_FILE_ATTRIBUTES && (a&FILE_ATTRIBUTE_REPARSE_POINT); }
}
QByteArray makeWordDocument(const QString& title,const QString& content) {
    QString document=QStringLiteral(R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?><w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><w:body><w:p><w:pPr><w:pStyle w:val="Title"/></w:pPr>)")+runs(title)+"</w:p>";
    for(QString line : content.split('\n')) {
        line=line.trimmed(); if(line.isEmpty()) continue;
        QString style="Body", extra;
        if(line.startsWith("#")) { style="Heading1"; line.remove(QRegularExpression("^#{1,6}\\s*")); if(line==title) continue; }
        else if(line.startsWith("- ") || line.startsWith("* ")) { line.remove(0,2); extra="<w:numPr><w:ilvl w:val=\"0\"/><w:numId w:val=\"1\"/></w:numPr>"; }
        document+="<w:p><w:pPr><w:pStyle w:val=\""+style+"\"/>"+extra+"</w:pPr>"+runs(line)+"</w:p>";
    }
    document+=R"(<w:sectPr><w:footerReference w:type="default" r:id="footer"/><w:pgSz w:w="12240" w:h="15840"/><w:pgMar w:top="1440" w:right="1440" w:bottom="1440" w:left="1440" w:header="720" w:footer="720"/></w:sectPr></w:body></w:document>)";
    const QByteArray styles=R"(<?xml version="1.0" encoding="UTF-8"?><w:styles xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii="Calibri" w:hAnsi="Calibri"/><w:sz w:val="24"/><w:color w:val="243342"/><w:lang w:val="ru-RU"/></w:rPr></w:rPrDefault><w:pPrDefault><w:pPr><w:spacing w:after="140" w:line="276" w:lineRule="auto"/><w:widowControl/></w:pPr></w:pPrDefault></w:docDefaults><w:style w:type="paragraph" w:default="1" w:styleId="Body"><w:name w:val="Normal"/></w:style><w:style w:type="paragraph" w:styleId="Title"><w:name w:val="Title"/><w:pPr><w:keepNext/><w:spacing w:before="0" w:after="300"/></w:pPr><w:rPr><w:b/><w:sz w:val="52"/><w:color w:val="173D60"/></w:rPr></w:style><w:style w:type="paragraph" w:styleId="Heading1"><w:name w:val="Heading 1"/><w:basedOn w:val="Body"/><w:next w:val="Body"/><w:pPr><w:keepNext/><w:keepLines/><w:spacing w:before="260" w:after="120"/><w:outlineLvl w:val="0"/></w:pPr><w:rPr><w:b/><w:sz w:val="32"/><w:color w:val="173D60"/></w:rPr></w:style></w:styles>)";
    const QByteArray numbering=R"(<?xml version="1.0" encoding="UTF-8"?><w:numbering xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:abstractNum w:abstractNumId="0"><w:lvl w:ilvl="0"><w:start w:val="1"/><w:numFmt w:val="bullet"/><w:lvlText w:val="&#8226;"/><w:lvlJc w:val="left"/><w:pPr><w:tabs><w:tab w:val="num" w:pos="360"/></w:tabs><w:ind w:left="360" w:hanging="220"/></w:pPr></w:lvl></w:abstractNum><w:num w:numId="1"><w:abstractNumId w:val="0"/></w:num></w:numbering>)";
    return zip({
        {"[Content_Types].xml",R"(<?xml version="1.0"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/><Override PartName="/word/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml"/><Override PartName="/word/numbering.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml"/><Override PartName="/word/footer.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml"/></Types>)"},
        {"_rels/.rels",R"(<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="doc" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/></Relationships>)"},
        {"word/document.xml",document.toUtf8()}, {"word/styles.xml",styles}, {"word/numbering.xml",numbering},
        {"word/_rels/document.xml.rels",R"(<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="styles" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/><Relationship Id="numbering" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/numbering" Target="numbering.xml"/><Relationship Id="footer" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/footer" Target="footer.xml"/></Relationships>)"},
        {"word/footer.xml",R"(<?xml version="1.0"?><w:ftr xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main"><w:p><w:pPr><w:jc w:val="center"/></w:pPr><w:r><w:rPr><w:sz w:val="18"/><w:color w:val="64748B"/></w:rPr><w:fldChar w:fldCharType="begin"/></w:r><w:r><w:instrText> PAGE </w:instrText></w:r><w:r><w:fldChar w:fldCharType="end"/></w:r></w:p></w:ftr>)"}
    });
}
QString CreateArtifactTool::defaultRoot() { return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)+"/JARVIS"; }
CreateArtifactTool::CreateArtifactTool(Kind kind,QString root) : m_kind(kind),m_root(root.isEmpty()?defaultRoot():root) {
    m_definition.name=kind==Kind::Folder?"create_folder":kind==Kind::Word?"create_word_document":"create_text_file";
    m_definition.permission=PermissionLevel::SafeAction; m_definition.timeoutMs=10000;
    m_definition.description="Create a NEW artifact inside the user's Documents/JARVIS folder. Never overwrites. Return the real saved path. Relative names only; parent folders must already exist. ";
    if(kind==Kind::Word) m_definition.description+="Creates a styled, genuine .docx. Supply a short title and complete Russian content when requested in Russian. Content supports # section headings, - bullets and **bold**. Write connected informative paragraphs, no invented sources. Do not use run_shell for Word creation.";
    if(kind==Kind::Text) m_definition.description+="Write the requested text or source code exactly, without markdown fences for code. Code is saved, never executed.";
    ArgumentSpec arg; arg.name="name";arg.type=ArgumentType::Text;arg.required=true;arg.maxTextChars=240;m_definition.arguments.push_back(arg);
    if(kind!=Kind::Folder) { arg.name="content";arg.maxTextChars=32000;m_definition.arguments.push_back(arg); }
    if(kind==Kind::Word) { arg.name="title";arg.maxTextChars=200;m_definition.arguments.push_back(arg); }
}
ToolResult CreateArtifactTool::execute(const ValidatedCall& call,const std::atomic<bool>& cancelled) {
    const auto failure=[&](const std::string& message){return ToolResult::failure(call.toolName(),ToolErrorCode::ExecutionFailed,message);};
    if(cancelled.load()) return ToolResult::failure(call.toolName(),ToolErrorCode::Cancelled,"Cancelled before creating the artifact");
    QString name=QString::fromStdString(call.textArgument("name")).replace('\\','/');
    if(name.isEmpty()||QDir::isAbsolutePath(name)||name.size()>240) return failure("Use a relative file name inside Documents/JARVIS.");
    for(const auto& part:name.split('/')) {
        if(part.isEmpty()||part=="."||part==".."||part.endsWith('.')||part.endsWith(' ')||part.contains(QRegularExpression("[<>:\"|?*\\x00-\\x1f]"))||QRegularExpression("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)",QRegularExpression::CaseInsensitiveOption).match(part).hasMatch()) return failure("The name contains an invalid or reserved path component.");
    }
    if(m_kind==Kind::Word&&!name.endsWith(".docx",Qt::CaseInsensitive)) name+=".docx";
    if(!QDir{}.mkpath(m_root)||reparse(m_root)) return failure("Cannot use the artifact folder.");
    QString parent=m_root;const auto parts=name.split('/');
    for(qsizetype i=0;i<parts.size()-1;++i){parent+='/'+parts[i];if(!QFileInfo(parent).isDir()||reparse(parent)) return failure("Create the parent folder first; links are not supported.");}
    const QString path=QDir(m_root).filePath(name);
    if(QFileInfo::exists(path)||reparse(path)) return failure("A file or folder with this name already exists. Choose a new name; nothing was overwritten.");
    if(m_kind==Kind::Folder) { if(!QDir(parent).mkdir(parts.last())) return failure("Could not create folder."); }
    else {
        const QString content=QString::fromStdString(call.textArgument("content"));
        if(m_kind==Kind::Word&&(content.contains(QRegularExpression("[\\x00-\\x08\\x0b\\x0c\\x0e-\\x1f]"))||QString::fromStdString(call.textArgument("title")).contains(QRegularExpression("[\\x00-\\x1f]")))) return failure("Document contains unsupported control characters.");
        const QByteArray bytes=m_kind==Kind::Word?makeWordDocument(QString::fromStdString(call.textArgument("title")),content):content.toUtf8();
        if(cancelled.load()) return ToolResult::failure(call.toolName(),ToolErrorCode::Cancelled,"Cancelled before writing");
        QFile file(path); if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly))return failure("Cannot create file; it may already exist.");
        if(file.write(bytes)!=bytes.size()||!file.flush()){file.close();file.remove();return failure("Could not finish writing the file.");} file.close();
    }
    return ToolResult::success(call.toolName(),{{"path",QDir::toNativeSeparators(path).toStdString()},{"status","created"}});
}
}
