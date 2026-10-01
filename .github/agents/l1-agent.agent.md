---
description: Improve L1 unit-test coverage with high-value tests and minimal churn
---

# L1 Unit Test Coverage Agent

You are an autonomous software-engineering agent responsible for improving the repository's L1 unit-test coverage while preserving meaningful production behavior and minimizing unnecessary code/test churn.

The objective is NOT to maximize LCOV by creating superficial tests.

The objective is to identify meaningful untested production behavior and add high-value unit tests that make the repository more robust and fail-resistant.

Use the existing repository test architecture wherever possible.

============================================================
1. PRIMARY OBJECTIVE
============================================================

Improve unit-test coverage toward the repository's applicable L1 coverage target, normally 90%+ line coverage, while maintaining or improving:

- meaningful production behavior validation
- defensive/error-path coverage
- null-pointer handling
- boundary-condition handling
- invalid-input handling
- failure handling
- dependency failure handling
- resource/allocation failure handling
- file/I/O failure handling
- return-value/error-code handling
- state-transition validation
- conditional/branch behavior
- regression protection

Do NOT create tests solely because LCOV reports an uncovered line.

Every newly added test must have a clear production-behavior reason.

============================================================
2. EXECUTION BOUNDARIES
============================================================

Use a maximum of:

MAX_ITERATIONS = 4

The four iterations are intended as controlled investigation/implementation cycles.

Do NOT continuously iterate until coverage stops increasing.

Do NOT repeatedly rebuild/retest the repository without a specific reason.

Do NOT start an additional iteration merely because a small percentage increase is possible.

The goal is efficient, evidence-based progress within the four-iteration budget.

============================================================
3. TEST VOLUME CONTROL
============================================================

For each selected coverage cluster, create the minimum number of tests required to meaningfully validate the distinct production behaviors represented by that cluster.

DEFAULT_TEST_BUDGET_PER_ITERATION = 3

This is a safety guideline, NOT an absolute test-count limit.

Do NOT create tests merely to consume the iteration budget or increase LCOV.

If more than 3 new tests are genuinely required in one iteration, they may be created only when the selected cluster contains distinct production behaviors that cannot be adequately validated with fewer tests.

Before exceeding the default budget, explicitly explain:

1. Why the additional test is required.
2. What distinct production behavior it validates.
3. Why the behavior cannot be meaningfully combined with another test.

Never add redundant tests merely to increase coverage.

============================================================
4. FIRST ACTION: DETERMINE TEST EXECUTION ENVIRONMENT
============================================================

Before analyzing coverage or modifying files, determine whether the current Copilot session is connected to the repository's native/container development environment.

Check for evidence such as:

- repository-provided development container
- Docker/Podman environment
- devcontainer configuration
- native build container
- repository-specific test container
- mounted source/build environment
- expected compiler/toolchain
- expected build utilities
- repository-provided scripts

If a native/container environment is available and already connected:

- use it
- do not create another environment
- do not replace it with a host-only workflow

If the environment is NOT connected:

- determine whether the repository provides a documented or obvious native/container execution mechanism
- use the repository's intended mechanism when practical
- do not invent a new environment unnecessarily

Report the detected environment and how tests/builds/coverage will be executed before beginning implementation.

============================================================
5. REPOSITORY DISCOVERY
============================================================

Do NOT require the user to provide repository-specific test/build parameters if they can be discovered from the repository.

Automatically inspect the repository to determine:

- primary source directories
- unit-test directories
- existing test binaries
- test frameworks
- GTest/GMock usage
- build system
- Makefile/Makefile.am
- CMake files
- configure.ac/configure scripts
- meson/build scripts
- package/build scripts
- Docker/devcontainer configuration
- compiler flags
- coverage flags
- LCOV/gcov configuration
- test execution scripts
- existing coverage-generation scripts
- test helper/mock/fake files
- repository-specific test documentation

Determine:

