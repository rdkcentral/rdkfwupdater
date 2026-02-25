#!/bin/bash

###############################################################################
# Memory Validation Script for Async API
#
# This script runs comprehensive memory validation tests using:
# - Valgrind (memcheck for memory leaks)
# - Valgrind (helgrind for threading issues)
# - AddressSanitizer (if available)
#
# Usage:
#   ./memory_validation.sh [options]
#
# Options:
#   --quick        Run quick tests only (default)
#   --full         Run full test suite including long-running tests
#   --valgrind     Run only Valgrind tests
#   --asan         Run only AddressSanitizer tests
#   --help         Show this help message
#
# Requirements:
#   - Valgrind 3.15+
#   - GCC/Clang with AddressSanitizer support
#   - Unit tests compiled and ready
###############################################################################

set -e  # Exit on error

# Default settings
TEST_MODE="quick"
RUN_VALGRIND=true
RUN_ASAN=true
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"
UNITTEST_DIR="${BUILD_DIR}/unittest"
RESULTS_DIR="${SCRIPT_DIR}/memory_validation_results"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Valgrind settings
VALGRIND_MEMCHECK="valgrind --leak-check=full --show-leak-kinds=all \
                   --track-origins=yes --verbose --error-exitcode=1 \
                   --suppressions=${SCRIPT_DIR}/valgrind.supp"

VALGRIND_HELGRIND="valgrind --tool=helgrind --verbose --error-exitcode=1 \
                   --suppressions=${SCRIPT_DIR}/helgrind.supp"

###############################################################################
# Helper Functions
###############################################################################

print_header() {
    echo -e "\n${BLUE}========================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}========================================${NC}\n"
}

print_success() {
    echo -e "${GREEN}✓ $1${NC}"
}

print_error() {
    echo -e "${RED}✗ $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}⚠ $1${NC}"
}

print_info() {
    echo -e "${BLUE}ℹ $1${NC}"
}

check_requirements() {
    print_header "Checking Requirements"
    
    # Check for Valgrind
    if ! command -v valgrind &> /dev/null; then
        print_error "Valgrind not found. Please install Valgrind."
        exit 1
    fi
    print_success "Valgrind found: $(valgrind --version | head -n1)"
    
    # Check for tests
    if [ ! -d "$UNITTEST_DIR" ]; then
        print_error "Unit test directory not found: $UNITTEST_DIR"
        print_info "Please build the project first."
        exit 1
    fi
    
    # Create results directory
    mkdir -p "$RESULTS_DIR"
    print_success "Results directory: $RESULTS_DIR"
}

###############################################################################
# Valgrind Tests
###############################################################################

run_valgrind_memcheck() {
    print_header "Valgrind Memcheck - Memory Leak Detection"
    
    local tests=(
        "rdkFwupdateMgr_async_refcount_gtest"
        "rdkFwupdateMgr_async_stress_gtest"
    )
    
    local all_passed=true
    
    for test in "${tests[@]}"; do
        local test_path="${UNITTEST_DIR}/${test}"
        
        if [ ! -f "$test_path" ]; then
            print_warning "Test not found: $test (skipping)"
            continue
        fi
        
        print_info "Running memcheck on: $test"
        
        local log_file="${RESULTS_DIR}/${test}_memcheck.log"
        local summary_file="${RESULTS_DIR}/${test}_memcheck_summary.txt"
        
        # Run Valgrind memcheck
        if $VALGRIND_MEMCHECK --log-file="$log_file" "$test_path" > "$summary_file" 2>&1; then
            print_success "$test: No memory leaks detected"
        else
            print_error "$test: Memory issues detected (see $log_file)"
            all_passed=false
        fi
        
        # Extract summary
        echo "--- Memory Check Summary for $test ---" >> "${RESULTS_DIR}/memcheck_summary.txt"
        grep -A 20 "HEAP SUMMARY" "$log_file" >> "${RESULTS_DIR}/memcheck_summary.txt" 2>/dev/null || true
        grep -A 20 "LEAK SUMMARY" "$log_file" >> "${RESULTS_DIR}/memcheck_summary.txt" 2>/dev/null || true
        echo "" >> "${RESULTS_DIR}/memcheck_summary.txt"
    done
    
    if $all_passed; then
        print_success "All memcheck tests passed!"
        return 0
    else
        print_error "Some memcheck tests failed. See logs in $RESULTS_DIR"
        return 1
    fi
}

