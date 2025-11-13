#!/usr/bin/env bash
# Script to build all examples and run them in separate tmux windows
# By default, uses tmux to run each example in a separate window and mounts a close call hook
# Usage: ./run_mvp.sh [--no-tmux]

set -e

# Check if git submodules are initialized and checked out
if git submodule status | grep -qE '^[-+]'; then
    echo "Error: Some git submodules are not initialized or checked out."
    echo "Run: git submodule update --init --recursive"
    exit 1
fi

# Check if tmux is installed
if ! command -v tmux &> /dev/null; then
    echo "Error: tmux is not installed."
    echo "Please install tmux."
    exit 1
fi

# Check if cmake is installed
if ! command -v cmake &> /dev/null; then
    echo "Error: cmake is not installed."
    echo "Please install cmake."
    exit 1
fi

# Check if git-lfs is installed
if ! command -v git-lfs &> /dev/null; then
    echo "Error: git-lfs is not installed."
    echo "Install it with: sudo apt-get install -y git-lfs"
    exit 1
fi

# Check if libboost-all-dev is installed
if ! dpkg -s libboost-all-dev &> /dev/null; then
    echo "Error: libboost-all-dev is not installed."
    echo "Install it with: sudo apt-get install -y libboost-all-dev"
    exit 1
fi

# Check if libacl1-dev is installed
if ! dpkg -s libacl1-dev &> /dev/null; then
    echo "Error: libacl1-dev is not installed."
    echo "Install it with: sudo apt-get install -y libacl1-dev"
    exit 1
fi


USE_TMUX=1
if [[ "$1" == "--no-tmux" ]]; then
    USE_TMUX=0
fi

EXAMPLES_DIR="$(dirname "$0")/../src/examples"
TARGET_DIR="$(dirname "$0")/../target/debug/examples"
SESSION="mvp_run"

if [[ ! -f "$EXAMPLES_DIR/Cargo.toml" ]]; then
    echo "examples/Cargo.toml not found" >&2
    exit 1
fi



# Source someip_tunnel env for the whole script
SOMEIP_TUNNEL_DIR="$(dirname "$0")/../external/someip_tunnel"

source "$SOMEIP_TUNNEL_DIR/scripts/env"

# Build all examples
(cargo build && cd "$EXAMPLES_DIR" && cargo build --examples)


# Build someip_tunnel if present
if [[ -d "$SOMEIP_TUNNEL_DIR" ]]; then
    if [[ -f "$SOMEIP_TUNNEL_DIR/scripts/build_vsomeip.sh" ]]; then
        echo "Building someip_tunnel..."
        (cd "$SOMEIP_TUNNEL_DIR" && scripts/build_vsomeip.sh)
    else
        echo "Warning: someip_tunnel build script not found in $SOMEIP_TUNNEL_DIR/scripts"
        exit 1
    fi
else
    echo "No path ../someip_tunnel, cannot proceed"
    exit 1
fi



# Find all .rs files in examples folder and use their names as binaries to start
EXAMPLE_BINS=()
while IFS= read -r -d '' RSFILE; do
    BIN_NAME=$(basename "$RSFILE" .rs)
    BIN_PATH="$TARGET_DIR/$BIN_NAME"
    if [[ -x "$BIN_PATH" ]]; then
        EXAMPLE_BINS+=("$BIN_PATH")
    else
        echo "Warning: binary $BIN_PATH not found or not executable" >&2

    fi
done < <(find "$EXAMPLES_DIR" -maxdepth 1 -type f -name '*.rs' -print0)



# Add someip_tunnel apps if present, ensuring tunnel/tunnel is first
SOMEIP_TUNNEL_EXAMPLES="$SOMEIP_TUNNEL_DIR/build/examples"
TUNNEL_BIN="$SOMEIP_TUNNEL_EXAMPLES/tunnel/tunnel"
EXTRA_BINS=()
if [[ -x "$TUNNEL_BIN" ]]; then
    EXAMPLE_BINS=("$TUNNEL_BIN" "${EXAMPLE_BINS[@]}")
else
    echo "Warning: $TUNNEL_BIN not found or not executable" >&2
