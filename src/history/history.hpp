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
    bool canUndo() const;
    bool canRedo() const;
    void clear();
    size_t undoSize() const;
    size_t redoSize() const;
private:
    void trimStack(std::vector<CommandPtr>& stack);
    std::vector<CommandPtr> undo_stack;
    std::vector<CommandPtr> redo_stack;
    size_t max_levels;
};

class Buffer;

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

class NewLineCommand : public Command {
public:
    NewLineCommand(Buffer* buf, int insert_before_line, const std::string& second_half, const std::string& orig_first_half);
    bool execute() override;
    bool undo() override;
    std::string description() const override;
private:
    Buffer* buffer;
    int insert_before_line;
    std::string second_half;
    std::string orig_first_half;
};

#endif
