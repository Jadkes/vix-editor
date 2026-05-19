#!/usr/bin/env bash
#
# install.sh - Vix Editor Installer
#
# Purpose: Cross-distro installer for Vix editor suite.
#          Detects Linux distribution, installs build dependencies,
#          builds both vix and vix_agent binaries, and installs
#          them to ~/.local/bin/.
#
# Usage:
#   ./install.sh            Full install (deps + build + install)
#   ./install.sh install    Same as above
#   ./install.sh uninstall  Remove installed files
#   ./install.sh status     Check current installation state
#   ./install.sh --help     Show this usage message
#
# Design: Self-contained g++ build (no cmake dependency).
#         All files installed under ~/.local/ (no system-wide changes).
#
# Thread-safety: Single-user script, no concurrent usage expected.

set -euo pipefail

# -- Constants -----------------------------------------------------------------

BINDIR="${HOME}/.local/bin"
DATADIR="${HOME}/.local/share/vix"
PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRCDIR="${PROJECT_DIR}/src"

# Files to compile
VIX_SOURCES="src/vix_editor.cpp src/core/buffer.cpp src/history/history.cpp"
AGENT_SOURCES="src/vix_agent.cpp"

# -- Colors --------------------------------------------------------------------

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

info()  { printf "${CYAN}[INFO]${NC}  %s\n" "$*"; }
ok()    { printf "${GREEN}[OK]${NC}    %s\n" "$*"; }
warn()  { printf "${YELLOW}[WARN]${NC}  %s\n" "$*"; }
error() { printf "${RED}[ERROR]${NC} %s\n" "$*"; }

# -- Help ----------------------------------------------------------------------

show_usage() {
    cat <<EOF
Vix Editor Installer

Usage:
  ./install.sh            Full install (deps + build + install)
  ./install.sh install    Same as above
  ./install.sh uninstall  Remove installed files
  ./install.sh status     Check current installation state
  ./install.sh --help     Show this message

The installer:
  1. Detects your Linux distribution
  2. Installs required build dependencies (g++, python3-dev, ncurses, xclip)
  3. Builds vix and vix_agent binaries
  4. Installs binaries to ~/.local/bin/
  5. Installs python brain to ~/.local/share/vix/
EOF
}

# -- Distribution Detection ----------------------------------------------------

detect_distro() {
    if [ ! -f /etc/os-release ]; then
        error "Cannot detect Linux distribution: /etc/os-release not found."
        error "Supported: Ubuntu, Debian, Fedora, Arch Linux."
        exit 1
    fi

    # shellcheck source=/dev/null
    . /etc/os-release

    case "${ID:-}" in
        ubuntu|debian)
            echo "debian"
            ;;
        fedora)
            echo "fedora"
            ;;
        arch)
            echo "arch"
            ;;
        *)
            error "Unsupported distribution: ${ID:-unknown}."
            error "Supported: Ubuntu, Debian, Fedora, Arch Linux."
            exit 1
            ;;
    esac
}

# -- Dependency Installation ---------------------------------------------------

install_deps_debian() {
    info "Installing dependencies for Debian/Ubuntu..."
    sudo apt update -qq
    sudo apt install -y g++ python3-dev libncurses-dev xclip
    ok "Dependencies installed."
}

install_deps_fedora() {
    info "Installing dependencies for Fedora..."
    sudo dnf install -y gcc-c++ python3-devel ncurses-devel xclip
    ok "Dependencies installed."
}

install_deps_arch() {
    info "Installing dependencies for Arch Linux..."
    sudo pacman -S --noconfirm gcc python ncurses xclip
    ok "Dependencies installed."
}

install_dependencies() {
    local distro
    distro="$(detect_distro)"

    case "${distro}" in
        debian) install_deps_debian ;;
        fedora) install_deps_fedora ;;
        arch)   install_deps_arch   ;;
    esac
}

# -- Build ---------------------------------------------------------------------

build_binaries() {
    info "Resolving Python build flags..."

    # Use python3-config for portable include/library paths
    local python_includes python_ldflags
    python_includes="$(python3-config --includes)"
    python_ldflags="$(python3-config --ldflags)"

    local cxxflags="-std=c++20 -Wall ${python_includes}"
    local ldflags="${python_ldflags} -lncurses"

    info "Building vix editor..."
    (
        cd "${PROJECT_DIR}"
        # shellcheck disable=SC2086
        g++ ${VIX_SOURCES} -o vix ${cxxflags} ${ldflags}
    )
    ok "vix built successfully."

    info "Building vix_agent..."
    (
        cd "${PROJECT_DIR}"
        # shellcheck disable=SC2086
        g++ ${AGENT_SOURCES} -o vix_agent ${cxxflags} ${ldflags}
    )
    ok "vix_agent built successfully."
}

# -- Install -------------------------------------------------------------------

