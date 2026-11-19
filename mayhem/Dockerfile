# syntax=docker/dockerfile:1.7
#
# Commit-image Dockerfile (SMALL form, PREFERRED) — builds ON TOP of the org C/C++ base
# image (ghcr.io/savantenvs/base). The base already provides debian + clang/llvm +
# libFuzzer/afl++ + the identities + the build-contract ENV, so this only adds the
# per-commit build (built on top of the prebuilt base, rather than building the toolchain in-image).
#
# Build (from the repo root):
#   DOCKER_BUILDKIT=1 docker build -f mayhem/Dockerfile \
#       --build-arg COMMIT=$(git rev-parse HEAD) \
#       [--build-arg SOURCE_DATE_EPOCH=$(date +%s)] \
#       -t mytag .

# The org base, pinned to a fixed DATE-tagged base (YYYYMMDD; shared by all integrations
# minted against it). Use the current dated tag; pin by digest in production: ...base@sha256:<digest>.
FROM ghcr.io/savantenvs/base:20261023

ARG COMMIT
ARG REPO_URL
# SOURCE_DATE_EPOCH stays an ARG (available to build.sh); do NOT bake it as an ENV — an
# empty value breaks clang ("must be a non-negative decimal integer") both here and for
# any in-image rebuild. build.sh unsets it when empty.
ARG SOURCE_DATE_EPOCH

# Sanitizer override: the base exports SANITIZER_FLAGS (ASan+UBSan, halting) as the default.
# To change it for a build (e.g. ASan-only, or add MSan): --build-arg SANITIZER_FLAGS="...".
# Declare it WITHOUT a default (no '='): an undefaulted ARG lets the base ENV flow through to
# build.sh when not passed, and an explicit --build-arg overrides it. (An empty default — `=` —
# would instead shadow the base ENV and silently wipe the sanitizers.)
ARG SANITIZER_FLAGS
# Coverage (optional, off by default): instrument the TEST build to measure how much of the project
# the suite exercises — --build-arg COVERAGE_FLAGS="-fprofile-instr-generate -fcoverage-mapping".
# Empty by default (no effect on the normal oracle build); build.sh appends it to the test build only.
ARG COVERAGE_FLAGS=
# Build parallelism (optional). build.sh falls back to nproc when unset: --build-arg MAYHEM_JOBS=8
ARG MAYHEM_JOBS=

# Speeduino's native (host) build is PlatformIO-driven and pulls its libraries from the PIO registry
# and GitHub at build time. Fetch each one ONCE here, pinned by commit SHA, into
# /opt/toolchains/deps/<name>-<version>/src (SPEC §6.2 item 19); build.sh compiles them from there
# offline. They are the upstream `[common] lib_deps` + `[env:native_base]` lib_deps of platformio.ini,
# plus ArduinoNativeFake's transitive SimpleArduinoFake -> FakeIt 2.4.0, and Unity for the test suite.
USER root
RUN set -eux; \
    fetch() { d="/opt/toolchains/deps/$1/src"; mkdir -p "$d"; cd "$d"; git init -q; \
      git fetch -q --depth 1 "https://github.com/$2" "$3"; git checkout -q FETCH_HEAD; \
      test "$(git rev-parse HEAD)" = "$3"; rm -rf .git; }; \
    fetch simplyatomic-1.0         wizard97/SimplyAtomic            e5113f65f4212b271eff58a6fba9659dea8b8534; \
    fetch avr-fast-shift-1.1.0     adbancroft/avr-fast-shift        a8b9f7db2358c47b21e93a6884266911f60d490e; \
    fetch libdivide-5.4.0          ridiculousfish/libdivide         ea0fb1ad79a116cd561595c930bcaf33147b3e6c; \
    fetch fastcrc-1.45             FrankBoesing/FastCRC             c669a8915dcec9d16fa60ee7772b7b2200f0cdc5; \
    fetch avr-fast-div-1.4.0       adbancroft/avr-fast-div          8e6a3e457372a19a53fa143911edc83f51add4cf; \
    fetch arduinonativefake-d5c808a adbancroft/ArduinoNativeFake    d5c808ad4f718840b1a7baae0b6c363c23378a6f; \
    fetch simplearduinofake-ed7ac17 adbancroft/SimpleArduinoFake    ed7ac177adf00c04900efd459fe64c4de82666ba; \
    fetch fakeit-2.4.0             eranpeer/FakeIt                  cb39d8a053876f74dfeed66dd335d3041f142095; \
    fetch unity-2.6.1              ThrowTheSwitch/Unity             cbcd08fa7de711053a3deec6339ee89cad5d2697; \
    chmod -R a+rX,go-w /opt/toolchains/deps
USER mayhem

# Source enters from the build CONTEXT (the CI checkout): the FULL repo (incl. .git
# history) is copied to /mayhem as `mayhem`; build.sh builds in place.
USER mayhem
WORKDIR /mayhem
COPY --chown=2000:2000 . /mayhem

# Anything a Dockerfile step materializes into the tree (downloaded fixtures, generated headers,
# vendored deps) MUST be committed or (re)created by build.sh: rlenv's patch grader runs
# `git clean -ffdX` before every graded build, so a gitignored, untracked file is gone by then and
# every honest patch scores zero (SPEC §6.2 item 16; verify-repo.sh checks it).
#
# OPTIONAL compile-cache pre-fill (SPEC §6.2 item 18). Bases minted with /opt/mayhem/ccache/prefill
# run the build under rlenv's pinned ccache, so the patch grader's first (otherwise cold) rebuild is
# warm — this is what keeps large C/C++ targets inside the build time limit. Older bases fall back to
# the plain build; a missing or stale pre-fill only costs a cold cache. Never enable ccache in
# build.sh or via ENV/PATH, and keep the compiler resolving by name (CC=clang) or nothing is cached.
RUN if [ -x /opt/mayhem/ccache/prefill ]; then /opt/mayhem/ccache/prefill bash mayhem/build.sh; \
    else bash mayhem/build.sh; fi

# Run the project's functional tests at build time IF the repo ships them (mayhem/test.sh is
# optional — omit it only when the repo has no tests meaningful for RL). A failing suite fails the
# image build, so the commit image is guaranteed to build AND pass its own tests.
RUN if [ -f mayhem/test.sh ]; then bash mayhem/test.sh; else echo "no mayhem/test.sh — repo ships no functional tests"; fi

LABEL org.opencontainers.image.source="${REPO_URL}" \
      org.opencontainers.image.revision="${COMMIT}" \
      rlenv.tier="commit" \
      rlenv.source_date_epoch="${SOURCE_DATE_EPOCH}" \
      rlenv.user="mayhem"

USER mayhem
WORKDIR /mayhem
