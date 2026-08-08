// tests/history_test.cpp - History undo/redo tests
//
// Exercises the command pattern: insert/delete round-trips, the redo path
// being discarded by a fresh edit, and the bounded stack trimming.

#include <gtest/gtest.h>
#include "history/history.hpp"
#include "core/buffer.hpp"

TEST(HistoryTest, UndoStartsEmpty) {
    History hist;
    Buffer buf;
    EXPECT_FALSE(hist.undo());
    EXPECT_FALSE(hist.redo());
}

TEST(HistoryTest, ExecuteThenUndoRestoresBuffer) {
    History hist;
    Buffer buf;
    hist.execute(InsertCommand::make(buf, "hello", 0, 0));
    EXPECT_EQ(buf[0], "hello");
    EXPECT_TRUE(hist.undo());
    EXPECT_EQ(buf[0], "");
}

TEST(HistoryTest, RedoReappliesInsert) {
    History hist;
    Buffer buf;
    hist.execute(InsertCommand::make(buf, "hello", 0, 0));
    hist.undo();
    EXPECT_EQ(buf[0], "");
    EXPECT_TRUE(hist.redo());
    EXPECT_EQ(buf[0], "hello");
}

TEST(HistoryTest, InsertDeleteRoundTrip) {
    History hist;
    Buffer buf;
    buf.Insert(0, 0, "abcdef");
    hist.execute(DeleteCommand::make(buf, "bc", 0, 1));
    EXPECT_EQ(buf[0], "adef");
    hist.undo();
    EXPECT_EQ(buf[0], "abcdef");
    hist.redo();
    EXPECT_EQ(buf[0], "adef");
}

TEST(HistoryTest, NewEditDiscardsRedo) {
    History hist;
    Buffer buf;
    hist.execute(InsertCommand::make(buf, "a", 0, 0));
    hist.undo();
    hist.execute(InsertCommand::make(buf, "b", 0, 0));
    EXPECT_FALSE(hist.redo());
}

TEST(HistoryTest, UndoAfterEditReturnsToEachState) {
    History hist;
    Buffer buf;
    hist.execute(InsertCommand::make(buf, "a", 0, 0));
    hist.execute(InsertCommand::make(buf, "b", 0, 1));
    EXPECT_EQ(buf[0], "ab");
    hist.undo();
    EXPECT_EQ(buf[0], "a");
    hist.undo();
    EXPECT_EQ(buf[0], "");
    EXPECT_FALSE(hist.undo());
}

TEST(HistoryTest, NullCommandIsRejected) {
    History hist;
    Buffer buf;
    EXPECT_FALSE(hist.execute(nullptr));
    EXPECT_FALSE(hist.undo());
}

TEST(HistoryTest, TrimKeepsOnlyMaxLevels) {
    History hist(3);
    Buffer buf;
    for (int i = 0; i < 10; i++)
        hist.execute(InsertCommand::make(buf, "x", 0, 0));
    // 10 undos available, but only the last 3 survive trimming.
    int undos = 0;
    while (hist.undo()) undos++;
    EXPECT_EQ(undos, 3);
}

TEST(HistoryTest, NewLineCommandSplitsAndUndoes) {
    History hist;
    Buffer buf;
    buf[0] = "hello";
    hist.execute(NewLineCommand::make(buf, 1, "hel", "lo", "hello"));
    EXPECT_EQ(buf.GetLineCount(), 2);
    EXPECT_EQ(buf[0], "hel");
    EXPECT_EQ(buf[1], "lo");
    hist.undo();
    EXPECT_EQ(buf.GetLineCount(), 1);
    EXPECT_EQ(buf[0], "hello");
}

TEST(HistoryTest, DescriptionIsPopulated) {
    InsertCommand cmd(nullptr, "abc", 0, 0);
    EXPECT_EQ(cmd.description(), "Insert: abc");
    DeleteCommand dcmd(nullptr, "abc", 0, 0);
    EXPECT_EQ(dcmd.description(), "Delete: abc");
}

TEST(HistoryTest, PasteCommandSplicesMultiLine) {
    History hist;
    Buffer buf;
    buf[0] = "hello";
    hist.execute(PasteCommand::make(buf, "foo\nbar", 0, 2));
    ASSERT_EQ(buf.GetLineCount(), 2);
    EXPECT_EQ(buf[0], "hefoo");
    EXPECT_EQ(buf[1], "barllo");
    hist.undo();
    ASSERT_EQ(buf.GetLineCount(), 1);
    EXPECT_EQ(buf[0], "hello");
    hist.redo();
    ASSERT_EQ(buf.GetLineCount(), 2);
    EXPECT_EQ(buf[0], "hefoo");
    EXPECT_EQ(buf[1], "barllo");
}

TEST(HistoryTest, PasteCommandSingleLineBehavesLikeInsert) {
    History hist;
    Buffer buf;
    buf[0] = "abcdef";
    hist.execute(PasteCommand::make(buf, "XY", 0, 2));
    EXPECT_EQ(buf[0], "abXYcdef");
    hist.undo();
    EXPECT_EQ(buf[0], "abcdef");
}

TEST(HistoryTest, PasteCommandWithTrailingNewline) {
    History hist;
    Buffer buf;
    buf[0] = "one";
    hist.execute(PasteCommand::make(buf, "two\n", 0, 3));
    ASSERT_EQ(buf.GetLineCount(), 2);
    EXPECT_EQ(buf[0], "onetwo");
    EXPECT_EQ(buf[1], "");
    hist.undo();
    ASSERT_EQ(buf.GetLineCount(), 1);
    EXPECT_EQ(buf[0], "one");
}

TEST(HistoryTest, DeleteRangeSingleLine) {
    History hist;
    Buffer buf;
    buf[0] = "abcdef";
    hist.execute(DeleteRangeCommand::make(buf, 0, 2, 0, 4));
    EXPECT_EQ(buf[0], "abef");
    hist.undo();
    EXPECT_EQ(buf[0], "abcdef");
}

TEST(HistoryTest, DeleteRangeAcrossLines) {
    History hist;
    Buffer buf;
    buf[0] = "alpha";
    buf.PushBack("beta");
    buf.PushBack("gamma");
    // From 'p' of alpha (col 2) to 'm' of gamma (col 2), exclusive of the end
    // cursor: removes "pha\nbeta\nga" and keeps "al" + "mma".
    hist.execute(DeleteRangeCommand::make(buf, 0, 2, 2, 2));
    ASSERT_EQ(buf.GetLineCount(), 1);
    EXPECT_EQ(buf[0], "almma");
    hist.undo();
    ASSERT_EQ(buf.GetLineCount(), 3);
    EXPECT_EQ(buf[0], "alpha");
    EXPECT_EQ(buf[1], "beta");
    EXPECT_EQ(buf[2], "gamma");
}

TEST(HistoryTest, DeleteRangeReversedArguments) {
    History hist;
    Buffer buf;
    buf[0] = "abcdef";
    // Same span given in reverse order; normalization must handle it.
    hist.execute(DeleteRangeCommand::make(buf, 0, 4, 0, 2));
    EXPECT_EQ(buf[0], "abef");
    hist.undo();
    EXPECT_EQ(buf[0], "abcdef");
}

TEST(HistoryTest, DeleteRangeZeroWidthIsNoOp) {
    History hist;
    Buffer buf;
    buf[0] = "abcdef";
    hist.execute(DeleteRangeCommand::make(buf, 0, 3, 0, 3));
    EXPECT_EQ(buf[0], "abcdef");
}
