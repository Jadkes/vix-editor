#!/usr/bin/env bash
#
# install.sh - Vix Editor Installer
#
# Features:
#   - Dialog-based terminal UI
#   - Prebuilt release installation
#   - Build from source using CMake + Ninja
#   - Optional clipboard integration
#   - Safe uninstall with separate config/data choices
#   - Linux distribution/package-manager detection
#   - Architecture detection
#   - Installation verification
#   - Temporary-file cleanup
#   - PATH detection
#
# Usage:
#   ./install.sh
#
# Requirements:
#   - Bash 4+
#   - dialog
#
# dialog is automatically offered for installation if missing.

set -u
set -o pipefail

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly SCRIPT_DIR
readonly REPO="Jadkes/vix-editor"
readonly FALLBACK_VERSION="1.1.0"
# Git tag for the fallback release. It must be kept in sync with
# FALLBACK_VERSION; the tag carries a "v" prefix ("v1.0.7").
readonly FALLBACK_TAG="v${FALLBACK_VERSION}"

PM=""
DISTRO=""
ARCH=""
SESSION=""

PKG_CMAKE=""
PKG_CC=""
PKG_NINJA=""
PKG_NCURSES=""
PKG_CURL=""

MISSING_PKGS=()
MISSING_GAPS=""

TMPDIR="$(mktemp -d -t vix-installer.XXXXXX)"

cleanup() {
    rm -rf -- "$TMPDIR"
}

trap cleanup EXIT
trap 'exit 130' INT TERM

# ---------------------------------------------------------------------------
# Generic helpers
# ---------------------------------------------------------------------------

die() {
    printf 'Error: %s\n' "$*" >&2
    exit 1
}

command_exists() {
    command -v "$1" >/dev/null 2>&1
}

is_root() {
    [[ "${EUID:-1}" -eq 0 ]]
}

require_tty() {
    if [[ ! -t 0 || ! -t 1 ]]; then
        printf '%s\n' \
            "Vix installer requires an interactive terminal." \
            "Run ./install.sh from a terminal." >&2
        exit 1
    fi
}

as_root() {
    if is_root; then
        "$@"
        return
    fi

    if ! command_exists sudo; then
        dialog --clear \
            --title "Administrator access required" \
            --msgbox \
            "This operation requires administrator privileges,\nbut sudo is not installed.\n\nPlease install sudo or run the installer as root." \
            0 0
        return 1
    fi

    sudo "$@"
}

# ---------------------------------------------------------------------------
# Distribution / package manager detection
# ---------------------------------------------------------------------------

detect_distro() {
    DISTRO="unknown"

    if [[ -r /etc/os-release ]]; then
        # shellcheck disable=SC1091
        . /etc/os-release
        DISTRO="${ID:-unknown}"
    fi
}

detect_pm_by_binary() {
    if command_exists apt-get; then
        printf 'apt\n'
    elif command_exists dnf; then
        printf 'dnf\n'
    elif command_exists pacman; then
        printf 'pacman\n'
    elif command_exists zypper; then
        printf 'zypper\n'
    elif command_exists apk; then
        printf 'apk\n'
    else
        return 1
    fi
}

detect_pm() {
    local id="" id_like=""

    if [[ -r /etc/os-release ]]; then
        # shellcheck disable=SC1091
        . /etc/os-release
        id="${ID:-}"
        id_like="${ID_LIKE:-}"
    fi

    case "$id" in
        debian|ubuntu|linuxmint|pop|elementary|kali|raspbian|devuan|mx)
            PM="apt"
            ;;
        fedora|rhel|centos|rocky|almalinux|ol)
            PM="dnf"
            ;;
        arch|archlinux|manjaro|endeavouros|cachyos|artix)
            PM="pacman"
            ;;
        opensuse|opensuse-leap|opensuse-tumbleweed|sles|sle-hpc)
            PM="zypper"
            ;;
        alpine)
            PM="apk"
            ;;
        *)
            case "$id_like" in
                *debian*)
                    PM="apt"
                    ;;
                *fedora*|*rhel*)
                    PM="dnf"
                    ;;
                *arch*)
                    PM="pacman"
                    ;;
                *suse*)
                    PM="zypper"
                    ;;
                *alpine*)
                    PM="apk"
                    ;;
                *)
                    PM="$(detect_pm_by_binary || true)"
                    ;;
            esac
            ;;
    esac

    [[ -n "$PM" ]]
}

setup_packages() {
    case "$PM" in
        apt)
            PKG_CMAKE="cmake"
            PKG_CC="build-essential"
            PKG_NINJA="ninja-build"
            PKG_NCURSES="libncursesw5-dev"
            PKG_CURL="curl"
            ;;
        dnf)
            PKG_CMAKE="cmake"
            PKG_CC="gcc-c++"
            PKG_NINJA="ninja-build"
            PKG_NCURSES="ncurses-devel"
            PKG_CURL="curl"
            ;;
        pacman)
            PKG_CMAKE="cmake"
            PKG_CC="base-devel"
            PKG_NINJA="ninja"
            PKG_NCURSES="ncurses"
            PKG_CURL="curl"
            ;;
        zypper)
            PKG_CMAKE="cmake"
            PKG_CC="gcc-c++"
            PKG_NINJA="ninja"
            PKG_NCURSES="ncurses-devel"
            PKG_CURL="curl"
            ;;
        apk)
            PKG_CMAKE="cmake"
            PKG_CC="build-base"
            PKG_NINJA="ninja"
            PKG_NCURSES="ncurses-dev"
            PKG_CURL="curl"
            ;;
        *)
            return 1
            ;;
    esac
}

