#!/usr/bin/env bash
# ─── CHN Tools — shared helpers ───────────────────────────────────────────────
# Sourced by chn-publish and chn-install. Not run directly.

# ── Terminal colors ────────────────────────────────────────────────────────────
RESET="\033[0m"
BOLD="\033[1m"
DIM="\033[2m"

FG_RED="\033[31m"
FG_GREEN="\033[32m"
FG_YELLOW="\033[33m"
FG_BLUE="\033[34m"
FG_MAGENTA="\033[35m"
FG_CYAN="\033[36m"
FG_WHITE="\033[37m"
FG_BRIGHT_WHITE="\033[97m"
FG_BRIGHT_GREEN="\033[92m"
FG_BRIGHT_CYAN="\033[96m"
FG_BRIGHT_YELLOW="\033[93m"
FG_BRIGHT_RED="\033[91m"
FG_GRAY="\033[90m"

BG_BLUE="\033[44m"
BG_GREEN="\033[42m"

# ── Print helpers ──────────────────────────────────────────────────────────────
info()    { printf "${FG_CYAN}${BOLD}  ▸${RESET}  %s\n" "$*"; }
ok()      { printf "${FG_BRIGHT_GREEN}${BOLD}  ✓${RESET}  %s\n" "$*"; }
warn()    { printf "${FG_BRIGHT_YELLOW}${BOLD}  ⚠${RESET}  %s\n" "$*"; }
err()     { printf "${FG_BRIGHT_RED}${BOLD}  ✗${RESET}  %s\n" "$*" >&2; }
step()    { printf "\n${FG_BRIGHT_WHITE}${BOLD}──${RESET} %s\n" "$*"; }
dim()     { printf "${FG_GRAY}  %s${RESET}\n" "$*"; }
label()   { printf "  ${FG_CYAN}%-20s${RESET} %s\n" "$1" "$2"; }

die() {
    printf "${FG_BRIGHT_RED}${BOLD}  ✗${RESET}  %b\n" "$*" >&2
    exit 1
}

# ── Config file path ───────────────────────────────────────────────────────────
CHN_HOME="${HOME}/.chn"
CHN_CONFIG="${CHN_HOME}/config"
CHN_CACHE="${CHN_HOME}/cache"
CHN_LIBS_DIR=""   # set by caller or auto-detected

# Registry constants
REGISTRY_BASE_URL="https://cdn.jsdelivr.net/gh"
GITHUB_API="https://api.github.com"

# ── Load ~/.chn/config ─────────────────────────────────────────────────────────
load_chn_config() {
    if [[ ! -f "$CHN_CONFIG" ]]; then
        return 1
    fi
    while IFS='=' read -r key val; do
        key="${key//[[:space:]]/}"
        val="${val//[[:space:]]/}"
        [[ "$key" =~ ^#.*$ ]] && continue
        [[ -z "$key" ]]       && continue
        case "$key" in
            REGISTRY_REPO)  REGISTRY_REPO="$val" ;;
            GITHUB_TOKEN)   GITHUB_TOKEN="$val"  ;;
            AUTHOR_NAME)    AUTHOR_NAME="$val"   ;;
            AUTHOR_EMAIL)   AUTHOR_EMAIL="$val"  ;;
        esac
    done < "$CHN_CONFIG"
    return 0
}

# ── Save a key=value into ~/.chn/config ───────────────────────────────────────
save_config_key() {
    local key="$1" val="$2"
    mkdir -p "$CHN_HOME"
    if grep -q "^${key}=" "$CHN_CONFIG" 2>/dev/null; then
        # update existing
        sed -i "s|^${key}=.*|${key}=${val}|" "$CHN_CONFIG"
    else
        echo "${key}=${val}" >> "$CHN_CONFIG"
    fi
}

# ── Detect chn-libs directory (walk up from CWD) ──────────────────────────────
find_chn_libs() {
    local dir="$1"
    # First try: next to wherever 'chn' binary is
    local chn_bin
    chn_bin="$(which chn 2>/dev/null)"
    if [[ -n "$chn_bin" ]]; then
        local chn_dir
        chn_dir="$(dirname "$(realpath "$chn_bin")")"
        if [[ -d "${chn_dir}/chn-libs" ]]; then
            echo "${chn_dir}/chn-libs"
            return
        fi
    fi
    # Fallback: walk up from given dir
    local cur="$dir"
    while [[ "$cur" != "/" ]]; do
        if [[ -d "${cur}/chn-libs" ]]; then
            echo "${cur}/chn-libs"
            return
        fi
        cur="$(dirname "$cur")"
    done
    # Last resort: /usr/local/share/chn/chn-libs
    if [[ -d "/usr/local/share/chn/chn-libs" ]]; then
        echo "/usr/local/share/chn/chn-libs"
        return
    fi
    echo ""
}