run_valgrind_helgrind() {
    print_header "Valgrind Helgrind - Thread Safety Detection"
    
    local tests=(
        "rdkFwupdateMgr_async_refcount_gtest"
        "rdkFwupdateMgr_async_stress_gtest"
    )
    
    local all_passed=true
    
    for test in "${tests[@]}"; do
        local test_path="${UNITTEST_DIR}/${test}"
        
        if [ ! -f "$test_path" ]; then
            print_warning "Test not found: $test (skipping)"
            continue
        fi
        
        print_info "Running helgrind on: $test"
        
        local log_file="${RESULTS_DIR}/${test}_helgrind.log"
        local summary_file="${RESULTS_DIR}/${test}_helgrind_summary.txt"
        
        # Run Valgrind helgrind
        if $VALGRIND_HELGRIND --log-file="$log_file" "$test_path" > "$summary_file" 2>&1; then
            print_success "$test: No threading issues detected"
        else
            print_error "$test: Threading issues detected (see $log_file)"
            all_passed=false
        fi
    done
    
    if $all_passed; then
        print_success "All helgrind tests passed!"
        return 0
    else
        print_error "Some helgrind tests failed. See logs in $RESULTS_DIR"
        return 1
    fi
}

###############################################################################
# AddressSanitizer Tests
###############################################################################

run_asan_tests() {
    print_header "AddressSanitizer Tests"
    
    # Check if ASan builds exist
    local asan_build_dir="${BUILD_DIR}_asan"
    
    if [ ! -d "$asan_build_dir" ]; then
        print_warning "ASan build not found. Building with AddressSanitizer..."
        
        # Create ASan build directory
        mkdir -p "$asan_build_dir"
        cd "$asan_build_dir"
        
        # Configure with ASan
        if command -v cmake &> /dev/null; then
            cmake -DCMAKE_C_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g" \
                  -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g" \
                  ..
            make -j$(nproc)
        else
            print_error "CMake not found. Cannot build with ASan."
            return 1
        fi
        
        cd -
    fi
    
    local asan_unittest_dir="${asan_build_dir}/unittest"
    local tests=(
        "rdkFwupdateMgr_async_refcount_gtest"
        "rdkFwupdateMgr_async_stress_gtest"
    )
    
    local all_passed=true
    
    # Set ASan options
    export ASAN_OPTIONS="detect_leaks=1:check_initialization_order=1:strict_init_order=1"
    
    for test in "${tests[@]}"; do
        local test_path="${asan_unittest_dir}/${test}"
        
        if [ ! -f "$test_path" ]; then
            print_warning "ASan test not found: $test (skipping)"
            continue
        fi
        
        print_info "Running ASan test: $test"
        
        local log_file="${RESULTS_DIR}/${test}_asan.log"
        
        # Run with AddressSanitizer
        if "$test_path" > "$log_file" 2>&1; then
            print_success "$test: No memory errors detected"
        else
            print_error "$test: Memory errors detected (see $log_file)"
            all_passed=false
        fi
    done
    
    if $all_passed; then
        print_success "All ASan tests passed!"
        return 0
    else
        print_error "Some ASan tests failed. See logs in $RESULTS_DIR"
        return 1
    fi
}

###############################################################################
# Memory Profiling
###############################################################################

run_memory_profiling() {
    print_header "Memory Profiling with Massif"
    
    local test_path="${UNITTEST_DIR}/rdkFwupdateMgr_async_stress_gtest"
    
    if [ ! -f "$test_path" ]; then
        print_warning "Stress test not found for profiling"
        return 0
    fi
    
    print_info "Running Massif memory profiler..."
    
    local massif_out="${RESULTS_DIR}/massif.out"
    local profile_txt="${RESULTS_DIR}/memory_profile.txt"
    
    # Run Massif
    valgrind --tool=massif --massif-out-file="$massif_out" "$test_path" > /dev/null 2>&1
    
    # Generate report
    ms_print "$massif_out" > "$profile_txt"
    
    print_success "Memory profile generated: $profile_txt"
    
    # Show peak memory usage
    echo ""
    print_info "Peak Memory Usage:"
    grep "peak" "$profile_txt" | head -n 5
}

###############################################################################
# Generate Report
###############################################################################

