#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "bbs_command_parser.h"
#include "bbs_fake_filesystem.h"
#include "bbs_fake_reply_channel.h"
#include "bbs_strings_it.h"

using namespace bbs;
using bbs_test::FakeFileSystem;
using bbs_test::FakeReplyChannel;

namespace {

void makeKey(uint8_t out[BBS_PUBKEY_LEN], uint8_t seed) {
  for (size_t i = 0; i < BBS_PUBKEY_LEN; i++) out[i] = (uint8_t)(seed + i);
}

struct Fixture {
  FakeFileSystem fs;
  UserStore users{fs};
  PendingWelcomeTable welcome{fs};
  PostStore posts{fs};
  MailStore mail{fs};
  SessionTable sessions;
  FakeReplyChannel reply;
  RoomRegistry room_registry{fs};
  Notifier notifier{users, sessions, room_registry, reply};
  RoomState rooms{fs};
  ModLog modlog;
  MotdStore motd{fs};

  std::string run(const uint8_t* pub_key, const char* input, uint32_t now_ts = 1000) {
    CommandContext ctx{users,    welcome, posts, mail,          sessions,
                        notifier, rooms,   room_registry, modlog, motd, pub_key, now_ts};
    char out[256];
    size_t n = processCommand(ctx, input, out, sizeof(out));
    return std::string(out, n);
  }
};

} // namespace

TEST(BbsCommandParser, UnknownFromUnregisteredGetsWelcomeOnce) {
  Fixture f;
  uint8_t key[BBS_PUBKEY_LEN];
  makeKey(key, 1);
  EXPECT_EQ(f.run(key, "ciao"), std::string(strings::kWelcome));
  EXPECT_EQ(f.run(key, "ciao"), std::string(strings::kUnknownPreRegister));
}

TEST(BbsCommandParser, HelpBeforeRegistrationIsReduced) {
  Fixture f;
  uint8_t key[BBS_PUBKEY_LEN];
  makeKey(key, 2);
  EXPECT_EQ(f.run(key, "H"), std::string(strings::kHelpPreRegister));
}

TEST(BbsCommandParser, LoginBeforeRegisterFails) {
  Fixture f;
  uint8_t key[BBS_PUBKEY_LEN];
  makeKey(key, 3);
  EXPECT_EQ(f.run(key, "LOGIN"), std::string(strings::kLoginNoAccount));
}

TEST(BbsCommandParser, RegisterThenLoginWorks) {
  Fixture f;
  uint8_t key[BBS_PUBKEY_LEN];
  makeKey(key, 4);
  auto reg = f.run(key, "REGISTER mario");
  EXPECT_EQ(reg, std::string(strings::kRegisterOkPrefix) + "mario");

  char expected[64];
  snprintf(expected, sizeof(expected), strings::kLoginWelcomeBackFmt, 0u, 0u);
  EXPECT_EQ(f.run(key, "LOGIN"), std::string(expected));
}

TEST(BbsCommandParser, LoginReportsUnreadCountsForSubscribedRoomsAndMail) {
  Fixture f;
  uint8_t author[BBS_PUBKEY_LEN], reader[BBS_PUBKEY_LEN];
  makeKey(author, 30);
  makeKey(reader, 31);
  f.run(author, "REGISTER pina");
  f.run(reader, "REGISTER remo");  // iscritto di default a Generale (0) e Annunci (1)

  f.run(author, "E ciao", 100);          // in Generale (0), sottoscritta di default
  f.run(author, "E 2 tecnico", 101);     // in Tecnico (2), NON sottoscritta di default
  f.run(author, "M remo ciao privato", 102);

  char expected[64];
  snprintf(expected, sizeof(expected), strings::kLoginWelcomeBackFmt, 1u, 1u);
  EXPECT_EQ(f.run(reader, "LOGIN", 200), std::string(expected));
}

TEST(BbsCommandParser, SubscribeThenLoginCountsTheNewRoomToo) {
  Fixture f;
  uint8_t author[BBS_PUBKEY_LEN], reader[BBS_PUBKEY_LEN];
  makeKey(author, 32);
  makeKey(reader, 33);
  f.run(author, "REGISTER uno");
  f.run(reader, "REGISTER due");

  auto sub = f.run(reader, "S 2");
  EXPECT_EQ(sub, std::string(strings::kSubscribedPrefix) + "Tecnico");

  f.run(author, "E 2 novita' tecniche", 100);

  char expected[64];
  snprintf(expected, sizeof(expected), strings::kLoginWelcomeBackFmt, 1u, 0u);
  EXPECT_EQ(f.run(reader, "LOGIN", 200), std::string(expected));
}

TEST(BbsCommandParser, UnsubscribeStopsCountingThatRoom) {
  Fixture f;
  uint8_t author[BBS_PUBKEY_LEN], reader[BBS_PUBKEY_LEN];
  makeKey(author, 34);
  makeKey(reader, 35);
  f.run(author, "REGISTER tre");
  f.run(reader, "REGISTER quattro");  // iscritto di default a 0 e 1

  auto unsub = f.run(reader, "U 1");
  EXPECT_EQ(unsub, std::string(strings::kUnsubscribedPrefix) + "Annunci");

  f.run(author, "E 1 annuncio", 100);

  char expected[64];
  snprintf(expected, sizeof(expected), strings::kLoginWelcomeBackFmt, 0u, 0u);
  EXPECT_EQ(f.run(reader, "LOGIN", 200), std::string(expected));
}

