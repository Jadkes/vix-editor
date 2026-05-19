// tests/buffer_test.cpp - Buffer class unit tests
//
// Purpose: Unit tests for Buffer editor functionality
// Covers all public methods of the Buffer class

#include <gtest/gtest.h>
#include "core/buffer.hpp"
#include <stdexcept>

TEST(BufferTest, DefaultConstruction)
{
    Buffer buf;
    EXPECT_EQ(buf.GetLineCount(), 1);
    EXPECT_EQ(buf.GetLine(0), "");
    EXPECT_FALSE(buf.IsModified());
    EXPECT_EQ(buf.GetFilename(), "");
}

TEST(BufferTest, PushBack)
{
    Buffer buf;
    buf.PushBack("hello");
    EXPECT_EQ(buf.GetLineCount(), 2);
    EXPECT_EQ(buf.GetLine(0), "");
    EXPECT_EQ(buf.GetLine(1), "hello");
}

TEST(BufferTest, InsertLine)
{
    Buffer buf;
    buf.PushBack("line2");
    buf.InsertLine(0, "inserted");
    EXPECT_EQ(buf.GetLineCount(), 3);
    EXPECT_EQ(buf.GetLine(0), "inserted");
    EXPECT_EQ(buf.GetLine(1), "");
    EXPECT_EQ(buf.GetLine(2), "line2");
}

TEST(BufferTest, InsertLineAtEnd)
{
    Buffer buf;
    buf.InsertLine(1, "last");
    EXPECT_EQ(buf.GetLineCount(), 2);
    EXPECT_EQ(buf.GetLine(0), "");
    EXPECT_EQ(buf.GetLine(1), "last");
}

TEST(BufferTest, EraseLine)
{
    Buffer buf;
    buf.PushBack("line2");
    buf.PushBack("line3");
    buf.EraseLine(1);
    EXPECT_EQ(buf.GetLineCount(), 2);
    EXPECT_EQ(buf.GetLine(0), "");
    EXPECT_EQ(buf.GetLine(1), "line3");
}

TEST(BufferTest, EraseLineLastRemaining)
{
    Buffer buf;
    buf.EraseLine(0);
    EXPECT_EQ(buf.GetLineCount(), 1);
    EXPECT_EQ(buf.GetLine(0), "");
}

TEST(BufferTest, AppendToLine)
{
    Buffer buf;
    buf.PushBack("hello");
    buf.AppendToLine(1, " world");
    EXPECT_EQ(buf.GetLine(1), "hello world");
}

TEST(BufferTest, AppendToLineInvalidIndex)
{
    Buffer buf;
    buf.AppendToLine(999, "text");
    EXPECT_EQ(buf.GetLineCount(), 1);
}

TEST(BufferTest, OperatorBrackets)
{
    Buffer buf;
    buf.PushBack("hello");
    EXPECT_EQ(buf[0], "");
    EXPECT_EQ(buf[1], "hello");
    buf[1] = "modified";
    EXPECT_EQ(buf.GetLine(1), "modified");
}

TEST(BufferTest, OperatorBracketsOutOfRangeNegative)
{
    Buffer buf;
    EXPECT_THROW(buf[-1], std::out_of_range);
}

TEST(BufferTest, OperatorBracketsOutOfRangeTooHigh)
{
    Buffer buf;
    EXPECT_THROW(buf[5], std::out_of_range);
}

TEST(BufferTest, Insert)
{
    Buffer buf;
    buf.PushBack("hello");
    buf.Insert(1, 0, ">");
    EXPECT_EQ(buf.GetLine(1), ">hello");
}

TEST(BufferTest, InsertAtEnd)
{
    Buffer buf;
    buf.PushBack("hello");
    buf.Insert(1, 5, "!");
    EXPECT_EQ(buf.GetLine(1), "hello!");
}

TEST(BufferTest, InsertClampCol)
{
    Buffer buf;
    buf.PushBack("hello");
    buf.Insert(1, -5, "X");
    EXPECT_EQ(buf.GetLine(1), "Xhello");
    buf.Insert(1, 999, "Y");
    EXPECT_EQ(buf.GetLine(1), "XhelloY");
}

TEST(BufferTest, InsertInvalidLine)
{
    Buffer buf;
    buf.Insert(999, 0, "text");
    EXPECT_EQ(buf.GetLineCount(), 1);
}

TEST(BufferTest, Delete)
{
    Buffer buf;
    buf.PushBack("hello world");
    buf.Delete(1, 0, 6);
    EXPECT_EQ(buf.GetLine(1), "world");
}