# ── CHN-CONF parser ────────────────────────────────────────────────────────────
# Sets variables: PKG_NAME, PKG_DESC, PKG_VERSION, PKG_LICENSE, PKG_LAUNCH,
#                 PKG_DEPS (array), PKG_KEYWORDS (array)
parse_chn_conf() {
    local conf_file="$1"

    [[ ! -f "$conf_file" ]] && die "CHN-CONF not found at: $conf_file"

    PKG_NAME=""
    PKG_DESC=""
    PKG_VERSION=""
    PKG_LICENSE=""
    PKG_LAUNCH=""
    PKG_DEPS=()
    PKG_KEYWORDS=()

    while IFS= read -r line; do
        # strip inline comments (-- ...)
        line="${line%%--*}"
        # trim leading/trailing whitespace
        line="$(echo "$line" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')"
        [[ -z "$line" ]] && continue

        local key="${line%%:*}"
        local val="${line#*:}"
        key="$(echo "$key" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')"
        val="$(echo "$val" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')"

        case "$key" in
            name)
                # strip surrounding quotes
                PKG_NAME="${val//\"/}"
                PKG_NAME="${PKG_NAME//\'/}"
                ;;
            description)
                PKG_DESC="${val//\"/}"
                PKG_DESC="${PKG_DESC//\'/}"
                ;;
            version)
                PKG_VERSION="$val"
                ;;
            license)
                PKG_LICENSE="$val"
                ;;
            launch)
                PKG_LAUNCH="$val"
                ;;
            dependencies)
                # parse array: [dep1, dep2, dep3]  or  []
                local deps_raw
                deps_raw="${val#\[}"
                deps_raw="${deps_raw%\]}"
                deps_raw="$(echo "$deps_raw" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')"
                if [[ -n "$deps_raw" ]]; then
                    IFS=',' read -ra PKG_DEPS <<< "$deps_raw"
                    # trim each
                    local trimmed=()
                    for d in "${PKG_DEPS[@]}"; do
                        d="$(echo "$d" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')"
                        [[ -n "$d" ]] && trimmed+=("$d")
                    done
                    PKG_DEPS=("${trimmed[@]}")
                fi
                ;;
            keywords)
                local kw_raw
                kw_raw="${val#\[}"
                kw_raw="${kw_raw%\]}"
                # could be space-separated without commas
                IFS=', ' read -ra PKG_KEYWORDS <<< "$kw_raw"
                local trimmed_kw=()
                for kw in "${PKG_KEYWORDS[@]}"; do
                    kw="$(echo "$kw" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')"
                    [[ -n "$kw" ]] && trimmed_kw+=("$kw")
                done
                PKG_KEYWORDS=("${trimmed_kw[@]}")
                ;;
        esac
    done < "$conf_file"
}

