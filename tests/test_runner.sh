#!/bin/sh
set -u

# ANSI color codes for visual clarity
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MY_LS="$PROJECT_ROOT/my_ls"

if [ ! -x "$MY_LS" ]; then
    printf "${RED}Error: %s not found or not executable. Run 'make' first.${NC}\n" "$MY_LS"
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
trap cleanup EXIT INT TERM 0

# Populate sandbox with test fixtures
setup_fixtures() {
    cd "$SANDBOX" || exit 1

    # Standard files of various sizes using standard Unix dd
    printf "Short text\n" > file_small.txt
    dd if=/dev/zero bs=4096 count=1 of=file_medium.txt 2>/dev/null
    dd if=/dev/zero bs=65536 count=1 of=file_large.txt 2>/dev/null
    touch file_empty.txt

    # Hidden files and directory
    printf "Hidden content\n" > .hidden_file
    mkdir .hidden_dir
    printf "Sub content\n" > .hidden_dir/inner.txt

    # Subdirectories
    mkdir -p dir_alpha/subdir
    printf "Alpha 1\n" > dir_alpha/a1.txt
    printf "Alpha 2\n" > dir_alpha/a2.txt
    printf "Nested\n" > dir_alpha/subdir/nested.txt

    mkdir dir_beta
    printf "Beta 1\n" > dir_beta/b1.txt
    mkdir dir_empty

    # Executable file
    printf '#!/bin/sh\n' > script_exec.sh
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
    test_name="$1"
    flags="$2"
    target="$3"
    env_prefix="${4:-POSIXLY_CORRECT=1}"

    TOTAL_TESTS=$((TOTAL_TESTS + 1))

    out_my="$OUT_DIR/out_my_${TOTAL_TESTS}.txt"
    out_sys="$OUT_DIR/out_sys_${TOTAL_TESTS}.txt"
    err_my="$OUT_DIR/err_my_${TOTAL_TESTS}.txt"
    err_sys="$OUT_DIR/err_sys_${TOTAL_TESTS}.txt"

    # Run my_ls inside sandbox
    (cd "$SANDBOX" && "$MY_LS" $flags $target > "$out_my" 2> "$err_my")

    # Run system ls inside sandbox
    (cd "$SANDBOX" && env $env_prefix ls $flags $target > "$out_sys" 2> "$err_sys")

    if diff -u "$out_my" "$out_sys" >/dev/null 2>&1; then
        printf "  [${GREEN}PASS${NC}] Test %d: %s\n" "$TOTAL_TESTS" "$test_name"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        printf "  [${RED}FAIL${NC}] Test %d: %s\n" "$TOTAL_TESTS" "$test_name"
        printf "      --- Difference ---\n"
        diff -u "$out_my" "$out_sys" | head -n 15
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
}

run_error_test() {
    test_name="$1"
    flags="$2"
    target="$3"

    TOTAL_TESTS=$((TOTAL_TESTS + 1))

    out_my="$OUT_DIR/out_my_${TOTAL_TESTS}.txt"
    err_my="$OUT_DIR/err_my_${TOTAL_TESTS}.txt"

    (cd "$SANDBOX" && "$MY_LS" $flags $target > "$out_my" 2> "$err_my")
    status_my=$?

    if [ "$status_my" -ne 0 ] && [ -s "$err_my" ]; then
        printf "  [${GREEN}PASS${NC}] Test %d: %s (exit status: %d)\n" "$TOTAL_TESTS" "$test_name" "$status_my"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        printf "  [${RED}FAIL${NC}] Test %d: %s (expected exit > 0 and stderr output)\n" "$TOTAL_TESTS" "$test_name"
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
}

printf "${BLUE}======================================================${NC}\n"
printf "${BLUE}  AUTOMATED TEST SUITE: my_ls vs System ls (NetBSD/POSIX)${NC}\n"
printf "${BLUE}======================================================${NC}\n"

setup_fixtures

printf "\n${YELLOW}--- 1. Basic Listing and Entry Filtering (-a, -A) ---${NC}\n"
run_test "Default listing (no flags)" "" ""
run_test "All entries (-a)" "-a" ""
run_test "Almost all entries (-A)" "-A" ""