TEST(BufferTest, DeleteClampCount)
{
    Buffer buf;
    buf.PushBack("ab");
    buf.Delete(1, 0, 100);
    EXPECT_EQ(buf.GetLine(1), "");
}

TEST(BufferTest, DeleteInvalidParams)
{
    Buffer buf;
    buf.PushBack("hello");

    buf.Delete(1, -1, 2);
    EXPECT_EQ(buf.GetLine(1), "hello");

    buf.Delete(999, 0, 2);
    EXPECT_EQ(buf.GetLine(1), "hello");
    EXPECT_EQ(buf.GetLineCount(), 2);

    buf.Delete(1, 50, 2);
    EXPECT_EQ(buf.GetLine(1), "hello");
}

TEST(BufferTest, Clear)
{
    Buffer buf;
    buf.PushBack("data");
    buf.InsertLine(0, "more");
    EXPECT_EQ(buf.GetLineCount(), 3);
    buf.SetModified(true);
    buf.Clear();
    EXPECT_EQ(buf.GetLineCount(), 1);
    EXPECT_EQ(buf.GetLine(0), "");
    EXPECT_FALSE(buf.IsModified());
}

TEST(BufferTest, GetAllLines)
{
    Buffer buf;
    buf.PushBack("a");
    buf.PushBack("b");
    const auto& all = buf.GetAllLines();
    ASSERT_EQ(all.size(), 3);
    EXPECT_EQ(all[0], "");
    EXPECT_EQ(all[1], "a");
    EXPECT_EQ(all[2], "b");
}

TEST(BufferTest, IsEmptyDefault)
{
    Buffer buf;
    EXPECT_FALSE(buf.IsEmpty());
}

TEST(BufferTest, GetLineSafeReturnsEmpty)
{
    Buffer buf;
    EXPECT_EQ(buf.GetLine(-1), "");
    EXPECT_EQ(buf.GetLine(100), "");
}

TEST(BufferTest, SetFilename)
{
    Buffer buf;
    EXPECT_EQ(buf.GetFilename(), "");
    buf.SetFilename("test.txt");
    EXPECT_EQ(buf.GetFilename(), "test.txt");
    buf.SetFilename("/path/to/file.cpp");
    EXPECT_EQ(buf.GetFilename(), "/path/to/file.cpp");
}

TEST(BufferTest, SetModified)
{
    Buffer buf;
    EXPECT_FALSE(buf.IsModified());
    buf.SetModified(true);
    EXPECT_TRUE(buf.IsModified());
    buf.SetModified(false);
    EXPECT_FALSE(buf.IsModified());
}

TEST(BufferTest, ClearModified)
{
    Buffer buf;
    buf.SetModified(true);
    EXPECT_TRUE(buf.IsModified());
    buf.ClearModified();
    EXPECT_FALSE(buf.IsModified());
}

TEST(BufferTest, ModifiedFlagOnEdit)
{
    Buffer buf;

    buf.Insert(0, 0, "x");
    EXPECT_TRUE(buf.IsModified());
    buf.ClearModified();

    buf.Delete(0, 0, 1);
    EXPECT_TRUE(buf.IsModified());
    buf.ClearModified();

    buf.EraseLine(0);
    EXPECT_TRUE(buf.IsModified());
    buf.ClearModified();

    buf.InsertLine(0, "x");
    EXPECT_TRUE(buf.IsModified());
    buf.ClearModified();

    buf.AppendToLine(0, "x");
    EXPECT_TRUE(buf.IsModified());
    buf.ClearModified();
}

TEST(BufferTest, LoadFileNonExistent)
{
    Buffer buf;
    buf.SetFilename("/nonexistent/path/file.txt");
    buf.LoadFile("/nonexistent/path/file.txt");
    EXPECT_EQ(buf.GetLineCount(), 1);
    EXPECT_EQ(buf.GetLine(0), "");
    EXPECT_FALSE(buf.IsModified());
}

TEST(BufferTest, SaveFileEmptyFilename)
{
    Buffer buf;
    buf.SetModified(true);
    buf.SaveFile();
    EXPECT_TRUE(buf.IsModified());
}

TEST(BufferTest, ModifiedFlagInitialState)
{
    Buffer buf;
    EXPECT_FALSE(buf.IsModified());
    buf.InsertLine(0, "test");
    EXPECT_TRUE(buf.IsModified());
}
