// SPDX-License-Identifier: GPL-3.0-or-later
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
    if (!cmd->undo()) {
        // A failed undo must not lose the command: put it back so the stack
        // stays consistent and the user can retry or redo past it.
        undo_stack.push_back(std::move(cmd));
        return false;
    }
    redo_stack.push_back(std::move(cmd));
    trimStack(redo_stack);
    return true;
}

bool History::redo() {
    if (redo_stack.empty()) return false;
    CommandPtr cmd = std::move(redo_stack.back());
    redo_stack.pop_back();
    if (!cmd->execute()) {
        // Mirror of the undo failure path: keep the command on the redo stack.
        redo_stack.push_back(std::move(cmd));
        return false;
    }
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

DeleteRangeCommand::DeleteRangeCommand(Buffer* buf, int a_y, int a_x, int b_y, int b_x)
    : buffer(buf), a_y(a_y), a_x(a_x), b_y(b_y), b_x(b_x) {}

bool DeleteRangeCommand::execute() {
    if (!buffer) return false;
    int n = buffer->GetLineCount();
    // Normalize so (a_y,a_x) <= (b_y,b_x) in row-major order.
    if (b_y < a_y || (b_y == a_y && b_x < a_x)) {
        std::swap(a_y, b_y);
        std::swap(a_x, b_x);
    }
    if (a_y < 0) a_y = 0;
    if (b_y >= n) b_y = n - 1;
    if (b_y < a_y) return false;
    if (a_x < 0) a_x = 0;
    if (a_x > (int)buffer->operator[](a_y).length()) a_x = (int)buffer->operator[](a_y).length();
    if (b_x > (int)buffer->operator[](b_y).length()) b_x = (int)buffer->operator[](b_y).length();

    saved.clear();
    for (int i = a_y; i <= b_y; i++) saved.push_back(buffer->operator[](i));

    std::string head = saved[0].substr(0, a_x);
    std::string tail = saved.back().substr(b_x);
    // Join the surviving fragments onto line a_y, then drop the lines between.
    buffer->operator[](a_y) = head + tail;
    for (int i = 0; i < b_y - a_y; i++) buffer->EraseLine(a_y + 1);
    buffer->SetModified(true);
    return true;
}

bool DeleteRangeCommand::undo() {
    if (!buffer) return false;
    if (a_y < 0 || a_y >= buffer->GetLineCount()) return false;
    // Restore the original lines; insert the collapsed ones below a_y.
    buffer->operator[](a_y) = saved[0];
    for (size_t i = 1; i < saved.size(); i++) buffer->InsertLine(a_y + (int)i, saved[i]);
    buffer->SetModified(true);
    return true;
}

std::string DeleteRangeCommand::description() const { return "Delete selection"; }

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

JoinLinesCommand::JoinLinesCommand(Buffer* buf, int lower_line)
    : buffer(buf), lower_line(lower_line), split_point(0) {}

bool JoinLinesCommand::execute() {
    if (!buffer) return false;
    if (lower_line < 1 || lower_line >= buffer->GetLineCount()) return false;
    // Record the boundary once so redo after undo splits at the same spot.
    split_point = buffer->operator[](lower_line - 1).length();
    buffer->operator[](lower_line - 1) += buffer->operator[](lower_line);
    buffer->EraseLine(lower_line);
    return true;
}

bool JoinLinesCommand::undo() {
    if (!buffer) return false;
    if (lower_line < 1 || lower_line > buffer->GetLineCount()) return false;
    std::string& upper = buffer->operator[](lower_line - 1);
    if (split_point > upper.length()) return false;
    std::string lower = upper.substr(split_point);
    upper.resize(split_point);
    buffer->InsertLine(lower_line, lower);
    return true;
}

std::string JoinLinesCommand::description() const {
    return "Join line " + std::to_string(lower_line + 1);
}

void CompositeCommand::add(CommandPtr part) { parts.push_back(std::move(part)); }

bool CompositeCommand::execute() {
    for (size_t i = 0; i < parts.size(); i++) {
        if (!parts[i]->execute()) {
            // Roll back the prefix that did apply, so a failed composite
            // leaves the buffer exactly as it started.
            for (size_t j = i; j-- > 0;) parts[j]->undo();
            return false;
        }
    }
    return true;
}

bool CompositeCommand::undo() {
    // Reverse the parts; on failure keep going so as much state as possible
    // is restored, but report the composite as failed.
    bool all_ok = true;
    for (size_t i = parts.size(); i-- > 0;)
        if (!parts[i]->undo()) all_ok = false;
    return all_ok;
}

std::string CompositeCommand::description() const {
    return parts.empty() ? "No-op" : parts.front()->description();
}