pm_install() {
    case "$PM" in
        apt)
            as_root apt-get update &&
                as_root apt-get install -y -- "$@"
            ;;
        dnf)
            as_root dnf install -y -- "$@"
            ;;
        pacman)
            as_root pacman -S --needed --noconfirm -- "$@"
            ;;
        zypper)
            as_root zypper --non-interactive install -y -- "$@"
            ;;
        apk)
            as_root apk add --no-cache -- "$@"
            ;;
        *)
            return 1
            ;;
    esac
}

# ---------------------------------------------------------------------------
# Architecture / session detection
# ---------------------------------------------------------------------------

detect_arch() {
    case "$(uname -m)" in
        x86_64|amd64)
            ARCH="linux-x86_64"
            ;;
        aarch64|arm64)
            ARCH="linux-aarch64"
            ;;
        armv7l|armv7*)
            ARCH="linux-armv7"
            ;;
        *)
            ARCH=""
            ;;
    esac
}

detect_session() {
    case "${XDG_SESSION_TYPE:-}" in
        wayland)
            SESSION="Wayland"
            ;;
        x11)
            SESSION="X11"
            ;;
        *)
            SESSION="Unknown"
            ;;
    esac
}

# ---------------------------------------------------------------------------
# dialog
# ---------------------------------------------------------------------------

ensure_dialog() {
    if command_exists dialog; then
        return 0
    fi

    clear

    printf '%s\n\n' \
        "Vix uses 'dialog' for its terminal user interface." \
        "It is not currently installed."

    printf 'Detected package manager: %s\n\n' "$PM"

    read -r -p "Install dialog now? [Y/n] " answer

    case "${answer,,}" in
        ""|y|yes)
            if ! pm_install dialog; then
                printf '%s\n' \
                    "" \
                    "Failed to install dialog." \
                    "Please install it manually and run the installer again." >&2
                return 1
            fi
            ;;
        *)
            printf '%s\n' \
                "" \
                "Installation cancelled." >&2
            return 1
            ;;
    esac

    command_exists dialog
}

# ---------------------------------------------------------------------------
# Build dependencies
# ---------------------------------------------------------------------------

check_ncursesw() {
    local candidates=(
        /usr/include/ncursesw/curses.h
        /usr/include/ncursesw/ncurses.h
        /usr/local/include/ncursesw/curses.h
        /usr/local/include/ncursesw/ncurses.h
    )

    local header
    for header in "${candidates[@]}"; do
        [[ -f "$header" ]] && return 0
    done

    # Do not blindly trust a header path. Let pkg-config help when available.
    if command_exists pkg-config &&
       pkg-config --exists ncursesw 2>/dev/null; then
        return 0
    fi

    return 1
}

check_build_tools() {
    MISSING_PKGS=()
    MISSING_GAPS=""

    if ! command_exists cmake; then
        MISSING_GAPS+="CMake\n"
        MISSING_PKGS+=("$PKG_CMAKE")
    fi

    if ! command_exists ninja; then
        MISSING_GAPS+="Ninja\n"
        MISSING_PKGS+=("$PKG_NINJA")
    fi

    if ! command_exists c++ &&
       ! command_exists g++ &&
       ! command_exists clang++; then
        MISSING_GAPS+="C++ compiler\n"
        MISSING_PKGS+=("$PKG_CC")
    fi

    if ! check_ncursesw; then
        MISSING_GAPS+="ncursesw development files\n"
        MISSING_PKGS+=("$PKG_NCURSES")
    fi

    # Deduplicate package names.
    local -A seen=()
    local dedup=()
    local package

    for package in "${MISSING_PKGS[@]}"; do
        if [[ -n "$package" && -z "${seen[$package]:-}" ]]; then
            seen["$package"]=1
            dedup+=("$package")
        fi
    done

    MISSING_PKGS=("${dedup[@]}")

    if [[ -n "$MISSING_GAPS" ]]; then
        return 1
    fi

    return 0
}

ensure_build_tools() {
    if check_build_tools; then
        return 0
    fi

    local packages
    packages="$(printf '%s ' "${MISSING_PKGS[@]}")"

    dialog --clear \
        --title "Build dependencies missing" \
        --yesno \
        "Vix cannot be built yet.\n\nMissing:\n\n${MISSING_GAPS}\n\nPackages to install:\n${packages}\n\nInstall them now?" \
        0 0

    case $? in
        0)
            ;;
        1|*)
            return 1
            ;;
    esac

    if ! pm_install "${MISSING_PKGS[@]}"; then
        dialog --clear \
            --title "Installation failed" \
            --msgbox \
            "The required build packages could not be installed.\n\nNo source build was started." \
            0 0
        return 1
    fi

    if ! check_build_tools; then
        dialog --clear \
            --title "Dependencies still missing" \
            --msgbox \
            "Some required build dependencies are still unavailable.\n\nPlease check your package-manager configuration and try again." \
            0 0
        return 1
    fi

    return 0
}

