#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "bbs_bot.h"
#include "bbs_digest.h"
#include "bbs_fake_filesystem.h"

using namespace bbs;
using bbs_test::FakeFileSystem;

namespace {

std::string reply(const char* sender, uint8_t hops, std::initializer_list<uint8_t> path, uint8_t hash_size,
                  size_t cap = 160) {
  std::string p(path.begin(), path.end());
  char out[256];
  size_t n = formatHopReply(sender, hops, (const uint8_t*)p.data(), hash_size, out, cap);
  EXPECT_EQ(n, strlen(out));
  EXPECT_LE(n, cap - 1);
  return std::string(out, n);
}

std::string senderOf(const char* text, bool expect_trigger = true) {
  char s[40] = "x";
  bool ok = parseTestTrigger(text, s, sizeof(s));
  EXPECT_EQ(ok, expect_trigger) << text;
  return ok ? std::string(s) : std::string();
}

}  // namespace

// --- Store ---

TEST(BbsBotStore, DefaultsWhenMissing) {
  FakeFileSystem fs;
  BotStore store(fs);
  const BotSettings& s = store.get();
  EXPECT_FALSE(s.enabled);
  ASSERT_EQ(s.count, 2);
  EXPECT_STREQ(s.channels[0].name, "Public");
  EXPECT_EQ(s.channels[0].psk_len, 16);
  uint8_t pub[32];
  ASSERT_EQ(base64Decode(kDigestPublicPskBase64, pub, sizeof(pub)), 16u);
  EXPECT_EQ(memcmp(s.channels[0].psk, pub, 16), 0);
  EXPECT_STREQ(s.channels[1].name, "#test");
  EXPECT_EQ(s.channels[1].psk_len, 0);
  EXPECT_TRUE(s.channels[1].isHashtag());
  EXPECT_EQ(store.generation(), 0u);
}

TEST(BbsBotStore, RoundTripAndGeneration) {
  FakeFileSystem fs;
  {
    BotStore store(fs);
    BotSettings s = store.get();
    s.enabled = true;
    BotChannel c;
    strcpy(c.name, "Locale");
    for (int i = 0; i < 32; i++) c.psk[i] = (uint8_t)(i + 1);
    c.psk_len = 32;
    s.channels[s.count++] = c;
    ASSERT_TRUE(store.save(s));
    EXPECT_EQ(store.generation(), 1u);
    EXPECT_EQ(store.get().count, 3);  // applicato dopo la scrittura
    ASSERT_TRUE(store.save(store.get()));
    EXPECT_EQ(store.generation(), 2u);
  }
  BotStore reloaded(fs);
  const BotSettings& r = reloaded.get();
  EXPECT_TRUE(r.enabled);
  ASSERT_EQ(r.count, 3);
  EXPECT_STREQ(r.channels[0].name, "Public");
  EXPECT_STREQ(r.channels[1].name, "#test");
  EXPECT_EQ(r.channels[1].psk_len, 0);
  EXPECT_STREQ(r.channels[2].name, "Locale");
  EXPECT_EQ(r.channels[2].psk_len, 32);
  EXPECT_EQ(r.channels[2].psk[31], 32);

  IFile* tmp = fs.open("/bbs/bot.dat.tmp", 'r');
  EXPECT_FALSE(tmp->valid());
  tmp->close();
}

TEST(BbsBotStore, EmptyChannelListRoundTrips) {
  FakeFileSystem fs;
  {
    BotStore store(fs);
    BotSettings s = store.get();
    s.enabled = true;
    s.count = 0;
    ASSERT_TRUE(store.save(s));
  }
  BotStore reloaded(fs);
  EXPECT_TRUE(reloaded.get().enabled);
  EXPECT_EQ(reloaded.get().count, 0);
}

