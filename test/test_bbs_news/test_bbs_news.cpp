#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "bbs_fake_filesystem.h"
#include "bbs_news.h"
#include "bbs_room_state.h"
#include "bbs_sha256.h"

using namespace bbs;
using bbs_test::FakeFileSystem;

namespace {

constexpr uint32_t kNow = 1793091700;  // poco dopo il ts del vettore di test

std::string hexOf(const uint8_t* p, size_t n) {
  static const char k[] = "0123456789abcdef";
  std::string s;
  for (size_t i = 0; i < n; i++) {
    s += k[p[i] >> 4];
    s += k[p[i] & 15];
  }
  return s;
}

void testKey(uint8_t key[32]) {
  for (size_t i = 0; i < 32; i++) key[i] = (uint8_t)i;
}

// Payload firmato con la chiave di test (0x00..0x1f).
std::string makePayload(const std::string& id, const std::string& ts, const std::string& source,
                        const std::string& title, bool trailing_newline = false) {
  uint8_t key[32];
  testKey(key);
  std::string msg = "v1\n" + id + "\n" + ts + "\n" + source + "\n" + title;
  uint8_t mac[32];
  hmacSha256(key, 32, (const uint8_t*)msg.data(), msg.size(), mac);
  return msg + "\n" + hexOf(mac, 32) + (trailing_newline ? "\n" : "");
}

const char* kVectorPayload =
    "v1\n3f2a9c1d7e8b4a60\n1793091600\nANSA\nTerremoto di magnitudo 3.2 al largo di Otranto, nessun danno\n"
    "0bb6e86b3ce7631bdd0382262ea63d32da0fcbf9e364b771d8e893976bfd91cd";

struct Fixture {
  FakeFileSystem fs;
  NewsStore news{fs};
  NewsSeen seen{fs};
  PostStore posts{fs};
  RoomRegistry rooms{fs};
  UserStore users{fs};
  UserId sys = kInvalidUserId;
  uint8_t room = kNoRoom;

  Fixture() {
    uint8_t node[BBS_PUBKEY_LEN];
    memset(node, 0x42, sizeof(node));
    sys = users.ensureSystemUser(node, "News", 100);
    rooms.addRoom("News", room);
    configure();
  }

  void configure(bool enabled = true, bool with_key = true, uint8_t room_id = 0xFE) {
    NewsSettings s = news.get();
    s.enabled = enabled;
    if (with_key) {
      testKey(s.key);
      s.key_len = 32;
    } else {
      s.key_len = 0;
    }
    s.room_id = (room_id == 0xFE) ? room : room_id;
    ASSERT_TRUE(news.save(s));
  }

  NewsIngestResult ingest(const std::string& payload, uint32_t now = kNow, bool valid = true) {
    return ingestNews(news, seen, posts, rooms, users, sys, payload.data(), payload.size(), now, valid);
  }

  std::string latestText() {
    PostRecord r;
    if (!posts.findLatest(room, r)) return "<none>";
    return r.text;
  }
};

} // namespace

// --- Parsing ---

TEST(BbsNewsParse, AcceptsProtocolVector) {
  NewsMessage m;
  ASSERT_TRUE(parseNewsMessage(kVectorPayload, strlen(kVectorPayload), m));
  EXPECT_STREQ(m.id, "3f2a9c1d7e8b4a60");
  EXPECT_EQ(m.ts, 1793091600u);
  EXPECT_STREQ(m.source, "ANSA");
  EXPECT_STREQ(m.title, "Terremoto di magnitudo 3.2 al largo di Otranto, nessun danno");
  EXPECT_EQ(hexOf(m.hmac, 32), "0bb6e86b3ce7631bdd0382262ea63d32da0fcbf9e364b771d8e893976bfd91cd");
}

