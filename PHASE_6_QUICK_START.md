# Phase 6 Quick Start Guide

**Quick reference for running Phase 6 memory validation tests**

---

## Prerequisites

```bash
# Install required tools (Ubuntu/Debian)
sudo apt-get install valgrind build-essential

# Install GTest (if not already installed)
sudo apt-get install libgtest-dev
cd /usr/src/gtest
sudo cmake .
sudo make
sudo cp *.a /usr/lib

# Verify installations
valgrind --version    # Should be 3.15+
g++ --version         # Should support C++11
```

---

## Quick Test (5 minutes)

### Step 1: Build
```bash
cd unittest
make rdkFwupdateMgr_async_refcount_gtest
make rdkFwupdateMgr_async_stress_gtest
```

### Step 2: Run
```bash
# Reference counting tests (quick, ~30 seconds)
./rdkFwupdateMgr_async_refcount_gtest

# Stress tests (quick mode, ~2 minutes)
./rdkFwupdateMgr_async_stress_gtest
```

**Expected Output:**
- All tests should pass (green `[  PASSED  ]`)
- No crashes or hangs
- Statistics printed at end

---

## Memory Leak Check (15 minutes)

### Using Valgrind

```bash
# Reference counting tests under Valgrind (~5 min)
valgrind --leak-check=full \
         --show-leak-kinds=all \
         --track-origins=yes \
         --suppressions=../test/valgrind.supp \
         ./rdkFwupdateMgr_async_refcount_gtest

# Check for leaks in summary
# Look for: "definitely lost: 0 bytes"
```

### Using Automated Script

```bash
# Quick validation (~10 min)
cd ..
chmod +x test/memory_validation.sh
./test/memory_validation.sh --quick

# Check results
cat test/memory_validation_results/VALIDATION_REPORT.md
```

**Expected Output:**
- "0 bytes definitely lost"
- "0 bytes indirectly lost"
- "All memory tests passed!"

---

## Thread Safety Check (30 minutes)

### Using Helgrind

```bash
cd unittest

# Run with Helgrind (~15 min per test)
valgrind --tool=helgrind \
         --suppressions=../test/helgrind.supp \
         ./rdkFwupdateMgr_async_refcount_gtest 2>&1 | tee helgrind.log

# Check for races
grep "Possible data race" helgrind.log
# Should find no races (or only known false positives)
```

### Using ThreadSanitizer (if available)

```bash
# Build with TSan
cd ..
mkdir build_tsan
cd build_tsan

# Configure with TSan
CFLAGS="-fsanitize=thread -g" \
CXXFLAGS="-fsanitize=thread -g" \
../configure

make -j4

# Run tests
cd unittest
./rdkFwupdateMgr_async_refcount_gtest
./rdkFwupdateMgr_async_stress_gtest
```

**Expected Output:**
- No data race warnings
- No lock order violations

---

## Stress Test (1+ hour)

### Short Stress Test (included in quick mode)
```bash
cd unittest
./rdkFwupdateMgr_async_stress_gtest
```

### Long-Running Test (1 hour)
```bash
# Enable long-running test
./rdkFwupdateMgr_async_stress_gtest --gtest_also_run_disabled_tests

# Or run specific long test
./rdkFwupdateMgr_async_stress_gtest --gtest_filter="*LongRunningStability"
```

**Expected Output:**
- Test runs for 1 hour
- No crashes or deadlocks
- Memory usage remains stable
- Statistics show balanced operations

---

## Full Validation (2-3 hours)

### Complete Suite

```bash
cd test
./memory_validation.sh --full
```

This runs:
1. ✅ Valgrind memcheck on all tests
2. ✅ Valgrind helgrind on all tests
3. ✅ AddressSanitizer build and tests
4. ✅ Massif memory profiling
5. ✅ Report generation

**Results Location:**
- `test/memory_validation_results/VALIDATION_REPORT.md`
- Individual logs in `test/memory_validation_results/*.log`

---

## Troubleshooting

### Test Fails to Build

```bash
# Check dependencies
pkg-config --cflags --libs glib-2.0 gio-2.0

# Check GTest
ls /usr/lib/libgtest*

# Rebuild from clean state
make clean
./configure
make -j4
```

