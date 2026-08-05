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

PasteCommand::PasteCommand(Buffer* buf, const std::string& text, int line, int col)
    : buffer(buf), text(text), line(line), col(col), inserted_lines(0) {}

bool PasteCommand::execute() {
    if (!buffer) return false;
    if (line < 0 || line >= buffer->GetLineCount()) return false;
    if (col < 0) col = 0;
    if (col > (int)buffer->operator[](line).length()) col = (int)buffer->operator[](line).length();

    orig_line = buffer->operator[](line);
    inserted_lines = 0;

    // Split the pasted text into lines; the first piece lands at (line, col),
    // each following piece becomes a new line, and the tail of the original
    // line moves after the last piece so the paste is a true splice.
    std::string tail = orig_line.substr(col);
    buffer->operator[](line) = orig_line.substr(0, col);
    int target_line = line;
    size_t start = 0;
    while (true) {
        size_t nl = text.find('\n', start);
        std::string piece = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        buffer->operator[](target_line) += piece;
        if (nl == std::string::npos) break;
        target_line++;
        buffer->InsertLine(target_line, "");
        inserted_lines++;
        start = nl + 1;
    }
    buffer->operator[](target_line) += tail;
    buffer->SetModified(true);
    return true;
}

bool PasteCommand::undo() {
    if (!buffer) return false;
    if (line < 0 || line >= buffer->GetLineCount()) return false;

    // Remove the extra lines the paste created, then restore the original line.
    for (int i = 0; i < inserted_lines; i++) {
        if (line + 1 < buffer->GetLineCount()) buffer->EraseLine(line + 1);
    }
    if (line < buffer->GetLineCount()) buffer->operator[](line) = orig_line;
    return true;
}

std::string PasteCommand::description() const { return "Paste"; }

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
