# The RTOS model: CMSIS-RTOS v1 on RTX 4 behaviour

Latasim runs CMSIS-RTOS v1 firmware on the host against a deterministic model of the Keil RTX 4 kernel (`src/rtos/kernel.hpp`). It is a behavioural model, not RTX itself:
- **What it keeps:** what firmware can observe matches, meaning which thread runs when, call results, and wake-up order.
- **What it drops:** RTX's memory layout, stacks, SVC and PendSV mechanics are not reproduced.

## Sources

The scheduling rules are taken from RTX 4.82's own sources in the CMSIS 5.8.0 pack (`CMSIS/RTOS/RTX/SRC`), function by function:

| RTX source | Model |
|---|---|
| `rt_List.c` | `put_prio` (priority order; equals in arrival order), `put_rdy_first`, `get_first`, the delay list |
| `rt_Task.c` | `dispatch`, `block`, `rt_tsk_pass` (yield), `rt_tsk_prio` (set priority), `rt_tsk_create`/`delete` |
| `rt_Robin.c` | round-robin on the system tick |
| `rt_System.c` | `rt_systick`, `rt_pop_req` (work queued by interrupts, run where PendSV runs) |
| `rt_Event.c` | signal flags |
| `rt_Mutex.c` | mutexes, priority inheritance and its restoration |
| `rt_Time.c`, `rt_CMSIS.c` | `osDelay`, millisecond-to-tick conversion, timers and the timer thread, return values |
| `RTX_CM_lib.h`, `RTX_Conf_CM.c` | start-up (main as a thread), configuration defaults |

Firmware compiles against RTX's own `cmsis_os.h` from the pack, so types, macros (`osThreadDef`, `osTimerDef`, `osMutexDef`) and constants are the real ones.

## Supported calls

| Area | Calls |
|---|---|
| Kernel | `osKernelInitialize`, `osKernelStart`, `osKernelRunning`, `osKernelSysTick` |
| Threads | `osThreadCreate`, `osThreadGetId`, `osThreadTerminate`, `osThreadYield`, `osThreadSetPriority`, `osThreadGetPriority`; returning from a thread function terminates it |
| Time | `osDelay` |
| Signals | `osSignalSet` (from threads and interrupt handlers), `osSignalClear`, `osSignalWait` (all-of or any-of, with timeout or `osWaitForever`) |
| Mutexes | `osMutexCreate`, `osMutexWait` (with timeout), `osMutexRelease`, `osMutexDelete`; recursive; priority inheritance |
| Timers | `osTimerCreate` (periodic, one-shot), `osTimerStart`, `osTimerStop`, `osTimerDelete`; callbacks on the timer thread |
| Latasim | `latasim_consume_us`, `latasim_consume_cycles` (`latasim_rtos.h`): modelled processor time |

**Not supported:**
- semaphores, memory pools, message queues and mail queues;
- `osWait`;
- `os_suspend`/`os_resume`.

Each of these stops the calling thread with an error naming the call, which reaches the caller of `run_until`. It never returns a value that firmware might misread.

## Scheduling semantics

**Priorities and threads**
- **Priorities:** RTX levels, where the idle demon is 0 and `osPriorityIdle`…`osPriorityRealtime` are 1–7.
- **Start-up:** `start_main(main)` does what RTX's start-up does: initialise the kernel, create `main` as a thread at `osPriorityNormal`, and start. The timer thread runs at `OS_TIMERPRIO` (default `osPriorityHigh`).
- **Main's own initialise:** when main calls `osKernelInitialize` again, it is boosted until `osKernelStart`, so creating threads never preempts it. That second start reprograms SysTick, as RTX's does.

**Ready list and switching**
- **Ready list:** highest priority first, equal priorities in arrival order.
- **Preemption:** a thread readied above the running one preempts it at once. The preempted thread goes back to the *front* of its level.
- **Readying at equal or lower priority:** the thread goes to the *back* of its level.
- **Round-robin (`OS_ROBIN`, `OS_ROBINTOUT`):** at each tick, the thread at the head of the ready list after the running one is put back has its time slice checked, exactly as `rt_chk_robin` does. The first slice is counted from the first tick the thread sees.
- **Yield:** passes the processor only to a ready thread of the *same* priority. Otherwise the caller keeps running.

**Delays and timeouts**
- **Delays:** milliseconds are converted to ticks rounded up (`rt_ms2tick`), and a delay of n ticks ends at the n-th tick.
- **Simultaneous wake-ups:** threads whose delays end on the same tick wake latest-delayed first (`rt_put_dly` inserts before equals).
- **Timed waits** (signals, mutexes) use the same list.

**Signals**
- **Flags:** 16 per thread.
- **Waiting:** a wait for given flags needs all of them. A wait for 0 takes any flag and returns the flags that came.
- **From an interrupt handler:** `osSignalSet` takes effect after the handler returns, where PendSV would run.