# ---------------------------------------------------------------------------
# Build presets
# ---------------------------------------------------------------------------

choose_preset() {
    dialog --clear \
        --title "Build configuration" \
        --menu \
        "Choose how Vix should be built:" \
        0 0 4 \
        default \
            "Release — recommended" \
        build-only \
            "Release — no tests, no gtest fetch" \
        sanitize \
            "RelWithDebInfo — ASan + UBSan" \
        --stdout
}

binary_for_preset() {
    case "$1" in
        default)
            printf '%s/build/vix\n' "$SCRIPT_DIR"
            ;;
        build-only)
            printf '%s/build-notests/vix\n' "$SCRIPT_DIR"
            ;;
        sanitize)
            printf '%s/build-asan/vix\n' "$SCRIPT_DIR"
            ;;
        *)
            return 1
            ;;
    esac
}

build_percent_feed() {
    local line pct current total

    while IFS= read -r line; do
        if [[ "$line" =~ ^\[([0-9]+)/([0-9]+)\] ]]; then
            current="${BASH_REMATCH[1]}"
            total="${BASH_REMATCH[2]}"

            if (( total > 0 )); then
                pct=$((current * 100 / total))
            else
                pct=0
            fi

            ((pct > 99)) && pct=99

            printf 'XXX\n%d\n%s\nXXX\n' "$pct" "$line"
        fi
    done

    printf 'XXX\n100\nBuild complete.\nXXX\n'
}

build_with() {
    local preset="$1"
    local log="$TMPDIR/build.log"
    local rc

    dialog --clear

    (
        cd -- "$SCRIPT_DIR" || exit 1

        printf 'Configuring Vix with preset: %s\n' "$preset"
        cmake --preset "$preset" || exit $?

        printf '\nBuilding Vix...\n'
        cmake --build --preset "$preset" || exit $?
    ) 2>&1 |
        tee "$log" |
        build_percent_feed |
        dialog --gauge "Building Vix..." 8 72 0

    rc=${PIPESTATUS[0]}

    if ((rc != 0)); then
        # Keep the build log in a location that survives the EXIT trap
        # cleanup; TMPDIR is removed when the script ends, so prefer a
        # persistent path and never promise one that disappears.
        local kept=""
        local kept_dir=""
        local cand=""

        for cand in \
            "${XDG_STATE_HOME:-$HOME/.local/state}/vix" \
            "$HOME"; do
            if mkdir -p -- "$cand" 2>/dev/null &&
                cp -f -- "$log" "$cand/vix-build.log" 2>/dev/null; then
                kept="$cand/vix-build.log"
                break
            fi
        done

        if [[ -z "$kept" ]]; then
            # No writable persistent directory; use a real temp dir outside
            # TMPDIR so the path we show still exists after exit.
            kept_dir="$(mktemp -d -t vix-buildlog.XXXXXX 2>/dev/null)"
            kept="${kept_dir:-$log}/build.log"
            [[ -n "$kept_dir" ]] && cp -f -- "$log" "$kept" 2>/dev/null
        fi

        dialog --clear \
            --title "Build failed" \
            --textbox "$log" 30 100

        dialog --clear \
            --title "Build failed" \
            --msgbox \
            "The Vix build failed.\n\nFull output kept at:\n    $kept" \
            0 0

        return 1
    fi

    rm -f -- "$log"

    local binary
    binary="$(binary_for_preset "$preset")"

    if [[ ! -x "$binary" ]]; then
        dialog --clear \
            --title "Build failed" \
            --msgbox \
            "CMake reported a successful build, but the Vix executable could not be found:\n\n$binary" \
            0 0
        return 1
    fi

    return 0
}

# ---------------------------------------------------------------------------
# Release download
# ---------------------------------------------------------------------------

download_file() {
    local url="$1"
    local output="$2"

    if command_exists curl; then
        curl \
            --fail \
            --location \
            --silent \
            --show-error \
            --proto '=https' \
            --tlsv1.2 \
            --output "$output" \
            "$url"
    elif command_exists wget; then
        wget \
            --https-only \
            --quiet \
            --output-document="$output" \
            "$url"
    else
        return 127
    fi
}

sha256_of() {
    # sha256 hex digest of a file, or nothing if no hashing tool exists.
    local file="$1"
    if command_exists sha256sum; then
        sha256sum -- "$file" 2>/dev/null | awk '{ print $1 }'
    elif command_exists openssl; then
        openssl dgst -sha256 -- "$file" 2>/dev/null | awk '{ print $NF }'
    elif command_exists shasum; then
        shasum -a 256 -- "$file" 2>/dev/null | awk '{ print $1 }'
    fi
}