TEST(BbsBotStore, ShortOrCorruptFileGivesDefaults) {
  FakeFileSystem fs;
  {
    IFile* f = fs.open("/bbs/bot.dat", 'w');
    uint8_t junk[5] = {1, 1, 2, 9, 9};
    f->write(junk, sizeof(junk));
    f->close();
    BotStore store(fs);
    EXPECT_FALSE(store.get().enabled);
    EXPECT_EQ(store.get().count, 2);
  }
  FakeFileSystem fs2;
  {
    BotStore store(fs2);
    BotSettings s = store.get();
    s.enabled = true;
    ASSERT_TRUE(store.save(s));
  }
  uint8_t buf[512];
  IFile* f = fs2.open("/bbs/bot.dat", 'r');
  size_t n = f->read(buf, sizeof(buf));
  f->close();
  ASSERT_GT(n, 10u);

  auto reload = [&](const uint8_t* data) {
    IFile* w = fs2.open("/bbs/bot.dat", 'w');
    w->write(data, n);
    w->close();
    return BotStore(fs2).get().enabled;
  };
  uint8_t mod[512];

  EXPECT_TRUE(reload(buf));
  memcpy(mod, buf, n);
  mod[0] = 99;  // versione ignota
  EXPECT_FALSE(reload(mod));
  memcpy(mod, buf, n);
  mod[2] = 9;  // count fuori range
  EXPECT_FALSE(reload(mod));
  memcpy(mod, buf, n);
  mod[3 + 24] = 7;  // psk_len non valido per il primo canale
  EXPECT_FALSE(reload(mod));
  memcpy(mod, buf, n);
  mod[3 + 24 + 33 + 1] = 'X';  // "#test" -> "#Xest": hashtag non normalizzato
  EXPECT_FALSE(reload(mod));
}

// Nota: la chiave dei canali hashtag (primi 16 byte di SHA-256 del nome, es.
// "#test" -> 9cd8fcf22a47333b591d96a2b848b73f) e' calcolata nell'adattatore.
// Nel build nativo SHA256 e' un mock (test/mocks/SHA256.h), quindi il valore
// reale non si puo' verificare qui.

// --- Nomi ---

TEST(BbsBotNames, HashtagNormalization) {
  char out[24];
  EXPECT_TRUE(normalizeHashtagName("#Test-1", out, sizeof(out)));
  EXPECT_STREQ(out, "#test-1");
  EXPECT_TRUE(normalizeHashtagName("#aaaaaaaaaaaaaaaaaaaaaa", out, sizeof(out)));  // 22
  EXPECT_FALSE(normalizeHashtagName("#aaaaaaaaaaaaaaaaaaaaaaa", out, sizeof(out)));  // 23
  EXPECT_FALSE(normalizeHashtagName("#", out, sizeof(out)));
  EXPECT_FALSE(normalizeHashtagName("test", out, sizeof(out)));
  EXPECT_FALSE(normalizeHashtagName("#te st", out, sizeof(out)));
  EXPECT_FALSE(normalizeHashtagName("#te_st", out, sizeof(out)));
}

TEST(BbsBotNames, KeyedNames) {
  EXPECT_TRUE(isValidKeyedName("Locale"));
  EXPECT_TRUE(isValidKeyedName("aaaaaaaaaaaaaaaaaaaaaaa"));  // 23
  EXPECT_FALSE(isValidKeyedName("aaaaaaaaaaaaaaaaaaaaaaaa"));  // 24
  EXPECT_FALSE(isValidKeyedName(""));
  EXPECT_FALSE(isValidKeyedName("a b"));
  EXPECT_FALSE(isValidKeyedName("#x"));
}

// --- Riconoscimento del comando ---

TEST(BbsBotTrigger, Accepted) {
  EXPECT_EQ(senderOf("mario: test"), "mario");
  EXPECT_EQ(senderOf("mario: !test"), "mario");
  EXPECT_EQ(senderOf("mario: TEST"), "mario");
  EXPECT_EQ(senderOf("mario:  !Test \r\n"), "mario");
  EXPECT_EQ(senderOf("Mario Rossi: test"), "Mario Rossi");
}

