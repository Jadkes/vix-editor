#ifndef VIX_BUFFER_HPP
#define VIX_BUFFER_HPP

#include <string>
#include <vector>
#include <fstream>

class Buffer
{
public:
    Buffer();
    explicit Buffer(const std::string& filename);

    void LoadFile(const std::string& fname);
    void SaveFile();
    void Clear();

    void Insert(int line, int col, const std::string& text);
    void Delete(int line, int col, int count);

    std::string GetLine(int n) const;
    std::string& operator[](int n);
    const std::vector<std::string>& GetAllLines() const;
    int GetLineCount() const;
    bool IsEmpty() const;
    void PushBack(const std::string& line);
    void EraseLine(int n);
    void InsertLine(int n, const std::string& text);
    void AppendToLine(int n, const std::string& text);
    std::string GetFilename() const;
    bool IsModified() const;

    void ClearModified();
    void SetModified(bool value);
    void SetFilename(const std::string& fname);

private:
    std::vector<std::string> lines;
    std::string filename;
    bool modified;
};

struct Cursor {
    int line;
    int col;
};

#endif