TEST(BbsCommandParser, SubscribeInvalidRoomRejected) {
  Fixture f;
  uint8_t key[BBS_PUBKEY_LEN];
  makeKey(key, 36);
  f.run(key, "REGISTER cinque");
  EXPECT_EQ(f.run(key, "S 9"), std::string(strings::kInvalidRoom));
  EXPECT_EQ(f.run(key, "S"), std::string(strings::kInvalidRoom));
}

TEST(BbsCommandParser, RegisterTwiceIsIdempotentMessage) {
  Fixture f;
  uint8_t key[BBS_PUBKEY_LEN];
  makeKey(key, 5);
  f.run(key, "REGISTER ada");
  auto second = f.run(key, "REGISTER ada");
  EXPECT_EQ(second, std::string(strings::kAlreadyRegisteredPrefix) + "ada");
}

TEST(BbsCommandParser, InvalidNicknameRejected) {
  Fixture f;
  uint8_t key[BBS_PUBKEY_LEN];
  makeKey(key, 6);
  EXPECT_EQ(f.run(key, "REGISTER"), std::string(strings::kInvalidNickname));
}

TEST(BbsCommandParser, LogoutAfterRegisterAcks) {
  Fixture f;
  uint8_t key[BBS_PUBKEY_LEN];
  makeKey(key, 7);
  f.run(key, "REGISTER bob");
  EXPECT_EQ(f.run(key, "LOGOUT"), std::string(strings::kLogoutAck));
}

TEST(BbsCommandParser, UnknownCommandAfterRegistration) {
  Fixture f;
  uint8_t key[BBS_PUBKEY_LEN];
  makeKey(key, 8);
  f.run(key, "REGISTER carla");
  EXPECT_EQ(f.run(key, "XYZ"), std::string(strings::kUnknownPostRegister));
}

TEST(BbsCommandParser, HelpAfterRegistrationIsFull) {
  Fixture f;
  uint8_t key[BBS_PUBKEY_LEN];
  makeKey(key, 9);
  f.run(key, "REGISTER dino");
  EXPECT_EQ(f.run(key, "H"), std::string(strings::kHelpPostRegister));
}

TEST(BbsCommandParser, RoomsListsAllActiveRooms) {
  Fixture f;
  uint8_t key[BBS_PUBKEY_LEN];
  makeKey(key, 10);
  f.run(key, "REGISTER eva");
  auto k = f.run(key, "K");
  EXPECT_NE(k.find("0:Generale"), std::string::npos);
  EXPECT_NE(k.find("1:Annunci"), std::string::npos);
  EXPECT_NE(k.find("2:Tecnico"), std::string::npos);
}

TEST(BbsCommandParser, RoomsShowsUnreadAndTotalPostCountPerRoom) {
  // L'autore non ha mai chiamato N: per lui "da leggere" ed "totale"
  // coincidono sempre, in tutte le stanze.
  Fixture f;
  uint8_t key[BBS_PUBKEY_LEN];
  makeKey(key, 11);
  f.run(key, "REGISTER furio");
  EXPECT_EQ(f.run(key, "K"), "0:Generale(0/0)\n1:Annunci(0/0)\n2:Tecnico(0/0)");

  f.run(key, "E uno", 100);
  f.run(key, "E due", 200);
  f.run(key, "E 2 tecnico", 300);
  EXPECT_EQ(f.run(key, "K"), "0:Generale(2/2)\n1:Annunci(0/0)\n2:Tecnico(1/1)");

  f.run(key, "DELPOST 0");  // cancellato: non deve piu' contare ne' come letto ne' come totale
  EXPECT_EQ(f.run(key, "K"), "0:Generale(1/1)\n1:Annunci(0/0)\n2:Tecnico(1/1)");
}

TEST(BbsCommandParser, RoomsUnreadDropsAfterReadingButTotalStaysTheSame) {
  Fixture f;
  uint8_t author[BBS_PUBKEY_LEN], reader[BBS_PUBKEY_LEN];
  makeKey(author, 12);
  makeKey(reader, 13);
  f.run(author, "REGISTER autoreK");
  f.run(reader, "REGISTER lettoreK");

  f.run(author, "E primo", 100);
  f.run(author, "E secondo", 200);

  // Il lettore non ha ancora letto nulla: da leggere == totale.
  EXPECT_EQ(f.run(reader, "K", 300), "0:Generale(2/2)\n1:Annunci(0/0)\n2:Tecnico(0/0)");

  f.run(reader, "N", 300);  // legge "primo"

  // Ora ha un solo da leggere, ma il totale della stanza resta 2.
  EXPECT_EQ(f.run(reader, "K", 300), "0:Generale(1/2)\n1:Annunci(0/0)\n2:Tecnico(0/0)");

  // Per l'autore, che non ha mai letto, restano entrambi i post da leggere.
  EXPECT_EQ(f.run(author, "K", 300), "0:Generale(2/2)\n1:Annunci(0/0)\n2:Tecnico(0/0)");
}

TEST(BbsCommandParser, PostThenReadInDefaultRoom) {
  Fixture f;
  uint8_t author[BBS_PUBKEY_LEN], reader[BBS_PUBKEY_LEN];
  makeKey(author, 11);
  makeKey(reader, 12);
  f.run(author, "REGISTER pino");
  f.run(reader, "REGISTER lina");

  auto postReply = f.run(author, "E ciao a tutti", 2000);
  EXPECT_EQ(postReply, std::string(strings::kPostOkPrefix) + "Generale");

  auto readReply = f.run(reader, "N", 2001);
  EXPECT_EQ(readReply, "pino: ciao a tutti");

  // Nessun altro messaggio nuovo dopo averlo letto.
  EXPECT_EQ(f.run(reader, "N", 2002), std::string(strings::kNoNewPostsPrefix) + "Generale");
}

