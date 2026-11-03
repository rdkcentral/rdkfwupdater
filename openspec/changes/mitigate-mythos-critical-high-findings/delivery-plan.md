# RDKEMW-24384 November Delivery Plan

Planning date: 2026-11-03. All dates are 2026; working days exclude Saturdays/Sundays. No public holidays are assumed. This file supersedes inconsistent dates and effort accounting from earlier chat estimates, but does not claim an executable estimate has been proven.

## Scope and Commitment Target

One story, no split: RDKFW-001..007 only, 2 Critical and 5 High. Inventory remains 18 total (2 Critical, 5 High, 10 Medium, 1 Low); RDKFW-008..018 are outside this story. Address all seven, finish developer testing/review, target reviewed code freeze Nov 20 and build handover to DevQA Nov 23. RDKFW-002 remains in scope despite temporary implementation pause.

RDKFW-004 code already exists at a19652a. Its tests/review are pending; do not repeat its coding effort at resume or count it as closed. Coding continues with RDKFW-005 while user executes RDKFW-004 validation and reviewer reviews supplied evidence.

## Effort Budget - Constrained Target, Not Reduced Security Criteria

| Activity | Developer working days | Engineering basis |
|---|---:|---|
| Focused analysis/policy validation | 1.5 | Reuse completed source/harness analysis; resolve state producer, principal policy and dependency boundary early, not broad reassessment |
| Coding | 6.0 | Shared filename policy for 001/003; shared checked accumulation for 004; bounded POST/callers for 005; credential/path policy for 006; URL/redirect controls for 007; small approved secure-reader change for 002 |
| Unit-test development | 2.0 | Extend existing GoogleTest mocks and fixtures with targeted boundary/authorization cases; do not redesign test infrastructure |
| Developer unit/security testing | 3.0 | User-run VM and controlled harness/MockXConf execution, including diagnosis/stabilization and required negative cases |
| Component regression/integration | 1.0 | Target affected legacy/Direct CDN and CLI/daemon paths, real legitimate clients and post-flash behavior |
| Review rework/final verification | 1.5 | Incremental small reviewer changes, affected test reruns and final integrated build evidence |
| **Developer target total** | **15.0** | Includes execution/debug/rework; excludes reviewer and DevQA own effort and external Mythos retest |

AI helps code/test drafting; compiler failures, execution, security policy decisions and human review still require time. This is an aggressive budget premised on small changes and reusable infrastructure. If an external transport patch, security policy redesign or substantial review rework is required, record additional effort rather than silently reduce tests.

Reviewer: five working days requested as a separate allocation, at least three mandatory. Reviewer effort overlaps developer coding/testing; it is not added to developer total and cannot be counted as developer capacity. Substantial changes require re-review. DevQA estimate: five working days separately after handover; developer responses remain developer effort, not QA effort. Mythos/approved external revalidation excluded from both the 15-day developer budget and assumed developer schedule.

## Capacity and Contingency - No Double Counting

| Window | Weekdays inclusive | Planned interpretation |
|---|---:|---|
| Nov 3-20 | 14 | Code/review/dev-test target; no weekends |
| Nov 3-23 | 15 | Entire requested development/handover window |
| Nov 3-25 | 17 | 15 after subtracting the two-day reserve; NOT 15 raw weekdays |
| Nov 3-30 | 20 | 18 available after two emergency/leave days |

The original two-day reserve is RETAINED. A 15-day workload cannot be called buffered within the 15-weekday pre-handover window; protected capacity before Nov 23 is 13. To execute the full timebox by Nov 23 without extra people, proposed sprint-level placement is reserve Nov 24-25, then up to three developer days Nov 26/27/30 for QA responses. That retains the reserve within the sprint but does NOT protect Nov 23 against pre-handover illness. Owner/manager must confirm placement or arrange two days of capacity support before handover. Reserve is not guaranteed leave and is not also counted as interruption time.

| Scenario through Nov 23 | Raw weekdays | Capacity after two reserve days and diversion | Interpretation |
|---|---:|---:|---|
| Full allocation | 15 | 13 | Fifteen-day target relies on unbuffered pre-handover execution or additional two-day support; explicitly confirm |
| Three diverted days | 15 | 10 | Recover capacity through manager support/reassignment; do not silently exclude findings or call the same date guaranteed |
| Five diverted days | 15 | 8 | Escalate resource support immediately; scope remains all seven and story is not declared complete without evidence |

## Daily Working Sequence and Rolling Review

