#!/usr/bin/env bash
set -uo pipefail

# ANSI color codes for visual clarity
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MY_LS="$PROJECT_ROOT/my_ls"

if [[ ! -x "$MY_LS" ]]; then
    echo -e "${RED}Error: $MY_LS not found or not executable. Run 'make' first.${NC}"
    exit 1
fi

TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# Create isolated test environment and isolated output directory
SANDBOX=$(mktemp -d /tmp/ls_test_sandbox_XXXXXX)
OUT_DIR=$(mktemp -d /tmp/ls_test_out_XXXXXX)

cleanup() {
    rm -rf "$SANDBOX" "$OUT_DIR"
}
trap cleanup EXIT INT TERM

# Populate sandbox with test fixtures
setup_fixtures() {
    cd "$SANDBOX" || exit 1

    # Standard files of various sizes
    echo "Short text" > file_small.txt
    python3 -c "import sys; sys.stdout.write('A' * 4096)" > file_medium.txt
    python3 -c "import sys; sys.stdout.write('B' * 65536)" > file_large.txt
    touch file_empty.txt

    # Hidden files and directory
    echo "Hidden content" > .hidden_file
    mkdir .hidden_dir
    echo "Sub content" > .hidden_dir/inner.txt

    # Subdirectories
    mkdir -p dir_alpha/subdir
    echo "Alpha 1" > dir_alpha/a1.txt
    echo "Alpha 2" > dir_alpha/a2.txt
    echo "Nested" > dir_alpha/subdir/nested.txt

    mkdir dir_beta
    echo "Beta 1" > dir_beta/b1.txt
    mkdir dir_empty

    # Executable file
    echo '#!/bin/sh' > script_exec.sh
    chmod +x script_exec.sh

    # Symbolic links
    ln -s file_small.txt link_to_file
    ln -s dir_alpha link_to_dir
    ln -s nonexistent_target broken_link

    # FIFO pipe if mkfifo is supported
    if command -v mkfifo >/dev/null 2>&1; then
        mkfifo fifo_pipe 2>/dev/null || true
    fi

    # Distinct timestamps
    touch -t 202201011200 file_old.txt
    touch -t 202401011200 file_recent.txt

    cd "$PROJECT_ROOT" || exit 1
}

run_test() {
    local test_name="$1"
    local flags="$2"
    local target="$3"
    local env_prefix="${4:-POSIXLY_CORRECT=1}"

    TOTAL_TESTS=$((TOTAL_TESTS + 1))

    local out_my="$OUT_DIR/out_my_${TOTAL_TESTS}.txt"
    local out_sys="$OUT_DIR/out_sys_${TOTAL_TESTS}.txt"
    local err_my="$OUT_DIR/err_my_${TOTAL_TESTS}.txt"
    local err_sys="$OUT_DIR/err_sys_${TOTAL_TESTS}.txt"

    # Run my_ls inside sandbox
    (cd "$SANDBOX" && "$MY_LS" $flags $target > "$out_my" 2> "$err_my")

    # Run system ls inside sandbox
    (cd "$SANDBOX" && env $env_prefix ls $flags $target > "$out_sys" 2> "$err_sys")

    if diff -u "$out_my" "$out_sys" >/dev/null 2>&1; then
        echo -e "  [${GREEN}PASS${NC}] Test ${TOTAL_TESTS}: ${test_name}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        echo -e "  [${RED}FAIL${NC}] Test ${TOTAL_TESTS}: ${test_name}"
        echo "      --- Difference ---"
        diff -u "$out_my" "$out_sys" | head -n 15
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
}

run_error_test() {
    local test_name="$1"
    local flags="$2"
    local target="$3"

    TOTAL_TESTS=$((TOTAL_TESTS + 1))

    local out_my="$OUT_DIR/out_my_${TOTAL_TESTS}.txt"
    local err_my="$OUT_DIR/err_my_${TOTAL_TESTS}.txt"

    (cd "$SANDBOX" && "$MY_LS" $flags $target > "$out_my" 2> "$err_my")
    local status_my=$?

    if [[ $status_my -ne 0 && -s "$err_my" ]]; then
        echo -e "  [${GREEN}PASS${NC}] Test ${TOTAL_TESTS}: ${test_name} (exit status: ${status_my})"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        echo -e "  [${RED}FAIL${NC}] Test ${TOTAL_TESTS}: ${test_name} (expected exit > 0 and stderr output)"
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
}

echo -e "${BLUE}======================================================${NC}"
echo -e "${BLUE}  AUTOMATED TEST SUITE: my_ls vs System ls (NetBSD/POSIX)${NC}"
echo -e "${BLUE}======================================================${NC}"

setup_fixtures

echo -e "\n${YELLOW}--- 1. Basic Listing and Entry Filtering (-a, -A) ---${NC}"
run_test "Default listing (no flags)" "" ""
run_test "All entries (-a)" "-a" ""
run_test "Almost all entries (-A)" "-A" ""

