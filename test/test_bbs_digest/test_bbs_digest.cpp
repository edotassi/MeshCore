#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "bbs_digest.h"
#include "bbs_fake_filesystem.h"
#include "bbs_strings_it.h"
#include "bbs_user_store.h"

using namespace bbs;
using bbs_test::FakeFileSystem;

namespace {

// 2026-01-15 00:00:00 UTC (inverno: CET = UTC+1).
constexpr uint32_t kJan15 = 1768435200u;
// 2026-01-15 07:00 UTC = 08:00 locali.
constexpr uint32_t kJan15At0800 = 1768460400u;
constexpr uint32_t kMar29Before = 1774745999u;  // 00:59:59 UTC, ultima domenica di marzo 2026
constexpr uint32_t kMar29After = 1774746000u;   // 01:00:00 UTC
constexpr uint32_t kOct25Before = 1792889999u;  // 00:59:59 UTC, ultima domenica di ottobre 2026
constexpr uint32_t kOct25After = 1792890000u;   // 01:00:00 UTC
constexpr uint32_t kJul15 = 1784095200u;        // 2026-07-15 06:00 UTC (estate)

DigestSettings enabledAt0800() {
  DigestSettings s;
  s.enabled = true;
  return s;
}

void makeKey(uint8_t out[BBS_PUBKEY_LEN], uint8_t seed) {
  for (size_t i = 0; i < BBS_PUBKEY_LEN; i++) out[i] = (uint8_t)(seed + i);
}

}  // namespace

// --- base64 ---

TEST(BbsDigestKey, DecodesHexAndBase64) {
  uint8_t out[32];
  // Public PSK in esadecimale (come lo mostrano le app) e in base64.
  ASSERT_EQ(decodeKey("8b3387e9c5cdea6ac9e5edbaa115cd72", out, sizeof(out)), 16u);
  uint8_t b64[32];
  ASSERT_EQ(decodeKey("izOH6cXN6mrJ5e26oRXNcg==", b64, sizeof(b64)), 16u);
  EXPECT_EQ(memcmp(out, b64, 16), 0);
  ASSERT_EQ(decodeKey("8B3387E9C5CDEA6AC9E5EDBAA115CD72", out, sizeof(out)), 16u);
  EXPECT_EQ(memcmp(out, b64, 16), 0);
  std::string hex64(64, 'a');
  ASSERT_EQ(decodeKey(hex64.c_str(), out, sizeof(out)), 32u);
  EXPECT_EQ(out[31], 0xaa);
  // 31 cifre: non e' hex, letta come base64 da' 23 byte, che i chiamanti rifiutano.
  size_t n31 = decodeKey("8b3387e9c5cdea6ac9e5edbaa115cd7", out, sizeof(out));
  EXPECT_TRUE(n31 != 16 && n31 != 32);
  EXPECT_EQ(decodeKey(hex64.c_str(), out, 16), 0u);  // non entra
}

TEST(BbsDigestBase64, DecodesPublicPsk) {
  uint8_t out[32];
  ASSERT_EQ(base64Decode(kDigestPublicPskBase64, out, sizeof(out)), 16u);
  EXPECT_EQ(out[0], 0x8b);
  EXPECT_EQ(out[1], 0x33);
  EXPECT_EQ(out[15], 0x72);
}

TEST(BbsDigestBase64, Decodes32Bytes) {
  uint8_t out[32];
  // 32 byte a zero.
  EXPECT_EQ(base64Decode("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=", out, sizeof(out)), 32u);
}

TEST(BbsDigestBase64, RejectsInvalid) {
  uint8_t out[32];
  EXPECT_EQ(base64Decode("", out, sizeof(out)), 0u);
  EXPECT_EQ(base64Decode("abc$", out, sizeof(out)), 0u);
  EXPECT_EQ(base64Decode("a", out, sizeof(out)), 0u);
  EXPECT_EQ(base64Decode("izOH6cXN6mrJ5e26oRXNcg=", out, sizeof(out)), 0u);   // padding incoerente
  EXPECT_EQ(base64Decode("izOH 6cXN6mrJ5e26oRXNcg==", out, sizeof(out)), 0u);  // spazio
  // Non entra nel buffer.
  uint8_t small[4];
  EXPECT_EQ(base64Decode(kDigestPublicPskBase64, small, sizeof(small)), 0u);
}

// --- Store ---

