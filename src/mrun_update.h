#ifndef MRUN_UPDATE_H
#define MRUN_UPDATE_H

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

#define MRUN_UPDATE_SHELL  L"powershell.exe"

#ifndef MRUN_UPDATE_SCRIPT
#define MRUN_UPDATE_SCRIPT \
    L"https://raw.githubusercontent.com/blendonl/mrun/main/install.ps1"
#endif

bool mrun_update_params(const wchar_t *script_url, const wchar_t *install_dir,
                        wchar_t *out, size_t cap);

#endif