TEST(BbsNewsParse, ProtocolVectorVerifiesWithTestKey) {
  NewsMessage m;
  ASSERT_TRUE(parseNewsMessage(kVectorPayload, strlen(kVectorPayload), m));
  uint8_t key[32];
  testKey(key);
  EXPECT_TRUE(verifyNews(m, key, 32));
  key[0] ^= 1;
  EXPECT_FALSE(verifyNews(m, key, 32));
  testKey(key);
  EXPECT_FALSE(verifyNews(m, key, 31));
  EXPECT_FALSE(verifyNews(m, key, 0));
  m.title[0] = 'X';
  EXPECT_FALSE(verifyNews(m, key, 32));
}

TEST(BbsNewsParse, ToleratesOneTrailingNewlineOnly) {
  NewsMessage m;
  std::string p = makePayload("a1", "100", "ANSA", "Titolo", true);
  EXPECT_TRUE(parseNewsMessage(p.data(), p.size(), m));
  std::string p2 = p + "\n";
  EXPECT_FALSE(parseNewsMessage(p2.data(), p2.size(), m));
}

TEST(BbsNewsParse, RejectsMalformedPayloads) {
  NewsMessage m;
  auto bad = [&](const std::string& p) { return !parseNewsMessage(p.data(), p.size(), m); };

  EXPECT_TRUE(bad(""));
  EXPECT_TRUE(bad(makePayload("a1", "100", "ANSA", "Titolo").replace(0, 2, "v2")));
  // id: vuoto, troppo lungo, caratteri non ammessi
  EXPECT_TRUE(bad(makePayload("", "100", "ANSA", "T")));
  EXPECT_TRUE(bad(makePayload(std::string(33, 'a'), "100", "ANSA", "T")));
  EXPECT_FALSE(bad(makePayload(std::string(32, 'a'), "100", "ANSA", "T")));
  EXPECT_TRUE(bad(makePayload("a b", "100", "ANSA", "T")));
  EXPECT_TRUE(bad(makePayload("a.b", "100", "ANSA", "T")));
  EXPECT_FALSE(bad(makePayload("A_b-9", "100", "ANSA", "T")));
  // ts: non numerico, zeri iniziali, vuoto, troppo grande, troppo lungo
  EXPECT_TRUE(bad(makePayload("a", "10x", "ANSA", "T")));
  EXPECT_TRUE(bad(makePayload("a", "0100", "ANSA", "T")));
  EXPECT_TRUE(bad(makePayload("a", "", "ANSA", "T")));
  EXPECT_TRUE(bad(makePayload("a", "-5", "ANSA", "T")));
  EXPECT_TRUE(bad(makePayload("a", "4294967296", "ANSA", "T")));
  EXPECT_TRUE(bad(makePayload("a", "12345678901", "ANSA", "T")));
  EXPECT_FALSE(bad(makePayload("a", "4294967295", "ANSA", "T")));
  EXPECT_FALSE(bad(makePayload("a", "0", "ANSA", "T")));
  // fonte: vuota, > 16 byte
  EXPECT_TRUE(bad(makePayload("a", "100", "", "T")));
  EXPECT_TRUE(bad(makePayload("a", "100", std::string(17, 'x'), "T")));
  EXPECT_FALSE(bad(makePayload("a", "100", std::string(16, 'x'), "T")));
  // titolo: vuoto, > 140 byte, controllo, UTF-8 non valido
  EXPECT_TRUE(bad(makePayload("a", "100", "ANSA", "")));
  EXPECT_TRUE(bad(makePayload("a", "100", "ANSA", std::string(141, 'x'))));
  EXPECT_FALSE(bad(makePayload("a", "100", "ANSA", std::string(140, 'x'))));
  EXPECT_TRUE(bad(makePayload("a", "100", "ANSA", "con\ttab")));
  EXPECT_TRUE(bad(makePayload("a", "100", "ANSA", "con\rcr")));
  EXPECT_TRUE(bad(makePayload("a", "100", "ANSA", "rotto\xC3")));
  EXPECT_TRUE(bad(makePayload("a", "100", "ANSA", "rotto\xFF" "x")));
  EXPECT_TRUE(bad(makePayload("a", "100", "ANSA", "rotto\xC3" "x")));
  EXPECT_FALSE(bad(makePayload("a", "100", "ANSA", "citt\xC3\xA0 \xE2\x82\xAC \xF0\x9F\x98\x80")));
  // righe in piu' / in meno
  EXPECT_TRUE(bad(makePayload("a", "100", "ANSA", "T") + "\nextra"));
  EXPECT_TRUE(bad("v1\na\n100\nANSA\nT"));
  EXPECT_TRUE(bad(makePayload("a", "100", "ANSA", "ti\ntolo")));
  // hmac: maiuscolo, corto, non esadecimale
  std::string ok = makePayload("a", "100", "ANSA", "T");
  std::string upper = ok;
  for (size_t i = ok.size() - 64; i < ok.size(); i++) upper[i] = (char)toupper(upper[i]);
  EXPECT_TRUE(upper == ok || bad(upper));  // se per caso non ha lettere, resta valido
  EXPECT_TRUE(bad(ok.substr(0, ok.size() - 1)));
  std::string nonhex = ok;
  nonhex[nonhex.size() - 1] = 'g';
  EXPECT_TRUE(bad(nonhex));
  // NUL incorporato
  std::string nul = ok;
  nul[3] = '\0';
  EXPECT_TRUE(bad(nul));
}