fi
for APP in poc_rain_sensor/rain_sensor poc_window_position_controller/window_position_controller; do
    APP_PATH="$SOMEIP_TUNNEL_EXAMPLES/$APP"
    if [[ -x "$APP_PATH" ]]; then
        EXTRA_BINS+=("$APP_PATH")
    else
        echo "Warning: $APP_PATH not found or not executable" >&2
    fi
done
# Remove duplicates (in case tunnel/tunnel was already in EXAMPLE_BINS)
declare -A SEEN_BINS
FINAL_BINS=()
for BIN in "${EXAMPLE_BINS[@]}" "${EXTRA_BINS[@]}"; do
    if [[ -x "$BIN" && -z "${SEEN_BINS[$BIN]}" ]]; then
        FINAL_BINS+=("$BIN")
        SEEN_BINS[$BIN]=1
    fi
done
EXAMPLE_BINS=("${FINAL_BINS[@]}")

if [[ ${#EXAMPLE_BINS[@]} -eq 0 ]]; then
    echo "No example binaries found for .rs files in $EXAMPLES_DIR or in $SOMEIP_TUNNEL_EXAMPLES" >&2
    exit 1
fi


if [[ $USE_TMUX -eq 1 ]]; then
    if ! command -v tmux >/dev/null 2>&1; then
        echo "Error: tmux is not installed." >&2
        echo "Install it on Ubuntu with: sudo apt update && sudo apt install tmux" >&2
        exit 2
    fi
    tmux kill-session -t "$SESSION" 2>/dev/null || true
    # Create a single session and window, then split panes for each unique example
    tmux kill-session -t "$SESSION" 2>/dev/null || true
    tmux -f scripts/.tmux new-session -d -s "$SESSION" -n exall "$SHELL"
    tmux set-option -t "$SESSION" mouse on
    # Bind Ctrl+L to kill the session
    tmux set-option -t "$SESSION" key-table root
    tmux bind-key -n C-l kill-session
    # Start tunnel first (if present)
    TUNNEL_BIN="$SOMEIP_TUNNEL_DIR/build/examples/tunnel/tunnel"
    pane_idx=0
    if [[ -x "$TUNNEL_BIN" ]]; then
        TUNNEL_CMD="clear; set +x; echo 'Running tunnel'; echo 'To close all: press Ctrl+L'; $TUNNEL_BIN"
        tmux send-keys -t "$SESSION:exall.0" "$TUNNEL_CMD" C-m
        # sleep 1
        pane_idx=1
    fi

    # Start gateway next
    GATEWAY_BIN="$(dirname "$0")/../target/debug/gateway"
    if [[ -x "$GATEWAY_BIN" ]]; then
        if [[ $pane_idx -eq 0 ]]; then
            tmux send-keys -t "$SESSION:exall.0" "clear; set +x; echo 'Running gateway'; echo 'To close all: press Ctrl+L'; $GATEWAY_BIN" C-m
        else
            tmux split-window -t "$SESSION:exall" -h "$SHELL"
            tmux send-keys -t "$SESSION:exall.$pane_idx" "clear; set +x; echo 'Running gateway'; echo 'To close all: press Ctrl+L'; $GATEWAY_BIN" C-m
        fi
        # sleep 1
        ((pane_idx++))
    else
        echo "Warning: gateway binary not found or not executable at $GATEWAY_BIN" >&2
    fi

    # sleep 2

    # Start the rest of the example bins, skipping tunnel and gateway if present
    for BIN in "${EXAMPLE_BINS[@]}"; do
        BIN_BASENAME=$(basename "$BIN")
        if [[ "$BIN" == "$TUNNEL_BIN" || "$BIN" == "$GATEWAY_BIN" ]]; then
            continue
        fi
        tmux split-window -t "$SESSION:exall" -h "$SHELL"
        CMD="clear; set +x; echo 'Running $BIN_BASENAME'; echo 'To close all: press Ctrl+L'; $BIN"
        tmux send-keys -t "$SESSION:exall.$pane_idx" "$CMD" C-m
        tmux select-layout -t "$SESSION:exall" tiled
        ((pane_idx++))
    done
    tmux select-pane -t "$SESSION:exall.0"
    echo "Attaching to tmux session: $SESSION (press Ctrl+L to close)"
    tmux attach -t "$SESSION"
else
    for BIN in "${EXAMPLE_BINS[@]}"; do
        "$BIN" &
    done
fi
