# `make release`: the Linux and Windows release archives in build/dist/ (scripts/release.sh; docs/development.md,
# "Releases"). On the host it runs in the dev container (run-dev-env.sh), which has the toolchains; in it, directly.
.PHONY: release
release:
	@if [ -d /opt/llvm-mingw ]; then ./scripts/release.sh; else ./run-dev-env.sh release; fi