echo -e "\n${YELLOW}--- 2. Formatting Options (-l, -n, -F, -d) ---${NC}"
run_test "Long format (-l)" "-l" ""
run_test "Numeric IDs long format (-n)" "-n" ""
run_test "Classify indicators (-F)" "-F" ""
run_test "Directory as plain file (-d)" "-d" "."
run_test "Long format with dir as plain file (-ld)" "-ld" "dir_alpha"
run_test "Long format on empty directory (-l)" "-l" "dir_empty"
run_test "Block counts on empty directory (-s)" "-s" "dir_empty"

echo -e "\n${YELLOW}--- 3. Sorting Algorithms (-S, -t, -r, -f) ---${NC}"
run_test "Sort by size descending (-S)" "-S" ""
run_test "Sort by time descending (-t)" "-t" ""
run_test "Reverse lexicographical sort (-r)" "-r" ""
run_test "Reverse sort by size (-rS)" "-rS" ""
run_test "Reverse sort by time (-rt)" "-rt" ""
run_test "Long format sorted by size (-lS)" "-lS" ""
run_test "Long format sorted by time (-lt)" "-lt" ""

echo -e "\n${YELLOW}--- 4. Inode, Blocks and Size Formats (-i, -s, -k, -h) ---${NC}"
run_test "Inode display (-i)" "-i" ""
run_test "Block count display (-s)" "-s" ""
run_test "Kilobytes block count (-sk)" "-sk" ""
run_test "Human-readable sizes (-h)" "-lh" ""
run_test "Combined inode and blocks (-is)" "-is" ""
run_test "Combined inode and long format (-li)" "-li" ""

echo -e "\n${YELLOW}--- 5. Time Variants (-u, -c) ---${NC}"
run_test "Access time sorting (-tu)" "-tu" ""
run_test "Status change time sorting (-tc)" "-tc" ""
run_test "Access time long format (-lu)" "-lu" ""
run_test "Status change time long format (-lc)" "-lc" ""

echo -e "\n${YELLOW}--- 6. Recursive Traversal (-R) ---${NC}"
run_test "Recursive listing (-R)" "-R" ""
run_test "Recursive listing with classify (-RF)" "-RF" ""
run_test "Recursive listing with subfolder argument" "-R" "dir_alpha"

echo -e "\n${YELLOW}--- 7. Flag Override Rules ---${NC}"
run_test "Override -k then -h (rightmost wins)" "-kh" ""
run_test "Override -h then -k (rightmost wins)" "-hk" ""
run_test "Override -l then -n (numeric wins)" "-ln" ""
# NetBSD manual: "The -l and -n options override each other, the last one specified determines the format used."
TOTAL_TESTS=$((TOTAL_TESTS + 1))
out_nl="$OUT_DIR/out_nl.txt"
out_l="$OUT_DIR/out_l.txt"
(cd "$SANDBOX" && "$MY_LS" -nl > "$out_nl")
(cd "$SANDBOX" && "$MY_LS" -l > "$out_l")
if diff -u "$out_nl" "$out_l" >/dev/null 2>&1; then
    echo -e "  [${GREEN}PASS${NC}] Test ${TOTAL_TESTS}: Override -n then -l (symbolic wins, NetBSD spec)"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    echo -e "  [${RED}FAIL${NC}] Test ${TOTAL_TESTS}: Override -n then -l (expected -l to override -n)"
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi
run_test "Override -c then -u (atime wins)" "-tcu" ""
run_test "Override -u then -c (ctime wins)" "-tuc" ""
run_test "Override -R then -d (-d disables -R)" "-Rd" "."
run_test "Override -d then -R (-d disables -R)" "-dR" "dir_alpha"

echo -e "\n${YELLOW}--- 8. Multiple Operands ---${NC}"
run_test "Multiple file operands" "" "file_small.txt file_medium.txt"
run_test "File and directory operands" "" "file_small.txt dir_alpha"
run_test "Multiple directory operands" "" "dir_alpha dir_beta"
run_test "Multiple operands with -l" "-l" "file_small.txt dir_alpha"

echo -e "\n${YELLOW}--- 9. Error Handling and Edge Cases ---${NC}"
run_error_test "Non-existent file operand" "" "non_existent_file_12345.xyz"
run_error_test "Non-existent directory operand" "" "non_existent_dir_67890/"
run_error_test "Invalid command-line option (-Z)" "-Z" ""

echo -e "\n${BLUE}======================================================${NC}"
echo -e "${BLUE}  TEST SUMMARY${NC}"
echo -e "${BLUE}======================================================${NC}"
echo -e "  Total Tests  : ${TOTAL_TESTS}"
echo -e "  Passed Tests : ${GREEN}${PASSED_TESTS}${NC}"
echo -e "  Failed Tests : ${RED}${FAILED_TESTS}${NC}"

if [[ $FAILED_TESTS -eq 0 ]]; then
    echo -e "\n${GREEN}>>> ALL TESTS PASSED! 100% SPECIFICATION COMPLIANCE <<<${NC}\n"
    exit 0
else
    echo -e "\n${RED}>>> SOME TESTS FAILED (${FAILED_TESTS}/${TOTAL_TESTS}) <<<${NC}\n"
    exit 1
fi