// --- Store ---

TEST(BbsNewsStore, DefaultsWhenFileMissing) {
  FakeFileSystem fs;
  NewsStore store(fs);
  const NewsSettings& s = store.get();
  EXPECT_FALSE(s.enabled);
  EXPECT_EQ(s.port, 1883);
  EXPECT_STREQ(s.topic, "bbs/news");
  EXPECT_STREQ(s.server, "");
  EXPECT_EQ(s.key_len, 0);
  EXPECT_EQ(s.room_id, kNoRoom);
  EXPECT_EQ(store.generation(), 0u);
}

TEST(BbsNewsStore, SavePersistsAllFieldsAndBumpsGeneration) {
  FakeFileSystem fs;
  {
    NewsStore store(fs);
    NewsSettings s = store.get();
    s.enabled = true;
    strcpy(s.server, "broker.example.org");
    s.port = 8883;
    strcpy(s.username, "bbs");
    strcpy(s.password, "pw");
    strcpy(s.topic, "x/y");
    testKey(s.key);
    s.key_len = 24;
    s.room_id = 3;
    ASSERT_TRUE(store.save(s));
    EXPECT_EQ(store.generation(), 1u);
  }
  NewsStore store2(fs);
  const NewsSettings& s = store2.get();
  EXPECT_TRUE(s.enabled);
  EXPECT_STREQ(s.server, "broker.example.org");
  EXPECT_EQ(s.port, 8883);
  EXPECT_STREQ(s.username, "bbs");
  EXPECT_STREQ(s.password, "pw");
  EXPECT_STREQ(s.topic, "x/y");
  EXPECT_EQ(s.key_len, 24);
  EXPECT_EQ(s.key[23], 23);
  EXPECT_EQ(s.room_id, 3);
}

TEST(BbsNewsStore, RejectsInvalidSettingsOnSave) {
  FakeFileSystem fs;
  NewsStore store(fs);
  NewsSettings s = store.get();
  s.key_len = 8;
  EXPECT_FALSE(store.save(s));
  s.key_len = 0;
  s.port = 0;
  EXPECT_FALSE(store.save(s));
  s.port = 1883;
  s.room_id = BBS_MAX_ROOMS;
  EXPECT_FALSE(store.save(s));
  EXPECT_EQ(store.generation(), 0u);
}

