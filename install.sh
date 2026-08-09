#!/usr/bin/env bash
#
# install.sh - Vix Editor Installer
#
# Detects the Linux distribution and its package manager, then walks the
# user through a dialog(1) menu: prebuilt-binary install, build from
# source, clipboard tools, or uninstall.
#
# Cli:           ./install.sh
# Requires:      dialog (offered for install if missing)
#
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="Jadkes/vix-editor"
VERSION="1.0.7"          # fallback when the GitHub API is unreachable
ARCH="linux-x86_64"

PM=""                  # package manager id: apt|dnf|pacman|zypper|apk
PKG_CMAKE=""           # build-tool package names for the detected PM
PKG_CC=""
PKG_NINJA=""
PKG_NCURSES=""
CLIP_PKGS=()           # clipboard bridge packages (used at runtime)

# ---------------------------------------------------------------------------
# Package-manager plumbing
# ---------------------------------------------------------------------------

# Run a command as root, omitting sudo when we already are root.
as_root() {
    if [[ $EUID -eq 0 ]]; then
        "$@"
    else
        sudo "$@"
    fi
}

# Install packages with the detected package manager.
pm_install() {
    case "$PM" in
        apt)    as_root apt-get install -y "$@" ;;
        dnf)    as_root dnf install -y "$@" ;;
        pacman) as_root pacman -S --noconfirm "$@" ;;
        zypper) as_root zypper --non-interactive install -y "$@" ;;
        apk)    as_root apk add --no-cache "$@" ;;
        *)      return 1 ;;
    esac
}

# Guess the package manager from /etc/os-release (ID / ID_LIKE), falling
# back to whichever manager binary is actually present.
detect_pm() {
    local id="" id_like=""
    if [[ -f /etc/os-release ]]; then
        id=$(sed -n 's/^ID=["'\'']\?\([^"'\'' ]*\).*/\1/p' /etc/os-release)
        id_like=$(sed -n 's/^ID_LIKE=["'\'']\?\([^"'\'' ]*\).*/\1/p' /etc/os-release)
    fi
    case "$id" in
        debian|ubuntu|linuxmint|pop|elementary|kali|raspbian|devuan|mx) PM=apt ;;
        fedora|rhel|centos|rocky|almalinux|ol|fedora-asan)             PM=dnf ;;
        arch|archlinux|manjaro|endeavouros|cachyos|artix)               PM=pacman ;;
        opensuse|opensuse-leap|opensuse-tumbleweed|sles|sle-hpc)        PM=zypper ;;
        alpine)                                                          PM=apk ;;
        *)
            case "$id_like" in
                *debian*) PM=apt ;;
                *fedora*|*rhel*) PM=dnf ;;
                *arch*) PM=pacman ;;
                *suse*) PM=zypper ;;
                *alpine*) PM=apk ;;
                *) PM=$(detect_pm_by_binary) ;;
            esac
            ;;
    esac
}

# Last resort: pick the first available manager binary.
detect_pm_by_binary() {
    local bin
    for bin in apt-get dnf pacman zypper apk; do
        if command -v "$bin" >/dev/null 2>&1; then
            echo "$bin"
            return 0
        fi
    done
    return 1
}

# Set the per-PM package-name mapping once detection has run.
setup_packages() {
    case "$PM" in
        apt)
            PKG_CMAKE=cmake; PKG_CC=build-essential; PKG_NINJA=ninja-build
            PKG_NCURSES=libncursesw5-dev
            CLIP_PKGS=(xclip wl-clipboard)
            ;;
        dnf)
            PKG_CMAKE=cmake; PKG_CC=gcc-c++; PKG_NINJA=ninja-build
            PKG_NCURSES=ncurses-devel
            CLIP_PKGS=(xclip wl-clipboard)
            ;;
        pacman)
            PKG_CMAKE=cmake; PKG_CC=base-devel; PKG_NINJA=ninja
            PKG_NCURSES=ncurses
            CLIP_PKGS=(xclip wl-clipboard)
            ;;
        zypper)
            PKG_CMAKE=cmake; PKG_CC=gcc-c++; PKG_NINJA=ninja
            PKG_NCURSES=ncurses-devel
            CLIP_PKGS=(xclip wl-clipboard)
            ;;
        apk)
            PKG_CMAKE=cmake; PKG_CC=build-base; PKG_NINJA=ninja
            PKG_NCURSES=ncurses-dev
            CLIP_PKGS=(xclip wl-clipboard)
            ;;
    esac
}

