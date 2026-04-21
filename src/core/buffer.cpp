#include "buffer.hpp"

Buffer::Buffer() : filename(""), modified(false)
{
    lines.push_back("");
}

Buffer::Buffer(const std::string& filename) : filename(filename), modified(false)
{
    LoadFile(filename);
}

void Buffer::LoadFile(const std::string& fname)
{
    std::ifstream f(fname);
    if (!f.is_open()) {
        lines.clear();
        lines.push_back("");
        filename = fname;
        return;
    }
    lines.clear();
    filename = fname;
    std::string l;
    while (std::getline(f, l)) {
        lines.push_back(l);
    }
    f.close();
    if (lines.empty()) {
        lines.push_back("");
    }
    modified = false;
}

void Buffer::SaveFile()
{
    if (filename.empty()) {
        return;
    }
    std::ofstream f(filename);
    if (f.is_open()) {
        for (auto& l : lines) {
            f << l << "\n";
        }
        f.close();
        modified = false;
    }
}

void Buffer::Clear()
{
    lines.clear();
    lines.push_back("");
    modified = false;
}

void Buffer::Insert(int line, int col, const std::string& text)
{
    if (line < 0 || line >= (int)lines.size()) return;
    if (col < 0) col = 0;
    if (col > (int)lines[line].length()) col = lines[line].length();
    lines[line].insert(col, text);
    modified = true;
}

void Buffer::Delete(int line, int col, int count)
{
    if (line < 0 || line >= (int)lines.size()) return;
    if (col < 0) return;
    if (col >= (int)lines[line].length()) return;
    if (col + count > (int)lines[line].length()) {
        count = lines[line].length() - col;
    }
    lines[line].erase(col, count);
    modified = true;
}

std::string Buffer::GetLine(int n) const
{
    if (n < 0 || n >= (int)lines.size()) {
        return "";
    }
    return lines[n];
}

std::string& Buffer::operator[](int n)
{
    return lines[n];
}

const std::vector<std::string>& Buffer::GetAllLines() const
{
    return lines;
}

int Buffer::GetLineCount() const
{
    return (int)lines.size();
}

bool Buffer::IsEmpty() const
{
    return lines.empty();
}

std::string Buffer::GetFilename() const
{
    return filename;
}

bool Buffer::IsModified() const
{
    return modified;
}

void Buffer::ClearModified()
{
    modified = false;
}

void Buffer::SetModified(bool value)
{
    modified = value;
}

void Buffer::SetFilename(const std::string& fname)
{
    filename = fname;
}

void Buffer::PushBack(const std::string& line)
{
    lines.push_back(line);
}

void Buffer::EraseLine(int n)
{
    if (n < 0 || n >= (int)lines.size()) return;
    lines.erase(lines.begin() + n);
    if (lines.empty()) {
        lines.push_back("");
    }
    modified = true;
}

void Buffer::InsertLine(int n, const std::string& text)
{
    if (n < 0 || n > (int)lines.size()) return;
    lines.insert(lines.begin() + n, text);
    modified = true;
}

void Buffer::AppendToLine(int n, const std::string& text)
{
    if (n < 0 || n >= (int)lines.size()) return;
    lines[n] += text;
    modified = true;
}