TEST(BbsNewsStore, CorruptOrWrongVersionFileFallsBackToDefaults) {
  FakeFileSystem fs;
  {
    NewsStore store(fs);
    NewsSettings s = store.get();
    s.enabled = true;
    ASSERT_TRUE(store.save(s));
  }
  {
    // Versione ignota.
    IFile* f = fs.open("/bbs/news.dat", 'r');
    uint8_t buf[300];
    size_t n = f->read(buf, sizeof(buf));
    f->close();
    buf[0] = 99;
    IFile* w = fs.open("/bbs/news.dat", 'w');
    w->write(buf, n);
    w->close();
    NewsStore store(fs);
    EXPECT_FALSE(store.get().enabled);
  }
  {
    // File corto.
    IFile* w = fs.open("/bbs/news.dat", 'w');
    uint8_t one = 1;
    w->write(&one, 1);
    w->close();
    NewsStore store(fs);
    EXPECT_FALSE(store.get().enabled);
  }
}

// --- Anti doppioni ---

TEST(BbsNewsSeen, ContainsAddAndPersist) {
  FakeFileSystem fs;
  {
    NewsSeen seen(fs);
    EXPECT_FALSE(seen.contains("abc"));
    EXPECT_TRUE(seen.add("abc"));
    EXPECT_TRUE(seen.contains("abc"));
    EXPECT_FALSE(seen.contains("abd"));
    EXPECT_TRUE(seen.add("abc"));  // no-op
  }
  NewsSeen seen2(fs);
  EXPECT_TRUE(seen2.contains("abc"));
  EXPECT_FALSE(seen2.contains("abd"));
}

TEST(BbsNewsSeen, RingForgetsOldestAfterCapacity) {
  FakeFileSystem fs;
  {
    NewsSeen seen(fs);
    for (size_t i = 0; i < kNewsSeenCapacity + 5; i++) {
      ASSERT_TRUE(seen.add(("id" + std::to_string(i)).c_str()));
    }
  }
  NewsSeen seen(fs);  // ricaricato da file
  for (size_t i = 0; i < 5; i++) EXPECT_FALSE(seen.contains(("id" + std::to_string(i)).c_str())) << i;
  for (size_t i = 5; i < kNewsSeenCapacity + 5; i++) EXPECT_TRUE(seen.contains(("id" + std::to_string(i)).c_str())) << i;
  // Continua a ruotare correttamente dopo il reload.
  ASSERT_TRUE(seen.add("nuovo"));
  EXPECT_FALSE(seen.contains("id5"));
  EXPECT_TRUE(seen.contains("id6"));
  EXPECT_TRUE(seen.contains("nuovo"));
}

TEST(BbsNewsSeen, CorruptFileIsIgnored) {
  FakeFileSystem fs;
  IFile* w = fs.open("/bbs/news_seen.dat", 'w');
  uint8_t junk[515];
  memset(junk, 0xFF, sizeof(junk));
  junk[0] = 1;
  w->write(junk, sizeof(junk));
  w->close();
  NewsSeen seen(fs);
  EXPECT_FALSE(seen.contains("x"));
  EXPECT_TRUE(seen.add("x"));
  EXPECT_TRUE(seen.contains("x"));
}

// --- Ingest ---

TEST(BbsNewsIngest, VectorIsAppendedAsPostWithNowTimestamp) {
  Fixture f;
  EXPECT_EQ(f.ingest(kVectorPayload), NewsIngestResult::OK);
  PostRecord r;
  ASSERT_TRUE(f.posts.findLatest(f.room, r));
  EXPECT_STREQ(r.text, "[ANSA] Terremoto di magnitudo 3.2 al largo di Otranto, nessun danno");
  EXPECT_EQ(r.timestamp, kNow);  // non il ts dello script (1793091600)
  EXPECT_EQ(r.user_id, f.sys);
  EXPECT_EQ(f.news.status().received_ok, 1u);
  EXPECT_EQ(f.news.status().last_ok_ts, kNow);
  EXPECT_EQ(f.news.status().last_result, NewsIngestResult::OK);
  EXPECT_TRUE(f.seen.contains("3f2a9c1d7e8b4a60"));
}

TEST(BbsNewsIngest, ClosedRoomDoesNotBlockIngest) {
  Fixture f;
  RoomState state(f.fs);
  ASSERT_TRUE(state.setClosed(f.room, true));
  EXPECT_EQ(f.ingest(makePayload("c1", "1793091600", "ANSA", "Titolo")), NewsIngestResult::OK);
}

