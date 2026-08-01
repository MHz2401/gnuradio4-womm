# NEXT STEPS: Start-of-Session DESIGN

**_Answer:_** SoapyUHD with a re-designed SoapySDR that will accommodate the 
spirit-and-intent of gnuradio4 `Blocks` as illustrated by the `LifeCycle.hpp` template class
and exemplified by the `HttpBlock` class. Experience shows that there are reasons 
to build UHD, SoapySDR, and SoapyUHD from source rather than brew, all of which
reduce to 'completeness.'  Brew UHD doesn't include the full driver and SDK options, so many useful diagnostics, 
python/C/C++ examples, 'extended' utilities, and env files - that can save you time - are
missing.  Some included utilities, such as the firmware updater and library are not in the standard brew path, and 
env that would othewise help find it are not set.  It's a similar story with Soapy, which is actually quite rich in 
this area (covers HackRF, which we can also test in multi-radio config).

**_Here are the intended Next Steps_**, presented in a context that should
clarify the interpretation of `HANDOFF.md`:  
 
### Both of the following statements are true:

- **_Any code currently_** in **_the `gnuradio4*` repos_** matching 
  `[S|s]oapy*\.?pp` is unusable for WoMM objectives, emphatically including 
  matching code we've worked on.  In particular, WoMM objectives for which
  the code is unusable apply to any user running multiple TX/RX radios in 
  sync that need to perform significant 'boot-and-sync' operations on startup, 
  which covers many current and potential users and use-cases. For example, one of our earier iterations achieved 
  full-throughput with four B210 devices using about 25% CPU on 16P/8E 
  cores: it could run for an arbitrary amount of time without overflow 
  or underflow. That implementaion made few-if-any changes to the SoapySDR 
  code. However, that code started and ran driver instances *in separate processes*, which breaks 
  the `gnuradio4` execution model.  Furthemore, it's difficult
  to map parameters of UHD to SoapySDR, trial-and-error can damage equipment, 
  and the SoapyUHD wrapper has not been tried in gnuradio4 by anyone we 
  know of.

- The Next Step is to build a block using SoapyUHD 'from scratch', which 
  necessarily means building SoapySDR from scratch.  As part of building
  a block, we need to modify SoapySDR architecture to allow each
  operation in `LifeCycle.hpp`, including the driver's response to `.start()`, to run 
  as its own non-blocking thread.  A good example of a block that does this
  is the `HttpBlock`. Because `gnuradio.org` publicly 
  states that `SoapySDR` is its basis for **all** gnuradio4 SDR blocks, 
  trying SoapyUHD prior to building a pure-UHD block from scratch maximizes
  the chance of future compatibility with gnuradio4 as it is upgraded over
  time.  The `LifeCycle.hpp` template class illustrates that our desired behavior is aligned with the stated 
  architectural goal for Blocks, Flowgraphs, and Schedulers in gnuradio4.

### Note: Brew vs. Build-from-Source 

- It's non-obvious, 

## Code Extract: `LifeCycle.hpp` 

**Exhibit A:** How the intended state machine works.  A 'proper' block (my term) 
should implement the spirit-and-intent of this diagram with `button-semantics`, 
as exemplified by pushing a button to call an elevator:

1. Push the elevator button to make it come to your floor.  
2. Pushing again is not helpful.  
3. Holding down the button does not make the elevator go faster.  
4. The button does immobilize you until the elevator arrives.

I believe that the template code in `LifeCycle.hpp` implies that the flowgraph,
user-action, and any controller or peer entity should not be blocked when 
initiating a state-changing input action to the block-as-state-machine. 
If the initiating entitty 'wants to wait' for state transition to complete, 
it should wait, and the block should indicate in-progress transition so the
initiator can poll.   

Furthermore, I could be wrong about the implications of `LifeCycle.hpp`, but
I am certain that _this is how I want and SDR block for Ettus and HackRF to 
Work on My Mac_.

```hpp
namespace gr::lifecycle {
/**
 * @enum lifecycle::State enumerates the possible states of a `Scheduler` lifecycle.
 *
 * Transition between the following states is triggered by specific actions or events:
 * - `IDLE`: The initial state before the scheduler has been initialized.
 * - `INITIALISED`: The scheduler has been initialized and is ready to start running.
 * - `RUNNING`: The scheduler is actively running.
 * - `REQUESTED_PAUSE`: A pause has been requested, and the scheduler is in the process of pausing.
 * - `PAUSED`: The scheduler is paused and can be resumed or stopped.
 * - `REQUESTED_STOP`: A stop has been requested, and the scheduler is in the process of stopping.
 * - `STOPPED`: The scheduler has been stopped and can be reset or re-initialized.
 * - `ERROR`: An error state that can be reached from any state at any time, requiring a reset.
 *
 * @note All `Block<T>`-derived classes can optionally implement any subset of the lifecycle methods
 * (`start()`, `stop()`, `reset()`, `pause()`, `resume()`) to handle state changes of the `Scheduler`.
 *
 * State diagram:
 *
 *                 Block<T>()              can be reached from
 *                    │                   anywhere and anytime.
 *              ┌─────┴────┐                   ┌────┴────┐
 * ┌────────────┤   IDLE   │                   │  ERROR  │
 * │            └────┬─────┘                   └────┬────┘
 * │                 │ init()                       │ reset()
 * │                 v                              │
 * │         ┌───────┴───────┐                      │
 * ├<────────┤  INITIALISED  ├<─────────────────────┤
 * │         └───────┬───────┘                      │
 * │                 │ start()                      │
 * │                 v                              │
 * │   stop() ┌──────┴──────┐                       │  ╓
 * │ ┌────────┤   RUNNING   ├<──────────┐           │  ║
 * │ │        └─────┬───────┘           │           │  ║
 * │ │              │ pause()           │           │  ║  isActive(lifecycle::State) ─> true
 * │ │              v                   │ resume()  │  ║
 * │ │    ┌─────────┴─────────┐   ┌─────┴─────┐     │  ║
 * │ │    │  REQUESTED_PAUSE  ├──>┤  PAUSED   │     │  ║
 * │ │    └──────────┬────────┘   └─────┬─────┘     │  ╙
 * │ │               │ stop()           │ stop()    │
 * │ │               v                  │           │
 * │ │     ┌─────────┴────────┐         │           │  ╓
 * │ └────>┤  REQUESTED_STOP  ├<────────┘           │  ║
 * │       └────────┬─────────┘                     │  ║
 * │                │                               │  ║  isShuttingDown(lifecycle::State) ─> true
 * │                v                               │  ║
 * │          ┌─────┴─────┐ reset()                 │  ║
 * └─────────>│  STOPPED  ├─────────────────────────┘  ║
 *            └─────┬─────┘                            ╙
 *                  │
 *                  v
 *              ~Block<T>()
 */
enum class State : char { IDLE, INITIALISED, RUNNING, REQUESTED_PAUSE, PAUSED, REQUESTED_STOP, STOPPED, ERROR };
using enum State;

} // namespace gr::lifecycle

```

### Contrast: SDR Block

The current 'SDR block' and associated examples are **code-slop** designed to
'have the right property and method names' such that it will:

(1) pass QA tests, and

(2) **_spoof_** the runtime graph into letting it `start()`.

For us, this literally results in a `non-starter`.  All of the gnuradio4 
block-and-driver slop is based on `SoapySDR`, but slop can be written 
with **_any_** driver.  

We probably need to modify the SoapyUHD and/or the SoapySDR drivers so that
they don't block in the gnuradio4 architecture.  
