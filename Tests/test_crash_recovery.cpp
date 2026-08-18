#include <catch2/catch_test_macros.hpp>

extern "C" {
#include <app/crash_recovery.h>
}

TEST_CASE("Crash recovery state machine advances through Explorer restart", "[crash_recovery]") {
    TE_CrashRecoveryState state = TE_CRASH_RECOVERY_RUNNING;

    /* If explorer is not dead, RUNNING should stay RUNNING */
    state = TE_CrashRecoveryAdvance(state, 0, false);
    REQUIRE(state == TE_CRASH_RECOVERY_RUNNING);

    /* When explorer dies, transitions to EXPLORER_DEAD */
    state = TE_CrashRecoveryAdvance(state, 0, true);
    REQUIRE(state == TE_CRASH_RECOVERY_EXPLORER_DEAD);

    /* Advance from EXPLORER_DEAD to WAITING_TASKBAR_CREATED */
    state = TE_CrashRecoveryAdvance(state, 0, false);
    REQUIRE(state == TE_CRASH_RECOVERY_WAITING_TASKBAR_CREATED);

    /* Advances to REHOOKING and then RUNNING */
    state = TE_CrashRecoveryAdvance(state, 0, false);
    REQUIRE(state == TE_CRASH_RECOVERY_REHOOKING);

    state = TE_CrashRecoveryAdvance(state, 0, false);
    REQUIRE(state == TE_CRASH_RECOVERY_RUNNING);
}
