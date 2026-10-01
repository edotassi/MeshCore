#include <gtest/gtest.h>

#include <string>

#include "bbs_fake_filesystem.h"
#include "bbs_post_store.h"

using namespace bbs;
using bbs_test::FakeFileSystem;

TEST(BbsPostStore, RejectsTextTooLong) {
  FakeFileSystem fs;
  PostStore store(fs);
  std::string long_text(BBS_MAX_TEXT_LEN + 1, 'x');
  EXPECT_FALSE(store.appendPost(0, 1, 100, long_text.c_str()));
}

TEST(BbsPostStore, RejectsEmptyText) {
  FakeFileSystem fs;
  PostStore store(fs);
  EXPECT_FALSE(store.appendPost(0, 1, 100, ""));
}

TEST(BbsPostStore, RejectsInvalidRoomId) {
  FakeFileSystem fs;
  PostStore store(fs);
  EXPECT_FALSE(store.appendPost(BBS_MAX_ROOMS, 1, 100, "ciao"));
  PostRecord rec;
  EXPECT_FALSE(store.findNextUnread(BBS_MAX_ROOMS, 0, rec));
}

TEST(BbsPostStore, MultiplePostsInChronologicalOrder) {
  FakeFileSystem fs;
  PostStore store(fs);
  ASSERT_TRUE(store.appendPost(1, 5, 100, "uno"));
  ASSERT_TRUE(store.appendPost(1, 6, 200, "due"));
  ASSERT_TRUE(store.appendPost(1, 7, 300, "tre"));

  PostRecord rec;
  ASSERT_TRUE(store.findNextUnread(1, 0, rec));
  EXPECT_STREQ(rec.text, "uno");
  ASSERT_TRUE(store.findNextUnread(1, rec.timestamp, rec));
  EXPECT_STREQ(rec.text, "due");
  ASSERT_TRUE(store.findNextUnread(1, rec.timestamp, rec));
  EXPECT_STREQ(rec.text, "tre");
  EXPECT_FALSE(store.findNextUnread(1, rec.timestamp, rec));
}

TEST(BbsPostStore, RoomsAreIndependentLogs) {
  FakeFileSystem fs;
  PostStore store(fs);
  ASSERT_TRUE(store.appendPost(0, 1, 100, "stanza zero"));
  ASSERT_TRUE(store.appendPost(1, 1, 100, "stanza uno"));

  PostRecord rec;
  ASSERT_TRUE(store.findNextUnread(0, 0, rec));
  EXPECT_STREQ(rec.text, "stanza zero");
  ASSERT_TRUE(store.findNextUnread(1, 0, rec));
  EXPECT_STREQ(rec.text, "stanza uno");
}

TEST(BbsPostStore, CountUnreadMatchesFindNextUnreadScan) {
  FakeFileSystem fs;
  PostStore store(fs);
  ASSERT_TRUE(store.appendPost(0, 1, 100, "uno"));
  ASSERT_TRUE(store.appendPost(0, 1, 200, "due"));
  ASSERT_TRUE(store.appendPost(0, 1, 300, "tre"));

  EXPECT_EQ(store.countUnread(0, 0), 3u);
  EXPECT_EQ(store.countUnread(0, 100), 2u);
  EXPECT_EQ(store.countUnread(0, 300), 0u);
  EXPECT_EQ(store.countUnread(1, 0), 0u);  // stanza diversa, nessun post
}

TEST(BbsPostStore, RepairOnMissingFileIsNoop) {
  FakeFileSystem fs;
  PostStore store(fs);
  EXPECT_TRUE(store.repairRoom(0));
}

// Simula uno spegnimento a meta' scrittura di un record: dopo un post valido,
// vengono scritti solo alcuni byte di intestazione del successivo, senza
// testo ne' CRC (esattamente come lascerebbe un flash interrotto a meta').
TEST(BbsPostStore, CorruptedTailIsRepairedAndAppendStillWorksAfter) {
  FakeFileSystem fs;
  PostStore store(fs);

  ASSERT_TRUE(store.appendPost(0, 1, 1000, "primo messaggio"));

  IFile* f = fs.open("/bbs/r00.log", 'a');
  ASSERT_TRUE(f->valid());
  uint8_t garbage[5] = {1, 0, 0, 0, 0};  // intestazione troncata, mai completata
  f->write(garbage, sizeof(garbage));
  f->close();

  ASSERT_TRUE(store.repairRoom(0));

  PostRecord rec;
  ASSERT_TRUE(store.findNextUnread(0, 0, rec));
  EXPECT_STREQ(rec.text, "primo messaggio");
  EXPECT_EQ(rec.timestamp, 1000u);
  EXPECT_FALSE(store.findNextUnread(0, rec.timestamp, rec));

  // Un nuovo post dopo la riparazione deve restare raggiungibile.
  ASSERT_TRUE(store.appendPost(0, 1, 2000, "secondo messaggio"));
  PostRecord rec2;
  ASSERT_TRUE(store.findNextUnread(0, 1000, rec2));
  EXPECT_STREQ(rec2.text, "secondo messaggio");
  EXPECT_EQ(rec2.timestamp, 2000u);
}

