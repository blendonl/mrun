#include "tests.h"
#include "../src/mrun_update.h"

#define URL L"https://example.invalid/install.ps1"
#define CAP 2048

static int count_of(const wchar_t *s, wchar_t c) {
    int n = 0;
    for (; *s; s++) n += *s == c;
    return n;
}

static bool ends_with(const wchar_t *s, const wchar_t *tail) {
    size_t sl = wcslen(s), tl = wcslen(tail);
    return sl >= tl && wcscmp(s + sl - tl, tail) == 0;
}

int main(void) {
    wchar_t out[CAP];

    CHECK(mrun_update_params(URL, L"C:\\Tools\\mrun\\", out, CAP),
          "a plain folder must build");
    CHECK(wcsncmp(out, L"-NoProfile -Command \"", 21) == 0,
          "starts with the PowerShell switches: %ls", out);
    CHECK(ends_with(out, L"}\""), "ends by closing -Command: %ls", out);
    CHECK(count_of(out, L'"') == 2,
          "-Command must stay one argument: %ls", out);
    CHECK(wcsstr(out, L"Invoke-RestMethod '" URL L"'") != NULL,
          "fetches the script it was given: %ls", out);
    CHECK(wcsstr(out, L"-InstallDir 'C:\\Tools\\mrun\\' -SkipPath -SkipDocs;")
              != NULL,
          "installs over the folder it was given, leaving PATH and any "
          "README or LICENSE there alone: %ls", out);

    CHECK(mrun_update_params(URL, L"C:\\Users\\O'Brien\\mrun\\", out, CAP),
          "an apostrophe must build");
    CHECK(wcsstr(out, L"'C:\\Users\\O''Brien\\mrun\\'") != NULL,
          "an apostrophe is doubled: %ls", out);

    CHECK(mrun_update_params(URL, L"C:\\It\u2019s\\", out, CAP),
          "a typographic apostrophe must build");
    CHECK(wcsstr(out, L"'C:\\It\u2019\u2019s\\'") != NULL,
          "PowerShell also ends strings on U+2019, so it is doubled too");

    CHECK(mrun_update_params(URL, L"C:\\a  b $env:X `n\\", out, CAP),
          "spaces, $ and backticks must build");
    CHECK(wcsstr(out, L"'C:\\a  b $env:X `n\\'") != NULL,
          "single quotes keep them literal: %ls", out);

    out[0] = L'x';
    CHECK(!mrun_update_params(URL, L"C:\\bad\"dir\\", out, CAP),
          "a double quote would split the -Command argument");
    CHECK(out[0] == L'\0', "a refusal leaves an empty string");

    CHECK(!mrun_update_params(URL, L"", out, CAP), "empty folder");
    CHECK(!mrun_update_params(URL, NULL, out, CAP), "NULL folder");
    CHECK(!mrun_update_params(NULL, L"C:\\mrun\\", out, CAP), "NULL script");
    CHECK(!mrun_update_params(URL, L"C:\\mrun\\", NULL, CAP), "NULL output");
    CHECK(!mrun_update_params(URL, L"C:\\mrun\\", out, 0), "zero capacity");

    CHECK(mrun_update_params(URL, L"C:\\mrun\\", out, CAP), "baseline");
    size_t needed = wcslen(out) + 1;

    wchar_t exact[CAP];
    CHECK(mrun_update_params(URL, L"C:\\mrun\\", exact, needed),
          "fits in exactly its length plus the terminator");
    CHECK(wcscmp(exact, out) == 0, "and is the same string");

    wchar_t small[CAP];
    small[0] = L'x';
    CHECK(!mrun_update_params(URL, L"C:\\mrun\\", small, needed - 1),
          "one short must be refused, not truncated");
    CHECK(small[0] == L'\0', "a truncated command is never left behind");

    return tests_report("mrun_update");
}
