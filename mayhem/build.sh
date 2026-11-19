#!/usr/bin/env bash
#
# mayhem/build.sh — build this repo's fuzz harness(es). EDIT per repo.
#
# Runs inside the commit image (mayhem/Dockerfile) as `mayhem` in /mayhem. The base image
# (ghcr.io/savantenvs/base) already exports the build contract — use these, don't redefine:
#   CC, CXX             stock clang / clang++
#   LIB_FUZZING_ENGINE  -fsanitize=fuzzer   (link into each harness that has a LLVMFuzzer entry)
#   SANITIZER_FLAGS     -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
#                       (ASan + UBSan, both set to HALT — so Mayhem catches memory AND UB defects)
#   DEBUG_FLAGS         -g -gdwarf-3   (DWARF debug info — always on for fuzz/standalone builds,
#                       independent of the sanitizer off-switch; DWARF version must be < 4)
#   RUST_DEBUG_FLAGS    -C debuginfo=2 -C force-frame-pointers=yes -C llvm-args=-gdwarf-3
#                       (thread through RUSTFLAGS on every cargo-fuzz build)
#   GO_DEBUG_FLAGS      -gcflags=all=-N -l
#                       (thread through go build / go-fuzz-build so the linked ELF keeps symbols)
#   SRC                 /mayhem (the repo source)
#
# Contract: build EVERYTHING here — one runnable binary per fuzz harness, AND the project's test
# suite (so mayhem/test.sh only has to RUN it, never compile). Keep it ADDITIVE (build upstream as
# upstream documents; don't edit upstream files). IMPORTANT: build the PROJECT ITSELF with
# $SANITIZER_FLAGS and $DEBUG_FLAGS (not just the harness) so the fuzzed code is instrumented
# AND carries DWARF < 4 symbols — otherwise ASan/UBSan only see the harness, not the library
# you're trying to find bugs in, and backtraces won't resolve project source lines. Build the TEST
# suite with the project's NORMAL flags (a clean, independent build) so test.sh stays an honest
# functional oracle and won't false-fail on benign UB. Leave the test binary/runner where test.sh
# expects it.
set -euo pipefail

# clang rejects SOURCE_DATE_EPOCH='' (empty) — it must be unset or a valid integer.
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

# Build knobs come from the ENVIRONMENT (overridable), with sane defaults — no if-statements,
# just parameter-expansion fallbacks. Default sanitizers are the base's ASan+UBSan (halting); override
# per build via the Dockerfile's `--build-arg SANITIZER_FLAGS="..."`.
# NB: SANITIZER_FLAGS uses `=` (no colon) on purpose — `=` only fills when the var is UNSET, so an
# explicit EMPTY value (`--build-arg SANITIZER_FLAGS=`) is honored and builds with NO sanitizers
# (useful when you want the program's natural crash / full backtrace, not an ASan report). The other
# knobs use `:=` (default on empty too). MAYHEM_JOBS sets build parallelism (falls back to nproc).
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
# DEBUG_FLAGS carries DWARF debug info INDEPENDENTLY of the sanitizer off-switch (so an empty
# SANITIZER_FLAGS still yields DWARF symbols). DWARF MUST be < 4 (Mayhem triage can't read >=4); clang-19's
# plain `-g` emits DWARF-5, so `-gdwarf-3` is explicit. Apply $DEBUG_FLAGS to the fuzz/harness/standalone
# builds (NOT the test/oracle build). Rust/Go carry the same intent via their language flags below.
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${RUST_DEBUG_FLAGS:=-C debuginfo=2 -C force-frame-pointers=yes -C llvm-args=-gdwarf-3}"
: "${GO_DEBUG_FLAGS:=-gcflags=all=-N -l}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
# COVERAGE_FLAGS: empty by default → no effect on the normal oracle build. Set it via the Dockerfile's
# `--build-arg COVERAGE_FLAGS="-fprofile-instr-generate -fcoverage-mapping"` to instrument the TEST
# build for source-coverage measurement (how much of the project the test suite actually exercises —
# a quality signal for the oracle; complements the anti-reward-hack sabotage check). APPEND it to the
# test build's compile+link flags in step 3 (NOT the fuzz build); after `test.sh` runs, merge with
# `llvm-profdata` and report with `llvm-cov`. Empty value is honored (`=`, not `:=`).
: "${COVERAGE_FLAGS=}"
export SANITIZER_FLAGS DEBUG_FLAGS RUST_DEBUG_FLAGS GO_DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"