TEST(BbsCommandParser, PostAndReadInExplicitRoom) {
  Fixture f;
  uint8_t author[BBS_PUBKEY_LEN], reader[BBS_PUBKEY_LEN];
  makeKey(author, 13);
  makeKey(reader, 14);
  f.run(author, "REGISTER gino");
  f.run(reader, "REGISTER sara");

  auto postReply = f.run(author, "E 2 guasto antenna", 3000);
  EXPECT_EQ(postReply, std::string(strings::kPostOkPrefix) + "Tecnico");

  // Nella stanza di default (0) non c'e' nulla di nuovo.
  EXPECT_EQ(f.run(reader, "N", 3001), std::string(strings::kNoNewPostsPrefix) + "Generale");
  // Nella stanza 2 (Tecnico) invece si'.
  EXPECT_EQ(f.run(reader, "N 2", 3002), "gino: guasto antenna");
}

TEST(BbsCommandParser, InvalidRoomIdRejected) {
  Fixture f;
  uint8_t key[BBS_PUBKEY_LEN];
  makeKey(key, 15);
  f.run(key, "REGISTER remo");
  EXPECT_EQ(f.run(key, "E 9 testo"), std::string(strings::kInvalidRoom));
  EXPECT_EQ(f.run(key, "N 9"), std::string(strings::kInvalidRoom));
}

TEST(BbsCommandParser, EmptyPostTextRejected) {
  Fixture f;
  uint8_t key[BBS_PUBKEY_LEN];
  makeKey(key, 16);
  f.run(key, "REGISTER teo");
  EXPECT_EQ(f.run(key, "E"), std::string(strings::kPostEmptyText));
  EXPECT_EQ(f.run(key, "E 1"), std::string(strings::kPostEmptyText));
}

TEST(BbsCommandParser, MailSendThenReadByRecipient) {
  Fixture f;
  uint8_t alice[BBS_PUBKEY_LEN], bob[BBS_PUBKEY_LEN];
  makeKey(alice, 20);
  makeKey(bob, 21);
  f.run(alice, "REGISTER alice");
  f.run(bob, "REGISTER bob");

  auto sendReply = f.run(alice, "M bob ci vediamo domani", 5000);
  EXPECT_EQ(sendReply, std::string(strings::kMailSentPrefix) + "bob");

  // Alice non ha mail per se' (il destinatario e' bob).
  EXPECT_EQ(f.run(alice, "M", 5001), std::string(strings::kMailNoNew));

  auto readReply = f.run(bob, "M", 5002);
  EXPECT_EQ(readReply, "alice: ci vediamo domani");

  // Bob l'ha gia' letta.
  EXPECT_EQ(f.run(bob, "M", 5003), std::string(strings::kMailNoNew));
}

TEST(BbsCommandParser, MailToUnknownNicknameRejected) {
  Fixture f;
  uint8_t key[BBS_PUBKEY_LEN];
  makeKey(key, 22);
  f.run(key, "REGISTER carlo");
  EXPECT_EQ(f.run(key, "M nessuno ciao"), std::string(strings::kMailUnknownRecipient));
}

TEST(BbsCommandParser, MailEmptyTextRejected) {
  Fixture f;
  uint8_t sender[BBS_PUBKEY_LEN], recipient[BBS_PUBKEY_LEN];
  makeKey(sender, 23);
  makeKey(recipient, 24);
  f.run(sender, "REGISTER dario");
  f.run(recipient, "REGISTER elena");
  EXPECT_EQ(f.run(sender, "M elena"), std::string(strings::kMailEmptyText));
}

TEST(BbsCommandParser, MailIsDeliveredAcrossDifferentRooms) {
  // La mail e' indipendente dalle stanze: un post in una stanza non la tocca.
  Fixture f;
  uint8_t alice[BBS_PUBKEY_LEN], bob[BBS_PUBKEY_LEN];
  makeKey(alice, 25);
  makeKey(bob, 26);
  f.run(alice, "REGISTER filo");
  f.run(bob, "REGISTER gaia");
  f.run(alice, "E messaggio pubblico", 6000);
  f.run(alice, "M gaia messaggio privato", 6001);

  EXPECT_EQ(f.run(bob, "M", 6002), "filo: messaggio privato");
}

TEST(BbsCommandParser, FirstRegisteredUserCanModerate) {
  Fixture f;
  uint8_t admin[BBS_PUBKEY_LEN], other[BBS_PUBKEY_LEN];
  makeKey(admin, 40);
  makeKey(other, 41);
  f.run(admin, "REGISTER capo");   // primo: admin
  f.run(other, "REGISTER pino");   // secondo: utente semplice

  EXPECT_EQ(f.run(admin, "BAN pino"), std::string(strings::kBanOkPrefix) + "pino");
}

TEST(BbsCommandParser, PlainUserCannotModerate) {
  Fixture f;
  uint8_t admin[BBS_PUBKEY_LEN], other[BBS_PUBKEY_LEN];
  makeKey(admin, 42);
  makeKey(other, 43);
  f.run(admin, "REGISTER capo2");
  f.run(other, "REGISTER lino");

  EXPECT_EQ(f.run(other, "BAN capo2"), std::string(strings::kPermissionDenied));
}