TEST(BbsNewsIngest, PreconditionsInOrder) {
  Fixture f;
  std::string p = makePayload("p1", "1793091600", "ANSA", "Titolo");

  f.configure(false);
  EXPECT_EQ(f.ingest(p), NewsIngestResult::NOT_ENABLED);
  f.configure(true, false);
  EXPECT_EQ(f.ingest(p), NewsIngestResult::NO_KEY);
  f.configure(true, true, kNoRoom);
  EXPECT_EQ(f.ingest(p), NewsIngestResult::NO_ROOM);
  f.configure(true, true, 7);  // id valido ma non nel registro
  EXPECT_EQ(f.ingest(p), NewsIngestResult::NO_ROOM);
  EXPECT_EQ(f.news.status().received_ok, 0u);
  EXPECT_EQ(f.news.status().last_result, NewsIngestResult::NO_ROOM);

  f.configure();
  EXPECT_EQ(f.ingest("rumore"), NewsIngestResult::BAD_FORMAT);
  EXPECT_EQ(f.latestText(), "<none>");
}

TEST(BbsNewsIngest, BadSignatureIsRejectedAndNotRemembered) {
  Fixture f;
  std::string p = makePayload("s1", "1793091600", "ANSA", "Titolo");
  std::string tampered = p;
  tampered.replace(tampered.find("Titolo"), 6, "Titolx");
  EXPECT_EQ(f.ingest(tampered), NewsIngestResult::BAD_SIGNATURE);
  EXPECT_EQ(f.latestText(), "<none>");
  EXPECT_FALSE(f.seen.contains("s1"));
  // Chiave diversa sul nodo.
  NewsSettings s = f.news.get();
  s.key[0] ^= 0xFF;
  ASSERT_TRUE(f.news.save(s));
  EXPECT_EQ(f.ingest(p), NewsIngestResult::BAD_SIGNATURE);
  // Con la chiave giusta e lo stesso id: ancora accettabile (la firma sbagliata non l'ha "bruciato").
  s.key[0] ^= 0xFF;
  ASSERT_TRUE(f.news.save(s));
  EXPECT_EQ(f.ingest(p), NewsIngestResult::OK);
}

TEST(BbsNewsIngest, SignatureWithShorterKeyWorks) {
  Fixture f;
  uint8_t key[16];
  for (size_t i = 0; i < 16; i++) key[i] = (uint8_t)(0xA0 + i);
  NewsSettings s = f.news.get();
  memset(s.key, 0, sizeof(s.key));
  memcpy(s.key, key, 16);
  s.key_len = 16;
  ASSERT_TRUE(f.news.save(s));
  std::string msg = "v1\nk1\n1793091600\nANSA\nTitolo";
  uint8_t mac[32];
  hmacSha256(key, 16, (const uint8_t*)msg.data(), msg.size(), mac);
  EXPECT_EQ(f.ingest(msg + "\n" + hexOf(mac, 32)), NewsIngestResult::OK);
}

TEST(BbsNewsIngest, DuplicateIdIsRejected) {
  Fixture f;
  std::string p = makePayload("d1", "1793091600", "ANSA", "Titolo");
  EXPECT_EQ(f.ingest(p), NewsIngestResult::OK);
  EXPECT_EQ(f.ingest(p, kNow + 1), NewsIngestResult::DUPLICATE);
  EXPECT_EQ(f.posts.countUnread(f.room, 0), 1u);
  EXPECT_EQ(f.news.status().received_ok, 1u);
  // Il duplicato resta tale dopo un riavvio.
  NewsSeen seen2(f.fs);
  EXPECT_TRUE(seen2.contains("d1"));
}