TEST(BbsBotTrigger, Rejected) {
  senderOf("mario: test 123", false);
  senderOf("mario: testo", false);
  senderOf("mario: !!test", false);
  senderOf("mario: ! test", false);
  senderOf("mario: tes", false);
  senderOf("mario: ", false);
  senderOf("test", false);
  senderOf("mario:test", false);
  senderOf(": test", false);
  senderOf("mario: test: x", false);
  senderOf("", false);
}

TEST(BbsBotTrigger, SenderTruncatedOnCodePoint) {
  char s[5];
  // "aàb" + ...: 'à' = 2 byte; con cap 3 entrano solo 2 byte -> "a" (non spezza 'à').
  char small[3];
  ASSERT_TRUE(parseTestTrigger("a\xC3\xA0" "b: test", small, sizeof(small)));
  EXPECT_STREQ(small, "a");
  ASSERT_TRUE(parseTestTrigger("abcdefgh: test", s, sizeof(s)));
  EXPECT_STREQ(s, "abcd");
}

// --- Risposta ---

TEST(BbsBotReply, ZeroHops) {
  EXPECT_EQ(reply("mario", 0, {}, 1), "@[mario] ricevuto diretto (0 hop)");
}

TEST(BbsBotReply, OneByteHashes) {
  EXPECT_EQ(reply("mario", 3, {0xA1, 0x7F, 0xC2}, 1), "@[mario] ricevuto con 3 hop via a1,7f,c2");
}

TEST(BbsBotReply, TwoAndThreeByteHashes) {
  EXPECT_EQ(reply("mario", 2, {0xA1, 0x02, 0x7F, 0x0B}, 2), "@[mario] ricevuto con 2 hop via a102,7f0b");
  EXPECT_EQ(reply("m", 1, {0x01, 0x02, 0xFF}, 3), "@[m] ricevuto con 1 hop via 0102ff");
}

TEST(BbsBotReply, TruncatesTrailingHopsWithEllipsis) {
  // Prefisso "@[mario] ricevuto con 5 hop via " = 32 caratteri.
  std::string full = reply("mario", 5, {1, 2, 3, 4, 5}, 1);
  EXPECT_EQ(full, "@[mario] ricevuto con 5 hop via 01,02,03,04,05");
  // cap 45 -> 44 byte utili: 5 hop interi non entrano (46), 4 hop + ",..." = 32+11+4 = 47 no, 3 hop + ",..." = 32+8+4 = 44 si.
  EXPECT_EQ(reply("mario", 5, {1, 2, 3, 4, 5}, 1, 45), "@[mario] ricevuto con 5 hop via 01,02,03,...");
  // Nessun hop intero entra: solo "...".
  EXPECT_EQ(reply("mario", 5, {1, 2, 3, 4, 5}, 1, 37), "@[mario] ricevuto con 5 hop via ...");
}

TEST(BbsBotReply, NeverExceedsCapAtAnySize) {
  for (size_t cap = 1; cap < 80; cap++) {
    for (uint8_t hs = 1; hs <= 3; hs++) {
      uint8_t path[18];
      for (int i = 0; i < 18; i++) path[i] = (uint8_t)(0x10 + i);
      for (uint8_t hops : {0, 1, 6}) {
        char out[128];
        memset(out, 'Z', sizeof(out));
        size_t n = formatHopReply("Mario \xC3\xA8", hops, path, hs, out, cap);
        ASSERT_LE(n, cap - 1) << cap;
        EXPECT_EQ(out[n], 0);
      }
    }
  }
}

