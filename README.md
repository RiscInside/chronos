# Chronos

Virtual time executor that works fully in userspace.

### Components

#### Completed

- [x] [chronosrt](chronosrt) - static library that implements core executor functionality and provides methods to control passage of virtual time, along with some other utilities (querying virtual time, critical sections to disable preemption, etc)

#### Planned

- [ ] [chronos-sabre]() - SaBRe plugin for Chronos
- [ ] [llvmchronos]() - LLVM plugin for rewriting LLVM bitcode to insert chronosrt calls

### Implementation details

#### Scheduling

The scheduling algorithm roughly resembles that presented in [Timekeeper](https://dl.acm.org/doi/abs/10.1145/2601381.2601395): each round we assign threads to CPUs, calculate how long they should run for ([chronosrt](chronosrt) aims to run each selected thread for `vruntime_step` nanoseconds in each round), and wait for all threads to compute. Similarly, [chronosrt](chronosrt) also uses `SCHED_FIFO` real-time scheduling policy to achieve more accurate results.

The important difference, however, is that [chronosrt](chronosrt) is a fully in-userspace executor, meaning that there is no need to load a kernel module/modify kernel code to run it. POSIX realtime timers and `SIGEV_THREAD_ID` notifications are used in conjuction to interrupt threads after a given period of time. A special scheduling thread keeps track of virtual runtime of all running threads and select ones with smallest vruntimes for every round.

`chronosrt_set_tdf()` function can be used to set the time dilation factor. Updating timer on every such call can be a bit expensive. Instead, scheduler keeps track of the average TDF over last round. The scheduler assumes that average TDF in the next round will be relatively close to that in the last round and makes use of this assumption to pick how long the thread should run for. Additionally, scheduler makes sure that threads that are too far ahead in virtual time (`2.0 * vruntime_step` ahead as of now) don't get selected until other threads catch up

Obviously, this assumption may not hold in practice. [chronosrt](chronosrt) tries to mitigate this by checking vruntime on some API calls (e.g. critical section exists, jumps, etc). 

#### Critical sections

Sometimes there is a need to disable preemption. In particular, we don't want long-running IO operations to be interrupted by frequent timer signals. This is achieved with critical sections that have the following API:

```c
// Enter critical section (disable rescheduling). Returns critical section id that is to be passed to
// chronosrt_exit_critical
int chronosrt_enter_critical();

// Exit critical section (enable rescheduling). Accepts critical section id from chronosrt_enter_critical
void chronosrt_exit_critical(int section_id);
```
`section_id` is used to handle nested critical sections

Masking out timer signals would be too expensive, as it requires a system call. Instead, two boolean flags are used to communicate critical section information: one flag is used to specify if preemption is disabled and another flag is used to communicate if timer interrupt ran over the course of critical section


### References

- Jereme Lamps, David M. Nicol, and Matthew Caesar. 2014. TimeKeeper: a lightweight virtual time system for linux. In Proceedings of the 2nd ACM SIGSIM Conference on Principles of Advanced Discrete Simulation (SIGSIM PADS '14). Association for Computing Machinery, New York, NY, USA, 179–186. https://doi.org/10.1145/2601381.2601395
- Arras, PA., Andronidis, A., Pina, L. et al. SaBRe: load-time selective binary rewriting. Int J Softw Tools Technol Transfer 24, 205–223 (2022). https://doi.org/10.1007/s10009-021-00644-w
