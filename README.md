# Nexus OS Scheduler

A university-level operating system simulation demonstrating core OS concepts including Inter-Process Communication (IPC), shared memory, message queues, and multi-threaded synchronization. 

This project simulates how an operating system scheduler dispatches processes (jobs) to available CPU cores (workers) while providing a live visualization of the system's runtime state through a Terminal User Interface (TUI).

## Key Concepts Demonstrated

- **Inter-Process Communication (IPC)**: utilizing System V shared memory and message queues to handle data exchange between strictly isolated processes.
- **Process Synchronization**: implementation of cross-process POSIX mutexes (`PTHREAD_PROCESS_SHARED`) to prevent race conditions during shared memory I/O operations.
- **Concurrency**: autonomous worker processes executing system tasks simultaneously.
- **Process Scheduling**: a master process functioning as a dispatcher, simulating job queuing and priority routing logic.
- **System Monitoring**: live telemetry tracking system health, job turn-around states, wait times, and worker execution progress.

## System Architecture

The environment splits the workload across three main binaries:

1. **Master (`master`)**: The central coordinator. It generates job payloads, manages the Ready Queue, emits system heartbeats, and dispatches workloads to specific workers via message queues.
2. **Workers (`worker`)**: Independent executor processes simulating CPU cores. They block on their specific message queue awaiting jobs, process the assigned "burst time", and continuously update their execution progress in the shared memory segment.
3. **UI Dashboard (`ui`)**: A standalone TUI using ANSI escapes and `ncurses`. It strictly reads from the shared memory segment to render real-time telemetry, queue throughput, and worker states without halting scheduler logic.

### Structural Flow
```text
┌──────────────┐
│   Master     │──┐
└──────────────┘  │    ┌─────────────────────────┐
┌──────────────┐  ├───>│    Shared Memory Segment│ (Global State, Queues, Stats)
│   Worker 1   │──┤    └─────────────────────────┘
└──────────────┘  │
┌──────────────┐  │    ┌─────────────────────────┐
│   Worker 2   │──├───>│ System V Message Queues │ (Targeted Job Dispatching)
└──────────────┘  │    └─────────────────────────┘
┌──────────────┐  │
│     UI       │──┘
└──────────────┘
```

## Build & Requirements

**Prerequisites:**
- Linux/Unix environment (or WSL on Windows)
- `gcc` toolchain
- `make`
- `ncurses` development libraries (e.g., `libncurses5-dev` or `ncurses-devel`)

**Build the binaries:**
```bash
make
```

## Usage

For the full visual experience, launch the core logic and the dashboard in two separate terminal sessions.

**Terminal 1 (Core Engine):**
```bash
make run
```
*Automatically purges leftover IPC resources and initializes the master and worker runtime.*

**Terminal 2 (Dashboard):**
```bash
./ui
```
*Attaches to the active simulation and brings up the live monitoring interface.*

**Teardown:**
Use `Ctrl+C` in Terminal 1 to shut down gracefully. If a hard crash occurs or you need to forcefully scrub the host's IPC allocation (shared memory + MSG queues), run:
```bash
make distclean
```

## License

Developed as a university operating systems lab project. Free to fork, study, and modify.