TEST(BbsBotReply, Utf8SenderNeverSplit) {
  // Sender di sole lettere a 2 byte: con poco spazio viene accorciato.
  const char* sender = "\xC3\xA8\xC3\xA8\xC3\xA8\xC3\xA8\xC3\xA8\xC3\xA8\xC3\xA8\xC3\xA8";
  for (size_t cap = 20; cap < 60; cap++) {
    char out[128];
    size_t n = formatHopReply(sender, 2, (const uint8_t*)"\x01\x02", 1, out, cap);
    ASSERT_LE(n, cap - 1);
    // Ogni byte di continuazione deve seguire un byte lead.
    for (size_t i = 0; i < n; i++) {
      if (((uint8_t)out[i] & 0xC0) == 0x80) {
        ASSERT_GT(i, 0u);
        ASSERT_NE((uint8_t)out[i - 1] & 0x80, 0) << cap;
      }
    }
    size_t cont = 0, lead = 0;
    for (size_t i = 0; i < n; i++) {
      if (((uint8_t)out[i] & 0xC0) == 0x80) cont++;
      else if ((uint8_t)out[i] >= 0xC0) lead++;
    }
    EXPECT_EQ(cont, lead) << cap;
  }
  EXPECT_EQ(reply("\xC3\xA8", 0, {}, 1), "@[\xC3\xA8] ricevuto diretto (0 hop)");
}

// --- Cooldown ---

TEST(BbsBotCooldown, FirstAlwaysAllowedThenBlockedPerChannel) {
  BotCooldown c;
  EXPECT_TRUE(c.allow(0, 0));
  EXPECT_FALSE(c.allow(0, 1000));
  EXPECT_FALSE(c.allow(0, kBotCooldownMs - 1));
  EXPECT_TRUE(c.allow(1, 1000));  // canale diverso: indipendente
  EXPECT_TRUE(c.allow(0, kBotCooldownMs));
  EXPECT_FALSE(c.allow(0, kBotCooldownMs + 1));
  EXPECT_FALSE(c.allow(kBotMaxChannels, 0));
}

TEST(BbsBotCooldown, MillisWraparound) {
  BotCooldown c;
  const uint32_t near_wrap = 0xFFFFFFFFu - 5000u;
  EXPECT_TRUE(c.allow(0, near_wrap));
  EXPECT_FALSE(c.allow(0, 0xFFFFFFFFu));         // 5 s dopo
  EXPECT_FALSE(c.allow(0, 20000u));              // ~25 s dopo, oltre lo zero
  EXPECT_TRUE(c.allow(0, 30000u));               // ~35 s dopo
}

// --- ping ---

TEST(BbsBotPing, RecognizesPingAndFormatsPong) {
  char s[40];
  EXPECT_EQ(parseBotCommand("mario: ping", s, sizeof(s)), BotCommand::PING);
  EXPECT_STREQ(s, "mario");
  EXPECT_EQ(parseBotCommand("mario:  !PING ", s, sizeof(s)), BotCommand::PING);
  EXPECT_EQ(parseBotCommand("mario: ping?", s, sizeof(s)), BotCommand::NONE);
  EXPECT_EQ(parseBotCommand("mario: pinga", s, sizeof(s)), BotCommand::NONE);
  EXPECT_FALSE(parseTestTrigger("mario: ping", s, sizeof(s)));

  char out[160];
  const uint8_t path[] = {0xa1, 0x7f, 0xc2};
  formatHopReply("mario", 3, path, 1, out, sizeof(out), true);
  EXPECT_STREQ(out, "@[mario] pong, 3 hop via a1,7f,c2");
  formatHopReply("mario", 0, nullptr, 1, out, sizeof(out), true);
  EXPECT_STREQ(out, "@[mario] pong, diretto (0 hop)");
}

// --- parseBotCommand / !news ---