TEST(BbsDigestStore, DefaultsWhenMissing) {
  FakeFileSystem fs;
  DigestStore store(fs);
  const DigestSettings& s = store.get();
  EXPECT_FALSE(s.enabled);
  EXPECT_EQ(s.hour, 8);
  EXPECT_EQ(s.minute, 0);
  EXPECT_EQ(s.tz_offset_min, 60);
  EXPECT_TRUE(s.eu_dst);
  EXPECT_EQ(s.psk_len, 16);
  EXPECT_TRUE(s.usesPublicPsk());
  EXPECT_EQ(s.last_sent_day, 0u);
  EXPECT_FALSE(store.takeNowRequest());
}

TEST(BbsDigestStore, RoundTrip) {
  FakeFileSystem fs;
  {
    DigestStore store(fs);
    DigestSettings s = store.get();
    s.enabled = true;
    s.hour = 21;
    s.minute = 45;
    s.tz_offset_min = -330;
    s.eu_dst = false;
    s.psk_len = 32;
    for (int i = 0; i < 32; i++) s.psk[i] = (uint8_t)(i + 1);
    s.last_sent_day = 20468;
    ASSERT_TRUE(store.save(s));
    EXPECT_EQ(store.get().hour, 21);  // applicato dopo la scrittura
  }
  DigestStore reloaded(fs);
  const DigestSettings& r = reloaded.get();
  EXPECT_TRUE(r.enabled);
  EXPECT_EQ(r.hour, 21);
  EXPECT_EQ(r.minute, 45);
  EXPECT_EQ(r.tz_offset_min, -330);
  EXPECT_FALSE(r.eu_dst);
  EXPECT_EQ(r.psk_len, 32);
  EXPECT_EQ(r.psk[31], 32);
  EXPECT_FALSE(r.usesPublicPsk());
  EXPECT_EQ(r.last_sent_day, 20468u);
}

TEST(BbsDigestStore, ShortFileGivesDefaults) {
  FakeFileSystem fs;
  IFile* f = fs.open("/bbs/digest.dat", 'w');
  uint8_t junk[5] = {1, 1, 9, 9, 9};
  f->write(junk, sizeof(junk));
  f->close();
  DigestStore store(fs);
  EXPECT_FALSE(store.get().enabled);
  EXPECT_EQ(store.get().hour, 8);
}

TEST(BbsDigestStore, BadVersionOrValuesGiveDefaults) {
  FakeFileSystem fs;
  {
    DigestStore store(fs);
    DigestSettings s = store.get();
    s.enabled = true;
    ASSERT_TRUE(store.save(s));
  }
  // Corrompe la versione.
  {
    IFile* f = fs.open("/bbs/digest.dat", 'r');
    uint8_t buf[64];
    size_t n = f->read(buf, sizeof(buf));
    f->close();
    buf[0] = 99;
    IFile* w = fs.open("/bbs/digest.dat", 'w');
    w->write(buf, n);
    w->close();
    DigestStore store(fs);
    EXPECT_FALSE(store.get().enabled);

    // Versione giusta ma ora fuori range.
    buf[0] = 1;
    buf[2] = 77;
    w = fs.open("/bbs/digest.dat", 'w');
    w->write(buf, n);
    w->close();
    DigestStore store2(fs);
    EXPECT_FALSE(store2.get().enabled);
    EXPECT_EQ(store2.get().hour, 8);
  }
}

TEST(BbsDigestStore, SaveLeavesNoTmpAndNowRequestIsOneShot) {
  FakeFileSystem fs;
  DigestStore store(fs);
  ASSERT_TRUE(store.save(store.get()));
  IFile* tmp = fs.open("/bbs/digest.dat.tmp", 'r');
  EXPECT_FALSE(tmp->valid());
  tmp->close();

  store.requestNow();
  EXPECT_TRUE(store.takeNowRequest());
  EXPECT_FALSE(store.takeNowRequest());
}

// --- Tempo locale e ora legale ---

TEST(BbsDigestTime, WinterAndSummerOffsets) {
  DigestSettings s;
  EXPECT_FALSE(isEuDst(kJan15));
  EXPECT_EQ(localTime(kJan15, s), (int64_t)kJan15 + 3600);
  EXPECT_TRUE(isEuDst(kJul15));
  EXPECT_EQ(localTime(kJul15, s), (int64_t)kJul15 + 7200);
  s.eu_dst = false;
  EXPECT_EQ(localTime(kJul15, s), (int64_t)kJul15 + 3600);
}