**Mutexes**
- **Inheritance:** a thread waiting for a mutex raises its owner to its own priority, if that is higher.
- **Restoring:** releasing restores the owner to its base priority, or to the highest first waiter of a mutex it still owns.
- **Handover:** ownership passes to the highest-priority waiter.
- **Limits:** only direct inheritance, as in RTX (`rt_mut_wait`), with no chain through several owners. A waiter that times out does not undo the raise.

**Raw priority inversion**
- **The kernel does not see it:** firmware that protects a resource with its own flag gets no inheritance, so the classic inversion appears (`firmware/rtos/realtime.c`, mode 0).
- **The fixes:** explicit elevation with `osThreadSetPriority` (mode 1), or an RTX mutex (mode 2).

**Timers**
- **Firing:** periods are in ticks, rounded up. An expired timer is a message to the timer thread, which calls its function.
- **Restarting a running timer:** keeps its old period, as `svcTimerStart` does.
- **Queue overflow:** the queue holds `OS_TIMERCBQS` messages. Overflowing it is an error, as `os_error(OS_ERR_TIMER_OVF)` is.

**Idle**
- When no thread is ready, the idle demon "runs": virtual time advances to the next event, with no host CPU spent. Its time is `idle_cycles()`.
- A firmware `os_idle_demon` is never called.

## Time

- **One clock:** RTOS time derives from the machine's virtual time, so there is no wall clock.
- **The tick:** it is SysTick, programmed as `rt_systick_init` does (RELOAD = period − 1, CURRENT = 0, CTRL = 7, lowest priority). Its period (`Config::tick_period`, `os_trv + 1`) and the tick length used for millisecond conversions (`Config::tick_us`, `OS_TICK`) are set separately, as `OS_CLOCK` and `OS_TICK` are in `RTX_Conf_CM.c`. The default is 10 ms at 100 MHz.
- **Processor time:** host-compiled code takes none. A thread uses processor time only in two ways:
  - **`latasim_consume_*`:** modelled computation. The thread can be preempted part way, and it returns when all of it has been used.
  - **`Config::call_cycles`:** the cost of each kernel call, zero by default. Set it for firmware that does nothing but call the kernel, such as a yield loop. With it at 0, such firmware would run forever at one instant, and the kernel reports that as an error after `max_zero_time_switches`.
- **A plain busy loop never lets virtual time pass**, so it hangs the host thread. It cannot be preempted: host code has no instructions to interrupt.

## Execution

**Fibers**
- **One per thread.** Each thread runs on a Win32 fiber (`src/rtos/fiber.hpp`). Only the kernel resumes fibers, so the host operating system never chooses what runs.
- **When a thread gives up the processor:**
  - in a kernel call that blocks or readies a higher thread;
  - in `consume`, when a tick or interrupt switches threads;
  - after a register store whose interrupt readied a higher thread (the store hook, `host/rtos_binding.hpp`).

**Interrupts and ticks**
- **Handlers:** they run as before (Phase 4), on the host stack.
- **Tick processing:** it runs from the SysTick handler, and interrupt-queued work runs in the thread-mode hook.

**Faults**
- **A fault inside a thread** stops that thread's fiber and is thrown from `run_until`. Faults include an unmapped register, an unsupported call and a kernel error.
- **The fiber is abandoned, not unwound,** so no exception crosses C frames.

## Inspection

`Kernel::threads()`, `thread(id)`, `running_thread()`, `mutexes()` and `timers()` give read-only views.

`timeline()` gives the run intervals: which thread ran from which cycle to which. `idle_cycles()` and `call_cycles_total()` give utilisation.

Thread, mutex and timer identities are small stable numbers, never pointers.

**RTOS trace events** (`TraceKind::Rtos`) record these, each with its virtual time, the thread's number and its priority:
- kernel start, create, run, preempt, yield, delay, wake, terminate;
- signal set, clear, wait, wake;
- mutex acquire, block, release;
- priority change, inherit, restore;
- timer start, stop, fire, callback.

## Differences from real RTX

- **Kernel calls take no processor time** unless `call_cycles` is set; there is no measured figure for RTX's SVC path yet.
- **Stacks are host stacks:** there is no stack overflow detection, `OS_STKSIZE`, `OS_TASKCNT` or private-stack limit.
- **No `os_error` hook:** errors are reported to the caller of `run_until`.
- **Interrupt-set signals** are handled after all pending handlers, where PendSV (lowest priority) runs. There is no FIFO overflow (`OS_FIFOSZ`).
- **Stock `os_idle_demon` busy loops cannot run** (see Time).

Evidence limits and open questions: [open-questions.md](open-questions.md).