# Speeduino is Arduino firmware; upstream builds and unit-tests it on the host through PlatformIO's
# `native` env (platformio.ini [env:native_base]: NATIVE_BOARD + board_native.h + ArduinoNativeFake).
# PlatformIO itself needs the network, so this script compiles the same sources with the same defines
# directly, against the libraries the Dockerfile pinned under /opt/toolchains/deps.
DEPS="${SPEEDUINO_DEPS:-/opt/toolchains/deps}"
SAF="$DEPS/simplearduinofake-ed7ac17/src/src"
ANF="$DEPS/arduinonativefake-d5c808a/src/src"
UNITY="$DEPS/unity-2.6.1/src/src"
OUT="$SRC/mayhem/build"          # gitignored (mayhem/.gitignore)
mkdir -p "$OUT"

DEFS=(-DUSE_LIBDIVIDE -DNATIVE_BOARD '-DEXTERNAL_BOARD_H="board_native.h"' -DARDUINO=101 -DUSBCON)
INCS=(-I"$SRC/speeduino" -I"$SRC/lib/ArduinoFake" -I"$SAF" -I"$SAF/arduino" -I"$ANF"
      -I"$DEPS/fakeit-2.4.0/src" -I"$DEPS/fastcrc-1.45/src/src" -I"$DEPS/libdivide-5.4.0/src"
      -I"$DEPS/simplyatomic-1.0/src" -I"$DEPS/avr-fast-shift-1.1.0/src/src"
      -I"$DEPS/avr-fast-div-1.4.0/src/src" -I"$UNITY")
CXXSTD=(-std=c++17 -fshort-enums -fexceptions -w -Wno-error=missing-template-arg-list-after-template-kw)