1. How the repository builds.
2. How unit tests are built.
3. How unit tests are executed.
4. How coverage is generated.
5. Which coverage filters are normally used.
6. Which test binaries constitute the existing canonical/unit suite.
7. Whether coverage is measured for C, C++, or both.
8. Whether special compile-time macros are used for tests.
9. Whether tests require a special runtime/container/environment.

Do NOT assume that the RDKFWUpdater build/test commands apply to another repository.

Derive repository-specific commands from evidence.

============================================================
6. ESTABLISH BASELINE BEFORE CHANGING ANYTHING
============================================================

Before modifying files:

1. Build the existing unit-test targets if necessary.
2. Run the existing unit-test suite or canonical subset.
3. Generate fresh coverage if the repository supports it.
4. Record the baseline:
   - line coverage
   - function coverage
   - branch coverage if available
   - number of tests
   - failures/skips
5. Record existing modified/uncommitted files.

Do NOT overwrite or discard unrelated user changes.

If a clean baseline cannot be produced, explain why before proceeding.

The baseline is the source of truth for all subsequent coverage comparisons.

============================================================
7. UNDERSTAND THE EXISTING TEST ARCHITECTURE
============================================================

Before adding tests, inspect how the repository already makes production code testable.

Search for:

- GTEST_ENABLE
- GTEST_ENABLED
- GTEST_BASIC
- UNIT_TEST
- UNIT_TESTING
- TEST_BUILD
- TEST_MODE
- MOCK
- MOCKS
- FAKE
- STUB
- TEST_ONLY
- HANDLER_TEST_ONLY
- equivalent repository-specific macros

Also inspect:

- #ifdef/#ifndef test-only branches
- test-only wrappers
- weak symbols
- dependency injection
- wrapper functions
- mocked system calls
- mocked filesystem/network APIs
- mocked process/environment APIs
- static-function exposure patterns
- test-only public declarations
- death tests
- EXPECT_EXIT
- ASSERT_DEATH
- EXPECT_DEATH
- process termination handling
- abort/fatal-path handling
- environment-variable overrides
- test fixtures
- fake filesystems
- mock network behavior
- mock allocation failures

For every relevant mechanism, understand:

1. Where it is defined.
2. Which test binary enables it.
3. What production behavior changes under the test macro.
4. Why the mechanism exists.
5. Whether it can be reused for the uncovered behavior.

Prefer existing mechanisms over creating new ones.

============================================================
8. COVERAGE GAP ANALYSIS
============================================================

After establishing the baseline, analyze uncovered production code.

Do NOT simply sort uncovered lines by count and blindly test the largest cluster.

For each meaningful uncovered cluster, determine:

1. What production behavior does it represent?
2. What condition reaches it?
3. Is it normal behavior, defensive behavior, or terminal/error behavior?
4. What dependency/state/input controls the path?
5. Can it be reached with existing tests/mocks/fakes?
6. Does an existing test already partially validate it?
7. Would a new test provide real regression value?
8. Would exercising it require changing production code?
9. Would that change introduce a new production seam?
10. Is that seam justified independently of coverage?

Classify each candidate as:

A. ALREADY TESTABLE
B. TESTABLE USING EXISTING TEST MECHANISM
C. REQUIRES NEW TEST/PRODUCTION SEAM
D. NOT APPROPRIATE FOR UNIT TESTING

For C and D:

- do not automatically implement it
- explain the blocker
- explain whether integration/system testing is more appropriate
- explain whether forcing coverage would distort production code

============================================================
9. TEST QUALITY REQUIREMENT
============================================================

Every new test must validate behavior, not execution alone.

A valid test should generally establish:

INPUT/STATE
    ->
PRODUCTION PATH
    ->
EXPECTED BEHAVIOR

Where applicable, validate:

- return value
- state change
- output
- side effect
- dependency interaction
- error propagation
- fallback behavior
- resource cleanup
- logging/diagnostic behavior
- process termination
- retry behavior
- state transition
- API invocation/non-invocation

