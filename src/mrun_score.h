#ifndef MRUN_SCORE_H
#define MRUN_SCORE_H

#include <stdbool.h>
#include <wchar.h>

#define MRUN_NO_MATCH (-1000000)

int  mrun_score(const wchar_t *query, const wchar_t *candidate);
bool mrun_matches(const wchar_t *query, const wchar_t *candidate);

#endif
