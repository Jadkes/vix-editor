/*
 * buffer.cpp - Line store for a single open document
 *
 * Keeps text as a std::vector<std::string> of lines (one entry per \n
 * terminator). The ends_with_newline flag records whether the source file
 * ended in a newline, and crlf records whether it used \r\n endings, so
 * SaveFile can round-trip byte-for-byte instead of silently appending,
 * dropping a trailing newline, or converting CRLF to LF.
 *
 * Not thread-safe; the editor only touches a buffer from the main loop.
 */
#include "buffer.hpp"

Buffer::Buffer() : filename(""), modified(false), ends_with_newline(false), crlf(false) { lines.push_back(""); }

void Buffer::LoadFile(const std::string& fname) {
    std::ifstream f(fname, std::ios::binary);
    // A missing/unreadable file opens as one empty line so the editor never
    // has to special-case a zero-line buffer.
    if (!f.is_open()) { lines.clear(); lines.push_back(""); filename = fname; return; }
    lines.clear(); filename = fname;
    // Peek the last byte to remember whether the file ends in a newline.
    f.seekg(0, std::ios::end);
    ends_with_newline = (f.tellg() > 0);
    if (ends_with_newline) {
        f.seekg(-1, std::ios::end);
        ends_with_newline = (f.get() == '\n');
    }
    f.seekg(0, std::ios::beg);
    // Collect raw lines first: getline splits on \n and leaves any \r behind,
    // but whether that \r was a CRLF marker or real content depends on whether
    // the line was actually newline-terminated.
    std::vector<std::string> raw;
    std::string l;
    while (std::getline(f, l)) raw.push_back(l);
    for (size_t i = 0; i < raw.size(); i++) {
        l = raw[i];
        // Every line getline returns is newline-terminated except the final
        // one when the file does not end with \n; a trailing \r there belongs
        // to the content, not to a line ending.
        bool terminated = (i + 1 < raw.size()) || ends_with_newline;
        if (!l.empty() && l.back() == '\r' && terminated) {
            // The first line decides the line-ending style so save can restore it.
            if (lines.empty()) crlf = true;
            l.pop_back();
        }
        lines.push_back(l);
    }
    if (lines.empty()) lines.push_back("");
    f.close();
    modified = false;
}

bool Buffer::SaveFile() {
    if (filename.empty()) return false;
    std::ofstream f(filename);
    if (!f.is_open()) return false;
    // Every line but the last gets a line ending; the last gets one only if the
    // original file ended with one. CRLF files keep their \r so the round-trip
    // is byte-for-byte instead of silently normalizing to LF.
    const char* nl = crlf ? "\r\n" : "\n";
    for (size_t i = 0; i < lines.size(); i++) {
        if (i + 1 < lines.size() || ends_with_newline)
            f << lines[i] << nl;
        else
            f << lines[i];
    }
    f.close();
    modified = false;
    return !f.fail();
}

void Buffer::Insert(int line, int col, const std::string& text) {
    if (line < 0 || line >= (int)lines.size()) return;
    // The line model has one entry per \n terminator; embedding a raw newline
    // in a "line" corrupts GetLineCount-based logic and the save path. Callers
    // that need to insert several lines use InsertLine / PasteCommand instead.
    if (text.find('\n') != std::string::npos) return;
    // Clamp so a stale cursor position from an undo/redo still lands in-bounds.
    if (col < 0) col = 0;
    if (col > (int)lines[line].length()) col = (int)lines[line].length();
    lines[line].insert(col, text);
    modified = true;
}

void Buffer::Delete(int line, int col, int count) {
    if (line < 0 || line >= (int)lines.size()) return;
    if (col < 0 || col >= (int)lines[line].length()) return;
    if (col + count > (int)lines[line].length()) count = (int)lines[line].length() - col;
    lines[line].erase(col, count);
    modified = true;
}

std::string& Buffer::operator[](int n) {
    // Throws rather than returning a dangling reference so bugs surface as a
    // caught exception instead of silent corruption.
    if (n < 0 || n >= (int)lines.size())
        throw std::out_of_range("Buffer::operator[]: index " + std::to_string(n) + " out of range");
    return lines[n];
}

int Buffer::GetLineCount() const { return (int)lines.size(); }
const std::string& Buffer::GetFilename() const { return filename; }
bool Buffer::IsModified() const { return modified; }
void Buffer::SetModified(bool value) { modified = value; }
void Buffer::SetFilename(const std::string& fname) { filename = fname; }
void Buffer::PushBack(const std::string& line) { lines.push_back(line); }

void Buffer::EraseLine(int n) {
    if (n < 0 || n >= (int)lines.size()) return;
    lines.erase(lines.begin() + n);
    // Never leave the document empty of lines; callers assume at least one.
    if (lines.empty()) lines.push_back("");
    modified = true;
}

void Buffer::InsertLine(int n, const std::string& text) {
    if (n < 0 || n > (int)lines.size()) return;
    lines.insert(lines.begin() + n, text);
    modified = true;
}