release_table() {
    # One line per release asset for $ARCH:
    #   "VERSION<TAB>URL<TAB>DIGEST"  (newest first)
    # The version comes from the asset filename, the URL and sha256 digest
    # come from GitHub itself, so neither the git tag nor a hand-built URL
    # is ever trusted: the "1.0" tag actually ships vix-1.0.0, and URL
    # segments (download/1.0/...) differ from asset names (vix-1.0.0-...).
    local api_url="https://api.github.com/repos/${REPO}/releases?per_page=100"
    local response=""

    if command_exists curl; then
        response="$(
            curl \
                --fail \
                --location \
                --silent \
                --show-error \
                --proto '=https' \
                --tlsv1.2 \
                --max-time 15 \
                "$api_url" 2>/dev/null
        )"
    elif command_exists wget; then
        response="$(
            wget \
                --https-only \
                --quiet \
                --timeout=15 \
                -O - \
                "$api_url" 2>/dev/null
        )"
    else
        return 1
    fi

    printf '%s\n' "$response" |
        awk -v arch="$ARCH" '
        {
            line = $0
            for (i = 1; i <= length(line); i++) {
                c = substr(line, i, 1)
                if (c == "{") open_cnt++
                else if (c == "}") close_cnt++
            }
            depth += open_cnt - close_cnt
            if (match($0, "\"name\"[[:space:]]*:[[:space:]]*\"")) {
                n = substr($0, RSTART + RLENGTH)
                sub(/".*/, "", n)
                if (n ~ "^vix-.*-" arch "\\.tar\\.gz$") asset_name = n
            }
            if (depth >= 2 && match($0, "\"digest\"[[:space:]]*:[[:space:]]*\"")) {
                d = substr($0, RSTART + RLENGTH)
                sub(/".*/, "", d)
                asset_digest = d
            }
            if (depth >= 2 &&
                match($0, "\"browser_download_url\"[[:space:]]*:[[:space:]]*\"")) {
                u = substr($0, RSTART + RLENGTH)
                sub(/".*/, "", u)
                if (asset_name != "" && asset_digest != "") {
                    file = asset_name
                    version = file
                    sub(/^vix-/, "", version)
                    re = "-" arch "\\.tar\\.gz$"
                    sub(re, "", version)
                    print version "\t" u "\t" asset_digest
                }
                asset_name = ""
                asset_digest = ""
            }
        }'
}

list_versions() {
    # Canonical versions available for $ARCH (1.0.7, 1.0.6, ... 1.0.0),
    # newest first, read from the release assets. Git tags are unreliable
    # (release "1.0" is really version 1.0.0), so they are never used here.
    release_table | cut -f1
}

release_info_for() {
    # "URL<TAB>DIGEST" for the release that ships $1 for $ARCH, or nothing
    # if no such release exists.
    local version="$1"
    release_table |
        awk -F '\t' -v v="$version" '$1 == v { print $2 "\t" $3; exit }'
}

release_digest_for() {
    # GitHub's sha256 digest for the release asset that ships $1, or nothing
    # if unknown. Used to verify a downloaded archive matches what GitHub
    # recorded at upload time.
    local version="$1"
    release_info_for "$version" | cut -f2
}

release_url_for() {
    # GitHub's browser_download_url for the release that ships $1 for the
    # current $ARCH, or nothing if no such release exists. When the release
    # API is unreachable, fall back to the known download URL for
    # FALLBACK_VERSION so the installer still works offline.
    local version="$1"
    local url=""
    local asset=""

    url="$(release_info_for "$version" | cut -f1)"

    if [[ -z "$url" && "$version" == "$FALLBACK_VERSION" ]]; then
        asset="vix-${FALLBACK_VERSION}-${ARCH}.tar.gz"
        url="https://github.com/${REPO}/releases/download/${FALLBACK_TAG}/${asset}"
    fi

    printf '%s\n' "$url"
    [[ -n "$url" ]]
}

choose_version() {
    # Ask the user whether to install the latest release or an older one,
    # then print the selected canonical version (e.g. 1.0.7 or 1.0.0).
    local versions=()
    local tag
    local sorted=""
    local newest=""
    local choice=""
    local -a older_args=()
    local chosen=""

    while IFS= read -r tag; do
        [[ -n "$tag" ]] && versions+=("$tag")
    done < <(list_versions)

    if ((${#versions[@]} == 0)); then
        dialog --clear \
            --title "No releases found" \
            --msgbox \
            "Could not fetch the release list from GitHub.\n\nFalling back to the last known version:\n    ${FALLBACK_VERSION}" \
            0 0
        printf '%s\n' "$FALLBACK_VERSION"
        return 0
    fi

    sorted="$(printf '%s\n' "${versions[@]}" | sort -V)"
    newest="$(printf '%s\n' "$sorted" | tail -n1)"

    choice="$(
        dialog --clear \
            --title "Vix version" \
            --menu \
            "Found ${#versions[@]} release(s).\n\nLatest release:  ${newest}\n\nInstall the latest release or pick an older one?" \
            0 0 4 \
            latest \
                "Latest — ${newest} (Recommended)" \
            other \
                "Older releases..." \
            --stdout
    )" || return 1

    case "$choice" in
        latest)
            printf '%s\n' "$newest"
            return 0
            ;;
        other)
            older_args+=(--title "Older Vix releases" --menu "Select a version:" 0 0 12)

            while IFS= read -r tag; do
                [[ -n "$tag" && "$tag" != "$newest" ]] && \
                    older_args+=("$tag" "vix ${tag}")
            done <<< "$sorted"

            if ((${#older_args[@]} <= 4)); then
                dialog --clear \
                    --title "No older releases" \
                    --msgbox \
                    "There are no older releases to install.\n\nThe latest version is ${newest}." \
                    0 0
                printf '%s\n' "$newest"
                return 0
            fi

            older_args+=(--stdout)

            chosen="$(dialog --clear "${older_args[@]}")" || return 1

            if [[ -n "$chosen" ]]; then
                printf '%s\n' "$chosen"
                return 0
            fi

            return 1
            ;;
        *)
            return 1
            ;;
    esac
}

download_with_gauge() {
    local url="$1"
    local output="$2"
    local message="$3"

    local total=0
    local got=0
    local pct=0
    local pid
    local rc

    if command_exists curl; then
        total="$(
            curl -fsSI \
                --proto '=https' \
                --tlsv1.2 \
                --max-time 10 \
                "$url" 2>/dev/null |
                awk 'BEGIN {IGNORECASE=1} /^content-length:/ {
                    gsub("\r", "", $2); print $2
                }' |
                tail -n1
        )"
    elif command_exists wget; then
        total="$(
            wget \
                --spider \
                --server-response \
                --timeout=10 \
                "$url" 2>&1 |
                awk 'BEGIN {IGNORECASE=1} /Length:/ {
                    print $2
                }' |
                tail -n1
        )"
    fi

    [[ "$total" =~ ^[0-9]+$ ]] || total=0

    download_file "$url" "$output" &
    pid=$!

    while kill -0 "$pid" 2>/dev/null; do
        if [[ -f "$output" ]]; then
            got="$(stat -c '%s' "$output" 2>/dev/null || printf '0')"

            if ((total > 0)); then
                pct=$((got * 100 / total))
                ((pct > 99)) && pct=99
            else
                pct=0
            fi
        fi

        printf 'XXX\n%d\n%s\nXXX\n' "$pct" "$message"
        sleep 0.15
    done

    wait "$pid"
    rc=$?

    if ((rc == 0)); then
        printf 'XXX\n100\n%s\nXXX\n' "$message"
    fi

    return "$rc"
}