TEST(BbsCommandParser, BannedUserGetsNoReplyToAnything) {
  Fixture f;
  uint8_t admin[BBS_PUBKEY_LEN], target[BBS_PUBKEY_LEN];
  makeKey(admin, 44);
  makeKey(target, 45);
  f.run(admin, "REGISTER capo3");
  f.run(target, "REGISTER birbo");
  f.run(admin, "BAN birbo");

  EXPECT_EQ(f.run(target, "H"), "");
  EXPECT_EQ(f.run(target, "K"), "");
  EXPECT_EQ(f.run(target, "LOGIN"), "");
}

TEST(BbsCommandParser, UnbanRestoresNormalReplies) {
  Fixture f;
  uint8_t admin[BBS_PUBKEY_LEN], target[BBS_PUBKEY_LEN];
  makeKey(admin, 46);
  makeKey(target, 47);
  f.run(admin, "REGISTER capo4");
  f.run(target, "REGISTER birbo2");
  f.run(admin, "BAN birbo2");
  ASSERT_EQ(f.run(target, "H"), "");

  EXPECT_EQ(f.run(admin, "UNBAN birbo2"), std::string(strings::kUnbanOkPrefix) + "birbo2");
  EXPECT_EQ(f.run(target, "H"), std::string(strings::kHelpPostRegister));
}

TEST(BbsCommandParser, MutedUserCannotPostOrMailButCanRead) {
  Fixture f;
  uint8_t admin[BBS_PUBKEY_LEN], target[BBS_PUBKEY_LEN];
  makeKey(admin, 48);
  makeKey(target, 49);
  f.run(admin, "REGISTER capo5");
  f.run(target, "REGISTER silente");
  f.run(admin, "MUTE silente");

  EXPECT_EQ(f.run(target, "E ciao"), std::string(strings::kMutedCannotPost));
  EXPECT_EQ(f.run(target, "M capo5 ciao"), std::string(strings::kMutedCannotPost));
  EXPECT_EQ(f.run(target, "K"), "0:Generale(0/0)\n1:Annunci(0/0)\n2:Tecnico(0/0)");  // la lettura resta permessa

  f.run(admin, "UNMUTE silente");
  EXPECT_EQ(f.run(target, "E finalmente"), std::string(strings::kPostOkPrefix) + "Generale");
}

TEST(BbsCommandParser, ClosedRoomRejectsNewPostsButStaysReadable) {
  Fixture f;
  uint8_t admin[BBS_PUBKEY_LEN], reader[BBS_PUBKEY_LEN];
  makeKey(admin, 50);
  makeKey(reader, 51);
  f.run(admin, "REGISTER capo6");
  f.run(reader, "REGISTER letto");

  f.run(admin, "E prima della chiusura", 100);
  EXPECT_EQ(f.run(admin, "CLOSE 0"), std::string(strings::kCloseOkPrefix) + "Generale");
  EXPECT_EQ(f.run(admin, "E dopo la chiusura", 200), std::string(strings::kRoomClosed));

  EXPECT_EQ(f.run(reader, "N", 300), "capo6: prima della chiusura");  // ancora leggibile

  EXPECT_EQ(f.run(admin, "OPEN 0"), std::string(strings::kOpenOkPrefix) + "Generale");
  EXPECT_EQ(f.run(admin, "E dopo la riapertura", 400), std::string(strings::kPostOkPrefix) + "Generale");
}

TEST(BbsCommandParser, DelPostRemovesOnlyTheLastOne) {
  Fixture f;
  uint8_t admin[BBS_PUBKEY_LEN], reader[BBS_PUBKEY_LEN];
  makeKey(admin, 52);
  makeKey(reader, 53);
  f.run(admin, "REGISTER capo7");
  f.run(reader, "REGISTER letto2");

  f.run(admin, "E resta", 100);
  f.run(admin, "E sparisce", 200);
  EXPECT_EQ(f.run(admin, "DELPOST 0"), std::string(strings::kDelPostOkPrefix) + "Generale");

  EXPECT_EQ(f.run(reader, "N", 300), "capo7: resta");
  EXPECT_EQ(f.run(reader, "N", 400), std::string(strings::kNoNewPostsPrefix) + "Generale");
}

TEST(BbsCommandParser, DelPostWithNothingToDeleteReportsIt) {
  Fixture f;
  uint8_t admin[BBS_PUBKEY_LEN];
  makeKey(admin, 54);
  f.run(admin, "REGISTER capo8");
  EXPECT_EQ(f.run(admin, "DELPOST 0"), std::string(strings::kDelPostNothingToDelete));
}

TEST(BbsCommandParser, SetModThenModeratorCanBanButNotPromote) {
  Fixture f;
  uint8_t admin[BBS_PUBKEY_LEN], mod[BBS_PUBKEY_LEN], other[BBS_PUBKEY_LEN];
  makeKey(admin, 55);
  makeKey(mod, 56);
  makeKey(other, 57);
  f.run(admin, "REGISTER capo9");
  f.run(mod, "REGISTER mod1");
  f.run(other, "REGISTER terzo");

  EXPECT_EQ(f.run(admin, "SETMOD mod1"), std::string(strings::kSetModOkPrefix) + "mod1");
  EXPECT_EQ(f.run(mod, "BAN terzo"), std::string(strings::kBanOkPrefix) + "terzo");
  // un moderatore non puo' promuovere altri: SETMOD/SETADMIN restano da admin.
  EXPECT_EQ(f.run(mod, "UNBAN terzo"), std::string(strings::kUnbanOkPrefix) + "terzo");
  EXPECT_EQ(f.run(mod, "SETADMIN terzo"), std::string(strings::kPermissionDenied));
}