# Firmware sources (everything upstream's native env compiles: speeduino/, speeduino/src/**, the
# in-repo Mega2560 fake) and the fake-Arduino layer they link against.
mapfile -t FW_SRCS < <(cd "$SRC" && { ls speeduino/*.cpp; find speeduino/src -name '*.cpp'; ls lib/ArduinoFake/*.cpp; } | LC_ALL=C sort)
mapfile -t DEP_SRCS < <({ ls "$SAF"/*.cpp "$SAF"/arduino/*.cpp "$SAF"/arduino/*.c "$ANF"/*.cpp \
                            "$DEPS"/fastcrc-1.45/src/src/*.cpp "$DEPS"/avr-fast-div-1.4.0/src/src/*.cpp; } 2>/dev/null | LC_ALL=C sort)

# compile_objs <dir> <flags...> < list-of-sources: compile each source into <dir>/obj
compile_objs() {
  local dir="$1"; shift
  mkdir -p "$dir/obj"
  xargs -P "$MAYHEM_JOBS" -I{} bash -c '
      f="$1"; shift; o="'"$dir"'/obj/$(echo "$f" | sed "s|/|_|g").o"
      case "$f" in *.c) cc="$CC"; std=();; *) cc="$CXX"; std=(-std=c++17 -fexceptions -Wno-error=missing-template-arg-list-after-template-kw);; esac
      # updates.cpp static_asserts on .size() of a reference parameter, which g++ (upstream'"'"'s
      # compiler) folds but clang rejects as non-constant; drop that compile-time-only check there.
      case "$f" in */speeduino/updates.cpp) std+=("-Dstatic_assert(...)=static_assert(true, \"\")");; esac
      "$cc" "${std[@]}" "$@" -c "$f" -o "$o"' _ {} "$@"
}
# make_lib <name>: archive $OUT/<name>/obj into $OUT/<name>/lib.a
make_lib() { rm -f "$OUT/$1/lib.a"; ar rcs "$OUT/$1/lib.a" "$OUT/$1"/obj/*.o; }

# Third-party libraries (fake Arduino layer, FastCRC, avr-fast-div) stay uninstrumented (SPEC §6.2
# item 19). This also keeps ASan out of the fake layer's pgm_read_dword(), which reads an 8-byte
# `unsigned long` on x86-64 and so over-reads the last entry of every 4-byte PROGMEM table.
DEP_FLAGS=(-O1 $DEBUG_FLAGS "${DEFS[@]}" "${INCS[@]}" -fshort-enums -w)

# 1) Firmware + fake layer, sanitized (the fuzzed code is instrumented, DWARF 3).
# -fno-sanitize=enum: the firmware loads its config bitfields (load sources, algorithms) straight from
# the stored tune bytes, so UBSan's invalid-enum-load check fires on every non-pristine image; that
# check alone is relaxed, the rest of UBSan and ASan stay on and halting.
FUZZ_FLAGS=($SANITIZER_FLAGS $DEBUG_FLAGS -fno-sanitize=enum -O1 -fsanitize=fuzzer-no-link "${DEFS[@]}" "${INCS[@]}" -fshort-enums -w)
# -fsanitize=fuzzer-no-link: SanCov edges on the firmware itself, not only the harness.
rm -rf "$OUT/fuzz/obj"
# comms.cpp and comms_legacy.cpp are compiled through mayhem/harnesses/comms{,_legacy}_fuzz.cpp, which
# #include them unchanged and add a per-input reset for their file-static protocol state.
mapfile -t FUZZ_FW_SRCS < <(printf '%s\n' "${FW_SRCS[@]}" | grep -vxE 'speeduino/comms(_legacy)?\.cpp')
FUZZ_FW_SRCS+=(mayhem/harnesses/comms_fuzz.cpp mayhem/harnesses/comms_legacy_fuzz.cpp)
printf '%s\n' "${FUZZ_FW_SRCS[@]/#/$SRC/}" | compile_objs "$OUT/fuzz" "${FUZZ_FLAGS[@]}"
printf '%s\n' "${DEP_SRCS[@]}"        | compile_objs "$OUT/fuzz" "${DEP_FLAGS[@]}"
make_lib fuzz

# 2) Harnesses: libFuzzer binary + run-once standalone reproducer each.
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$OUT/lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$OUT/standalone_main.o"
for h in fuzz_serial fuzz_secondary_serial fuzz_eeprom_load; do
  $CXX "${CXXSTD[@]}" "${FUZZ_FLAGS[@]}" -c "$SRC/mayhem/harnesses/$h.cpp" -o "$OUT/$h.o"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$OUT/$h.o" "$OUT/lsan_off.o" \
       "$OUT/fuzz/lib.a" -lpthread -o "$SRC/$h"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS "$OUT/$h.o" "$OUT/standalone_main.o" "$OUT/lsan_off.o" \
       "$OUT/fuzz/lib.a" -lpthread -o "$SRC/$h-standalone"
done

# 3) Upstream's own Unity suites (test/test_*), NORMAL flags, one runner per suite; test.sh runs them.
rm -rf "$OUT/test/obj"
printf '%s\n' "${FW_SRCS[@]/#/$SRC/}" "${DEP_SRCS[@]}" | \
  compile_objs "$OUT/test" -O1 -DUNIT_TEST $COVERAGE_FLAGS "${DEFS[@]}" "${INCS[@]}" -fshort-enums -w -DUNITY_INCLUDE_SETUP_STUBS
make_lib test
$CC -O1 $COVERAGE_FLAGS -DUNITY_INCLUDE_SETUP_STUBS -c "$UNITY/unity.c" -o "$OUT/test/unity.o"
mkdir -p "$OUT/tests"
for t in ${SPEEDUINO_TESTS:-test_pages test_storage test_composite_log test_tables test_math test_secondary test_fuel test_ign test_engineprotect test_utils}; do
  mapfile -t tsrcs < <(ls "$SRC/test/$t"/*.cpp; ls "$SRC"/test/*.cpp)
  $CXX "${CXXSTD[@]}" -O1 -DUNIT_TEST $COVERAGE_FLAGS "${DEFS[@]}" "${INCS[@]}" -I"$SRC/test" -DUNITY_INCLUDE_SETUP_STUBS \
       "${tsrcs[@]}" "$OUT/test/unity.o" "$OUT/test/lib.a" -lpthread -o "$OUT/tests/$t"
done