# ---------------------------------------------------------------------------
# dialog
# ---------------------------------------------------------------------------

# dialog(1) drives every screen. If it is missing, offer to install it
# through the detected package manager before anything else happens.
ensure_dialog() {
    if command -v dialog >/dev/null 2>&1; then
        return 0
    fi
    clear
    echo "The vix installer shows its menus with 'dialog' (ncurses),"
    echo "which is not installed on this system."
    echo
    read -r -p "Install dialog now via $PM? [y/N] " ans
    case "${ans,,}" in
        y|yes)
            pm_install dialog || return 1
            command -v dialog >/dev/null 2>&1
            ;;
        *)
            echo "Aborting - install dialog or run the build manually."
            return 1
            ;;
    esac
}

# ---------------------------------------------------------------------------
# Build-from-source
# ---------------------------------------------------------------------------

# True when a wide-char (or plain) ncurses development header is present.
ncurses_dev_present() {
    local h
    for h in \
        /usr/include/ncursesw/curses.h \
        /usr/include/ncursesw/ncurses.h \
        /usr/include/ncurses/ncurses.h \
        /usr/include/ncurses.h; do
        [[ -f "$h" ]] && return 0
    done
    return 1
}

# Collect the names and packages of every tool needed to build. Fills the
# global MISSING_PKGS array; returns 0 when nothing is missing.
MISSING_PKGS=()
check_build_tools() {
    MISSING_PKGS=()
    # Message returned to the user once, with the full list of gaps.
    local gaps=()

    if ! command -v cmake >/dev/null 2>&1; then
        gaps+=(cmake); MISSING_PKGS+=("$PKG_CMAKE")
    fi
    if ! command -v ninja >/dev/null 2>&1 && ! command -v ninja-build >/dev/null 2>&1; then
        gaps+=("ninja build tool"); MISSING_PKGS+=("$PKG_NINJA")
    fi
    if ! command -v c++ >/dev/null 2>&1 && ! command -v g++ >/dev/null 2>&1; then
        gaps+=("a C++20 compiler"); MISSING_PKGS+=("$PKG_CC")
    fi
    if ! ncurses_dev_present; then
        gaps+=("ncursesw development headers"); MISSING_PKGS+=("$PKG_NCURSES")
    fi

    # Deduplicate (base-devel likes to pull in several holes at once).
    local -A seen=()
    local dedup=() p
    for p in "${MISSING_PKGS[@]}"; do
        [[ -n "$p" && -z "${seen[$p]:-}" ]] && { seen[$p]=1; dedup+=("$p"); }
    done
    MISSING_PKGS=("${dedup[@]}")

    if ((${#gaps[@]} > 0)); then
        MISSING_GAPS="${gaps[*]}"
        return 1
    fi
    return 0
}

# Show which tools are absent and offer to install them with the package
# manager. Returns 0 when the toolchain is complete; "no" bounces back to
# the main menu.
ensure_build_tools() {
    if check_build_tools; then
        return 0
    fi
    dialog --clear \
        --title "Missing build tools" \
        --yesno "vix needs these to compile from source:\n\n    ${MISSING_GAPS}\n\nThe installer will run:\n    ${PM}: ${MISSING_PKGS[*]}\n\nInstall them now?" 0 0
    local rc=$?
    if [[ $rc -eq 1 ]]; then
        return 1        # user said no -> back to main menu
    fi
    [[ $rc -eq 0 ]] || exit 1
    pm_install "${MISSING_PKGS[@]}" || {
        dialog --clear --msgbox "Package install failed. Nothing was changed." 0 0
        return 1
    }
    check_build_tools
}

# Let the user pick a build preset. "default (Recommended)" is always the
# first item and the returned value matches a CMakePresets.json name.
choose_preset() {
    dialog --clear \
        --title "Build preset" \
        --menu "Pick a build configuration (first attempt needs network to\nfetch googletest for the unit tests):" 0 0 3 \
            default    "default (Recommended) - Release, with tests" \
            build-only "build-only - Release, no tests (fastest)" \
            debug      "debug - Debug build with sanitizers" --stdout
}

# Feed a dialog --gauge from ninja's '[a/b] Building...' lines. The gauge
# only changes its prompt through the XXX protocol (XXX, a percent, text
# up to the next XXX), so each step emits a full block. A trailing 100
# covers "nothing to rebuild" so the bar always lands at the end.
build_percent_feed() {
    while IFS= read -r line; do
        if [[ "$line" =~ ^\[([0-9]+)/([0-9]+)\] ]]; then
            local pct=$(( BASH_REMATCH[1] * 100 / BASH_REMATCH[2] ))
            (( pct > 99 )) && pct=99
            printf 'XXX\n%d\n%s\nXXX\n' "$pct" "$line"
        fi
    done
    printf 'XXX\n100\ndone\nXXX\n'
}

# Compile the tree with the chosen preset, streaming into a percentage
# gauge (percentages come from ninja's [a/b] progress). Keeps a log for
# the failure screen. Returns 0 on success.
build_with() {
    local preset="$1" log="/tmp/vix-build-$$.log" rc
    dialog --clear
    (
        cd "$SCRIPT_DIR" \
            && cmake --preset "$preset" \
            && cmake --build --preset "$preset" 2>&1
    ) | tee "$log" | build_percent_feed | dialog --gauge "Compiling vix ..." 8 70 0
    rc=${PIPESTATUS[0]}
    if [[ $rc -ne 0 ]]; then
        dialog --clear --textbox "$log" 30 80
        dialog --clear --msgbox "Build failed - see the output above." 0 0
        rm -f "$log"
        return 1
    fi
    rm -f "$log"
    return 0
}

# The binary the preset produced. Keep this in sync with CMakePresets.json.
binary_for_preset() {
    local preset="$1"
    case "$preset" in
        default)    echo "$SCRIPT_DIR/build/vix" ;;
        debug)      echo "$SCRIPT_DIR/build-debug/vix" ;;
        build-only) echo "$SCRIPT_DIR/build-notests/vix" ;;
    esac
}