TEST(BbsCommandParser, SetAdminAndSetUserRoundTrip) {
  Fixture f;
  uint8_t admin[BBS_PUBKEY_LEN], other[BBS_PUBKEY_LEN];
  makeKey(admin, 58);
  makeKey(other, 59);
  f.run(admin, "REGISTER capo10");
  f.run(other, "REGISTER futuro");

  EXPECT_EQ(f.run(admin, "SETADMIN futuro"), std::string(strings::kSetAdminOkPrefix) + "futuro");
  EXPECT_EQ(f.run(other, "SETUSER capo10"), std::string(strings::kSetUserOkPrefix) + "capo10");
  // ora capo10 e' un utente semplice: non puo' piu' bannare.
  EXPECT_EQ(f.run(admin, "BAN futuro"), std::string(strings::kPermissionDenied));
}

TEST(BbsCommandParser, ModerationTargetNotFoundReported) {
  Fixture f;
  uint8_t admin[BBS_PUBKEY_LEN];
  makeKey(admin, 60);
  f.run(admin, "REGISTER capo11");
  EXPECT_EQ(f.run(admin, "BAN fantasma"), std::string(strings::kTargetNotFound));
}

TEST(BbsCommandParser, ModLogCountsRecordedActions) {
  Fixture f;
  uint8_t admin[BBS_PUBKEY_LEN], other[BBS_PUBKEY_LEN];
  makeKey(admin, 61);
  makeKey(other, 62);
  f.run(admin, "REGISTER capo12");
  f.run(other, "REGISTER quarto");

  char expected0[64];
  snprintf(expected0, sizeof(expected0), strings::kModLogCountFmt, 0u);
  EXPECT_EQ(f.run(admin, "MODLOG"), std::string(expected0));

  f.run(admin, "BAN quarto");
  f.run(admin, "UNBAN quarto");

  char expected2[64];
  snprintf(expected2, sizeof(expected2), strings::kModLogCountFmt, 2u);
  EXPECT_EQ(f.run(admin, "MODLOG"), std::string(expected2));
}

TEST(BbsCommandParser, RateLimitBlocksExcessPostsThenRecoversNextWindow) {
  Fixture f;
  uint8_t key[BBS_PUBKEY_LEN];
  makeKey(key, 63);
  f.run(key, "REGISTER prolifico");

  for (uint8_t i = 0; i < BBS_MAX_MESSAGES_PER_MINUTE; i++) {
    auto reply = f.run(key, "E messaggio", 1000);
    EXPECT_EQ(reply, std::string(strings::kPostOkPrefix) + "Generale");
  }
  EXPECT_EQ(f.run(key, "E troppo", 1000), std::string(strings::kRateLimited));
  EXPECT_EQ(f.run(key, "E ancora troppo", 1030), std::string(strings::kRateLimited));
  EXPECT_EQ(f.run(key, "E finalmente ok", 1060), std::string(strings::kPostOkPrefix) + "Generale");
}

TEST(BbsCommandParser, WhoListsActiveUsersOnly) {
  Fixture f;
  uint8_t a[BBS_PUBKEY_LEN], b[BBS_PUBKEY_LEN], c[BBS_PUBKEY_LEN];
  makeKey(a, 70);
  makeKey(b, 71);
  makeKey(c, 72);
  f.run(a, "REGISTER anna", 1000);
  f.run(b, "REGISTER bruno", 1000);
  f.run(c, "REGISTER carlo", 1000);
  f.run(c, "LOGOUT", 1000);  // carlo non e' piu' attivo

  auto who = f.run(a, "WHO", 1000);
  EXPECT_NE(who.find("anna"), std::string::npos);
  EXPECT_NE(who.find("bruno"), std::string::npos);
  EXPECT_EQ(who.find("carlo"), std::string::npos);
}

TEST(BbsCommandParser, WhoAfterLogoutStillShowsAskerSinceAskingIsActivity) {
  // Chiedere WHO e' a sua volta un comando, quindi riattiva chi lo chiede
  // (stesso principio per cui LOGIN/H/qualunque comando rinnova la sessione):
  // dopo un LOGOUT, il primo WHO successivo mostra comunque chi lo chiede.
  // "Nessuno online" (kWhoNoOne) resta un ramo difensivo per uno stato che
  // l'interfaccia a comandi non puo' produrre da sola.
  Fixture f;
  uint8_t a[BBS_PUBKEY_LEN];
  makeKey(a, 73);
  f.run(a, "REGISTER solo1", 1000);
  f.run(a, "LOGOUT", 1000);
  EXPECT_EQ(f.run(a, "WHO", 1000), "solo1");
}

TEST(BbsCommandParser, StatsReportsCounts) {
  Fixture f;
  uint8_t a[BBS_PUBKEY_LEN], b[BBS_PUBKEY_LEN];
  makeKey(a, 74);
  makeKey(b, 75);
  f.run(a, "REGISTER autoreS", 1000);
  f.run(b, "REGISTER lettoreS", 1000);
  f.run(a, "E un post", 1000);
  f.run(a, "M lettoreS una mail", 1000);

  char expected[96];
  snprintf(expected, sizeof(expected), strings::kStatsFmt, 2u, 2u, 1u, 1u);
  EXPECT_EQ(f.run(a, "STATS", 1000), std::string(expected));
}

