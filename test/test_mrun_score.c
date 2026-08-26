#include "tests.h"
#include "../src/mrun_score.h"

#define HIT(q, c)  CHECK(mrun_score(L##q, L##c) != MRUN_NO_MATCH,             \
                         "expected '%ls' to match '%ls'", L##q, L##c)
#define MISS(q, c) CHECK(mrun_score(L##q, L##c) == MRUN_NO_MATCH,             \
                         "expected '%ls' NOT to match '%ls'", L##q, L##c)
#define BEATS(q, a, b)                                                        \
    CHECK(mrun_score(L##q, L##a) > mrun_score(L##q, L##b),                    \
          "'%ls': expected '%ls' (%d) to outrank '%ls' (%d)", L##q, L##a,     \
          mrun_score(L##q, L##a), L##b, mrun_score(L##q, L##b))

int main(void) {
    HIT("fire", "Firefox");
    HIT("fox",  "Firefox");
    HIT("vsc",  "Visual Studio Code");
    HIT("wt",   "Windows Terminal");
    HIT("",     "Firefox");

    HIT("ffx",  "Firefox");
    MISS("vsc",  "Discord");
    MISS("xyz",  "Firefox");
    MISS("foxi", "Firefox");
    MISS("firefoxx", "Firefox");
    MISS("a",    "");

    CHECK(mrun_score(L"", L"Firefox") == 0, "empty query must be neutral");
    CHECK(mrun_score(L"anything", NULL) == MRUN_NO_MATCH, "NULL candidate");
    CHECK(mrun_score(NULL, L"Firefox") == 0, "NULL query is an empty query");

    CHECK(mrun_score(L"FiReFoX", L"firefox") != MRUN_NO_MATCH,
          "matching must fold case");
    CHECK(mrun_score(L"firefox", L"FIREFOX") != MRUN_NO_MATCH,
          "matching must fold case");

    BEATS("cmd",  "cmd",             "cmd.exe");
    BEATS("cmd",  "cmd.exe",         "Command Prompt");
    BEATS("code", "Code",            "Visual Studio Code");
    BEATS("term", "Terminal",        "Windows Terminal");
    BEATS("fire", "Firefox",         "Notepad Firewall Helper");
    BEATS("abc",  "abcdef",          "aXbXcXdef");
    BEATS("note", "Notepad",         "Notepad++ Plugin Manager");

    CHECK(mrun_matches(L"vsc", L"Visual Studio Code"), "mrun_matches: hit");
    CHECK(!mrun_matches(L"vsc", L"Discord"),           "mrun_matches: miss");

    return tests_report("mrun_score");
}