# ---------------------------------------------------------------------------
# Installation location
# ---------------------------------------------------------------------------

choose_location() {
    dialog --clear \
        --title "Installation location" \
        --menu \
        "Where should Vix be installed?" \
        0 0 4 \
        user \
            "$HOME/.local/bin — no root required (Recommended)" \
        local \
            "/usr/local/bin — administrator access" \
        usr \
            "/usr/bin — administrator access" \
        custom \
            "Custom directory" \
        --stdout
}

location_path() {
    local slot="$1"

    case "$slot" in
        user)
            printf '%s/.local/bin\n' "$HOME"
            ;;
        local)
            printf '/usr/local/bin\n'
            ;;
        usr)
            printf '/usr/bin\n'
            ;;
        custom)
            dialog --clear \
                --title "Custom installation directory" \
                --inputbox \
                "Enter an absolute directory path:" \
                0 0 \
                "$HOME/.local/bin" \
                --stdout
            ;;
        *)
            return 1
            ;;
    esac
}

validate_location() {
    local path="$1"

    [[ -n "$path" ]] || return 1
    [[ "$path" = /* ]] || return 1

    return 0
}

# ---------------------------------------------------------------------------
# Binary installation
# ---------------------------------------------------------------------------

install_binary() {
    local source="$1"
    local destination="$2"
    local target="$destination/vix"
    local temporary="$destination/.vix.tmp.$$"

    [[ -f "$source" ]] || return 1

    if [[ "$destination" == "$HOME/"* ||
          "$destination" == "$HOME" ]]; then

        mkdir -p -- "$destination" || return 1

        if ! install -m 0755 -- "$source" "$temporary"; then
            rm -f -- "$temporary"
            return 1
        fi

        mv -f -- "$temporary" "$target"
    else
        as_root mkdir -p -- "$destination" || return 1

        if ! as_root install -m 0755 -- "$source" "$temporary"; then
            as_root rm -f -- "$temporary" 2>/dev/null || true
            return 1
        fi

        if ! as_root mv -f -- "$temporary" "$target"; then
            as_root rm -f -- "$temporary" 2>/dev/null || true
            return 1
        fi
    fi

    return 0
}

install_with_progress() {
    local source="$1"
    local destination="$2"
    local rc

    dialog --clear

    install_binary "$source" "$destination"
    rc=$?

    if ((rc != 0)); then
        dialog --clear \
            --title "Installation failed" \
            --msgbox \
            "Vix could not be installed to:\n\n$destination/vix\n\nCheck the directory permissions and try again." \
            0 0
        return "$rc"
    fi

    return 0
}

verify_binary() {
    local binary="$1"
    local expected_version="${2:-}"

    [[ -x "$binary" ]] || return 1

    local output
    output="$("$binary" --version 2>&1)" || return 1

    if [[ -n "$expected_version" &&
          "$output" != *"$expected_version"* ]]; then
        return 1
    fi

    return 0
}

# ---------------------------------------------------------------------------
# PATH
# ---------------------------------------------------------------------------

path_contains() {
    local wanted="$1"
    local entry

    IFS=: read -r -a entries <<< "${PATH:-}"

    for entry in "${entries[@]}"; do
        [[ "$entry" == "$wanted" ]] && return 0
    done

    return 1
}

show_path_notice() {
    local destination="$1"

    [[ "$destination" == "$HOME/"* ]] || return 0

    if path_contains "$destination"; then
        return 0
    fi

    dialog --clear \
        --title "PATH notice" \
        --msgbox \
        "Vix was installed successfully.\n\nHowever, this directory is not currently in your PATH:\n\n$destination\n\nYou can add it manually to your shell configuration.\n\nFor example, Bash/Zsh:\n    export PATH=\"\$HOME/.local/bin:\$PATH\"" \
        0 0
}

# ---------------------------------------------------------------------------
# Prebuilt release installation
# ---------------------------------------------------------------------------

install_release() {
    local version
    local asset
    local url
    local archive
    local work
    local slot
    local destination
    local binary
    local download_rc
    local expected_digest
    local actual_digest

    if [[ -z "$ARCH" ]]; then
        dialog --clear \
            --title "Unsupported architecture" \
            --msgbox \
            "There is no known Vix prebuilt binary for this architecture:\n\n$(uname -m)\n\nYou can build Vix from source instead." \
            0 0
        return 1
    fi

    if ! command_exists curl && ! command_exists wget; then
        dialog --clear \
            --title "Download tool missing" \
            --yesno \
            "Vix needs curl or wget to download the release.\n\nInstall curl now?" \
            0 0

        if [[ $? -ne 0 ]]; then
            return 1
        fi

        if ! pm_install "$PKG_CURL"; then
            dialog --clear \
                --title "Installation failed" \
                --msgbox \
                "curl could not be installed." \
                0 0
            return 1
        fi
    fi

    version="$(choose_version)" || return 1

    url="$(release_url_for "$version")"
    if [[ -z "$url" ]]; then
        dialog --clear \
            --title "No binary for this release" \
            --msgbox \
            "There is no ${ARCH} prebuilt binary attached to release ${version}.\n\nTry a different version or build from source instead." \
            0 0
        return 1
    fi

    asset="${url##*/}"

    dialog --clear \
        --title "Install Vix" \
        --yesno \
        "Vix ${version}\n\nArchitecture:\n    ${ARCH}\n\nPackage:\n    ${asset}\n\nThis will download the prebuilt release from GitHub.\n\nContinue?" \
        0 0 || return 1

    archive="$TMPDIR/$asset"

    # Feed the gauge/progress stream into a dialog --gauge widget, like the
    # classic debian-installer progress screen. PIPESTATUS[0] is the download
    # exit code (the pipeline would otherwise report the widget's rc).
    download_with_gauge \
        "$url" \
        "$archive" \
        "Downloading Vix ${version} (${ARCH})..." |
        dialog \
            --clear \
            --title "Downloading Vix ${version}" \
            --gauge "Preparing download..." 8 60 0

    download_rc=${PIPESTATUS[0]}

    dialog --clear

    if ((download_rc != 0)); then
        dialog --clear \
            --title "Download failed" \
            --msgbox \
            "The Vix release could not be downloaded.\n\nURL:\n$url\n\nCheck your internet connection or try building from source." \
            0 0
        return 1
    fi

    if [[ ! -s "$archive" ]]; then
        dialog --clear \
            --title "Invalid download" \
            --msgbox \
            "The downloaded release archive is empty." \
            0 0
        return 1
    fi

    expected_digest="$(release_digest_for "$version")"
    if [[ -n "$expected_digest" ]]; then
        actual_digest="sha256:$(sha256_of "$archive")"
        if [[ -z "$actual_digest" || "$actual_digest" != "$expected_digest" ]]; then
            dialog --clear \
                --title "Checksum mismatch" \
                --msgbox \
                "The downloaded release archive does not match the checksum published for Vix ${version}.\n\nExpected:\n    ${expected_digest}\nGot:\n    ${actual_digest:-unavailable}\n\nThe download may have been tampered with. Aborting for your safety." \
                0 0
            return 1
        fi
    fi

    dialog --clear \
        --title "Preparing release" \
        --infobox \
        "Verifying and extracting the release archive..." \
        5 60

    if ! tar -tzf "$archive" >/dev/null 2>&1; then
        dialog --clear \
            --title "Invalid release" \
            --msgbox \
            "The downloaded file is not a valid Vix release archive." \
            0 0
        return 1
    fi

    # Reject archive members that could escape $TMPDIR during extraction:
    # absolute paths and any member containing "..".
    if tar -tzf "$archive" 2>/dev/null | grep -qE '^/|(^|/)\.\.(/|$)'; then
        dialog --clear \
            --title "Unsafe release archive" \
            --msgbox \
            "The release archive contains unsafe file paths and was refused.\n\nThis protects your system from malicious archives." \
            0 0
        return 1
    fi

    if ! tar -xzf "$archive" -C "$TMPDIR"; then
        dialog --clear \
            --title "Extraction failed" \
            --msgbox \
            "The Vix release archive could not be extracted." \
            0 0
        return 1
    fi

    work="$TMPDIR/vix"

    if [[ ! -f "$work" ]]; then
        dialog --clear \
            --title "Invalid release" \
            --msgbox \
            "The release archive did not contain the expected Vix executable." \
            0 0
        return 1
    fi

    chmod 0755 "$work" 2>/dev/null || true

    slot="$(choose_location)" || return 1
    destination="$(location_path "$slot")" || return 1

    validate_location "$destination" || {
        dialog --clear \
            --title "Invalid directory" \
            --msgbox \
            "The installation directory must be an absolute path." \
            0 0
        return 1
    }

    if ! install_with_progress "$work" "$destination"; then
        return 1
    fi

    binary="$destination/vix"

    if ! verify_binary "$binary" "$version"; then
        dialog --clear \
            --title "Verification failed" \
            --msgbox \
            "Vix was copied successfully, but the installed executable could not be verified.\n\nPath:\n$binary" \
            0 0
        return 1
    fi

    dialog --clear \
        --title "Installation complete" \
        --msgbox \
        "✓ Vix ${version} installed successfully.\n\nLocation:\n    $binary\n\nExecutable verification:\n    ✓ vix --version\n\nYou can now run:\n    vix" \
        0 0

    show_path_notice "$destination"

    return 0
}

# ---------------------------------------------------------------------------
# Source installation
# ---------------------------------------------------------------------------

install_from_source() {
    local preset
    local source
    local slot
    local destination
    local binary

    if ! ensure_build_tools; then
        return 1
    fi

    preset="$(choose_preset)" || return 1
    [[ -n "$preset" ]] || return 1

    if ! build_with "$preset"; then
        return 1
    fi

    source="$(binary_for_preset "$preset")" || return 1

    if [[ ! -x "$source" ]]; then
        dialog --clear \
            --title "Build output missing" \
            --msgbox \
            "The build completed, but the Vix executable could not be found:\n\n$source" \
            0 0
        return 1
    fi

    slot="$(choose_location)" || return 1
    destination="$(location_path "$slot")" || return 1

    validate_location "$destination" || {
        dialog --clear \
            --title "Invalid directory" \
            --msgbox \
            "The installation directory must be an absolute path." \
            0 0
        return 1
    }

    if ! install_with_progress "$source" "$destination"; then
        return 1
    fi

    binary="$destination/vix"

    if ! verify_binary "$binary"; then
        dialog --clear \
            --title "Verification failed" \
            --msgbox \
            "The executable was installed but could not be started for verification.\n\nPath:\n$binary" \
            0 0
        return 1
    fi

    local version_output
    version_output="$("$binary" --version 2>&1)"

    dialog --clear \
        --title "Installation complete" \
        --msgbox \
        "✓ Vix built and installed successfully.\n\nVersion:\n    $version_output\n\nLocation:\n    $binary\n\nBuild preset:\n    $preset" \
        0 0

    show_path_notice "$destination"

    return 0
}

# ---------------------------------------------------------------------------
# Clipboard
# ---------------------------------------------------------------------------

clipboard_packages_for_session() {
    case "$SESSION" in
        Wayland)
            printf 'wl-clipboard\n'
            ;;
        X11)
            printf 'xclip\n'
            ;;
        *)
            printf 'xclip\nwl-clipboard\n'
            ;;
    esac
}

