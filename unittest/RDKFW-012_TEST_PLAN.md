# RDKFW-012: token logging

User executes all builds/tests; none were run by assistant. Both startFactoryProtectService copies stop logging raw security utility output, parsed token, and Authorization header. Request construction and authorization header passed to doCurlPutRequest are unchanged.

L1: rebuild and run the existing startFactoryProtectService tests in rdkfw_main_gtest and rdkfwupdatemgr_main_flow_gtest. Verify successful request and curl-init-failure behavior. These tests alone do not establish secret redaction.

L2: in a disposable test environment, arrange for WPEFrameworkSecurityUtility to return a unique nonproduction token through the real parser and capture the receiving FactoryProtect request. Exercise the legitimate FactoryProtect gate (PLATCO stage-lock flow) or invoke the function in a controlled component driver for BOTH executables. Verify the receiver gets the expected Bearer token, while fresh swupdate/stdout/stderr logs contain neither the token nor the raw token JSON nor Authorization header. Repeat a rejected/failed request and inspect error logs as well. Restore utility/script/stage-lock fixtures. Do not print actual device credentials into reports.

Harness: sibling rdkfwupdater_harness/rdkfwupdater-develop_Medium_005/xione_infeasible/poc_rdkfwupdater-008.sh. Read header/INSTRUCTIONS and run only an approved isolated reach/controlled-token test. July XiOne could not enter the PLATCO-only path; the same gate is not a redaction PASS. If the feature is absent use both-function L1 driver evidence and record device applicability.

Evidence: tested commit, executable/build flags, synthetic token identifier (not a real secret), gate/receiver trace, fresh-log negative search, existing regression results and cleanup. External transport/helper logs need separate inspection if they emit credentials; this patch removes the three known component call-site logs, not every dependency log.