# ---------------------------------------------------------------------------
# Install/uninstall of the vix binary
# ---------------------------------------------------------------------------

# Pick where the binary should land. User-local needs no root; the system
# directories do. Slots a custom path through an input box.
choose_location() {
    dialog --clear \
        --title "Install location" \
        --menu "Where should the vix binary go?" 0 0 0 \
            user   "\$HOME/.local/bin - no root needed (Recommended)" \
            local  "/usr/local/bin - needs sudo" \
            usr    "/usr/bin - needs sudo" \
            custom "Pick your own directory" --stdout
}

# Map an install-location slot to its filesystem path.
location_path() {
    local slot="$1"
    case "$slot" in
        user)   echo "$HOME/.local/bin" ;;
        local)  echo "/usr/local/bin" ;;
        usr)    echo "/usr/bin" ;;
        custom)
            dialog --clear --inputbox "Install directory:" 0 0 "$HOME/.local/bin" --stdout
            ;;
    esac
}

# Copy a built binary or extracted release file into place.
# Animate a short "Installing to ..." bar, then do the actual copy with
# dialog cleared so a sudo password prompt (system dirs) owns the
# terminal cleanly. Returns the install result.
install_with_gauge() {
    local src="$1" dest="$2" i rc
    dialog --clear
    {
        for i in 20 45 70 90; do
            printf 'XXX\n%d\nInstalling to %s/vix ...\nXXX\n' "$i" "$dest"
            sleep 0.1
        done
    } | dialog --gauge "" 8 70 0
    install_binary "$src" "$dest"
    rc=$?
    return "$rc"
}