install_clipboard() {
    local packages=()
    local package_list

    while IFS= read -r package; do
        [[ -n "$package" ]] && packages+=("$package")
    done < <(clipboard_packages_for_session)

    package_list="$(printf '%s\n' "${packages[@]}")"

    dialog --clear \
        --title "Clipboard integration" \
        --yesno \
        "Vix can use the system clipboard through a platform bridge.\n\nDetected session:\n    $SESSION\n\nRecommended package(s):\n\n$package_list\n\nInstall clipboard integration?" \
        0 0 || return 0

    if ! pm_install "${packages[@]}"; then
        dialog --clear \
            --title "Clipboard installation failed" \
            --msgbox \
            "The clipboard package could not be installed.\n\nVix itself is unaffected." \
            0 0
        return 1
    fi

    dialog --clear \
        --title "Clipboard integration" \
        --msgbox \
        "✓ Clipboard integration installed.\n\nVix can now use the system clipboard through the detected desktop session." \
        0 0

    return 0
}

# ---------------------------------------------------------------------------
# Uninstall
# ---------------------------------------------------------------------------

uninstall_vix() {
    local candidates=(
        "$HOME/.local/bin/vix"
        "/usr/local/bin/vix"
        "/usr/bin/vix"
    )

    local found=()
    local path

    for path in "${candidates[@]}"; do
        [[ -f "$path" ]] && found+=("$path")
    done

    if ((${#found[@]} == 0)); then
        dialog --clear \
            --title "Vix not found" \
            --msgbox \
            "No Vix executable was found in the standard installation locations." \
            0 0
        return 0
    fi

    local config_dir="$HOME/.config/vix"
    local data_dir="$HOME/.local/share/vix"

    local checklist=()

    checklist+=(
        binary
        "Vix executable"
        on
    )

    if [[ -e "$config_dir" ]]; then
        checklist+=(
            config
            "Configuration: $config_dir"
            off
        )
    fi

    if [[ -e "$data_dir" ]]; then
        checklist+=(
            data
            "User data: $data_dir"
            off
        )
    fi

    local selected

    selected="$(
        dialog --clear \
            --title "Uninstall Vix" \
            --checklist \
            "Select what you want to remove.\n\nUser configuration and data are NOT selected by default." \
            0 0 8 \
            "${checklist[@]}" \
            --separate-output \
            --stdout
    )" || return 0

    [[ -n "$selected" ]] || return 0

    local selected_binary=0
    local selected_config=0
    local selected_data=0

    while IFS= read -r path; do
        case "$path" in
            binary) selected_binary=1 ;;
            config) selected_config=1 ;;
            data) selected_data=1 ;;
        esac
    done <<< "$selected"

    if ((selected_binary == 0)); then
        selected_config=0
        selected_data=0
    fi

    local summary=""

    if ((selected_binary)); then
        summary+="Executable:\n"
        for path in "${found[@]}"; do
            summary+="  • $path\n"
        done
    fi

    if ((selected_config)); then
        summary+="\nConfiguration:\n  • $config_dir\n"
    fi

    if ((selected_data)); then
        summary+="\nUser data:\n  • $data_dir\n"
    fi

    dialog --clear \
        --title "Confirm uninstall" \
        --yesno \
        "The following will be removed:\n\n${summary}\n\nThis action cannot be undone.\n\nContinue?" \
        0 0 || return 0

    if ((selected_binary)); then
        for path in "${found[@]}"; do
            if [[ "$path" == "$HOME/"* ]]; then
                rm -f -- "$path"
            else
                as_root rm -f -- "$path" || {
                    dialog --clear \
                        --title "Uninstall failed" \
                        --msgbox \
                        "Could not remove:\n\n$path" \
                        0 0
                    return 1
                }
            fi
        done
    fi

    # User data is never removed with sudo.
    if ((selected_config)); then
        rm -rf -- "$config_dir"
    fi

    if ((selected_data)); then
        rm -rf -- "$data_dir"
    fi

    dialog --clear \
        --title "Uninstall complete" \
        --msgbox \
        "✓ Selected Vix components have been removed." \
        0 0

    return 0
}

