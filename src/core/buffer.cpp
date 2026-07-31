#include "buffer.hpp"

Buffer::Buffer() : filename(""), modified(false), ends_with_newline(false) { lines.push_back(""); }

Buffer::Buffer(const std::string& fname) : filename(fname), modified(false), ends_with_newline(false) { LoadFile(fname); }

void Buffer::LoadFile(const std::string& fname) {
    std::ifstream f(fname, std::ios::binary);
    if (!f.is_open()) { lines.clear(); lines.push_back(""); filename = fname; return; }
    lines.clear(); filename = fname;
    f.seekg(0, std::ios::end);
    ends_with_newline = (f.tellg() > 0);
    if (ends_with_newline) {
        f.seekg(-1, std::ios::end);
        ends_with_newline = (f.get() == '\n');
    }
    f.seekg(0, std::ios::beg);
    std::string l;
    while (std::getline(f, l)) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
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
    for (size_t i = 0; i < lines.size(); i++) {
        if (i + 1 < lines.size() || ends_with_newline)
            f << lines[i] << "\n";
        else
            f << lines[i];
    }
    f.close();
    modified = false;
    return !f.fail();
}

void Buffer::Clear() { lines.clear(); lines.push_back(""); modified = false; }

void Buffer::Insert(int line, int col, const std::string& text) {
    if (line < 0 || line >= (int)lines.size()) return;
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

std::string Buffer::GetLine(int n) const {
    if (n < 0 || n >= (int)lines.size()) return "";
    return lines[n];
}

std::string& Buffer::operator[](int n) {
    if (n < 0 || n >= (int)lines.size())
        throw std::out_of_range("Buffer::operator[]: index " + std::to_string(n) + " out of range");
    return lines[n];
}

const std::vector<std::string>& Buffer::GetAllLines() const { return lines; }
int Buffer::GetLineCount() const { return (int)lines.size(); }
bool Buffer::IsEmpty() const { return lines.empty(); }
std::string Buffer::GetFilename() const { return filename; }
bool Buffer::IsModified() const { return modified; }
void Buffer::ClearModified() { modified = false; }
void Buffer::SetModified(bool value) { modified = value; }
void Buffer::SetFilename(const std::string& fname) { filename = fname; }
void Buffer::PushBack(const std::string& line) { lines.push_back(line); }

void Buffer::EraseLine(int n) {
    if (n < 0 || n >= (int)lines.size()) return;
    lines.erase(lines.begin() + n);
    if (lines.empty()) lines.push_back("");
    modified = true;
}

void Buffer::InsertLine(int n, const std::string& text) {
    if (n < 0 || n > (int)lines.size()) return;
    lines.insert(lines.begin() + n, text);
    modified = true;
}

void Buffer::AppendToLine(int n, const std::string& text) {
    if (n < 0 || n >= (int)lines.size()) return;
    lines[n] += text;
    modified = true;
}
