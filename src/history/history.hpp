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
    bool execute() override;
    bool undo() override;
    std::string description() const override;
private:
    Buffer* buffer;
    std::string text;
    int line, col;
};

// Split the line above insert_before_line into first_half | second_half.
// execute() and undo() both rewrite the line, so redo reproduces the split.
class NewLineCommand : public Command {
public:
    NewLineCommand(Buffer* buf, int insert_before_line,
                   const std::string& first_half, const std::string& second_half,
                   const std::string& orig_full_line);
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