TEST(BbsCommandParser, SearchFindsPostAcrossUsers) {
  Fixture f;
  uint8_t a[BBS_PUBKEY_LEN], b[BBS_PUBKEY_LEN];
  makeKey(a, 76);
  makeKey(b, 77);
  f.run(a, "REGISTER autoreSe");
  f.run(b, "REGISTER cercatore");
  f.run(a, "E antenna rotta oggi");

  EXPECT_EQ(f.run(b, "SEARCH 0 antenna"), "autoreSe: antenna rotta oggi");
}

TEST(BbsCommandParser, SearchNoMatchOrEmptyKeyword) {
  Fixture f;
  uint8_t a[BBS_PUBKEY_LEN];
  makeKey(a, 78);
  f.run(a, "REGISTER cercatore2");
  EXPECT_EQ(f.run(a, "SEARCH 0"), std::string(strings::kSearchEmptyKeyword));
  EXPECT_EQ(f.run(a, "SEARCH 0 introvabile"), std::string(strings::kSearchNoMatch));
  EXPECT_EQ(f.run(a, "SEARCH 9 qualcosa"), std::string(strings::kInvalidRoom));
}

TEST(BbsCommandParser, MotdNotSetByDefault) {
  Fixture f;
  uint8_t a[BBS_PUBKEY_LEN];
  makeKey(a, 79);
  f.run(a, "REGISTER utenteM");
  EXPECT_EQ(f.run(a, "MOTD"), std::string(strings::kMotdNotSet));
}

TEST(BbsCommandParser, AdminSetsMotdEveryoneSeesItOnShowAndLogin) {
  Fixture f;
  uint8_t admin[BBS_PUBKEY_LEN], other[BBS_PUBKEY_LEN];
  makeKey(admin, 80);
  makeKey(other, 81);
  f.run(admin, "REGISTER capoM");
  f.run(other, "REGISTER altroM");

  EXPECT_EQ(f.run(admin, "MOTD Manutenzione stasera"), std::string(strings::kMotdSetOk));
  EXPECT_EQ(f.run(other, "MOTD"), "Manutenzione stasera");
  EXPECT_EQ(f.run(other, "LOGIN"), "Manutenzione stasera");  // ha la precedenza sui conteggi
}

TEST(BbsCommandParser, PlainUserCannotSetMotd) {
  Fixture f;
  uint8_t admin[BBS_PUBKEY_LEN], other[BBS_PUBKEY_LEN];
  makeKey(admin, 82);
  makeKey(other, 83);
  f.run(admin, "REGISTER capoM2");
  f.run(other, "REGISTER altroM2");
  EXPECT_EQ(f.run(other, "MOTD ciao"), std::string(strings::kPermissionDenied));
}

TEST(BbsCommandParser, MotdClearRestoresLoginCounts) {
  Fixture f;
  uint8_t admin[BBS_PUBKEY_LEN];
  makeKey(admin, 84);
  f.run(admin, "REGISTER capoM3");
  f.run(admin, "MOTD un avviso");
  EXPECT_EQ(f.run(admin, "MOTD CLEAR"), std::string(strings::kMotdCleared));

  char expected[64];
  snprintf(expected, sizeof(expected), strings::kLoginWelcomeBackFmt, 0u, 0u);
  EXPECT_EQ(f.run(admin, "LOGIN"), std::string(expected));
}

TEST(BbsCommandParser, RetentionDropsOldestPostsAutomaticallyOnPost) {
  Fixture f;
  uint8_t a[BBS_PUBKEY_LEN], b[BBS_PUBKEY_LEN];
  makeKey(a, 85);
  makeKey(b, 86);
  f.run(a, "REGISTER autoreR");
  f.run(b, "REGISTER lettoreR");

  // +100s per post: resta ben fuori dalla finestra di anti-abuso di 60s
  // (BBS_MAX_MESSAGES_PER_MINUTE), altrimenti la maggior parte dei post
  // verrebbe respinta dal rate limit invece che accettata e poi scartata
  // dalla ritenzione.
  uint32_t last_ts = 1000;
  for (uint32_t i = 0; i < BBS_MAX_POSTS_PER_ROOM + 5; i++) {
    last_ts = 1000 + i * 100;
    ASSERT_EQ(f.run(a, "E messaggio", last_ts), std::string(strings::kPostOkPrefix) + "Generale");
  }
  // b non ha piu' fatto nulla dalla REGISTER iniziale: un rapido "tocco" lo
  // tiene online per il controllo STATS qui sotto (altrimenti la sua
  // sessione sarebbe scaduta molto prima, per il solo trascorrere del tempo
  // simulato dai tanti post di a).
  f.run(b, "H", last_ts);

  char expected[96];
  snprintf(expected, sizeof(expected), strings::kStatsFmt, 2u, 2u, (unsigned)BBS_MAX_POSTS_PER_ROOM, 0u);
  EXPECT_EQ(f.run(a, "STATS", last_ts + 1), std::string(expected));
}