# ---------------------------------------------------------------------------
# Main menu
# ---------------------------------------------------------------------------

menu_loop() {
    while :; do
        local choice

        choice="$(
            dialog --clear \
                --title "Vix Editor Installer" \
                --menu \
                "Vix Editor ${FALLBACK_VERSION}\n\nSystem: ${DISTRO} • $(uname -m) • ${SESSION}\n\nWhat would you like to do?" \
                0 0 7 \
                install \
                    "Install prebuilt release — Recommended" \
                source \
                    "Build and install from source" \
                clipboard \
                    "Install clipboard integration" \
                uninstall \
                    "Uninstall Vix" \
                quit \
                    "Exit installer" \
                --stdout
        )"

        case "$choice" in
            install)
                install_release
                ;;
            source)
                install_from_source
                ;;
            clipboard)
                install_clipboard
                ;;
            uninstall)
                uninstall_vix
                ;;
            quit|"")
                dialog --clear \
                    --title "Vix Installer" \
                    --msgbox \
                    "Thanks for using Vix.\n\nGoodbye!" \
                    0 0
                return 0
                ;;
        esac
    done
}

# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

main() {
    require_tty

    detect_distro

    if ! detect_pm; then
        clear
        printf '%s\n' \
            "Unsupported Linux distribution." \
            "" \
            "The installer supports:" \
            "  • Debian / Ubuntu / derivatives" \
            "  • Fedora / RHEL / derivatives" \
            "  • Arch Linux / derivatives" \
            "  • openSUSE" \
            "  • Alpine Linux" \
            "" \
            "No supported package manager was detected." >&2
        exit 1
    fi

    setup_packages || die "Failed to configure package-manager support."

    detect_arch
    detect_session

    ensure_dialog || exit 1

    menu_loop
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    main "$@"
fi