TEST(BbsPostStore, DeleteLastPostHidesItFromReadsAndCounts) {
  FakeFileSystem fs;
  PostStore store(fs);
  ASSERT_TRUE(store.appendPost(0, 1, 100, "uno"));
  ASSERT_TRUE(store.appendPost(0, 1, 200, "due"));

  ASSERT_TRUE(store.deleteLastPost(0));

  EXPECT_EQ(store.countUnread(0, 0), 1u);
  PostRecord rec;
  ASSERT_TRUE(store.findNextUnread(0, 0, rec));
  EXPECT_STREQ(rec.text, "uno");
  EXPECT_FALSE(store.findNextUnread(0, rec.timestamp, rec));  // "due" e' cancellato, non compare
}

TEST(BbsPostStore, DeleteLastPostOnEmptyRoomFails) {
  FakeFileSystem fs;
  PostStore store(fs);
  EXPECT_FALSE(store.deleteLastPost(0));
}

TEST(BbsPostStore, DeletedPostDoesNotBlockLaterAppends) {
  FakeFileSystem fs;
  PostStore store(fs);
  ASSERT_TRUE(store.appendPost(0, 1, 100, "uno"));
  ASSERT_TRUE(store.deleteLastPost(0));
  ASSERT_TRUE(store.appendPost(0, 1, 200, "due"));

  PostRecord rec;
  ASSERT_TRUE(store.findNextUnread(0, 0, rec));
  EXPECT_STREQ(rec.text, "due");
}

TEST(BbsPostStore, RetentionIsNoopUnderLimit) {
  FakeFileSystem fs;
  PostStore store(fs);
  ASSERT_TRUE(store.appendPost(0, 1, 100, "uno"));
  ASSERT_TRUE(store.appendPost(0, 1, 200, "due"));
  EXPECT_TRUE(store.enforceRetention(0, 10));
  EXPECT_EQ(store.countUnread(0, 0), 2u);
}

TEST(BbsPostStore, RetentionDropsOldestBeyondLimit) {
  FakeFileSystem fs;
  PostStore store(fs);
  ASSERT_TRUE(store.appendPost(0, 1, 100, "uno"));
  ASSERT_TRUE(store.appendPost(0, 1, 200, "due"));
  ASSERT_TRUE(store.appendPost(0, 1, 300, "tre"));
  ASSERT_TRUE(store.appendPost(0, 1, 400, "quattro"));

  ASSERT_TRUE(store.enforceRetention(0, 2));

  EXPECT_EQ(store.countUnread(0, 0), 2u);
  PostRecord rec;
  ASSERT_TRUE(store.findNextUnread(0, 0, rec));
  EXPECT_STREQ(rec.text, "tre");  // "uno" e "due" scartati per sempre
  ASSERT_TRUE(store.findNextUnread(0, rec.timestamp, rec));
  EXPECT_STREQ(rec.text, "quattro");
}

TEST(BbsPostStore, RetentionOnOtherRoomsUnaffected) {
  FakeFileSystem fs;
  PostStore store(fs);
  ASSERT_TRUE(store.appendPost(0, 1, 100, "stanza0"));
  ASSERT_TRUE(store.appendPost(1, 1, 100, "stanza1"));
  ASSERT_TRUE(store.enforceRetention(0, 0));  // scarta tutto in stanza 0

  EXPECT_EQ(store.countUnread(0, 0), 0u);
  EXPECT_EQ(store.countUnread(1, 0), 1u);
}

TEST(BbsPostStore, SearchFindsMostRecentMatch) {
  FakeFileSystem fs;
  PostStore store(fs);
  ASSERT_TRUE(store.appendPost(0, 1, 100, "vecchia antenna rotta"));
  ASSERT_TRUE(store.appendPost(0, 1, 200, "tutto ok oggi"));
  ASSERT_TRUE(store.appendPost(0, 1, 300, "nuova antenna installata"));

  PostRecord rec;
  ASSERT_TRUE(store.searchRecent(0, "antenna", 200, rec));
  EXPECT_STREQ(rec.text, "nuova antenna installata");
}

TEST(BbsPostStore, SearchNoMatchReturnsFalse) {
  FakeFileSystem fs;
  PostStore store(fs);
  ASSERT_TRUE(store.appendPost(0, 1, 100, "ciao a tutti"));
  PostRecord rec;
  EXPECT_FALSE(store.searchRecent(0, "introvabile", 200, rec));
}

TEST(BbsPostStore, SearchRespectsMaxScanWindow) {
  FakeFileSystem fs;
  PostStore store(fs);
  ASSERT_TRUE(store.appendPost(0, 1, 100, "parola chiave qui"));
  ASSERT_TRUE(store.appendPost(0, 1, 200, "riempitivo uno"));
  ASSERT_TRUE(store.appendPost(0, 1, 300, "riempitivo due"));

  PostRecord rec;
  // Con max_scan=1 si vedono solo gli ultimi 1 post ("riempitivo due"): il
  // match nel primo post (fuori dalla finestra) non deve essere trovato.
  EXPECT_FALSE(store.searchRecent(0, "parola", 1, rec));
  EXPECT_TRUE(store.searchRecent(0, "parola", 10, rec));
}

TEST(BbsPostStore, SearchSkipsDeletedPosts) {
  FakeFileSystem fs;
  PostStore store(fs);
  ASSERT_TRUE(store.appendPost(0, 1, 100, "segreto da cancellare"));
  ASSERT_TRUE(store.deleteLastPost(0));
  PostRecord rec;
  EXPECT_FALSE(store.searchRecent(0, "segreto", 200, rec));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
