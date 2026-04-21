/*
 * History - Undo/Redo implementation
 *
 * Implements Command pattern for text editor undo/redo.
 */
#include "history.hpp"
#include "../core/buffer.hpp"
#include <algorithm>
#include <cassert>

History::History(size_t max_levels) : max_levels(max_levels) {}

bool History::execute(CommandPtr cmd)
{
    if (!cmd) return false;
    if (!cmd->execute()) return false;
    undo_stack.push_back(std::move(cmd));
    trimStack(undo_stack);
    redo_stack.clear();
    return true;
}

bool History::undo()
{
    if (undo_stack.empty()) return false;
    CommandPtr cmd = std::move(undo_stack.back());
    undo_stack.pop_back();
    if (!cmd->undo()) return false;
    redo_stack.push_back(std::move(cmd));
    trimStack(redo_stack);
    return true;
}

bool History::redo()
{
    if (redo_stack.empty()) return false;
    CommandPtr cmd = std::move(redo_stack.back());
    redo_stack.pop_back();
    if (!cmd->execute()) return false;
    undo_stack.push_back(std::move(cmd));
    trimStack(undo_stack);
    return true;
}

bool History::canUndo() const
{
    return !undo_stack.empty();
}
bool History::canRedo() const
{
    return !redo_stack.empty();
}
void History::clear()
{
    undo_stack.clear();
    redo_stack.clear();
}
size_t History::undoSize() const
{
    return undo_stack.size();
}
size_t History::redoSize() const
{
    return redo_stack.size();
}

void History::trimStack(std::vector<CommandPtr>& stack)
{
    if (stack.size() > max_levels) stack.erase(stack.begin(), stack.end() - max_levels);
}

InsertCommand::InsertCommand(Buffer* buf, const std::string& text, int line, int col)
    : buffer(buf), text(text), line(line), col(col) {}

bool InsertCommand::execute()
{
    if (!buffer) return false;
    buffer->Insert(line, col, text);
    return true;
}

bool InsertCommand::undo()
{
    if (!buffer) return false;
    buffer->Delete(line, col, (int)text.length());
    return true;
}

std::string InsertCommand::description() const
{
    return "Insert: " + text;
}

DeleteCommand::DeleteCommand(Buffer* buf, const std::string& text, int line, int col)
    : buffer(buf), text(text), line(line), col(col) {}

bool DeleteCommand::execute()
{
    if (!buffer) return false;
    buffer->Delete(line, col, (int)text.length());
    return true;
}

bool DeleteCommand::undo()
{
    if (!buffer) return false;
    buffer->Insert(line, col, text);
    return true;
}

std::string DeleteCommand::description() const
{
    return "Delete: " + text;
}

NewLineCommand::NewLineCommand(Buffer* buf, int insert_before_line, const std::string& second_half)
    : buffer(buf), insert_before_line(insert_before_line), second_half(second_half) {}

bool NewLineCommand::execute()
{
    if (!buffer) return false;
    buffer->InsertLine(insert_before_line, second_half);
    return true;
}

bool NewLineCommand::undo()
{
    if (!buffer) return false;
    if (insert_before_line >= buffer->GetLineCount()) return false;
    std::string remaining = (*buffer)[insert_before_line];
    buffer->EraseLine(insert_before_line);
    if (insert_before_line > 0) (*buffer)[insert_before_line - 1] += remaining;
    return true;
}

std::string NewLineCommand::description() const
{
    return "NewLine at " + std::to_string(insert_before_line);
}