install_binary() {
    local src="$1" dest="$2"
    if [[ "$dest" == "$HOME/.local/bin" ]]; then
        mkdir -p "$dest"
        install -m755 "$src" "$dest/vix"
    else
        as_root mkdir -p "$dest"
        as_root install -m755 "$src" "$dest/vix"
    fi
}

# ---------------------------------------------------------------------------
# Prebuilt-binary (release) install
# ---------------------------------------------------------------------------

# Latest release tag from the GitHub API; falls back to VERSION offline.
latest_version() {
    local out
    out=$(curl -s --max-time 10 "https://api.github.com/repos/$REPO/releases/latest" 2>/dev/null \
        | sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"v\(\([0-9]*\.\)\{2\}[0-9]*\)".*/\1/p')
    if [[ -n "$out" ]]; then
        echo "$out"
    else
        echo "$VERSION"
    fi
}

# Download a URL into $out, feeding a real percentage into dialog --gauge.
# The total comes from a HEAD request; a polling loop compares bytes on
# disk against it. Returns the download tool's exit status.
download_with_gauge() {
    local url="$1" out="$2" msg="$3" total=0 got=0 pct=0 rc
    if command -v curl >/dev/null 2>&1; then
        total=$(curl -sIL --max-time 10 "$url" \
            | sed -n 's/^[Cc]ontent-[Ll]ength:[[:space:]]*\([0-9]*\).*/\1/p' \
            | tail -1)
    else
        total=$(wget --spider --server-response "$url" 2>&1 \
            | sed -n 's/.*[Cc]ontent-[Ll]ength:[[:space:]]*\([0-9]*\).*/\1/p' \
            | tail -1)
    fi
    total=${total:-0}

    # Run the download in the background; the gauge loop watches the file.
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL -o "$out" "$url" &
    else
        wget -q -O "$out" "$url" &
    fi
    local dpid=$!
    while kill -0 "$dpid" 2>/dev/null; do
        if [[ -f "$out" ]]; then
            got=$(stat -c%s "$out" 2>/dev/null || echo 0)
            if (( total > 0 )); then
                pct=$(( got * 100 / total ))
                (( pct > 100 )) && pct=100
            fi
        fi
        printf 'XXX\n%d\n%s\nXXX\n' "$pct" "$msg"
        sleep 0.1
    done
    wait "$dpid"
    rc=$?
    printf 'XXX\n100\n%s (done)\nXXX\n' "$msg"
    return "$rc"
}

# Download, unpack, and install the prebuilt release archive.
install_release() {
    local ver asset url tmpfile work rc

    if ! command -v curl >/dev/null 2>&1 && ! command -v wget >/dev/null 2>&1; then
        dialog --clear --msgbox "Neither curl nor wget is installed." 0 0
        return 1
    fi

    ver=$(latest_version)
    asset="vix-${ver}-${ARCH}.tar.gz"
    url="https://github.com/$REPO/releases/download/v${ver}/$asset"

    dialog --clear \
        --title "Prebuilt binary" \
        --yesno "Download vix ${ver} and install it?\n\n    $asset\n    (${ARCH})\n\nRelease binary needs no toolchain." 0 0 || {
        return 1
    }

    tmpfile="$(mktemp -d)"
    dialog --clear
    download_with_gauge "$url" "$tmpfile/$asset" "Downloading vix $ver ..." \
        | dialog --gauge "" 8 70 0
    rc=${PIPESTATUS[0]}
    if [[ $rc -ne 0 ]]; then
        dialog --clear --msgbox "Download failed: $url" 0 0
        rm -rf "$tmpfile"
        return 1
    fi

    tar -xzf "$tmpfile/$asset" -C "$tmpfile"   # yields "$tmpfile/vix"
    work="$tmpfile/vix"

    local slot dest
    slot=$(choose_location) || { rm -rf "$tmpfile"; return 1; }
    dest=$(location_path "$slot") || { rm -rf "$tmpfile"; return 1; }
    [[ -n "$dest" ]] || { rm -rf "$tmpfile"; return 1; }

    install_with_gauge "$work" "$dest" || { rm -rf "$tmpfile"; return 1; }
    rm -rf "$tmpfile"
    dialog --clear \
        --title "Installed" \
        --msgbox "vix $ver installed to:\n\n    $dest/vix\n\nRun 'vix --version' to verify." 0 0
}

# ---------------------------------------------------------------------------
# Clipboard tools
# ---------------------------------------------------------------------------

# vix bridges the system clipboard through xclip/xsel (X11) or wl-copy/
# wl-paste (Wayland). Installing them is optional - the editor keeps an
# internal clipboard either way.
install_clipboard() {
    dialog --clear \
        --title "Clipboard tools" \
        --yesno "Install the clipboard bridge?\n\n    ${CLIP_PKGS[*]}\n\nOptional - vix works without them, but 'yank' and\n'paste' won't reach the system clipboard." 0 0 || return 0
    pm_install "${CLIP_PKGS[@]}" || {
        dialog --clear --msgbox "Clipboard install failed." 0 0
        return 1
    }
    dialog --clear --msgbox "Clipboard tools installed." 0 0
}

# ---------------------------------------------------------------------------
# Uninstall
# ---------------------------------------------------------------------------

# Walk the known install paths, collect whatever exists, and offer to
# delete the binary plus the user's vix data directories.
uninstall_vix() {
    local candidates=(
        "$HOME/.local/bin/vix"
        /usr/local/bin/vix
        /usr/bin/vix
    )
    local found=() c
    for c in "${candidates[@]}"; do
        [[ -f "$c" ]] && found+=("$c")
    done

    if ((${#found[@]} == 0)); then
        dialog --clear --msgbox "No vix binary found in the usual places." 0 0
        return 0
    fi

    local data_dirs=(
        "$HOME/.local/share/vix"
        "$HOME/.config/vix"
    )
    local found_data=() d
    for d in "${data_dirs[@]}"; do
        [[ -e "$d" ]] && found_data+=("$d")
    done

    dialog --clear \
        --title "Uninstall" \
        --yesno "Remove these binaries?\n\n    $(printf '%s\n    ' "${found[@]}")\n$([[ ((${#found_data[@]}>0)) ]] && printf 'Plus data:\n    %s\n' "${found_data[*]}")" 0 0 || return 0

    for c in "${found[@]}"; do
        if [[ "$c" == "$HOME/.local/bin/"* ]]; then
            rm -f "$c"
        else
            as_root rm -f "$c"
        fi
    done
    for d in "${found_data[@]}"; do
        as_root rm -rf "$d"
    done
    dialog --clear --msgbox "vix has been removed." 0 0
}

# ---------------------------------------------------------------------------
# Main menu
# ---------------------------------------------------------------------------

menu_loop() {
    while :; do
        local choice
        choice=$(dialog --clear \
            --title "vix installer" \
            --menu "What do you want to do?" 0 0 0 \
                install  "Install the prebuilt release binary (Recommended)" \
                build    "Build and install from source" \
                clip     "Install clipboard tools (xclip / wl-clipboard)" \
                uninstall "Remove vix from this system" \
                quit     "Leave the installer" --stdout)
        case "$choice" in
            install)
                install_release ;;
            build)
                ensure_build_tools || continue
                local preset dest
                preset=$(choose_preset) || continue
                [[ -n "$preset" ]] || continue
                build_with "$preset" || continue
                slot=$(choose_location) || continue
                dest=$(location_path "$slot") || continue
                [[ -n "$dest" ]] || continue
                install_with_gauge "$(binary_for_preset "$preset")" "$dest" || continue
                dialog --clear --msgbox "vix installed to:\n\n    $dest/vix\n\nRun 'vix --version' to verify." 0 0
                ;;
            clip)
                install_clipboard ;;
            uninstall)
                uninstall_vix ;;
            quit|"")
                dialog --clear --msgbox "Bye! Run './install.sh' again to reinstall." 0 0
                return 0
                ;;
        esac
    done
}

# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

# Only run the interactive installer when executed directly, so the pure
# helpers stay sourceable for testing.
if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    detect_pm
    if [[ -z "$PM" ]]; then
        echo "Unsupported distribution - cannot determine a package manager." >&2
        exit 1
    fi
    setup_packages
    ensure_dialog || exit 1
    menu_loop
fi