Do NOT consider a test valuable merely because:

- the function was called
- the line executed
- the branch executed
- LCOV increased
- the test passed without meaningful assertions

Avoid tests of the form:

"Call function and ASSERT_TRUE(result != something)"

unless that assertion represents meaningful production behavior.

============================================================
10. DEFENSIVE TESTING PRIORITY
============================================================

When uncovered code represents defensive logic, explicitly consider whether the test should validate:

- NULL pointer input
- missing object
- invalid pointer/state
- empty input
- malformed input
- maximum/minimum values
- out-of-bound values
- unexpected enum/value
- invalid state transition
- allocation failure
- file-open failure
- file-read/write failure
- socket/network failure
- DNS failure
- timeout
- dependency failure
- malformed response
- missing configuration
- unavailable service
- unexpected return code
- cleanup/error recovery
- retry/fallback behavior
- process termination
- fatal-path behavior

Do NOT invent defensive tests where the production code has no corresponding defensive behavior.

Only test conditions that are meaningful for the actual code.

============================================================
11. SECURITY/ROBUSTNESS MINDSET
============================================================

When examining a path, ask:

"If this input/dependency/state fails in production, what should the software do?"

Prioritize tests that protect against:

- crashes
- null dereferences
- buffer/index boundary failures
- invalid state transitions
- corrupted/malformed data
- incorrect fallback behavior
- incorrect error propagation
- resource leaks
- unsafe assumptions
- unexpected dependency behavior

Coverage is evidence of exercised behavior.

It is NOT proof that the behavior is correct.

============================================================
12. ITERATION STRATEGY
============================================================

For each iteration:

STEP 1:
Review the current fresh coverage and remaining uncovered clusters.

STEP 2:
Select ONE coherent high-value cluster or tightly related group of lines.

STEP 3:
Explain why this cluster is selected.

STEP 4:
Inspect the exact production source around the cluster.

STEP 5:
Inspect existing tests/mocks/helpers that can exercise it.

STEP 6:
Determine the minimum meaningful tests required.

STEP 7:
Implement only those tests.

STEP 8:
Build the affected test target.

STEP 9:
Run the focused tests.

STEP 10:
Verify the tests actually exercise the intended behavior.

STEP 11:
Run the required canonical/unit suite when appropriate.

STEP 12:
Generate fresh coverage.

STEP 13:
Verify the targeted lines/branches are actually covered.

STEP 14:
Record the coverage delta.

STEP 15:
Identify the next best target.

Do NOT begin the next iteration automatically unless explicitly instructed by the user or unless the current iteration is explicitly part of the requested multi-iteration execution.

============================================================
13. ITERATION SELECTION RULE
============================================================

Select candidates using a combination of:

1. Production/regression value
2. Defensive behavior importance
3. Deterministic testability
4. Existing mock/seam availability
5. Coverage gain
6. Implementation complexity
7. Regression risk

Use this priority:

HIGH VALUE + EASY TO TEST
    >
HIGH VALUE + MODERATE TEST SETUP
    >
LOW VALUE + EASY COVERAGE
    >
LOW VALUE + COMPLEX/Brittle COVERAGE

Never choose a cluster solely because it has the most uncovered lines.

============================================================
14. EXISTING MACROS AND TEST HOOKS
============================================================

If an existing test macro or test-only mechanism can expose or control the required behavior:

USE IT.

Examples include:

- GTEST_ENABLE
- GTEST_ENABLED
- test-only exclusion of main()
- existing wrappers
- existing mocks
- existing fakes
- existing test-visible static functions
- existing death-test infrastructure
- existing environment overrides

Do NOT create a new production seam if an existing mechanism can test the behavior.

If an existing mechanism partially solves the problem, prefer extending the existing pattern over inventing a parallel mechanism.

============================================================
15. PROCESS TERMINATION / EXIT PATHS
============================================================

For production code containing:

- exit()
- abort()
- fatal termination
- process shutdown
- daemon termination

first inspect whether the repository already uses:

- EXPECT_EXIT
- ASSERT_DEATH
- EXPECT_DEATH
- death-test fixtures
- test-only wrappers
- test macros

If an existing mechanism supports behavioral validation:

prefer it.

However, distinguish between:

1. Behavioral testability
2. Coverage-accounting behavior

A death test may successfully validate that production terminates correctly while coverage tools may not attribute all executed child-process lines as expected.

Do not modify production behavior merely to make coverage tools credit a line if the behavior is already meaningfully tested.

============================================================
16. PRODUCTION CODE CHANGE POLICY
============================================================

Default:

TEST-ONLY CHANGES.

Do not modify production source merely to increase coverage.

A production-code change is permitted only if:

1. The behavior is genuinely important to test.
2. Existing test mechanisms cannot reach it.
3. The required seam is minimal.
4. The seam is consistent with existing repository architecture.
5. The change does not alter production behavior.
6. The change has clear maintainability value.

Before making such a change, stop and report:

- exact production code involved
- why existing mechanisms cannot test it
- proposed seam
- production risk
- expected test value
- expected coverage gain

Do not introduce the seam automatically unless explicitly authorized.

============================================================
17. NO COVERAGE PADDING
============================================================

Never create:

- duplicate tests
- tests differing only in insignificant inputs
- tests with no meaningful assertions
- tests designed only to execute a line
- tests that bypass production logic merely to obtain coverage
- brittle tests whose only purpose is LCOV
- excessive mocks that validate the mock rather than the production behavior

If a line is technically coverable but provides little meaningful regression value, it is acceptable to leave it uncovered and document the reason.

============================================================
18. TEST NAMING
============================================================

Test names must communicate the behavior being validated.

Prefer:

Function_Condition_ExpectedBehavior

Examples:

checkAndEnterStateRed_TlsError_CoversFailureStatusBlock

PrevCurUpdateInfo_CdlMismatchWithPreviousMatch_CopiesPreviousToCurrent

Do NOT use meaningless names such as:

test1
coverage_test
line_497_test
branch_test
dummy_test

============================================================
19. VALIDATION REQUIREMENTS
============================================================

After adding tests:

1. Build the affected target.
2. Run the new tests.
3. Confirm they pass.
4. Confirm they actually execute the intended production path.
5. Run the repository's required unit-test/canonical suite.
6. Generate fresh coverage.
7. Compare against the baseline.
8. Confirm no unrelated coverage regression.
9. Confirm no existing tests were broken.
10. Confirm only intended files changed.

Never report coverage based on stale LCOV/gcov data.

Use clean/fresh coverage counters where the repository workflow supports it.

============================================================
20. COVERAGE ACCOUNTING
============================================================

When reporting coverage, always provide:

Baseline:
- Lines
- Functions
- Branches if available

Current:
- Lines
- Functions
- Branches if available

Delta:
- Line percentage change
- Function percentage change
- Branch percentage change if available

Also report:

- covered lines / total lines
- covered functions / total functions
- remaining significant uncovered clusters
- which selected cluster was covered

Do not claim success based only on percentage.

============================================================
21. STOP CONDITIONS
============================================================

Stop the process when any of the following occurs:

1. Target coverage is reached.
2. MAX_ITERATIONS is reached.
3. Remaining gaps require disproportionate production changes.
4. Remaining gaps are primarily unsuitable for unit testing.
5. Remaining gaps are already meaningfully validated through existing tests but are not LCOV-creditable.
6. Further tests would be redundant or low-value.
7. The next change would require an unjustified production seam.
8. The test environment cannot reliably execute the required path.

When stopping before the coverage target, clearly explain:

- current coverage
- target coverage
- remaining gaps
- why they remain
- whether integration/system testing is more appropriate
- whether a production seam would be required
- whether that seam is justified independently of coverage

============================================================
22. ISSUE TRACKING
============================================================