# ── CHN-CONF validation ────────────────────────────────────────────────────────
validate_chn_conf() {
    local errors=0

    # Required fields
    if [[ -z "$PKG_NAME" ]]; then
        err "CHN-CONF: 'name' is required"
        ((errors++))
    else
        # name: lowercase alphanumeric + hyphens only
        if ! [[ "$PKG_NAME" =~ ^[a-z][a-z0-9_-]*$ ]]; then
            err "CHN-CONF: 'name' must be lowercase alphanumeric (hyphens/underscores ok), got: '$PKG_NAME'"
            ((errors++))
        fi
        # name length
        if [[ ${#PKG_NAME} -gt 64 ]]; then
            err "CHN-CONF: 'name' too long (max 64 chars)"
            ((errors++))
        fi
    fi

    if [[ -z "$PKG_VERSION" ]]; then
        err "CHN-CONF: 'version' is required"
        ((errors++))
    else
        # Strict semver: MAJOR.MINOR.PATCH (all numeric, no leading zeros except 0 itself)
        if ! [[ "$PKG_VERSION" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]]; then
            err "CHN-CONF: 'version' must be semver format (e.g. 1.0.0), got: '$PKG_VERSION'"
            err "         Leading zeros are not allowed (e.g. '01.00.00' is invalid)"
            ((errors++))
        fi
    fi

    if [[ -z "$PKG_DESC" ]]; then
        err "CHN-CONF: 'description' is required"
        ((errors++))
    fi

    if [[ -z "$PKG_LICENSE" ]]; then
        err "CHN-CONF: 'license' is required"
        ((errors++))
    else
        # Must be a known SPDX license identifier
        local known_licenses="MIT Apache-2.0 GPL-2.0 GPL-3.0 LGPL-2.1 LGPL-3.0 BSD-2-Clause BSD-3-Clause ISC MPL-2.0 AGPL-3.0 Unlicense CC0-1.0 WTFPL"
        local valid=0
        for lic in $known_licenses; do
            if [[ "$PKG_LICENSE" == "$lic" ]]; then
                valid=1
                break
            fi
        done
        if [[ $valid -eq 0 ]]; then
            err "CHN-CONF: unknown license '$PKG_LICENSE'"
            err "         Known: $known_licenses"
            ((errors++))
        fi
    fi

    if [[ -z "$PKG_LAUNCH" ]]; then
        err "CHN-CONF: 'launch' is required (path to main .chn file)"
        ((errors++))
    fi

    return $errors
}

# ── Semver comparison: returns 0 if a >= b ─────────────────────────────────────
semver_gte() {
    local a="$1" b="$2"
    python3 -c "
a=tuple(int(x) for x in '$a'.split('.'))
b=tuple(int(x) for x in '$b'.split('.'))
exit(0 if a >= b else 1)
" 2>/dev/null
}

# ── Progress bar ───────────────────────────────────────────────────────────────
# Usage: progress_bar <current> <total> <label>
PROGRESS_WIDTH=40
progress_bar() {
    local current="$1"
    local total="$2"
    local label="${3:-}"
    local pct=$(( current * 100 / total ))
    local filled=$(( current * PROGRESS_WIDTH / total ))
    local empty=$(( PROGRESS_WIDTH - filled ))

    local bar=""
    local i
    for (( i=0; i<filled; i++ )); do bar+="█"; done
    for (( i=0; i<empty;  i++ )); do bar+="░"; done

    printf "\r  ${FG_CYAN}[${RESET}${FG_BRIGHT_GREEN}%s${RESET}${FG_CYAN}]${RESET} ${FG_BRIGHT_WHITE}%3d%%${RESET}  %s" \
        "$bar" "$pct" "$label"
}

progress_done() {
    local label="${1:-Done}"
    local bar=""
    local i
    for (( i=0; i<PROGRESS_WIDTH; i++ )); do bar+="█"; done
    printf "\r  ${FG_CYAN}[${RESET}${FG_BRIGHT_GREEN}%s${RESET}${FG_CYAN}]${RESET} ${FG_BRIGHT_WHITE}100%%${RESET}  ${FG_BRIGHT_GREEN}%s${RESET}\n" \
        "$bar" "$label"
}

# ── Animated spinner ───────────────────────────────────────────────────────────
SPINNER_PID=""
start_spinner() {
    local label="$1"
    (
        local frames=("⠋" "⠙" "⠹" "⠸" "⠼" "⠴" "⠦" "⠧" "⠇" "⠏")
        local i=0
        while true; do
            printf "\r  ${FG_CYAN}%s${RESET}  %s" "${frames[$i]}" "$label"
            i=$(( (i+1) % ${#frames[@]} ))
            sleep 0.1
        done
    ) &
    SPINNER_PID=$!
    disown "$SPINNER_PID"
}

stop_spinner() {
    if [[ -n "$SPINNER_PID" ]]; then
        kill "$SPINNER_PID" 2>/dev/null
        wait "$SPINNER_PID" 2>/dev/null
        SPINNER_PID=""
        printf "\r%${COLUMNS}s\r" ""   # clear the spinner line
    fi
}

# ── Base64 encode (portable) ───────────────────────────────────────────────────
b64encode() {
    python3 -c "
import sys, base64
data = sys.stdin.buffer.read()
print(base64.b64encode(data).decode(), end='')
"
}

# ── GitHub API helpers ─────────────────────────────────────────────────────────
# Requires: GITHUB_TOKEN, REGISTRY_REPO

gh_api_get() {
    local path="$1"
    curl -sf \
        -H "Authorization: token ${GITHUB_TOKEN}" \
        -H "Accept: application/vnd.github.v3+json" \
        "${GITHUB_API}/repos/${REGISTRY_REPO}/contents/${path}"
}

# Get the sha of an existing file (needed to update it)
gh_get_sha() {
    local path="$1"
    gh_api_get "$path" 2>/dev/null | python3 -c "
import sys, json
try:
    d = json.load(sys.stdin)
    print(d.get('sha',''))
except:
    print('')
"
}

# Create or update a file in the GitHub repo
gh_api_put() {
    local path="$1"
    local message="$2"
    local content_b64="$3"
    local sha="${4:-}"   # empty = create new

    local payload
    if [[ -n "$sha" ]]; then
        payload=$(python3 -c "
import json
print(json.dumps({'message': '$message', 'content': '$content_b64', 'sha': '$sha'}))
")
    else
        payload=$(python3 -c "
import json
print(json.dumps({'message': '$message', 'content': '$content_b64'}))
")
    fi

    curl -sf \
        -X PUT \
        -H "Authorization: token ${GITHUB_TOKEN}" \
        -H "Accept: application/vnd.github.v3+json" \
        -H "Content-Type: application/json" \
        -d "$payload" \
        "${GITHUB_API}/repos/${REGISTRY_REPO}/contents/${path}"
}

# Upload a local file to the GitHub registry
gh_upload_file() {
    local local_path="$1"    # local file path
    local remote_path="$2"   # path inside the repo
    local commit_msg="$3"

    [[ ! -f "$local_path" ]] && return 1

    local content_b64
    content_b64="$(b64encode < "$local_path")"

    local sha
    sha="$(gh_get_sha "$remote_path")"

    gh_api_put "$remote_path" "$commit_msg" "$content_b64" "$sha" > /dev/null
}






# ── Test / CI mode ─────────────────────────────────────────────────────────────
# Set CHN_TEST_MODE=1 to bypass network checks (for dry-run testing)
check_cdn() {
    [[ "${CHN_TEST_MODE:-0}" == "1" ]] && return 0
    curl -sf --max-time 8 "https://cdn.jsdelivr.net/npm/jquery@3/dist/jquery.min.js" \
        -o /dev/null 2>/dev/null
}

check_github_api() {
    [[ "${CHN_TEST_MODE:-0}" == "1" ]] && return 0
    curl -sf --max-time 8 \
        -H "Authorization: token ${GITHUB_TOKEN}" \
        "${GITHUB_API}/user" > /dev/null 2>&1
}

# ── Fetch registry.json from jsDelivr ─────────────────────────────────────────
fetch_registry() {
    local url="${REGISTRY_BASE_URL}/${REGISTRY_REPO}@main/registry.json"
    curl -sf --max-time 15 "$url" 2>/dev/null
}

# ── LICENSE template generator ────────────────────────────────────────────────
generate_license() {
    local license_type="$1"
    local full_name="$2"
    local year="$3"
    local company="$4"
    local email="$5"

    local copyright_holder="$full_name"
    [[ -n "$company" ]] && copyright_holder="$company ($full_name)"

    case "$license_type" in
        MIT)
cat <<LICENSE
MIT License

Copyright (c) ${year} ${copyright_holder}
${email:+Contact: $email}

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
LICENSE
            ;;

        Apache-2.0)
cat <<LICENSE
Apache License
Version 2.0, January 2004
http://www.apache.org/licenses/

Copyright (c) ${year} ${copyright_holder}
${email:+Contact: $email}

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
LICENSE
            ;;

        GPL-3.0)
cat <<LICENSE
GNU GENERAL PUBLIC LICENSE
Version 3, 29 June 2007

Copyright (c) ${year} ${copyright_holder}
${email:+Contact: $email}

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <https://www.gnu.org/licenses/>.
LICENSE
            ;;

        BSD-2-Clause)
cat <<LICENSE
BSD 2-Clause License

Copyright (c) ${year}, ${copyright_holder}
${email:+Contact: $email}

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
LICENSE
            ;;

        ISC)
cat <<LICENSE
ISC License

Copyright (c) ${year} ${copyright_holder}
${email:+Contact: $email}

Permission to use, copy, modify, and/or distribute this software for any purpose
with or without fee is hereby granted, provided that the above copyright notice
and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
PERFORMANCE OF THIS SOFTWARE.
LICENSE
            ;;

        Unlicense)
cat <<LICENSE
This is free and unencumbered software released into the public domain.

Copyright (c) ${year} ${copyright_holder}
${email:+Contact: $email}

Anyone is free to copy, modify, publish, use, compile, sell, or distribute
this software, either in source code form or as a compiled binary, for any
purpose, commercial or non-commercial, and by any means.

In jurisdictions that recognize copyright laws, the author or authors of
this software dedicate any and all copyright interest in the software to
the public domain. We make this dedication for the benefit of the public
at large and to the detriment of our heirs and successors.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED.
LICENSE
            ;;

        *)
cat <<LICENSE
Copyright (c) ${year} ${copyright_holder}
${email:+Contact: $email}

Licensed under: ${license_type}
LICENSE
            ;;
    esac
}