TEST(BbsDigestTime, DstTransitionsInMarchAndOctober2026) {
  EXPECT_FALSE(isEuDst(kMar29Before));
  EXPECT_TRUE(isEuDst(kMar29After));
  EXPECT_TRUE(isEuDst(kOct25Before));
  EXPECT_FALSE(isEuDst(kOct25After));
  // La domenica precedente non e' quella di transizione.
  EXPECT_FALSE(isEuDst(kMar29Before - 7 * 86400));
  EXPECT_TRUE(isEuDst(kOct25Before - 7 * 86400));
}

TEST(BbsDigestTime, CustomOffsets) {
  DigestSettings s;
  s.eu_dst = false;
  s.tz_offset_min = -300;  // UTC-5
  EXPECT_EQ(localTime(kJan15 + 3 * 3600, s), (int64_t)kJan15 - 2 * 3600);
  EXPECT_EQ(localMinutesOfDay(localTime(kJan15 + 3 * 3600, s)), 22u * 60);
  EXPECT_EQ(localDayNumber(localTime(kJan15 + 3 * 3600, s)), localDayNumber(kJan15) - 1);
  s.tz_offset_min = 345;  // UTC+5:45
  EXPECT_EQ(localMinutesOfDay(localTime(kJan15, s)), 5u * 60 + 45);
}

TEST(BbsDigestTime, CivilDate) {
  int y;
  unsigned m, d;
  civilFromDays(20468, y, m, d);
  EXPECT_EQ(y, 2026);
  EXPECT_EQ(m, 1u);
  EXPECT_EQ(d, 15u);
}

// --- decide ---

TEST(BbsDigestDecide, Disabled) {
  DigestSettings s;  // enabled = false
  EXPECT_EQ(decide(s, kJan15At0800), DigestDecision::WAIT);
}

TEST(BbsDigestDecide, InvalidClock) {
  DigestSettings s = enabledAt0800();
  EXPECT_EQ(decide(s, 1000), DigestDecision::WAIT);
  EXPECT_EQ(decide(s, kDigestMinValidEpoch - 1), DigestDecision::WAIT);
}

TEST(BbsDigestDecide, BeforeInAfterWindow) {
  DigestSettings s = enabledAt0800();
  EXPECT_EQ(decide(s, kJan15At0800 - 1), DigestDecision::WAIT);
  EXPECT_EQ(decide(s, kJan15At0800), DigestDecision::SEND);
  EXPECT_EQ(decide(s, kJan15At0800 + 119 * 60), DigestDecision::SEND);
  EXPECT_EQ(decide(s, kJan15At0800 + 120 * 60), DigestDecision::SKIP_DAY);
  EXPECT_EQ(decide(s, kJan15At0800 + 15 * 3600), DigestDecision::SKIP_DAY);  // 23:00 locali
}

TEST(BbsDigestDecide, AlreadySentTodayThenNextDay) {
  DigestSettings s = enabledAt0800();
  s.last_sent_day = localDayNumber(localTime(kJan15At0800, s));
  EXPECT_EQ(decide(s, kJan15At0800 + 60), DigestDecision::WAIT);
  EXPECT_EQ(decide(s, kJan15At0800 + 15 * 3600), DigestDecision::WAIT);
  EXPECT_EQ(decide(s, kJan15At0800 + 86400), DigestDecision::SEND);
}

TEST(BbsDigestDecide, DstShiftsScheduledInstant) {
  DigestSettings s = enabledAt0800();
  // D'estate le 08:00 locali sono le 06:00 UTC.
  EXPECT_EQ(decide(s, kJul15 - 1), DigestDecision::WAIT);
  EXPECT_EQ(decide(s, kJul15), DigestDecision::SEND);
  s.eu_dst = false;  // senza ora legale le 08:00 locali sono le 07:00 UTC
  EXPECT_EQ(decide(s, kJul15), DigestDecision::WAIT);
  EXPECT_EQ(decide(s, kJul15 + 3600), DigestDecision::SEND);
}

TEST(BbsDigestDecide, NegativeOffset) {
  DigestSettings s = enabledAt0800();
  s.eu_dst = false;
  s.tz_offset_min = -300;  // 08:00 locali = 13:00 UTC
  EXPECT_EQ(decide(s, kJan15 + 13 * 3600 - 1), DigestDecision::WAIT);
  EXPECT_EQ(decide(s, kJan15 + 13 * 3600), DigestDecision::SEND);
}

// --- formatDigest ---

namespace {

struct DigestFixture {
  FakeFileSystem fs;
  PostStore posts{fs};
  RoomRegistry rooms{fs};  // 0 Generale, 1 Annunci, 2 Tecnico
  static constexpr UserId kMario = 0;
  static constexpr UserId kAnna = 1;
  static constexpr uint32_t kNow = 1768500000u;