printf "\n${YELLOW}--- 2. Formatting Options (-l, -n, -F, -d) ---${NC}\n"
run_test "Long format (-l)" "-l" ""
run_test "Numeric IDs long format (-n)" "-n" ""
run_test "Classify indicators (-F)" "-F" ""
run_test "Directory as plain file (-d)" "-d" "."
run_test "Long format with dir as plain file (-ld)" "-ld" "dir_alpha"
run_test "Long format on empty directory (-l)" "-l" "dir_empty"
run_test "Block counts on empty directory (-s)" "-s" "dir_empty"

printf "\n${YELLOW}--- 3. Sorting Algorithms (-S, -t, -r, -f) ---${NC}\n"
run_test "Sort by size descending (-S)" "-S" ""
run_test "Sort by time descending (-t)" "-t" ""
run_test "Reverse lexicographical sort (-r)" "-r" ""
run_test "Reverse sort by size (-rS)" "-rS" ""
run_test "Reverse sort by time (-rt)" "-rt" ""
run_test "Long format sorted by size (-lS)" "-lS" ""
run_test "Long format sorted by time (-lt)" "-lt" ""

printf "\n${YELLOW}--- 4. Inode, Blocks and Size Formats (-i, -s, -k, -h) ---${NC}\n"
run_test "Inode display (-i)" "-i" ""
run_test "Block count display (-s)" "-s" ""
run_test "Kilobytes block count (-sk)" "-sk" ""
run_test "Human-readable sizes (-h)" "-lh" ""
run_test "Combined inode and blocks (-is)" "-is" ""
run_test "Combined inode and long format (-li)" "-li" ""

printf "\n${YELLOW}--- 5. Time Variants (-u, -c) ---${NC}\n"
run_test "Access time sorting (-tu)" "-tu" ""
run_test "Status change time sorting (-tc)" "-tc" ""
run_test "Access time long format (-lu)" "-lu" ""
run_test "Status change time long format (-lc)" "-lc" ""

printf "\n${YELLOW}--- 6. Recursive Traversal (-R) ---${NC}\n"
run_test "Recursive listing (-R)" "-R" ""
run_test "Recursive listing with classify (-RF)" "-RF" ""
run_test "Recursive listing with subfolder argument" "-R" "dir_alpha"

printf "\n${YELLOW}--- 7. Flag Override Rules ---${NC}\n"
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
    printf "  [${GREEN}PASS${NC}] Test %d: Override -n then -l (symbolic wins, NetBSD spec)\n" "$TOTAL_TESTS"
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    printf "  [${RED}FAIL${NC}] Test %d: Override -n then -l (expected -l to override -n)\n" "$TOTAL_TESTS"
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi
run_test "Override -c then -u (atime wins)" "-tcu" ""
run_test "Override -u then -c (ctime wins)" "-tuc" ""
run_test "Override -R then -d (-d disables -R)" "-Rd" "."
run_test "Override -d then -R (-d disables -R)" "-dR" "dir_alpha"

printf "\n${YELLOW}--- 8. Multiple Operands ---${NC}\n"
run_test "Multiple file operands" "" "file_small.txt file_medium.txt"
run_test "File and directory operands" "" "file_small.txt dir_alpha"
run_test "Multiple directory operands" "" "dir_alpha dir_beta"
run_test "Multiple operands with -l" "-l" "file_small.txt dir_alpha"

printf "\n${YELLOW}--- 9. Error Handling and Edge Cases ---${NC}\n"
run_error_test "Non-existent file operand" "" "non_existent_file_12345.xyz"
run_error_test "Non-existent directory operand" "" "non_existent_dir_67890/"
run_error_test "Invalid command-line option (-Z)" "-Z" ""

printf "\n${BLUE}======================================================${NC}\n"
printf "${BLUE}  TEST SUMMARY${NC}\n"
printf "${BLUE}======================================================${NC}\n"
printf "  Total Tests  : %d\n" "$TOTAL_TESTS"
printf "  Passed Tests : ${GREEN}%d${NC}\n" "$PASSED_TESTS"
printf "  Failed Tests : ${RED}%d${NC}\n" "$FAILED_TESTS"

if [ "$FAILED_TESTS" -eq 0 ]; then
    printf "\n${GREEN}>>> ALL TESTS PASSED! 100%% SPECIFICATION COMPLIANCE <<<${NC}\n\n"
    exit 0
else
    printf "\n${RED}>>> SOME TESTS FAILED (%d/%d) <<<${NC}\n\n" "$FAILED_TESTS" "$TOTAL_TESTS"
    exit 1
fi