TEST(BbsBotCommand, RecognizesTestAndNews) {
  char s[40];
  EXPECT_EQ(parseBotCommand("mario: test", s, sizeof(s)), BotCommand::TEST);
  EXPECT_STREQ(s, "mario");
  EXPECT_EQ(parseBotCommand("mario: !TEST", s, sizeof(s)), BotCommand::TEST);
  EXPECT_EQ(parseBotCommand("mario: !news", s, sizeof(s)), BotCommand::NEWS);
  EXPECT_STREQ(s, "mario");
  EXPECT_EQ(parseBotCommand("Anna Rossi:  !NeWs  ", s, sizeof(s)), BotCommand::NEWS);
  EXPECT_STREQ(s, "Anna Rossi");
}

TEST(BbsBotCommand, RejectsNearMisses) {
  char s[40];
  EXPECT_EQ(parseBotCommand("mario: news", s, sizeof(s)), BotCommand::NONE);
  EXPECT_EQ(parseBotCommand("mario: !news oggi", s, sizeof(s)), BotCommand::NONE);
  EXPECT_EQ(parseBotCommand("mario: !newsx", s, sizeof(s)), BotCommand::NONE);
  EXPECT_EQ(parseBotCommand("mario: !!news", s, sizeof(s)), BotCommand::NONE);
  EXPECT_EQ(parseBotCommand("!news", s, sizeof(s)), BotCommand::NONE);   // senza mittente
  EXPECT_EQ(parseBotCommand(": !news", s, sizeof(s)), BotCommand::NONE);
  EXPECT_EQ(parseBotCommand("mario: !news", s, 0), BotCommand::NONE);
  // parseTestTrigger resta solo per "test".
  EXPECT_FALSE(parseTestTrigger("mario: !news", s, sizeof(s)));
  EXPECT_TRUE(parseTestTrigger("mario: test", s, sizeof(s)));
}

TEST(BbsBotNewsReply, WithNewsAndWithout) {
  char out[160];
  size_t n = formatNewsReply("mario", "[ANSA] Titolo di prova", out, sizeof(out));
  EXPECT_EQ(std::string(out, n), "@[mario] [ANSA] Titolo di prova");
  n = formatNewsReply("mario", nullptr, out, sizeof(out));
  EXPECT_EQ(std::string(out, n), "@[mario] nessuna news disponibile");
  n = formatNewsReply("mario", "", out, sizeof(out));
  EXPECT_EQ(std::string(out, n), "@[mario] nessuna news disponibile");
}

TEST(BbsBotNewsReply, TruncatesOnUtf8BoundaryWithinBudget) {
  // Il testo ha 'e' accentate da 2 byte: ogni taglio deve cadere su un confine.
  std::string text = "[ANSA] ";
  for (int i = 0; i < 40; i++) text += "\xC3\xA8";
  for (size_t cap = 2; cap < 140; cap++) {
    char out[160];
    size_t n = formatNewsReply("mario", text.c_str(), out, cap);
    ASSERT_EQ(n, strlen(out));
    ASSERT_LE(n, cap - 1);
    // Nessun byte di continuazione orfano in coda.
    size_t k = n;
    while (k > 0 && ((uint8_t)out[k - 1] & 0xC0) == 0x80) k--;
    if (k > 0 && (uint8_t)out[k - 1] >= 0xC0) {
      size_t expect = ((uint8_t)out[k - 1] >= 0xF0) ? 4 : ((uint8_t)out[k - 1] >= 0xE0) ? 3 : 2;
      ASSERT_EQ(n - (k - 1), expect) << "cap=" << cap;
    }
  }
}

TEST(BbsBotNewsReply, LongSenderIsShortenedOnlyIfNeeded) {
  char out[64];
  std::string sender(39, 's');
  size_t n = formatNewsReply(sender.c_str(), "[ANSA] titolo lungo abbastanza", out, 32);
  EXPECT_LE(n, 31u);
  EXPECT_EQ(std::string(out, 2), "@[");
  EXPECT_NE(std::string(out, n).find("] [ANSA"), std::string::npos);  // il testo non sparisce
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