install_binaries() {
    info "Installing binaries to ${BINDIR}..."
    mkdir -p "${BINDIR}"

    cp "${PROJECT_DIR}/vix" "${BINDIR}/vix"
    cp "${PROJECT_DIR}/vix_agent" "${BINDIR}/vix_agent"
    chmod +x "${BINDIR}/vix" "${BINDIR}/vix_agent"
    ok "Binaries installed."

    # Install python brain file
    info "Installing python brain to ${DATADIR}..."
    mkdir -p "${DATADIR}"
    if [ -f "${PROJECT_DIR}/vix_brain.py" ]; then
        cp "${PROJECT_DIR}/vix_brain.py" "${DATADIR}/vix_brain.py"

        # Also copy alongside binaries so the C++ code (which looks in exeDir) can find it
        cp "${PROJECT_DIR}/vix_brain.py" "${BINDIR}/vix_brain.py"

        ok "Python brain installed."
    else
        warn "vix_brain.py not found in project root, skipping."
    fi

    # Check PATH
    case ":${PATH}:" in
        *:"${BINDIR}":*)
            ;;
        *)
            warn "${BINDIR} is not in your PATH."
            warn "Add it to your shell config:"
            warn "  echo 'export PATH=\"\${PATH}:${BINDIR}\"' >> ~/.bashrc"
            warn "  # or ~/.zshrc if using zsh"
            ;;
    esac
}

# -- Uninstall -----------------------------------------------------------------

uninstall() {
    local had_anything=false

    if [ -f "${BINDIR}/vix" ]; then
        rm "${BINDIR}/vix"
        info "Removed ${BINDIR}/vix"
        had_anything=true
    fi

    if [ -f "${BINDIR}/vix_agent" ]; then
        rm "${BINDIR}/vix_agent"
        info "Removed ${BINDIR}/vix_agent"
        had_anything=true
    fi

    if [ -f "${BINDIR}/vix_brain.py" ]; then
        rm "${BINDIR}/vix_brain.py"
        info "Removed ${BINDIR}/vix_brain.py"
        had_anything=true
    fi

    if [ -d "${DATADIR}" ]; then
        rm -rf "${DATADIR}"
        info "Removed ${DATADIR}/"
        had_anything=true
    fi

    if [ "${had_anything}" = true ]; then
        ok "Vix editor has been uninstalled."
    else
        info "Nothing to uninstall. Vix editor is not installed."
    fi
}

# -- Status --------------------------------------------------------------------

check_command() {
    if command -v "$1" &>/dev/null; then
        printf "  ${GREEN}✓${NC} %s\n" "$1"
    else
        printf "  ${RED}✗${NC} %s (not found)\n" "$1"
    fi
}

check_package() {
    local pkg="$1"
    if dpkg -s "${pkg}" &>/dev/null 2>&1; then
        printf "  ${GREEN}✓${NC} %s\n" "${pkg}"
    elif rpm -q "${pkg}" &>/dev/null 2>&1; then
        printf "  ${GREEN}✓${NC} %s\n" "${pkg}"
    elif pacman -Qi "${pkg}" &>/dev/null 2>&1; then
        printf "  ${GREEN}✓${NC} %s\n" "${pkg}"
    else
        printf "  ${RED}✗${NC} %s (not installed)\n" "${pkg}"
    fi
}

show_status() {
    echo ""
    info "Vix Editor Installation Status"
    echo ""

    # Required commands
    echo "  Required tools:"
    check_command "g++"
    check_command "python3"
    check_command "python3-config"

    # Distribution
    echo ""
    if [ -f /etc/os-release ]; then
        # shellcheck source=/dev/null
        . /etc/os-release
        printf "  Distribution: ${CYAN}%s${NC}\n" "${PRETTY_NAME:-${ID:-unknown}}"
    fi

    # Installed binaries
    echo ""
    echo "  Installed files:"
    if [ -f "${BINDIR}/vix" ]; then
        printf "  ${GREEN}✓${NC} %s/vix\n" "${BINDIR}"
    else
        printf "  ${RED}✗${NC} %s/vix (not installed)\n" "${BINDIR}"
    fi
    if [ -f "${BINDIR}/vix_agent" ]; then
        printf "  ${GREEN}✓${NC} %s/vix_agent\n" "${BINDIR}"
    else
        printf "  ${RED}✗${NC} %s/vix_agent (not installed)\n" "${BINDIR}"
    fi
    if [ -f "${DATADIR}/vix_brain.py" ]; then
        printf "  ${GREEN}✓${NC} %s/vix_brain.py\n" "${DATADIR}"
    else
        printf "  ${RED}✗${NC} %s/vix_brain.py (not installed)\n" "${DATADIR}"
    fi

    # PATH check
    echo ""
    case ":${PATH}:" in
        *:"${BINDIR}":*)
            printf "  ${GREEN}✓${NC} %s is in PATH\n" "${BINDIR}"
            ;;
        *)
            printf "  ${RED}✗${NC} %s is NOT in PATH\n" "${BINDIR}"
            printf "  Add it: export PATH=\"\${PATH}:%s\"\n" "${BINDIR}"
            ;;
    esac
    echo ""
}

# -- Main ----------------------------------------------------------------------

main() {
    local cmd="${1:-}"

    case "${cmd}" in
        --help|-h)
            show_usage
            exit 0
            ;;
        install|"")
            echo "=============================================="
            echo "  Vix Editor Installer"
            echo "=============================================="
            echo ""

            install_dependencies
            echo ""
            build_binaries
            echo ""
            install_binaries
            echo ""
            ok "Installation complete! Run 'vix' to start the editor."
            ;;
        uninstall)
            echo "=============================================="
            echo "  Vix Editor Uninstaller"
            echo "=============================================="
            echo ""
            uninstall
            ;;
        status)
            show_status
            ;;
        *)
            error "Unknown command: ${cmd}"
            echo ""
            show_usage
            exit 1
            ;;
    esac
}

main "$@"