TEST(BbsCommandParser, RoomAddByAdminCreatesAUsableRoom) {
  Fixture f;
  uint8_t admin[BBS_PUBKEY_LEN], other[BBS_PUBKEY_LEN];
  makeKey(admin, 40);
  makeKey(other, 41);
  f.run(admin, "REGISTER capo");  // primo utente registrato: admin di bootstrap
  f.run(other, "REGISTER mario");

  EXPECT_EQ(f.run(admin, "ROOM ADD Giardino"), std::string(strings::kRoomAddOkPrefix) + "Giardino");

  // La nuova stanza (id 3) e' subito usabile: post, lettura, iscrizione.
  EXPECT_EQ(f.run(other, "E 3 ciao a tutti"), std::string(strings::kPostOkPrefix) + "Giardino");
  EXPECT_EQ(f.run(admin, "N 3"), std::string("mario: ciao a tutti"));
}

TEST(BbsCommandParser, RoomAddByPlainUserIsDenied) {
  Fixture f;
  uint8_t user[BBS_PUBKEY_LEN];
  makeKey(user, 42);
  f.run(user, "REGISTER capo");  // primo utente: diventa admin di bootstrap...
  uint8_t second[BBS_PUBKEY_LEN];
  makeKey(second, 43);
  f.run(second, "REGISTER altro");  // ...questo no, resta utente semplice

  EXPECT_EQ(f.run(second, "ROOM ADD Altra"), std::string(strings::kPermissionDenied));
}

TEST(BbsCommandParser, RoomAddDuplicateOrInvalidNameReportsError) {
  Fixture f;
  uint8_t admin[BBS_PUBKEY_LEN];
  makeKey(admin, 44);
  f.run(admin, "REGISTER capo");

  EXPECT_EQ(f.run(admin, "ROOM ADD Generale"), std::string(strings::kRoomNameDuplicate));
  EXPECT_EQ(f.run(admin, "ROOM ADD"), std::string(strings::kRoomUsage));
}

TEST(BbsCommandParser, RoomDelRemovesRoomAndPurgesItsData) {
  Fixture f;
  uint8_t admin[BBS_PUBKEY_LEN], reader[BBS_PUBKEY_LEN];
  makeKey(admin, 45);
  makeKey(reader, 46);
  f.run(admin, "REGISTER capo");
  f.run(reader, "REGISTER remo");
  f.run(reader, "S 2");  // iscritto anche a Tecnico (id 2), non di default
  f.run(admin, "E 2 un post qualunque", 100);

  EXPECT_EQ(f.run(admin, "ROOM DEL 2"), std::string(strings::kRoomDelOkPrefix) + "Tecnico");
  EXPECT_EQ(f.run(admin, "K").find("2:"), std::string::npos);  // non compare piu' nell'elenco

  // L'id 2 viene riassegnato a una stanza nuova, senza eredita' dalla
  // precedente (post, iscrizioni): verificato registrando una stanza nuova
  // con lo stesso id e controllando che 'reader' non sia gia' iscritto.
  EXPECT_EQ(f.run(admin, "ROOM ADD Cucina"), std::string(strings::kRoomAddOkPrefix) + "Cucina");
  EXPECT_EQ(f.run(admin, "N 2"), std::string(strings::kNoNewPostsPrefix) + "Cucina");  // niente post vecchi
  EXPECT_EQ(f.run(reader, "E 2 nuovo in cucina", 200), std::string(strings::kPostOkPrefix) + "Cucina");
}

TEST(BbsCommandParser, RoomDelByPlainUserIsDenied) {
  Fixture f;
  uint8_t admin[BBS_PUBKEY_LEN], user[BBS_PUBKEY_LEN];
  makeKey(admin, 47);
  makeKey(user, 48);
  f.run(admin, "REGISTER capo");
  f.run(user, "REGISTER altro");

  EXPECT_EQ(f.run(user, "ROOM DEL 2"), std::string(strings::kPermissionDenied));
}

TEST(BbsCommandParser, RoomDelUnknownRoomReportsInvalid) {
  Fixture f;
  uint8_t admin[BBS_PUBKEY_LEN];
  makeKey(admin, 49);
  f.run(admin, "REGISTER capo");

  EXPECT_EQ(f.run(admin, "ROOM DEL 9"), std::string(strings::kInvalidRoom));
}

TEST(BbsCommandParser, RoomUnknownSubcommandReportsUsage) {
  Fixture f;
  uint8_t admin[BBS_PUBKEY_LEN];
  makeKey(admin, 50);
  f.run(admin, "REGISTER capo");

  EXPECT_EQ(f.run(admin, "ROOM"), std::string(strings::kRoomUsage));
  EXPECT_EQ(f.run(admin, "ROOM FOO"), std::string(strings::kRoomUsage));
}

TEST(BbsCommandParser, PinLastPostThenReadItWithPinned) {
  Fixture f;
  uint8_t admin[BBS_PUBKEY_LEN], author[BBS_PUBKEY_LEN];
  makeKey(admin, 60);
  makeKey(author, 61);
  f.run(admin, "REGISTER capo6");
  f.run(author, "REGISTER mario6");
  f.run(author, "E annuncio importante", 100);

  EXPECT_EQ(f.run(admin, "PIN 0"), std::string(strings::kPinOkPrefix) + "Generale");
  EXPECT_EQ(f.run(admin, "PINNED"), std::string("mario6: annuncio importante"));
  EXPECT_EQ(f.run(admin, "PINNED 0"), std::string("mario6: annuncio importante"));
}