Milestones are targets, not a statement that tests or approvals occurred. Parallelism is reviewer/user VM execution against completed commits while assistant develops the next related change; do not double-count one developer doing two full-day activities concurrently.

| Date | Work package and output |
|---|---|
| Tue Nov 3 | RDKFW-004 committed a19652a; issue/complete PR test package and record pending L1/L2. Request 002 producer/default policy, 006 principal policy and 007 dependency information. |
| Wed Nov 4 | RDKFW-005 builder/caller contract and bounded append code; unit boundary vectors. User starts 004 VM tests. |
| Thu Nov 5 | Complete 005 callers/test development; send 004/005 review-ready evidence as results arrive. Reviewer begins rolling review. |
| Fri Nov 6 | Shared RDKFW-001/003 filename/path policy and earliest entry checks; validate directory/open boundary design. |
| Mon Nov 9 | Finish 001/003 integration across both legacy/Direct CDN paths and targeted tests; prepare malicious MockXConf case. |
| Tue Nov 10 | RDKFW-006 actual-peer policy, handler ownership and flash path constraints; negative/legitimate client cases. |
| Wed Nov 11 | RDKFW-007 initial/effective HTTPS and verified redirect boundary; user endpoint trace plan. Nov 11 is a weekday, no unconfirmed holiday excluded. |
| Thu Nov 12 | RDKFW-002 approved secure reader/provenance and mocked branch tests, after owner unpause; do not guess producer. |
| Fri Nov 13 | Last substantive code/PR target for all seven; targeted unit development complete; send reviewer full current delta and test matrix. |
| Mon Nov 16 | User targeted L1 and MockXConf/L2 execution; repair observed failures while reviewer reviews. |
| Tue Nov 17 | D-Bus credential/path negatives, signed HTTPS redirect evidence; address reviewer comments. |
| Wed Nov 18 | Canary branch safety and both-binary peripheral rollback tests; close local failures. |
| Thu Nov 19 | Integrated regression/user build results; final review rework and affected retest. |
| Fri Nov 20 | Reviewed code-freeze target; confirm all seven have developer results and no blocking comments; final known build commit. |
| Mon Nov 23 | Handover reviewed/tested build and seven-row evidence matrix to DevQA; update checkpoint with actual commit/image and date. |

No work scheduled Nov 7-8, 14-15, 21-22. A last substantive PR on Nov 13 gives reviewer five working days Nov 16-20. Earlier PRs can be reviewed Nov 5 onward. Review-ready means test plan supplied and executed developer evidence available; early drafts can be shared but are not falsely described as validated.

Use existing topic branch with one incrementally updated PR unless reviewer explicitly agrees distinct grouped/stacked branches. Related groups: 004; 005; 001/003; 006/007; 002. Separate reviewer packages do not split the user story. No new branch/commit/push without the user's authorization for that operation.

## DevQA and Closure

Handover on Nov 23 means QA begins Nov 24, not before. Five QA weekdays are Nov 24/25/26/27/30 (no assumed holidays). Expected QA completion target is Nov 30 if tests pass and feedback remains small. There is no sixth QA day before month end; significant defects/re-review threaten that target and need immediate escalation. User-owned execution and evidence must still cover all seven.

Story security closure requires Mythos, approved security scan, or documented security-team revalidation plus review, targeted negative/unit and component evidence. That external decision is not guaranteed Nov 30 and is not developer-owned effort. Do not mark story/finding closed merely because DevQA received the build.

## Escalation Triggers

| Trigger | Concrete action |
|---|---|
| 002 producer/default policy or 006 allowed principals unknown after early Nov 4 request | Ask owner/security for explicit decision; continue unblocked fixes; log blocker and capacity impact, no invented default |
| 007 requires external dependency code or unsupported curl option | Obtain actual version/source and integration owner by Nov 6; account for added task/effort |
| Last substantive PR not ready by Nov 13 | Notify reviewer/manager that five-day review overlaps freeze; request review support or capacity recovery |
| VM/harness setup gates or pre-existing failures | Classify setup/blocker separately; record actual failing command and impacted acceptance test, no fabricated pass |
| Emergency or spillover before Nov 23 | Report diverted days once; use agreed reserve/resource recovery without quietly removing scoped findings |
| Significant reviewer/QA defect | Repair and rerun; revise actual milestones openly rather than claim a completed seven-finding build |

## Daily Manager Update Template

Date / branch / latest commit:
Findings coded today / findings still open:
L1/L2/on-device plan supplied / results received:
Review package and comments remaining:
Owner/dependency decisions required:
Nov 20 code-freeze and Nov 23 handover target status:
Next task IDs and actual effort used: