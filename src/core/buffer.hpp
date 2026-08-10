/*
 * buffer.hpp - Line store for a single open document
 *
 * A Buffer owns the lines of one file plus its name and dirty flag. It is
 * the single unit editors read/write; History commands mutate a Buffer.
 * Deliberately free of ncurses so it can be tested standalone.
 */
#ifndef VIX_BUFFER_HPP
#define VIX_BUFFER_HPP
#include <string>
#include <vector>
#include <fstream>
#include <stdexcept>

class Buffer {
public:
    Buffer();

    void LoadFile(const std::string& fname);
    bool SaveFile();

    void Insert(int line, int col, const std::string& text);
    void Delete(int line, int col, int count);

    std::string& operator[](int n);
    int GetLineCount() const;
    void PushBack(const std::string& line);
    void EraseLine(int n);
    void InsertLine(int n, const std::string& text);
    const std::string& GetFilename() const;
    bool IsModified() const;

    void SetModified(bool value);
    void SetFilename(const std::string& fname);

private:
    std::vector<std::string> lines;
    std::string filename;
    bool modified;
    bool ends_with_newline;
    bool crlf;
};

#endif