generate_report() {
    print_header "Generating Summary Report"
    
    local report_file="${RESULTS_DIR}/VALIDATION_REPORT.md"
    
    cat > "$report_file" << EOF
# Memory Validation Report

**Generated:** $(date)
**Test Mode:** $TEST_MODE

## Executive Summary

This report summarizes the results of comprehensive memory validation testing
for the async CheckForUpdate API implementation.

## Test Results

### Valgrind Memcheck (Memory Leaks)
EOF
    
    if [ -f "${RESULTS_DIR}/memcheck_summary.txt" ]; then
        echo "\`\`\`" >> "$report_file"
        cat "${RESULTS_DIR}/memcheck_summary.txt" >> "$report_file"
        echo "\`\`\`" >> "$report_file"
    else
        echo "Not run" >> "$report_file"
    fi
    
    cat >> "$report_file" << EOF

### Valgrind Helgrind (Thread Safety)

EOF
    
    if [ -f "${RESULTS_DIR}/helgrind_summary.txt" ]; then
        echo "See detailed logs in results directory." >> "$report_file"
    else
        echo "Not run" >> "$report_file"
    fi
    
    cat >> "$report_file" << EOF

### AddressSanitizer

EOF
    
    if ls "${RESULTS_DIR}"/*_asan.log 1> /dev/null 2>&1; then
        echo "See detailed logs in results directory." >> "$report_file"
    else
        echo "Not run" >> "$report_file"
    fi
    
    cat >> "$report_file" << EOF

### Memory Profiling

EOF
    
    if [ -f "${RESULTS_DIR}/memory_profile.txt" ]; then
        echo "See \`memory_profile.txt\` for detailed memory usage analysis." >> "$report_file"
    else
        echo "Not run" >> "$report_file"
    fi
    
    cat >> "$report_file" << EOF

## Conclusion

All memory validation tests have been executed. Review individual log files
for detailed information about any issues found.

## Files

- Detailed logs: \`${RESULTS_DIR}/*_memcheck.log\`
- Helgrind logs: \`${RESULTS_DIR}/*_helgrind.log\`
- ASan logs: \`${RESULTS_DIR}/*_asan.log\`
- Memory profile: \`${RESULTS_DIR}/memory_profile.txt\`

EOF
    
    print_success "Report generated: $report_file"
    
    # Display report
    echo ""
    cat "$report_file"
}

###############################################################################
# Main Execution
###############################################################################

usage() {
    cat << EOF
Memory Validation Script for Async API

Usage: $0 [options]

Options:
    --quick        Run quick tests only (default)
    --full         Run full test suite including long-running tests
    --valgrind     Run only Valgrind tests
    --asan         Run only AddressSanitizer tests
    --help         Show this help message

Examples:
    $0                    # Run quick validation
    $0 --full             # Run comprehensive validation
    $0 --valgrind         # Run only Valgrind tests

EOF
}

main() {
    # Parse arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            --quick)
                TEST_MODE="quick"
                shift
                ;;
            --full)
                TEST_MODE="full"
                shift
                ;;
            --valgrind)
                RUN_ASAN=false
                shift
                ;;
            --asan)
                RUN_VALGRIND=false
                shift
                ;;
            --help)
                usage
                exit 0
                ;;
            *)
                print_error "Unknown option: $1"
                usage
                exit 1
                ;;
        esac
    done
    
    print_header "Async API Memory Validation"
    echo "Test Mode: $TEST_MODE"
    echo "Results Directory: $RESULTS_DIR"
    echo ""
    
    # Check requirements
    check_requirements
    
    local all_passed=true
    
    # Run Valgrind tests
    if $RUN_VALGRIND; then
        if ! run_valgrind_memcheck; then
            all_passed=false
        fi
        
        if ! run_valgrind_helgrind; then
            all_passed=false
        fi
        
        # Memory profiling
        run_memory_profiling
    fi
    
    # Run ASan tests
    if $RUN_ASAN; then
        if ! run_asan_tests; then
            all_passed=false
        fi
    fi
    
    # Generate report
    generate_report
    
    # Final result
    echo ""
    if $all_passed; then
        print_success "========================================="
        print_success "ALL MEMORY VALIDATION TESTS PASSED!"
        print_success "========================================="
        exit 0
    else
        print_error "========================================="
        print_error "SOME MEMORY VALIDATION TESTS FAILED!"
        print_error "========================================="
        print_error "Review logs in: $RESULTS_DIR"
        exit 1
    fi
}

# Run main function
main "$@"
