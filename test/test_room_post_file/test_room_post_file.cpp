#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "bbs_fake_filesystem.h"
#include "room_post_file.h"

using namespace room;
using bbs_test::FakeFileSystem;

namespace {

PostRecord makeRec(uint8_t key, uint32_t ts, const char* text) {
  PostRecord r;
  memset(&r, 0, sizeof(r));
  memset(r.pub_key, key, kPubKeyLen);
  r.timestamp = ts;
  strncpy(r.text, text, kMaxPostTextLen);
  return r;
}

std::vector<uint8_t> readAll(FakeFileSystem& fs, const char* path) {
  std::vector<uint8_t> v;
  bbs::IFile* f = fs.open(path, 'r');
  if (!f || !f->valid()) return v;
  v.resize(f->size());
  size_t n = f->read(v.data(), v.size());
  v.resize(n);
  f->close();
  return v;
}

void writeAll(FakeFileSystem& fs, const char* path, const std::vector<uint8_t>& v) {
  bbs::IFile* f = fs.open(path, 'w');
  ASSERT_TRUE(f && f->valid());
  if (!v.empty()) f->write(v.data(), v.size());
  f->close();
}

constexpr size_t kHeader = 9;

} // namespace

TEST(RoomPostFile, MissingFileGivesZeroRecords) {
  FakeFileSystem fs;
  PostFile pf(fs);
  PostRecord out[4];
  EXPECT_EQ(pf.load(out, 4), 0u);
}

TEST(RoomPostFile, RoundTripKeepsChronologicalOrder) {
  FakeFileSystem fs;
  PostFile pf(fs);
  PostRecord in[3] = {makeRec(1, 100, "primo"), makeRec(2, 200, "secondo"), makeRec(1, 300, "terzo")};
  ASSERT_TRUE(pf.save(in, 3));

  PostRecord out[8];
  ASSERT_EQ(pf.load(out, 8), 3u);
  for (int i = 0; i < 3; i++) {
    EXPECT_EQ(memcmp(out[i].pub_key, in[i].pub_key, kPubKeyLen), 0);
    EXPECT_EQ(out[i].timestamp, in[i].timestamp);
    EXPECT_STREQ(out[i].text, in[i].text);
  }
}

TEST(RoomPostFile, EmptySaveLoadsZero) {
  FakeFileSystem fs;
  PostFile pf(fs);
  ASSERT_TRUE(pf.save(nullptr, 0));
  PostRecord out[2];
  EXPECT_EQ(pf.load(out, 2), 0u);
}

TEST(RoomPostFile, MaxLengthTextAndEmptyText) {
  FakeFileSystem fs;
  PostFile pf(fs);
  std::string longtext(kMaxPostTextLen, 'x');
  PostRecord in[2] = {makeRec(1, 10, longtext.c_str()), makeRec(2, 11, "")};
  ASSERT_TRUE(pf.save(in, 2));
  PostRecord out[2];
  ASSERT_EQ(pf.load(out, 2), 2u);
  EXPECT_STREQ(out[0].text, longtext.c_str());
  EXPECT_STREQ(out[1].text, "");
}

TEST(RoomPostFile, CapKeepsNewest) {
  FakeFileSystem fs;
  PostFile pf(fs);
  PostRecord in[5];
  for (int i = 0; i < 5; i++) {
    char t[16];
    snprintf(t, sizeof(t), "post%d", i);
    in[i] = makeRec((uint8_t)i, 100 + i, t);
  }
  ASSERT_TRUE(pf.save(in, 5));
  PostRecord out[3];
  ASSERT_EQ(pf.load(out, 3), 3u);
  EXPECT_STREQ(out[0].text, "post2");
  EXPECT_STREQ(out[1].text, "post3");
  EXPECT_STREQ(out[2].text, "post4");
}

