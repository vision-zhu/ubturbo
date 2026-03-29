#!/bin/bash
# Fast incremental DT runner for smap.
#
# First run (or -f): runs source prep from run_dt.sh, then builds and tests.
# Subsequent runs: skips source prep, only rebuilds changed files.
#
# Usage:
#   ./run_dt_fast.sh              # incremental build + run all tests
#   ./run_dt_fast.sh -f           # force full rebuild (re-copy src, clean build)
#   ./run_dt_fast.sh -t TestName  # run only tests matching a filter (substring)
#   ./run_dt_fast.sh -c           # also generate coverage report

set -euo pipefail

CURRENT_PATH=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
cd "${CURRENT_PATH}"

FORCE_REBUILD=false
TEST_FILTER=''
COVERAGE=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        -f|--force)    FORCE_REBUILD=true; shift ;;
        -t|--test)     TEST_FILTER="$2"; shift 2 ;;
        -c|--coverage) COVERAGE=true; shift ;;
        *) echo "Usage: $0 [-f] [-t TestFilter] [-c]"; exit 1 ;;
    esac
done

BUILD_DIR="$CURRENT_PATH/build"
DT_SRC_DIR="$CURRENT_PATH/dt_src"

N_CPUS=$(grep processor /proc/cpuinfo | wc -l)
JOBS=$(( N_CPUS > 2 ? N_CPUS - 2 : 1 ))

# ── Step 1: Source preparation ────────────────────────────────────────────────
# Delegate to run_dt.sh but only the source-prep and cmake/build phases.
# We patch out the test run and coverage steps by stopping at the make invocation.

if [[ "$FORCE_REBUILD" == true || ! -d "$DT_SRC_DIR/src" ]]; then
    echo "[fast-dt] Running source preparation (first time or forced)..."

    # Extract and run only the source-prep portion of run_dt.sh.
    # We source the helper functions and run everything up to the build step,
    # but with the make -j 0 bug fixed.

    # Source just the helper function definitions (lines 9-85)
    source <(sed -n '9,85p' "$CURRENT_PATH/run_dt.sh")

    # Re-run src prep steps (lines 91-222 of run_dt.sh)
    DT_SRC_DIR_LOCAL=$CURRENT_PATH/dt_src
    if [[ -d "$DT_SRC_DIR_LOCAL" ]]; then
        rm -rf "${DT_SRC_DIR_LOCAL:?}"/*
    else
        mkdir -p "$DT_SRC_DIR_LOCAL"
    fi
    cp -r "${CURRENT_PATH}/../src" "$DT_SRC_DIR_LOCAL"
    code_dir=$(cd "$DT_SRC_DIR_LOCAL" && pwd)

    rm -f "${CURRENT_PATH}/tiering/test_iomem.cpp"

    # Run all src prep from run_dt.sh lines 102-220
    eval "$(sed -n '102,220p' "$CURRENT_PATH/run_dt.sh")"

    echo "[fast-dt] Source preparation complete."
else
    echo "[fast-dt] dt_src exists — skipping src copy (use -f to force)."
fi

# ── Step 2: mockcpp patch (once) ─────────────────────────────────────────────

mock_patch_path=$CURRENT_PATH/3rdparty/mockcpp/mockcpp_support_arm64.patch
if [[ ! -e "$mock_patch_path" ]]; then
    dos2unix $CURRENT_PATH/3rdparty/mockcpp_support_arm64.patch
    cp -r $CURRENT_PATH/3rdparty/mockcpp_support_arm64.patch $CURRENT_PATH/3rdparty/mockcpp
    pushd $CURRENT_PATH/3rdparty/mockcpp > /dev/null
    dos2unix src/UnixCodeModifier.cpp
    git apply mockcpp_support_arm64.patch
    popd > /dev/null
fi

# ── Step 3: cmake configure (only when Makefile missing or forced) ────────────

mkdir -p "$BUILD_DIR"

if [[ "$FORCE_REBUILD" == true ]]; then
    echo "[fast-dt] Cleaning build directory..."
    rm -rf "${BUILD_DIR:?}"/*
fi

cd "$BUILD_DIR"

if [[ ! -f "$BUILD_DIR/Makefile" ]]; then
    echo "[fast-dt] Configuring cmake..."
    cmake -DCMAKE_BUILD_TYPE=Debug "$CURRENT_PATH" || {
        echo "[fast-dt] cmake configure failed!"
        exit 1
    }
fi

# ── Step 4: Build ─────────────────────────────────────────────────────────────

echo "[fast-dt] Building with $JOBS jobs..."
make -j${JOBS} || {
    echo "[fast-dt] Build failed!"
    exit 1
}

# ── Step 5: Run tests ─────────────────────────────────────────────────────────

if [[ -n "$TEST_FILTER" ]]; then
    echo "[fast-dt] Running tests matching: *${TEST_FILTER}*"
    ./smap_dt --gtest_filter="*${TEST_FILTER}*"
else
    echo "[fast-dt] Running all tests..."
    ./smap_dt
fi

# ── Step 6: Coverage (optional) ───────────────────────────────────────────────

if [[ "$COVERAGE" == true ]]; then
    echo "[fast-dt] Generating coverage report..."
    mkdir -p gcovr_report
    lcov --d ./ --c --output-file test.info --rc lcov_branch_coverage=1
    lcov -e test.info '*/dt_src/src/*' --output-file coverage.info --rc lcov_branch_coverage=1
    genhtml -o gcovr_report coverage.info --show-details --legend --rc lcov_branch_coverage=1
    echo "[fast-dt] Coverage: ${BUILD_DIR}/gcovr_report/index.html"
fi

echo "[fast-dt] Done."
