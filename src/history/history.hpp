/*
 * history.hpp - Undo/redo via the Command pattern
 *
 * Every edit is wrapped in a Command so execute() and undo() are each
 * other's inverse. History owns two bounded stacks (undo/redo) and trims
 * the oldest entries once a cap is hit. Commands hold a plain Buffer*
 * owned by the owning Tab, which outlives the history.
 */
#ifndef VIX_HISTORY_HPP
#define VIX_HISTORY_HPP
#include <memory>
#include <vector>
#include <string>

class Command {
public:
    virtual ~Command() = default;
    virtual bool execute() = 0;
    virtual bool undo() = 0;
    virtual std::string description() const = 0;
};

using CommandPtr = std::unique_ptr<Command>;

class History {
public:
    explicit History(size_t max_levels = 100);
    bool execute(CommandPtr cmd);
    bool undo();
    bool redo();
private:
    void trimStack(std::vector<CommandPtr>& stack);  // drop oldest past max_levels
    std::vector<CommandPtr> undo_stack;
    std::vector<CommandPtr> redo_stack;
    size_t max_levels;
};

class Buffer;

// Insert text at (line, col); undo deletes exactly that span.
class InsertCommand : public Command {
public:
    InsertCommand(Buffer* buf, const std::string& text, int line, int col);
    // Factory: the command borrows a Buffer the caller owns and outlives.
    static CommandPtr make(Buffer& buf, std::string text, int line, int col) {
        return std::make_unique<InsertCommand>(&buf, std::move(text), line, col);
    }
    bool execute() override;
    bool undo() override;
    std::string description() const override;
private:
    Buffer* buffer;
    std::string text;
    int line, col;
};

// Mirror of InsertCommand: delete a span; undo re-inserts it verbatim.
class DeleteCommand : public Command {
public:
    DeleteCommand(Buffer* buf, const std::string& text, int line, int col);
    static CommandPtr make(Buffer& buf, std::string text, int line, int col) {
        return std::make_unique<DeleteCommand>(&buf, std::move(text), line, col);
    }
    bool execute() override;
    bool undo() override;
    std::string description() const override;
private:
    Buffer* buffer;
    std::string text;
    int line, col;
};

// Paste text that may span multiple lines at (line, col). execute() splits
// the text on '\n', splices the pieces into the buffer and keeps the tail of
// the original line after the last piece; undo() reverses all of it.
class PasteCommand : public Command {
public:
    PasteCommand(Buffer* buf, const std::string& text, int line, int col);
    static CommandPtr make(Buffer& buf, std::string text, int line, int col) {
        return std::make_unique<PasteCommand>(&buf, std::move(text), line, col);
    }
    bool execute() override;
    bool undo() override;
    std::string description() const override;
private:
    Buffer* buffer;
    std::string text;
    int line, col;
    std::string orig_line;  // full content of the line before the paste
    int inserted_lines;     // how many extra lines execute() added
};

// Delete a span that may cross lines: from (a_y,a_x) inclusive to (b_y,b_x)
// exclusive, normalized so a <= b. execute() stitches the surviving pieces
// together; undo() splices the captured lines back verbatim.
class DeleteRangeCommand : public Command {
public:
    DeleteRangeCommand(Buffer* buf, int a_y, int a_x, int b_y, int b_x);
    static CommandPtr make(Buffer& buf, int a_y, int a_x, int b_y, int b_x) {
        return std::make_unique<DeleteRangeCommand>(&buf, a_y, a_x, b_y, b_x);
    }
    bool execute() override;
    bool undo() override;
    std::string description() const override;
private:
    Buffer* buffer;
    int a_y, a_x, b_y, b_x;
    std::vector<std::string> saved;  // full original lines a_y..b_y inclusive
};

// Split the line above insert_before_line into first_half | second_half.
// execute() and undo() both rewrite the line, so redo reproduces the split.
class NewLineCommand : public Command {
public:
    NewLineCommand(Buffer* buf, int insert_before_line,
                   const std::string& first_half, const std::string& second_half,
                   const std::string& orig_full_line);
    static CommandPtr make(Buffer& buf, int insert_before_line,
                           std::string first_half, std::string second_half,
                           std::string orig_full_line) {
        return std::make_unique<NewLineCommand>(&buf, insert_before_line,
                                                std::move(first_half), std::move(second_half),
                                                std::move(orig_full_line));
    }
    bool execute() override;
    bool undo() override;
    std::string description() const override;
private:
    Buffer* buffer;
    int insert_before_line;
    std::string first_half;
    std::string second_half;
    std::string orig_full_line;
};

#endif