### Valgrind Shows Leaks

```bash
# Run with verbose output
valgrind --leak-check=full \
         --show-leak-kinds=all \
         --track-origins=yes \
         --verbose \
         --log-file=leak_detail.log \
         ./rdkFwupdateMgr_async_refcount_gtest

# Review detailed log
less leak_detail.log

# Check if it's a known false positive
grep "g_type_init\|g_main_loop\|dbus_connection" leak_detail.log
```

### Tests Hang or Deadlock

```bash
# Run with timeout
timeout 60s ./rdkFwupdateMgr_async_stress_gtest

# If hangs, get backtrace
gdb ./rdkFwupdateMgr_async_stress_gtest
(gdb) run
# When hung, Ctrl+C
(gdb) thread apply all bt
```

### Helgrind Reports Races

```bash
# Check if it's a false positive
grep "g_atomic\|g_once\|g_type" helgrind.log

# Add suppression if needed
echo "{
   my_suppression
   Helgrind:Race
   fun:my_function
}" >> test/helgrind.supp
```

---

## Interpreting Results

### Success Indicators ✅
- All tests pass (green output)
- Valgrind: "0 bytes definitely lost"
- No "Possible data race" warnings
- Memory usage stable over time
- Performance meets requirements (< 1ms P95)

### Warning Signs ⚠️
- Tests occasionally fail (race condition?)
- Valgrind: "possibly lost" bytes (may be OK)
- Helgrind: warnings in GLib code (likely false positive)
- High latency (> 10ms P95)

### Critical Issues ❌
- Tests consistently fail
- Valgrind: "definitely lost" bytes
- Helgrind: races in our code (not GLib)
- Memory usage grows unbounded
- Crashes or hangs

---

## Quick Commands Reference

```bash
# Build tests
make rdkFwupdateMgr_async_refcount_gtest rdkFwupdateMgr_async_stress_gtest

# Run tests
./rdkFwupdateMgr_async_refcount_gtest
./rdkFwupdateMgr_async_stress_gtest

# Valgrind memcheck
valgrind --leak-check=full --suppressions=../test/valgrind.supp ./rdkFwupdateMgr_async_refcount_gtest

# Valgrind helgrind
valgrind --tool=helgrind --suppressions=../test/helgrind.supp ./rdkFwupdateMgr_async_refcount_gtest

# Full validation
../test/memory_validation.sh --full

# View results
cat ../test/memory_validation_results/VALIDATION_REPORT.md
```

---

## Expected Timeline

| Task | Duration | Status |
|------|----------|--------|
| Build tests | 5 min | ⏳ Ready |
| Run quick tests | 5 min | ⏳ Ready |
| Valgrind memcheck | 15 min | ⏳ Ready |
| Helgrind | 30 min | ⏳ Ready |
| Stress test | 5 min | ⏳ Ready |
| Long stress test | 1 hour | ⏳ Ready |
| Full validation | 2-3 hours | ⏳ Ready |

**Total for complete validation: ~4 hours**

---

## Next Steps After Validation

1. **If all tests pass:**
   - Document results in PHASE_6_PROGRESS_REPORT.md
   - Update IMPLEMENTATION_PLAN with completion status
   - Proceed to Phase 7 (Error Handling Refinement)

2. **If issues found:**
   - Document issues in bug tracker
   - Fix issues in implementation
   - Re-run validation
   - Update documentation

3. **If false positives:**
   - Add suppressions to valgrind.supp / helgrind.supp
   - Document in ASYNC_MEMORY_MANAGEMENT.md
   - Re-run to confirm suppressed

---

## Help and Support

### Documentation
- [Memory Management Guide](ASYNC_MEMORY_MANAGEMENT.md)
- [Phase 6 Validation Plan](PHASE_6_MEMORY_VALIDATION_PLAN.md)
- [Progress Report](PHASE_6_PROGRESS_REPORT.md)

### Common Issues
- See "Troubleshooting" section above
- Check existing GitHub issues
- Review test output logs

### Contact
- Project maintainers
- RDK community forums
- Internal team chat

---

**Last Updated:** 2026-02-25  
**Document Version:** 1.0