TEST(BbsCommandParser, PinningANewPostReplacesThePreviousPin) {
  Fixture f;
  uint8_t admin[BBS_PUBKEY_LEN], author[BBS_PUBKEY_LEN];
  makeKey(admin, 62);
  makeKey(author, 63);
  f.run(admin, "REGISTER capo7");
  f.run(author, "REGISTER mario7");
  f.run(author, "E primo", 100);
  f.run(admin, "PIN 0");
  f.run(author, "E secondo", 200);

  EXPECT_EQ(f.run(admin, "PIN 0"), std::string(strings::kPinOkPrefix) + "Generale");
  EXPECT_EQ(f.run(admin, "PINNED"), std::string("mario7: secondo"));  // solo un fissato per stanza
}

TEST(BbsCommandParser, UnpinRemovesThePin) {
  Fixture f;
  uint8_t admin[BBS_PUBKEY_LEN], author[BBS_PUBKEY_LEN];
  makeKey(admin, 64);
  makeKey(author, 65);
  f.run(admin, "REGISTER capo8");
  f.run(author, "REGISTER mario8");
  f.run(author, "E qualcosa", 100);
  f.run(admin, "PIN 0");

  EXPECT_EQ(f.run(admin, "UNPIN 0"), std::string(strings::kUnpinOkPrefix) + "Generale");
  EXPECT_EQ(f.run(admin, "PINNED"), std::string(strings::kNoPinnedPrefix) + "Generale");
  EXPECT_EQ(f.run(admin, "UNPIN 0"), std::string(strings::kUnpinNothingToUnpin));
}

TEST(BbsCommandParser, PinWithNoPostsReportsNothingToPin) {
  Fixture f;
  uint8_t admin[BBS_PUBKEY_LEN];
  makeKey(admin, 66);
  f.run(admin, "REGISTER capo9");

  EXPECT_EQ(f.run(admin, "PIN 0"), std::string(strings::kPinNothingToPin));
}

TEST(BbsCommandParser, PinAndUnpinByPlainUserAreDenied) {
  Fixture f;
  uint8_t admin[BBS_PUBKEY_LEN], user[BBS_PUBKEY_LEN];
  makeKey(admin, 67);
  makeKey(user, 68);
  f.run(admin, "REGISTER capo10");
  f.run(user, "REGISTER altro10");

  EXPECT_EQ(f.run(user, "PIN 0"), std::string(strings::kPermissionDenied));
  EXPECT_EQ(f.run(user, "UNPIN 0"), std::string(strings::kPermissionDenied));
}

TEST(BbsCommandParser, DeletingAPinnedPostHidesItFromPinned) {
  Fixture f;
  uint8_t admin[BBS_PUBKEY_LEN], author[BBS_PUBKEY_LEN];
  makeKey(admin, 69);
  makeKey(author, 70);
  f.run(admin, "REGISTER capo11");
  f.run(author, "REGISTER mario11");
  f.run(author, "E fissato poi cancellato", 100);
  f.run(admin, "PIN 0");

  EXPECT_EQ(f.run(admin, "DELPOST 0"), std::string(strings::kDelPostOkPrefix) + "Generale");
  EXPECT_EQ(f.run(admin, "PINNED"), std::string(strings::kNoPinnedPrefix) + "Generale");
}

TEST(BbsCommandParser, AllStringsFitInTextBudgetWithNicknameMargin) {
  const char* all[] = {
      strings::kWelcome,          strings::kUnknownPreRegister,      strings::kHelpPreRegister,
      strings::kHelpPostRegister, strings::kLoginNoAccount,          strings::kLoginWelcomeBackFmt,
      strings::kLogoutAck,        strings::kAlreadyRegisteredPrefix, strings::kRegisterOkPrefix,
      strings::kInvalidNickname,  strings::kUnknownPostRegister,     strings::kStoreFull,
      strings::kInvalidRoom,      strings::kPostEmptyText,           strings::kPostOkPrefix,
      strings::kNoNewPostsPrefix, strings::kMailNoNew,               strings::kMailUnknownRecipient,
      strings::kMailEmptyText,    strings::kMailSentPrefix,          strings::kSubscribedPrefix,
      strings::kUnsubscribedPrefix,        strings::kNewPostsNotifyFmt,        strings::kPermissionDenied,
      strings::kTargetNotFound,            strings::kMutedCannotPost,          strings::kRoomClosed,
      strings::kRateLimited,               strings::kDelPostNothingToDelete,   strings::kBanOkPrefix,
      strings::kUnbanOkPrefix,             strings::kMuteOkPrefix,             strings::kUnmuteOkPrefix,
      strings::kSetModOkPrefix,            strings::kSetAdminOkPrefix,         strings::kSetUserOkPrefix,
      strings::kDelPostOkPrefix,           strings::kCloseOkPrefix,            strings::kOpenOkPrefix,
      strings::kModLogCountFmt,            strings::kRoomUsage,                strings::kRoomNameInvalid,
      strings::kRoomNameDuplicate,         strings::kRoomsFull,                strings::kRoomAddFailed,
      strings::kRoomDelFailed,             strings::kRoomAddOkPrefix,          strings::kRoomDelOkPrefix,
      strings::kPinNothingToPin,           strings::kUnpinNothingToUnpin,      strings::kPinOkPrefix,
      strings::kUnpinOkPrefix,             strings::kNoPinnedPrefix,
  };
  for (auto* s : all) {
    // Margine largo: copre sia il caso nickname (fino a 15 char) sia il
    // caso kLoginWelcomeBackFmt (due numeri fino a 10 cifre ciascuno al
    // posto di %u, caso di fatto mai raggiunto in pratica).
    EXPECT_LE(strlen(s) + 2 * BBS_NICK_LEN, BBS_MAX_TEXT_LEN) << s;
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
