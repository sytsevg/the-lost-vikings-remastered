#!/bin/sh
set -eu

# The dev container (dev-env/Dockerfile), with this folder mounted in it.
#   ./run-dev-env.sh           Claude Code in the container (the same as `claude`)
#   ./run-dev-env.sh build     build the game: build/release/bin/vikings-remaster
#   ./run-dev-env.sh test      build, then every check: lint, unit tests, coverage, and the replays
#   ./run-dev-env.sh claude    Claude Code in the container
# UPDATE=1 rebuilds only the Claude Code layer if a newer version exists; REBUILD=1 rebuilds the whole image.

MODE="${1:-claude}"
case "$MODE" in
  build|test|claude) ;;
  *) echo "usage: $0 [build|test|claude]" >&2; exit 1 ;;
esac

IMAGE_NAME="claude-code"
BUILD_DIR="$PWD/dev-env"

if ! command -v podman >/dev/null 2>&1; then
  echo "Error: podman is not installed." >&2
  exit 1
fi

build() {
  [ -d "$BUILD_DIR" ] || { echo "Error: Build directory '$BUILD_DIR' not found." >&2; exit 1; }
  echo "Building $IMAGE_NAME from $BUILD_DIR..."
  podman build "$@" -t "$IMAGE_NAME" "$BUILD_DIR"
}

if [ "${REBUILD:-0}" = "1" ]; then
  # Full rebuild: newer base image, fresh apt/LLVM layers.
  build --pull=newer --no-cache --build-arg CLAUDE_CODE_VERSION=latest
elif ! podman image exists "$IMAGE_NAME"; then
  build --build-arg CLAUDE_CODE_VERSION=latest
elif [ "${UPDATE:-0}" = "1" ]; then
  # Only the claude-code layer: resolve the latest version with the image's own npm
  # (no node needed on the host) and rebuild only if it differs from what's installed.
  LATEST=$(podman run --rm --entrypoint npm "$IMAGE_NAME" view @anthropic-ai/claude-code version)
  CURRENT=$(podman run --rm --entrypoint claude "$IMAGE_NAME" --version | awk '{print $1}')
  if [ "$LATEST" != "$CURRENT" ]; then
    echo "Updating claude-code $CURRENT -> $LATEST"
    build --build-arg CLAUDE_CODE_VERSION="$LATEST"
  else
    echo "claude-code $CURRENT is already the latest."
  fi
fi

# The game and the checks need no Claude Code: a shell in the same image runs them and leaves.
run_in_container() {
  TTY=""
  [ -t 0 ] && TTY="-it"
  exec podman run $TTY --rm \
    --userns=keep-id:uid=1000,gid=1000 \
    -e HOME=/home/node \
    -v "$PWD":/workspace \
    --entrypoint bash \
    "$IMAGE_NAME" -c "$1"
}

BUILD_GAME="cmake --preset release && cmake --build --preset release"
case "$MODE" in
  build) run_in_container "$BUILD_GAME && echo && echo 'Built: build/release/bin/vikings-remaster (run it on the host)'" ;;
  test) run_in_container "$BUILD_GAME && ./scripts/check.sh && ./scripts/goldens.sh" ;;
esac

mkdir -p "$PWD/.claude"
touch "$PWD/.claude.json"

# Optional: long-lived token from `claude setup-token`; otherwise /login once (persists in .claude/)
TOKEN_ARGS=""
[ -n "${CLAUDE_CODE_OAUTH_TOKEN:-}" ] && TOKEN_ARGS="-e CLAUDE_CODE_OAUTH_TOKEN"

# No display, audio or GPU passthrough: everything in the container runs headless (tests, DOSBox-X with SDL's
# dummy drivers); the game itself is run on the host.
exec podman run -it --rm \
  --userns=keep-id:uid=1000,gid=1000 \
  -e HOME=/home/node \
  -e CLAUDE_CONFIG_DIR=/home/node/.claude \
  $TOKEN_ARGS \
  -v "$PWD":/workspace \
  -v "$PWD/.claude":/home/node/.claude \
  "$IMAGE_NAME" --permission-mode auto
