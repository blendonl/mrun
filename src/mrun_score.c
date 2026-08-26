#include "mrun_score.h"

#include <wctype.h>

#define BONUS_BASE        16
#define BONUS_FIRST       40
#define BONUS_BOUNDARY    30
#define BONUS_CAMEL       24
#define BONUS_CONSECUTIVE 20
#define BONUS_SAME_CASE    2
#define BONUS_PREFIX     100
#define BONUS_EXACT      300

#define PENALTY_GAP        3
#define PENALTY_GAP_MAX   30
#define PENALTY_LEADING    4
#define PENALTY_LEADING_MAX 40
#define PENALTY_LENGTH_MAX 100

static bool is_separator(wchar_t c) {
    return c == L' ' || c == L'-' || c == L'_' || c == L'.' || c == L'\\' ||
           c == L'/' || c == L'(' || c == L')' || c == L':' || c == L',' ||
           c == L'+' || c == L'\t';
}

static bool same_fold(wchar_t a, wchar_t b) {
    return towlower(a) == towlower(b);
}

static int clamp(int v, int hi) {
    return v > hi ? hi : v;
}

int mrun_score(const wchar_t *query, const wchar_t *candidate) {
    if (!candidate) return MRUN_NO_MATCH;
    if (!query || !query[0]) return 0;

    int score     = 0;
    int last      = -1;
    const wchar_t *q = query;

    for (int i = 0; candidate[i] && *q; i++) {
        if (!same_fold(candidate[i], *q)) continue;

        int bonus = BONUS_BASE;

        if (i == 0) {
            bonus += BONUS_FIRST;
        } else if (is_separator(candidate[i - 1])) {
            bonus += BONUS_BOUNDARY;
        } else if (iswlower(candidate[i - 1]) && iswupper(candidate[i])) {
            bonus += BONUS_CAMEL;
        }

        if (last >= 0 && i == last + 1) bonus += BONUS_CONSECUTIVE;
        if (candidate[i] == *q)         bonus += BONUS_SAME_CASE;

        int gap = (last < 0) ? i : i - last - 1;
        if (gap > 0)
            bonus -= (last < 0)
                     ? clamp(gap * PENALTY_LEADING, PENALTY_LEADING_MAX)
                     : clamp(gap * PENALTY_GAP,     PENALTY_GAP_MAX);

        score += bonus;
        last   = i;
        q++;
    }

    if (*q) return MRUN_NO_MATCH;

    int qlen = 0, clen = 0;
    while (query[qlen])     qlen++;
    while (candidate[clen]) clen++;

    bool prefix = true;
    for (int i = 0; i < qlen; i++)
        if (!same_fold(query[i], candidate[i])) { prefix = false; break; }

    if (prefix) score += (qlen == clen) ? BONUS_EXACT : BONUS_PREFIX;

    score -= clamp(clen / 2, PENALTY_LENGTH_MAX);
    return score;
}

bool mrun_matches(const wchar_t *query, const wchar_t *candidate) {
    return mrun_score(query, candidate) != MRUN_NO_MATCH;
}
