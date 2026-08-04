/*
 * history.cpp - Grow/shrink undo and redo stacks via Command objects
 *
 * execute() pushes onto undo_stack and clears redo_stack (a fresh edit
 * invalidates forward history); undo() moves the top command across, and
 * redo() plays it forward again. trimStack keeps each stack bounded.
 */
#include "history.hpp"
#include "../core/buffer.hpp"
#include <algorithm>

History::History(size_t max_levels) : max_levels(max_levels) {}

bool History::execute(CommandPtr cmd) {
    if (!cmd) return false;
    if (!cmd->execute()) return false;
    undo_stack.push_back(std::move(cmd));
    trimStack(undo_stack);
    redo_stack.clear();  // any new edit discards the redo path
    return true;
}

bool History::undo() {
    if (undo_stack.empty()) return false;
    CommandPtr cmd = std::move(undo_stack.back());
    undo_stack.pop_back();
    if (!cmd->undo()) return false;
    redo_stack.push_back(std::move(cmd));
    trimStack(redo_stack);
    return true;
}

bool History::redo() {
    if (redo_stack.empty()) return false;
    CommandPtr cmd = std::move(redo_stack.back());
    redo_stack.pop_back();
    if (!cmd->execute()) return false;
    undo_stack.push_back(std::move(cmd));
    trimStack(undo_stack);
    return true;
}

void History::trimStack(std::vector<CommandPtr>& stack) {
    if (stack.size() > max_levels)
        stack.erase(stack.begin(), stack.end() - max_levels);
}

InsertCommand::InsertCommand(Buffer* buf, const std::string& text, int line, int col)
    : buffer(buf), text(text), line(line), col(col) {}

bool InsertCommand::execute() {
    if (!buffer) return false;
    buffer->Insert(line, col, text);
    return true;
}

bool InsertCommand::undo() {
    if (!buffer) return false;
    buffer->Delete(line, col, (int)text.length());
    return true;
}

std::string InsertCommand::description() const { return "Insert: " + text; }

DeleteCommand::DeleteCommand(Buffer* buf, const std::string& text, int line, int col)
    : buffer(buf), text(text), line(line), col(col) {}

bool DeleteCommand::execute() {
    if (!buffer) return false;
    buffer->Delete(line, col, (int)text.length());
    return true;
}

bool DeleteCommand::undo() {
    if (!buffer) return false;
    buffer->Insert(line, col, text);
    return true;
}

std::string DeleteCommand::description() const { return "Delete: " + text; }

NewLineCommand::NewLineCommand(Buffer* buf, int insert_before_line,
                               const std::string& first_half, const std::string& second_half,
                               const std::string& orig_full_line)
    : buffer(buf), insert_before_line(insert_before_line),
      first_half(first_half), second_half(second_half), orig_full_line(orig_full_line) {}

bool NewLineCommand::execute() {
    if (!buffer) return false;
    if (insert_before_line < 1 || insert_before_line > buffer->GetLineCount()) return false;
    // Rewrite the split line and insert the dangling half below it.
    (*buffer)[insert_before_line - 1] = first_half;
    buffer->InsertLine(insert_before_line, second_half);
    return true;
}

bool NewLineCommand::undo() {
    if (!buffer) return false;
    if (insert_before_line >= buffer->GetLineCount()) return false;
    buffer->EraseLine(insert_before_line);
    // Restore the original unsplit line so undo/redo round-trips exactly.
    if (insert_before_line > 0 && insert_before_line - 1 < buffer->GetLineCount())
        (*buffer)[insert_before_line - 1] = orig_full_line;
    return true;
}

std::string NewLineCommand::description() const {
    return "NewLine at " + std::to_string(insert_before_line);
}