TEST(RoomPostFile, BadCrcRecordIsSkipped) {
  FakeFileSystem fs;
  PostFile pf(fs);
  PostRecord in[3] = {makeRec(1, 100, "uno"), makeRec(2, 200, "due"), makeRec(3, 300, "tre")};
  ASSERT_TRUE(pf.save(in, 3));

  auto data = readAll(fs, kPostsPath);
  // primo record: 37 byte fissi + 3 di testo + 2 di crc; si corrompe il testo del secondo
  size_t second = kHeader + (37 + 3 + 2);
  data[second + 37] ^= 0xFF;
  writeAll(fs, kPostsPath, data);

  PostRecord out[4];
  ASSERT_EQ(pf.load(out, 4), 2u);
  EXPECT_STREQ(out[0].text, "uno");
  EXPECT_STREQ(out[1].text, "tre");
}

TEST(RoomPostFile, TruncatedFileStopsCleanly) {
  FakeFileSystem fs;
  PostFile pf(fs);
  PostRecord in[3] = {makeRec(1, 100, "uno"), makeRec(2, 200, "due"), makeRec(3, 300, "tre")};
  ASSERT_TRUE(pf.save(in, 3));

  auto data = readAll(fs, kPostsPath);
  data.resize(data.size() - 4);  // taglia a meta' del terzo record
  writeAll(fs, kPostsPath, data);

  PostRecord out[4];
  ASSERT_EQ(pf.load(out, 4), 2u);
  EXPECT_STREQ(out[1].text, "due");

  data.resize(5);  // anche l'intestazione e' troncata
  writeAll(fs, kPostsPath, data);
  EXPECT_EQ(pf.load(out, 4), 0u);
}

TEST(RoomPostFile, GarbageAndUnknownVersionGiveZero) {
  FakeFileSystem fs;
  PostFile pf(fs);
  PostRecord in[1] = {makeRec(1, 100, "uno")};
  ASSERT_TRUE(pf.save(in, 1));
  PostRecord out[2];

  auto data = readAll(fs, kPostsPath);
  data[4] = 99;  // versione ignota
  writeAll(fs, kPostsPath, data);
  EXPECT_EQ(pf.load(out, 2), 0u);

  std::vector<uint8_t> junk(100, 0xAB);
  writeAll(fs, kPostsPath, junk);
  EXPECT_EQ(pf.load(out, 2), 0u);
}

TEST(RoomPostFile, SaveOverwritesViaTmpAndRename) {
  FakeFileSystem fs;
  PostFile pf(fs);
  PostRecord a[2] = {makeRec(1, 100, "vecchio1"), makeRec(1, 101, "vecchio2")};
  ASSERT_TRUE(pf.save(a, 2));
  PostRecord b[1] = {makeRec(2, 500, "nuovo")};
  ASSERT_TRUE(pf.save(b, 1));

  PostRecord out[4];
  ASSERT_EQ(pf.load(out, 4), 1u);
  EXPECT_STREQ(out[0].text, "nuovo");
  // il temporaneo non resta
  EXPECT_TRUE(readAll(fs, kPostsTmpPath).empty());
}

TEST(RoomPostFile, StaysUnderRoomDirectory) {
  EXPECT_EQ(strncmp(kPostsPath, "/room/", 6), 0);
  EXPECT_EQ(strncmp(kPostsTmpPath, "/room/", 6), 0);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

TEST(RoomPostFile, FallsBackToTmpWhenMainFileMissing) {
  FakeFileSystem fs;
  PostFile pf(fs);
  PostRecord recs[2] = {makeRec(1, 100, "uno"), makeRec(2, 200, "due")};
  ASSERT_TRUE(pf.save(recs, 2));

  // spegnimento tra remove e rename: c'e' solo il temporaneo
  std::vector<uint8_t> bytes = readAll(fs, kPostsPath);
  fs.remove(kPostsPath);
  writeAll(fs, kPostsTmpPath, bytes);

  PostRecord out[4];
  ASSERT_EQ(2u, pf.load(out, 4));
  EXPECT_EQ(200u, out[1].timestamp);
  EXPECT_STREQ("due", out[1].text);
}
