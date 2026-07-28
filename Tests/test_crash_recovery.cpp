#include <catch2/catch_test_macros.hpp>

extern "C" {
#include <app/crash_recovery.h>
}

TEST_CASE("Crash recovery state machine advances through Explorer restart", "[crash_recovery]") {
    TE_CrashRecoveryState state = TE_CRASH_RECOVERY_RUNNING;
    state = TE_CrashRecoveryAdvance(state, 0, true);
    REQUIRE(state == TE_CRASH_RECOVERY_EXPLORER_DEAD);

    state = TE_CrashRecoveryAdvance(state, 0, false);
    REQUIRE(state == TE_CRASH_RECOVERY_WAITING_TASKBAR_CREATED);

    state = TE_CrashRecoveryAdvance(state, 0, false);
    REQUIRE(state == TE_CRASH_RECOVERY_REHOOKING);

    state = TE_CrashRecoveryAdvance(state, 0, false);
    REQUIRE(state == TE_CRASH_RECOVERY_RUNNING);
}
