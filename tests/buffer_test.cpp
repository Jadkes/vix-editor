// tests/buffer_test.cpp - Buffer unit tests
//
// Covers line editing, load/save round-trips (including the CRLF and
// trailing-newline preservation added for byte-for-byte saves), and the
// bounds checks on the raw accessors.

#include <gtest/gtest.h>
#include "core/buffer.hpp"
#include <cstdio>
#include <fstream>
#include <iterator>

TEST(BufferTest, DefaultConstruction) {
    Buffer buf;
    EXPECT_EQ(buf.GetLineCount(), 1);
    EXPECT_EQ(buf[0], "");
    EXPECT_FALSE(buf.IsModified());
    EXPECT_EQ(buf.GetFilename(), "");
}

TEST(BufferTest, PushBack) {
    Buffer buf;
    buf.PushBack("hello");
    EXPECT_EQ(buf.GetLineCount(), 2);
    EXPECT_EQ(buf[0], "");
    EXPECT_EQ(buf[1], "hello");
}

TEST(BufferTest, InsertLine) {
    Buffer buf;
    buf.PushBack("line2");
    buf.InsertLine(0, "inserted");
    EXPECT_EQ(buf.GetLineCount(), 3);
    EXPECT_EQ(buf[0], "inserted");
    EXPECT_EQ(buf[1], "");
    EXPECT_EQ(buf[2], "line2");
}

TEST(BufferTest, InsertLineAtEnd) {
    Buffer buf;
    buf.InsertLine(1, "last");
    EXPECT_EQ(buf.GetLineCount(), 2);
    EXPECT_EQ(buf[0], "");
    EXPECT_EQ(buf[1], "last");
}

TEST(BufferTest, InsertLineRejectsNegativeIndex) {
    Buffer buf;
    buf.InsertLine(-1, "nope");
    EXPECT_EQ(buf.GetLineCount(), 1);
}

TEST(BufferTest, EraseLine) {
    Buffer buf;
    buf.PushBack("line2");
    buf.PushBack("line3");
    buf.EraseLine(1);
    EXPECT_EQ(buf.GetLineCount(), 2);
    EXPECT_EQ(buf[0], "");
    EXPECT_EQ(buf[1], "line3");
}

TEST(BufferTest, EraseLineNeverEmptiesBuffer) {
    Buffer buf;
    buf.EraseLine(0);
    EXPECT_EQ(buf.GetLineCount(), 1);
    EXPECT_EQ(buf[0], "");
}

TEST(BufferTest, EraseLineRejectsNegativeIndex) {
    Buffer buf;
    buf.PushBack("keep");
    buf.EraseLine(-1);
    EXPECT_EQ(buf.GetLineCount(), 2);
    EXPECT_EQ(buf[1], "keep");
}

TEST(BufferTest, InsertText) {
    Buffer buf;
    buf.Insert(0, 0, "ab");
    buf.Insert(0, 1, "X");
    EXPECT_EQ(buf[0], "aXb");
    EXPECT_TRUE(buf.IsModified());
}

TEST(BufferTest, InsertTextClampsOutOfRange) {
    Buffer buf;
    buf.Insert(0, -5, "a");
    buf.Insert(0, 500, "b");
    buf.Insert(99, 0, "ignored");
    EXPECT_EQ(buf[0], "ab");
    EXPECT_EQ(buf.GetLineCount(), 1);
}

TEST(BufferTest, InsertRejectsEmbeddedNewline) {
    Buffer buf;
    buf.Insert(0, 0, "a\nb");
    EXPECT_EQ(buf[0], "");
    EXPECT_EQ(buf.GetLineCount(), 1);
}

TEST(BufferTest, DeleteText) {
    Buffer buf;
    buf.Insert(0, 0, "abcdef");
    buf.Delete(0, 2, 3);
    EXPECT_EQ(buf[0], "abf");
}

TEST(BufferTest, DeleteTextTruncatesPastEnd) {
    Buffer buf;
    buf.Insert(0, 0, "abc");
    buf.Delete(0, 1, 100);
    EXPECT_EQ(buf[0], "a");
}

TEST(BufferTest, DeleteTextRejectsInvalidColumn) {
    Buffer buf;
    buf.Insert(0, 0, "abc");
    buf.Delete(0, -1, 1);
    buf.Delete(0, 5, 1);
    EXPECT_EQ(buf[0], "abc");
}