TEST(BbsNewsIngest, StaleAndFutureOnlyWithValidClock) {
  Fixture f;
  const uint32_t now = 1800000000;
  EXPECT_EQ(f.ingest(makePayload("t1", std::to_string(now - 72 * 3600 - 1), "ANSA", "Vecchia"), now),
            NewsIngestResult::STALE);
  EXPECT_EQ(f.ingest(makePayload("t2", std::to_string(now - 72 * 3600), "ANSA", "Al limite"), now),
            NewsIngestResult::OK);
  EXPECT_EQ(f.ingest(makePayload("t3", std::to_string(now + 3600 + 1), "ANSA", "Futura"), now),
            NewsIngestResult::STALE);
  EXPECT_EQ(f.ingest(makePayload("t4", std::to_string(now + 3600), "ANSA", "Quasi futura"), now),
            NewsIngestResult::OK);
  // Orologio non valido: il controllo non si applica.
  EXPECT_EQ(f.ingest(makePayload("t5", "1000", "ANSA", "Qualunque"), 500, false), NewsIngestResult::OK);
  // Una scartata come STALE non viene ricordata.
  EXPECT_FALSE(f.seen.contains("t1"));
}

TEST(BbsNewsIngest, LongTextIsTruncatedOnUtf8Boundary) {
  Fixture f;
  // 16 + 140 byte: "[xxxxxxxxxxxxxxxx] " + titolo con caratteri da 2 byte.
  std::string title;
  while (title.size() + 2 <= 140) title += "\xC3\xA8";
  std::string p = makePayload("u1", "1793091600", std::string(16, 's'), title);
  ASSERT_EQ(f.ingest(p), NewsIngestResult::OK);
  PostRecord r;
  ASSERT_TRUE(f.posts.findLatest(f.room, r));
  EXPECT_LE(r.text_len, BBS_MAX_TEXT_LEN);
  EXPECT_EQ(r.text_len, strlen(r.text));
  // Il prefisso e' lungo 19 byte: il resto (132) cade a meta' di un carattere da 2 byte? 151-19=132 pari: esatto.
  size_t body = r.text_len - 19;
  EXPECT_EQ(body % 2, 0u);
  EXPECT_EQ(std::string(r.text, 19), "[" + std::string(16, 's') + "] ");

  // Con una fonte da 15 byte il prefisso e' dispari (18): il taglio a 151 spezzerebbe un carattere.
  std::string p2 = makePayload("u2", "1793091600", std::string(15, 's'), title);
  ASSERT_EQ(f.ingest(p2), NewsIngestResult::OK);
  ASSERT_TRUE(f.posts.findLatest(f.room, r));
  EXPECT_LE(r.text_len, BBS_MAX_TEXT_LEN);
  EXPECT_EQ((r.text_len - 18) % 2, 0u);
  EXPECT_NE(r.text_len, BBS_MAX_TEXT_LEN);  // un byte in meno per non spezzare il carattere
}

TEST(BbsNewsIngest, EnforcesRetention) {
  Fixture f;
  for (uint32_t i = 0; i < BBS_MAX_POSTS_PER_ROOM; i++) {
    ASSERT_TRUE(f.posts.appendPost(f.room, f.sys, 1000 + i, "vecchia"));
  }
  EXPECT_EQ(f.ingest(makePayload("r1", "1793091600", "ANSA", "Nuova")), NewsIngestResult::OK);
  EXPECT_EQ(f.posts.countUnread(f.room, 0), BBS_MAX_POSTS_PER_ROOM);
  EXPECT_EQ(f.latestText(), "[ANSA] Nuova");
}

TEST(BbsNewsIngest, MissingSystemUserIsIoError) {
  Fixture f;
  f.sys = kInvalidUserId;
  EXPECT_EQ(f.ingest(makePayload("m1", "1793091600", "ANSA", "Titolo")), NewsIngestResult::IO_ERROR);
  EXPECT_EQ(f.latestText(), "<none>");
}

TEST(BbsNewsIngest, ResultNames) {
  EXPECT_STREQ(newsResultName(NewsIngestResult::OK), "OK");
  EXPECT_STREQ(newsResultName(NewsIngestResult::BAD_SIGNATURE), "BAD_SIGNATURE");
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
