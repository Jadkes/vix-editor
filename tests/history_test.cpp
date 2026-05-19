// tests/history_test.cpp - History class unit tests
#include <gtest/gtest.h>
#include "history/history.hpp"
#include "core/buffer.hpp"

TEST(HistoryTest, InitialState)
{
    History hist;
    EXPECT_FALSE(hist.canUndo());
    EXPECT_FALSE(hist.canRedo());
    EXPECT_EQ(hist.undoSize(), 0u);
    EXPECT_EQ(hist.redoSize(), 0u);
}

TEST(HistoryTest, CanUndoAfterExecute)
{
    History hist;
    Buffer buf;
    hist.execute(std::make_unique<InsertCommand>(&buf, "hello", 0, 0));
    EXPECT_TRUE(hist.canUndo());
    EXPECT_FALSE(hist.canRedo());
}

TEST(HistoryTest, Undo)
{
    History hist;
    Buffer buf;
    hist.execute(std::make_unique<InsertCommand>(&buf, "hello", 0, 0));
    EXPECT_TRUE(hist.undo());
    EXPECT_FALSE(hist.canUndo());
    EXPECT_TRUE(hist.canRedo());
    EXPECT_EQ(hist.redoSize(), 1u);
}

TEST(HistoryTest, Redo)
{
    History hist;
    Buffer buf;
    hist.execute(std::make_unique<InsertCommand>(&buf, "hello", 0, 0));
    hist.undo();
    EXPECT_TRUE(hist.redo());
    EXPECT_TRUE(hist.canUndo());
    EXPECT_FALSE(hist.canRedo());
}

TEST(HistoryTest, UndoRedoStack)
{
    History hist;
    Buffer buf;
    buf.PushBack("");
    buf.PushBack("");
    hist.execute(std::make_unique<InsertCommand>(&buf, "a", 0, 0));
    hist.execute(std::make_unique<InsertCommand>(&buf, "b", 1, 0));
    EXPECT_EQ(hist.undoSize(), 2u);

    hist.undo();
    EXPECT_EQ(hist.undoSize(), 1u);
    EXPECT_EQ(hist.redoSize(), 1u);

    hist.redo();
    EXPECT_EQ(hist.undoSize(), 2u);
    EXPECT_EQ(hist.redoSize(), 0u);
}

TEST(HistoryTest, NewActionClearsRedo)
{
    History hist;
    Buffer buf;
    buf.PushBack("");
    hist.execute(std::make_unique<InsertCommand>(&buf, "a", 0, 0));
    hist.undo();
    EXPECT_EQ(hist.redoSize(), 1u);

    hist.execute(std::make_unique<InsertCommand>(&buf, "b", 1, 0));
    EXPECT_EQ(hist.redoSize(), 0u);
}

TEST(HistoryTest, Clear)
{
    History hist;
    Buffer buf;
    hist.execute(std::make_unique<InsertCommand>(&buf, "hello", 0, 0));
    hist.clear();
    EXPECT_FALSE(hist.canUndo());
    EXPECT_FALSE(hist.canRedo());
}

TEST(HistoryTest, MultipleUndo)
{
    History hist;
    Buffer buf;
    buf.PushBack("");
    buf.PushBack("");
    hist.execute(std::make_unique<InsertCommand>(&buf, "a", 0, 0));
    hist.execute(std::make_unique<InsertCommand>(&buf, "b", 1, 0));
    hist.execute(std::make_unique<InsertCommand>(&buf, "c", 2, 0));

    EXPECT_TRUE(hist.undo());
    EXPECT_TRUE(hist.undo());
    EXPECT_EQ(hist.undoSize(), 1u);
    EXPECT_EQ(hist.redoSize(), 2u);
}

TEST(HistoryTest, InsertCommandDescription)
{
    Buffer buf;
    InsertCommand cmd(&buf, "test", 0, 5);
    EXPECT_EQ(cmd.description(), "Insert: test");
}

TEST(HistoryTest, DeleteCommandDescription)
{
    Buffer buf;
    DeleteCommand cmd(&buf, "test", 0, 5);
    EXPECT_EQ(cmd.description(), "Delete: test");
}

TEST(HistoryTest, NewLineCommandDescription)
{
    Buffer buf;
    NewLineCommand cmd(&buf, 10, "");
    EXPECT_EQ(cmd.description(), "NewLine at 10");
}
