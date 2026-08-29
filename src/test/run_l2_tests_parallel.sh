#!/bin/bash
# Parallel L2 Test Runner with Progress and Timing (GNU Parallel)
# Usage: ./run_l2_tests_parallel.sh [num_jobs]

# Force C locale for consistent number formatting (decimal point)
export LC_ALL=C

SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"
TEST_BINARY="$SCRIPT_DIR/test_cascoin"
NUM_JOBS=${1:-$(nproc)}
TEMP_DIR=$(mktemp -d)

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}═══════════════════════════════════════════════════════════════${NC}"
echo -e "${BLUE}  Cascoin L2 Parallel Test Runner${NC}"
echo -e "${BLUE}═══════════════════════════════════════════════════════════════${NC}"
echo ""

# Get all L2 test suites
L2_SUITES=$("$TEST_BINARY" --list_content 2>&1 | grep -E "^l2_.*_tests" | sed 's/\*$//' | sort -u)

if [ -z "$L2_SUITES" ]; then
    echo -e "${RED}No L2 test suites found!${NC}"
    exit 1
fi

TOTAL_SUITES=$(echo "$L2_SUITES" | wc -l)
echo -e "${YELLOW}Found $TOTAL_SUITES L2 test suites${NC}"
echo -e "${YELLOW}Running with $NUM_JOBS parallel jobs${NC}"
echo ""

# Create runner script for parallel
cat > "$TEMP_DIR/run_one.sh" << 'RUNNER'
#!/bin/bash
suite=$1
test_binary=$2
temp_dir=$3

output_file="$temp_dir/${suite}.log"
time_file="$temp_dir/${suite}.time"

start_time=$(date +%s.%N)
if "$test_binary" --run_test="$suite" --log_level=test_suite > "$output_file" 2>&1; then
    status="PASS"
else
    status="FAIL"
fi
end_time=$(date +%s.%N)
duration=$(echo "$end_time - $start_time" | bc)

echo "$status $duration" > "$time_file"

if [ "$status" = "PASS" ]; then
    printf "\033[0;32m✓\033[0m %-45s %8.3fs\n" "$suite" "$duration"
else
    printf "\033[0;31m✗\033[0m %-45s %8.3fs\n" "$suite" "$duration"
fi
RUNNER
chmod +x "$TEMP_DIR/run_one.sh"

echo -e "${BLUE}Running tests...${NC}"
echo ""

START_TIME=$(date +%s.%N)

# Run with GNU parallel - progress bar and ETA
echo "$L2_SUITES" | parallel --bar -j "$NUM_JOBS" "$TEMP_DIR/run_one.sh" {} "$TEST_BINARY" "$TEMP_DIR"

END_TIME=$(date +%s.%N)
TOTAL_TIME=$(echo "$END_TIME - $START_TIME" | bc)

echo ""
echo -e "${BLUE}═══════════════════════════════════════════════════════════════${NC}"
echo -e "${BLUE}  Results Summary${NC}"
echo -e "${BLUE}═══════════════════════════════════════════════════════════════${NC}"

# Collect results
PASSED=0
FAILED=0
FAILED_SUITES=""
declare -A TIMES

for suite in $L2_SUITES; do
    time_file="$TEMP_DIR/${suite}.time"
    if [ -f "$time_file" ]; then
        result=$(cat "$time_file")
        status=$(echo "$result" | cut -d' ' -f1)
        duration=$(echo "$result" | cut -d' ' -f2)
        TIMES[$suite]=$duration
        
        if [ "$status" = "PASS" ]; then
            ((PASSED++))
        else
            ((FAILED++))
            FAILED_SUITES="$FAILED_SUITES $suite"
        fi
    fi
done

echo ""
printf "%-25s %s\n" "Total test suites:" "$TOTAL_SUITES"
printf "%-25s ${GREEN}%s${NC}\n" "Passed:" "$PASSED"
printf "%-25s ${RED}%s${NC}\n" "Failed:" "$FAILED"
printf "%-25s %.3fs\n" "Total time:" "$TOTAL_TIME"
echo ""

# Show top 5 slowest tests
echo -e "${YELLOW}Top 5 slowest test suites:${NC}"
for suite in $L2_SUITES; do
    time_file="$TEMP_DIR/${suite}.time"
    if [ -f "$time_file" ]; then
        duration=$(cat "$time_file" | cut -d' ' -f2)
        echo "$duration $suite"
    fi
done | sort -rn | head -5 | while read duration suite; do
    printf "  %-45s %8.3fs\n" "$suite" "$duration"
done
echo ""

# Show failed test details
if [ $FAILED -gt 0 ]; then
    echo -e "${RED}═══════════════════════════════════════════════════════════════${NC}"
    echo -e "${RED}  Failed Test Details${NC}"
    echo -e "${RED}═══════════════════════════════════════════════════════════════${NC}"
    for suite in $FAILED_SUITES; do
        echo ""
        echo -e "${RED}--- $suite ---${NC}"
        cat "$TEMP_DIR/${suite}.log" | tail -30
    done
fi

# Cleanup
rm -rf "$TEMP_DIR"

# Exit with error if any tests failed
if [ $FAILED -gt 0 ]; then
    exit 1
fi

echo -e "${GREEN}All L2 tests passed!${NC}"
exit 0
