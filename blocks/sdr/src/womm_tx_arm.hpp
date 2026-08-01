#ifndef WOMM_TX_ARM_HPP
#define WOMM_TX_ARM_HPP

// womm_tx_arm — the runtime interlock every transmit-capable harness must pass before a
// single sample reaches the air.
//
// WHY THIS IS NOT A BUILD FLAG: a build flag cannot see who is at the keyboard. A licensed
// operator carries the consequences of an emission, so the gate has to be a person, at the
// time of the emission, looking at the parameters that are actually about to be used.
//
// The load-bearing line is the isatty() check. It is what makes transmission structurally
// unreachable from ctest, CI, cron, a pipe, a background job or an ssh session with no tty
// — not a comment asking politely, and not a flag someone can set. There is deliberately
// no --yes, no environment override, and no timeout that proceeds on its own.
//
// Print what was REQUESTED AT RUNTIME, never a compiled default: a banner that shows a
// constant while the radio uses something else has already burned this project once.

#include <chrono>
#include <cstdio>
#include <print>
#include <string>
#include <unistd.h>

namespace womm {

struct TxEmission {
    std::string device;      // how the radio was addressed, not its serial
    double      frequencyHz = 0.;
    double      bandwidthHz = 0.;
    double      gainDb      = 0.;
    std::string antenna;
    double      durationSec = 0.;
    double      dutyCycle   = 1.;
};

// Returns true only if a human at a terminal said yes. Never returns true otherwise.
[[nodiscard]] inline bool txArm(const TxEmission& emission) {
    std::println("");
    std::println("  ┌────────────────────────────────────────────────────────────────┐");
    std::println("  │  ⚠  TRANSMIT ARMING — THIS WILL RADIATE                        │");
    std::println("  └────────────────────────────────────────────────────────────────┘");
    std::println("");
    std::println("    device      {}", emission.device);
    std::println("    frequency   {:.6f} MHz", emission.frequencyHz / 1e6);
    std::println("    bandwidth   {:.6f} MHz", emission.bandwidthHz / 1e6);
    std::println("    TX gain     {:.1f} dB", emission.gainDb);
    std::println("    antenna     {}", emission.antenna);
    std::println("    duration    {:.3f} s", emission.durationSec);
    std::println("    duty cycle  {:.1f} %", emission.dutyCycle * 100.);
    std::println("");
    std::println("    Every value above is what was requested at runtime, not a compiled default.");
    std::println("");
    std::println("    Transmitting without a licence, or outside the terms of one, is a federal");
    std::println("    offence. You are asserting that this emission is authorised on this");
    std::println("    frequency, at this power, from this site.");
    std::println("");

    // The gate. No terminal means no operator, so there is nobody to authorise anything.
    if (isatty(STDIN_FILENO) == 0) {
        std::println(stderr, "    REFUSED: stdin is not a terminal.");
        std::println(stderr, "    A transmit arming decision requires an operator present at a console.");
        std::println(stderr, "    This is why transmission cannot be reached from ctest, CI, a pipe or a");
        std::println(stderr, "    background job, and it is not overridable.");
        std::println("");
        return false;
    }

    std::print("    Press ENTER to transmit, or Ctrl-C to abort: ");
    std::fflush(stdout);
    const int first = std::getchar();
    if (first != '\n' && first != EOF) {
        // Anything other than a bare Enter is treated as a change of mind rather than
        // guessed at.
        std::println("");
        std::println(stderr, "    ABORTED: expected ENTER.");
        return false;
    }
    if (first == EOF) {
        std::println("");
        std::println(stderr, "    ABORTED: end of input.");
        return false;
    }
    std::println("    ARMED.");
    std::println("");
    return true;
}

} // namespace womm

#endif // WOMM_TX_ARM_HPP