TEST(BufferTest, OperatorBracketThrowsOutOfRange) {
    Buffer buf;
    EXPECT_THROW(buf[5], std::out_of_range);
    EXPECT_THROW(buf[-1], std::out_of_range);
}

TEST(BufferTest, FilenameAndModifiedSetters) {
    Buffer buf;
    buf.SetFilename("foo.txt");
    buf.SetModified(true);
    EXPECT_EQ(buf.GetFilename(), "foo.txt");
    EXPECT_TRUE(buf.IsModified());
    buf.SetModified(false);
    EXPECT_FALSE(buf.IsModified());
}

TEST(BufferTest, LoadFileMissingOpensEmptyUntitled) {
    Buffer buf;
    buf.LoadFile("/nonexistent/vix-test-file.txt");
    EXPECT_EQ(buf.GetFilename(), "/nonexistent/vix-test-file.txt");
    EXPECT_EQ(buf.GetLineCount(), 1);
    EXPECT_EQ(buf[0], "");
    EXPECT_FALSE(buf.IsModified());
}

// --- save round-trip -------------------------------------------------------

namespace {
// Write bytes to a temp file and return its path. The caller owns cleanup.
std::string WriteTemp(const std::string& contents) {
    static int counter = 0;
    std::string path = ::testing::TempDir() + "/vix_buffer_test_" +
                       std::to_string(counter++) + ".txt";
    std::ofstream f(path, std::ios::binary);
    f << contents;
    f.close();
    return path;
}
}  // namespace

TEST(BufferTest, SaveRoundTripLf) {
    std::string path = WriteTemp("one\ntwo\n");
    Buffer buf;
    buf.LoadFile(path);
    EXPECT_TRUE(buf.SaveFile());
    std::ifstream in(path, std::ios::binary);
    std::string contents((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    EXPECT_EQ(contents, "one\ntwo\n");
    std::remove(path.c_str());
}

TEST(BufferTest, SaveRoundTripCrlf) {
    std::string path = WriteTemp("one\r\ntwo\r\n");
    Buffer buf;
    buf.LoadFile(path);
    EXPECT_TRUE(buf.SaveFile());
    std::ifstream in(path, std::ios::binary);
    std::string contents((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    EXPECT_EQ(contents, "one\r\ntwo\r\n");
    std::remove(path.c_str());
}

TEST(BufferTest, SaveRoundTripNoTrailingNewline) {
    std::string path = WriteTemp("one\ntwo");
    Buffer buf;
    buf.LoadFile(path);
    EXPECT_TRUE(buf.SaveFile());
    std::ifstream in(path, std::ios::binary);
    std::string contents((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    EXPECT_EQ(contents, "one\ntwo");
    std::remove(path.c_str());
}

TEST(BufferTest, SaveRoundTripPreservesEndingStyleAfterEdit) {
    std::string path = WriteTemp("one\r\ntwo\r\n");
    Buffer buf;
    buf.LoadFile(path);
    buf.Insert(0, 0, "x");
    EXPECT_TRUE(buf.SaveFile());
    std::ifstream in(path, std::ios::binary);
    std::string contents((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    EXPECT_EQ(contents, "xone\r\ntwo\r\n");
    std::remove(path.c_str());
}

// A lone \r (or one on the final line of a file without a trailing \n) is
// content, not a truncated CRLF; it must survive the round-trip untouched.
TEST(BufferTest, SaveRoundTripPreservesLoneCarriageReturn) {
    for (const char* sample : {"\r", "b\r", "a\r\nb\r", "word\r"}) {
        std::string path = WriteTemp(sample);
        Buffer buf;
        buf.LoadFile(path);
        EXPECT_TRUE(buf.SaveFile());
        std::ifstream in(path, std::ios::binary);
        std::string contents((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
        EXPECT_EQ(contents, sample);
        std::remove(path.c_str());
    }
}

// Mixed endings are normalized to the style of the first line: the second
// "\n" below becomes "\r\n". Byte-for-byte applies only to uniform files;
// this freezes the deliberate first-line-wins tradeoff.
TEST(BufferTest, MixedLineEndingsFollowFirstLine) {
    std::string path = WriteTemp("a\r\nb\n");
    Buffer buf;
    buf.LoadFile(path);
    EXPECT_TRUE(buf.SaveFile());
    std::ifstream in(path, std::ios::binary);
    std::string contents((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    EXPECT_EQ(contents, "a\r\nb\r\n");
    std::remove(path.c_str());
}