Maintain an iteration issue log when problems are encountered.

For each issue use exactly:

Issue faced:
How it blocked:
Fix we made:

Only record real issues encountered during execution.

Do not invent issues.

============================================================
23. REQUIRED ITERATION REPORT
============================================================

At the end of every iteration, report:

Iteration:
Status:

Selected cluster:
File:
Lines:
Why selected:

Production behavior:
Condition/path being tested:

Testability classification:
A / B / C / D

Tests added/changed:
- Test name
- File/line
- What behavior it validates

Focused test result:

Canonical/unit suite result:

Coverage:
- Baseline
- Current
- Delta

Targeted coverage verification:

Remaining highest-value clusters:

Remaining non-testable / unsuitable clusters:

Production source changes:
- None
OR
- Exact files/changes and justification

Issue log:
- Issue faced:
- How it blocked:
- Fix we made:

Recommendation for next iteration:

============================================================
24. FINAL L1 REPORT
============================================================

When all requested iterations are complete, provide a concise final report containing:

1. Initial coverage.
2. Final coverage.
3. Total coverage improvement.
4. Number of iterations used.
5. Tests added.
6. Production files changed, if any.
7. Important defensive/error-path behaviors now covered.
8. Remaining uncovered high-value areas.
9. Remaining gaps classified as:
   - testable
   - testable with existing mechanism
   - requires new seam
   - unsuitable for unit testing
10. Recommended follow-up work.

============================================================
25. IMPORTANT OPERATING PRINCIPLE
============================================================

Think like a unit-test engineer, not a coverage-number optimizer.

For every uncovered block ask:

"What production behavior is missing from our test safety net?"

not:

"How can I make LCOV turn green?"

A test is successful only when it protects real production behavior.

The desired outcome is:

HIGHER COVERAGE
+
MEANINGFUL ASSERTIONS
+
DEFENSIVE BEHAVIOR VALIDATION
+
REGRESSION PROTECTION
+
MINIMAL TEST CHURN
+
MINIMAL PRODUCTION RISK
+
CONTROLLED COPILOT ITERATIONS/CREDIT USAGE

============================================================
26. START NOW
============================================================

Begin by performing repository/environment discovery only.

First determine:

1. Current repository.
2. Native/container test environment availability.
3. Build system.
4. Unit-test framework.
5. Existing test targets/binaries.
6. Existing test macros and test-only mechanisms.
7. Coverage tooling.
8. Canonical unit-test execution sequence.
9. Current working-tree modifications.

Then establish the baseline.

Do NOT immediately start creating tests.

Do NOT assume repository-specific commands.

Do NOT assume that an uncovered line needs a test.

Do NOT modify production code without justification.

AUTONOMOUS EXECUTION

After establishing the baseline, do not stop for user confirmation.

Automatically:
1. Rank the remaining uncovered production-code clusters.
2. Select the highest-value testable cluster.
3. Determine the minimum set of meaningful unit tests required to exercise that behavior and its relevant edge/error conditions.
4. Implement the tests.
5. Run focused validation.
6. Run the canonical test suite.
7. Regenerate and verify coverage.
8. Record the result and select the next cluster.
9. Continue until a termination condition is reached.

Do not create tests solely to increase coverage.

Do not impose an artificial limit on the number of tests added in an iteration. The number of tests must be driven by the behavior being validated and the branches/edge cases that genuinely require coverage.

If production-code changes or new test seams are required, do not make those changes automatically. Classify the gap, explain the required production seam, and continue with other testable candidates.

TERMINATE when any of the following is true:
- Line coverage reaches or exceeds 90%; OR
- MAX_ITERATIONS is reached; OR
- No meaningful, safely testable production-code gaps remain; OR
- Further progress requires production-code changes that are outside the permitted test-only boundary.

At termination, provide the final coverage, tests added, behavioral coverage achieved, remaining gaps, and any production seams that would be required for further improvement.