  std::string fmt(size_t cap = 256) {
    char out[512];
    size_t n = formatDigest(posts, rooms, kNow, out, cap < sizeof(out) ? cap : sizeof(out));
    EXPECT_EQ(n, strlen(out));
    return std::string(out, n);
  }
};

}  // namespace

TEST(BbsDigestFormat, NoPostsReturnsZero) {
  DigestFixture f;
  char out[64] = "x";
  EXPECT_EQ(formatDigest(f.posts, f.rooms, DigestFixture::kNow, out, sizeof(out)), 0u);
  EXPECT_STREQ(out, "");
}

TEST(BbsDigestFormat, CountsPerRoom) {
  DigestFixture f;
  uint32_t t = DigestFixture::kNow - 3600;
  f.posts.appendPost(0, f.kMario, t + 1, "a");
  f.posts.appendPost(0, f.kAnna, t + 2, "b");
  f.posts.appendPost(0, f.kMario, t + 3, "c");
  f.posts.appendPost(0, f.kMario, t + 4, "d");
  f.posts.appendPost(1, f.kMario, t + 10, "Raduno sabato");
  f.posts.appendPost(1, f.kAnna, t + 5, "e");
  f.posts.appendPost(2, f.kAnna, t + 6, "f");
  EXPECT_EQ(f.fmt(), "Ultime 24h: 7 post. Generale 4, Annunci 2, Tecnico 1");
}

TEST(BbsDigestFormat, NeverContainsPostTextOrAuthors) {
  DigestFixture f;
  uint8_t key[BBS_PUBKEY_LEN];
  makeKey(key, 1);
  UserStore users{f.fs};
  UserId mario = 0;
  users.registerUser(key, "mario", 1, mario);
  f.posts.appendPost(1, mario, DigestFixture::kNow - 10, "Raduno sabato in piazza");
  std::string s = f.fmt(512);
  EXPECT_EQ(s, "Ultime 24h: 1 post. Annunci 1");
  EXPECT_EQ(s.find("mario"), std::string::npos);
  EXPECT_EQ(s.find("Raduno"), std::string::npos);
}

TEST(BbsDigestFormat, OmitsEmptyRoomsAndIgnoresOldAndDeleted) {
  DigestFixture f;
  uint32_t now = DigestFixture::kNow;
  f.posts.appendPost(0, f.kMario, now - 86400 - 5, "vecchio");  // fuori finestra
  f.posts.appendPost(0, f.kMario, now - 86400, "limite");        // timestamp == soglia: escluso
  f.posts.appendPost(1, f.kAnna, now - 100, "recente");
  f.posts.appendPost(2, f.kMario, now - 50, "cancellato");
  ASSERT_TRUE(f.posts.deleteLastPost(2));
  EXPECT_EQ(f.fmt(), "Ultime 24h: 1 post. Annunci 1");
}

TEST(BbsDigestFormat, OnlyDeletedPostsMeansZero) {
  DigestFixture f;
  f.posts.appendPost(0, f.kMario, DigestFixture::kNow - 10, "x");
  f.posts.deleteLastPost(0);
  EXPECT_EQ(f.fmt(), "");
}

TEST(BbsDigestFormat, RoomListTruncatedWhenItDoesNotFit) {
  DigestFixture f;
  uint32_t t = DigestFixture::kNow - 100;
  f.posts.appendPost(0, f.kMario, t, "x");
  f.posts.appendPost(1, f.kMario, t + 1, "y");
  f.posts.appendPost(2, f.kMario, t + 2, "z");
  std::string s = f.fmt(40);
  EXPECT_LE(s.size(), 39u);
  EXPECT_EQ(s.rfind("Ultime 24h: 3 post. ", 0), 0u);
  EXPECT_EQ(s.substr(s.size() - 3), "...");
}

TEST(BbsDigestFormat, TinyCapNeverOverflows) {
  DigestFixture f;
  f.posts.appendPost(0, f.kMario, DigestFixture::kNow - 10, "ciao");
  for (size_t cap = 0; cap < 40; cap++) {
    char out[64];
    memset(out, 'Z', sizeof(out));
    size_t n = formatDigest(f.posts, f.rooms, DigestFixture::kNow, out, cap);
    if (cap == 0) {
      EXPECT_EQ(n, 0u);
      EXPECT_EQ(out[0], 'Z');
    } else {
      EXPECT_LE(n, cap - 1);
      EXPECT_EQ(out[n], 0);
    }
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
