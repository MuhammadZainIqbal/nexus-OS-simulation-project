/*
 * ╔══════════════════════════════════════════════════════════════════════════════╗
 * ║                     OS RESOURCE SCHEDULER - COMMON HEADER                    ║
 * ║                          Single Source of Truth                              ║
 * ╠══════════════════════════════════════════════════════════════════════════════╣
 * ║  Author      : Zain Iqbal                                                    ║
 * ║  Description : Centralized header file containing all shared definitions,   ║
 * ║                data structures, macros, and constants for the OS Resource   ║
 * ║                Scheduler demonstrating IPC, Shared Memory, Message Queues,  ║
 * ║                Multi-threading, and Synchronization concepts.               ║
 * ╚══════════════════════════════════════════════════════════════════════════════╝
 */

#ifndef COMMON_H
#define COMMON_H

/* ═══════════════════════════════════════════════════════════════════════════════
 *                         FEATURE TEST MACROS (MUST BE FIRST!)
 * ═══════════════════════════════════════════════════════════════════════════════
 * These MUST be defined before ANY #include to enable POSIX/GNU extensions.
 * Without these, functions like usleep(), kill(), sigaction() won't be declared.
 */
#define _GNU_SOURCE             /* Enable GNU extensions (superset of POSIX)      */
#define _POSIX_C_SOURCE 200809L /* POSIX.1-2008 compliance                        */
#define _DEFAULT_SOURCE         /* Default definitions (replaces _BSD_SOURCE)     */

/* ═══════════════════════════════════════════════════════════════════════════════
 *                              POSIX HEADERS
 * ═══════════════════════════════════════════════════════════════════════════════ */

#include <stdio.h>          /* Standard I/O operations (printf, fprintf, etc.)    */
#include <stdlib.h>         /* Memory allocation, process control (malloc, exit)  */
#include <string.h>         /* String manipulation functions (memset, strcpy)     */
#include <stdarg.h>         /* Variadic functions (va_list, va_start, va_end)     */
#include <unistd.h>         /* POSIX API (fork, sleep, getpid, etc.)              */
#include <sys/types.h>      /* Data types used in system calls (pid_t, key_t)     */
#include <sys/ipc.h>        /* IPC access structures and constants                */
#include <sys/shm.h>        /* Shared memory operations (shmget, shmat, shmdt)    */
#include <sys/msg.h>        /* Message queue operations (msgget, msgsnd, msgrcv)  */
#include <pthread.h>        /* POSIX threads (mutex, thread creation/sync)        */
#include <time.h>           /* Time functions (time, clock_gettime)               */
#include <signal.h>         /* Signal handling (SIGINT, signal handlers)          */
#include <errno.h>          /* Error number definitions (errno, perror)           */
#include <stdbool.h>        /* Boolean type support (true, false)                 */
#include <stdint.h>         /* Fixed-width integer types (uint32_t, int64_t)      */


/* ═══════════════════════════════════════════════════════════════════════════════
 *                            SYSTEM CONFIGURATION MACROS
 * ═══════════════════════════════════════════════════════════════════════════════ */

/*
 * IPC Keys: Unique identifiers for System V IPC mechanisms.
 * These keys are used by shmget() and msgget() to create/access IPC objects.
 * Convention: Use ftok() in production, but fixed keys for demonstration.
 */
#define SHM_KEY             0x1234      /* Shared Memory segment key              */
#define MSG_KEY             0x5678      /* Message Queue key                      */

/*
 * System Capacity Constants
 */
#define NUM_WORKERS         3           /* Number of worker processes             */
#define MAX_JOBS_IN_QUEUE   20          /* Maximum jobs in ready queue            */
#define MAX_JOB_NAME_LEN    32          /* Maximum length of job name             */

/*
 * Timing Constants (in microseconds and milliseconds)
 */
#define UI_REFRESH_RATE_MS      100     /* UI refresh rate: 100ms (0.1s)          */
#define UI_REFRESH_RATE_US      100000  /* UI refresh rate in microseconds        */
#define HEARTBEAT_INTERVAL_MS   500     /* Master heartbeat interval: 500ms       */
#define HEARTBEAT_TIMEOUT_MS    2000    /* Timeout to detect system hang: 2s      */

/*
 * Job Processing Constants
 */
#define MIN_BURST_TIME      1           /* Minimum burst time (seconds)           */
#define MAX_BURST_TIME      10          /* Maximum burst time (seconds)           */


/* ═══════════════════════════════════════════════════════════════════════════════
 *                           ANSI COLOR CODES FOR UI
 * ═══════════════════════════════════════════════════════════════════════════════
 * These escape codes create a visually immersive terminal experience.
 * Usage: printf(COLOR_RED "Error!" COLOR_RESET);
 */

/* Reset */
#define COLOR_RESET         "\033[0m"   /* Reset all attributes                   */

/* Text Styles */
#define STYLE_BOLD          "\033[1m"   /* Bold text                              */
#define STYLE_DIM           "\033[2m"   /* Dim/faint text                         */
#define STYLE_UNDERLINE     "\033[4m"   /* Underlined text                        */
#define STYLE_BLINK         "\033[5m"   /* Blinking text (terminal dependent)     */

/* Foreground Colors */
#define COLOR_BLACK         "\033[30m"
#define COLOR_RED           "\033[31m"  /* Errors, Critical, High Priority        */
#define COLOR_GREEN         "\033[32m"  /* Success, Running, Active               */
#define COLOR_YELLOW        "\033[33m"  /* Warnings, Medium Priority              */
#define COLOR_BLUE          "\033[34m"  /* Information, Low Priority              */
#define COLOR_MAGENTA       "\033[35m"  /* Special events                         */
#define COLOR_CYAN          "\033[36m"  /* System messages                        */
#define COLOR_WHITE         "\033[37m"  /* Default text                           */

/* Bright/Bold Foreground Colors */
#define COLOR_BRIGHT_RED    "\033[91m"  /* Kernel Panic, System Hang              */
#define COLOR_BRIGHT_GREEN  "\033[92m"  /* Job Complete                           */
#define COLOR_BRIGHT_YELLOW "\033[93m"  /* Job Processing                         */
#define COLOR_BRIGHT_BLUE   "\033[94m"  /* Worker Idle                            */
#define COLOR_BRIGHT_CYAN   "\033[96m"  /* Queue Updates                          */

/* Background Colors */
#define BG_RED              "\033[41m"  /* Critical background                    */
#define BG_GREEN            "\033[42m"  /* Success background                     */
#define BG_YELLOW           "\033[43m"  /* Warning background                     */
#define BG_BLUE             "\033[44m"  /* Info background                        */

/*
 * Priority-specific Color Mappings
 * Visual hierarchy: High=Red (urgent), Medium=Yellow (caution), Low=Blue (calm)
 */
#define PRIORITY_HIGH_COLOR     COLOR_RED           /* Priority 1 - Critical      */
#define PRIORITY_MED_COLOR      COLOR_YELLOW        /* Priority 2 - Normal        */
#define PRIORITY_LOW_COLOR      COLOR_BLUE          /* Priority 3 - Background    */

/*
 * Worker Status Color Mappings
 */
#define STATUS_IDLE_COLOR       COLOR_BRIGHT_BLUE   /* Worker waiting for jobs    */
#define STATUS_BUSY_COLOR       COLOR_BRIGHT_GREEN  /* Worker processing          */
#define STATUS_ERROR_COLOR      COLOR_BRIGHT_RED    /* Worker error state         */


/* ═══════════════════════════════════════════════════════════════════════════════
 *                           UI SYMBOLS AND BOX DRAWING
 * ═══════════════════════════════════════════════════════════════════════════════
 * Unicode box-drawing characters for creating visually appealing UI elements.
 */

/* Box Drawing Characters (Single Line) */
#define BOX_HORIZONTAL      "─"
#define BOX_VERTICAL        "│"
#define BOX_TOP_LEFT        "┌"
#define BOX_TOP_RIGHT       "┐"
#define BOX_BOTTOM_LEFT     "└"
#define BOX_BOTTOM_RIGHT    "┘"
#define BOX_T_DOWN          "┬"
#define BOX_T_UP            "┴"
#define BOX_T_RIGHT         "├"
#define BOX_T_LEFT          "┤"
#define BOX_CROSS           "┼"

/* Box Drawing Characters (Double Line) */
#define DBOX_HORIZONTAL     "═"
#define DBOX_VERTICAL       "║"
#define DBOX_TOP_LEFT       "╔"
#define DBOX_TOP_RIGHT      "╗"
#define DBOX_BOTTOM_LEFT    "╚"
#define DBOX_BOTTOM_RIGHT   "╝"

/* Progress Bar Characters */
#define PROGRESS_FULL       "█"         /* Filled progress block                  */
#define PROGRESS_PARTIAL    "▓"         /* Partially filled block                 */
#define PROGRESS_LIGHT      "░"         /* Empty/light block                      */
#define PROGRESS_EMPTY      " "         /* Empty space                            */

/* Status Indicators */
#define SYMBOL_CHECK        "✓"         /* Success/Complete                       */
#define SYMBOL_CROSS        "✗"         /* Error/Failed                           */
#define SYMBOL_ARROW        "➜"         /* Active/Current                         */
#define SYMBOL_BULLET       "●"         /* Bullet point                           */
#define SYMBOL_CIRCLE       "○"         /* Empty circle                           */
#define SYMBOL_STAR         "★"         /* Highlighted item                       */


/* ═══════════════════════════════════════════════════════════════════════════════
 *                              ENUMERATION TYPES
 * ═══════════════════════════════════════════════════════════════════════════════ */

/*
 * Job Priority Levels
 * Used for scheduling decisions - lower value = higher priority
 * This enables implementation of Priority Scheduling algorithms.
 */
typedef enum {
    PRIORITY_HIGH   = 1,    /* Critical jobs - processed first (Real-time tasks)  */
    PRIORITY_MEDIUM = 2,    /* Normal jobs - standard processing                  */
    PRIORITY_LOW    = 3     /* Background jobs - processed when system is idle    */
} JobPriority;

/*
 * Job State in the System
 * Tracks the lifecycle of each job through the scheduler.
 */
typedef enum {
    JOB_STATE_EMPTY     = 0,    /* Slot is empty/available in the queue           */
    JOB_STATE_WAITING   = 1,    /* Job is in Ready Queue, waiting for CPU         */
    JOB_STATE_RUNNING   = 2,    /* Job is currently being executed by a worker    */
    JOB_STATE_COMPLETED = 3,    /* Job has finished execution                     */
    JOB_STATE_FAILED    = 4     /* Job execution failed (error handling)          */
} JobState;

/*
 * Worker Status
 * Represents the current operational state of each worker process.
 */
typedef enum {
    WORKER_IDLE     = 0,    /* Worker is waiting for jobs from the queue          */
    WORKER_BUSY     = 1,    /* Worker is actively processing a job                */
    WORKER_STARTING = 2,    /* Worker is initializing (startup phase)             */
    WORKER_STOPPING = 3,    /* Worker is shutting down gracefully                 */
    WORKER_ERROR    = 4     /* Worker encountered an error                        */
} WorkerStatus;

/*
 * System State Flags
 * Used by UI to display system health indicators.
 */
typedef enum {
    SYSTEM_OK           = 0,    /* System operating normally                      */
    SYSTEM_WARNING      = 1,    /* Non-critical issue detected                    */
    SYSTEM_HANG         = 2,    /* Master process not responding (no heartbeat)   */
    SYSTEM_PANIC        = 3     /* Critical failure - kernel panic state          */
} SystemHealthStatus;


/* ═══════════════════════════════════════════════════════════════════════════════
 *                              DATA STRUCTURES
 * ═══════════════════════════════════════════════════════════════════════════════ */

/*
 * Job Structure
 * Represents a single unit of work in the scheduler.
 * This is the fundamental data unit passed through the entire system.
 *
 * Memory Layout Considerations:
 * - Aligned for efficient memory access
 * - Fixed-size for predictable shared memory usage
 */
typedef struct {
    int             id;                         /* Unique job identifier          */
    char            name[MAX_JOB_NAME_LEN];     /* Human-readable job name        */
    JobPriority     priority;                   /* Job priority (1=High, 3=Low)   */
    int             burst_time;                 /* Total CPU time required (sec)  */
    int             remaining_time;             /* Time left to complete (sec)    */
    JobState        state;                      /* Current state of the job       */
    time_t          arrival_time;               /* When job entered the system    */
    time_t          start_time;                 /* When execution began           */
    time_t          completion_time;            /* When execution finished        */
    int             assigned_worker;            /* Worker ID processing this job  */
} Job;

/*
 * Message Structure for IPC Message Queues
 * Used to pass jobs from Master to Worker processes.
 *
 * IMPORTANT: The 'mtype' field is REQUIRED for System V message queues.
 * - mtype > 0: Used for selective message reception
 * - Workers can filter messages by type (e.g., worker_id + 1)
 *
 * Message Flow: Master --[msgq]--> Worker
 */
typedef struct {
    long            mtype;                      /* Message type (REQUIRED >= 1)   */
    Job             job;                        /* Job data payload               */
    int             command;                    /* Command code (see below)       */
} Message;

/*
 * Message Command Codes
 * Used in Message.command to specify the action type.
 */
#define MSG_CMD_NEW_JOB     1       /* New job assignment                         */
#define MSG_CMD_SHUTDOWN    2       /* Graceful shutdown command                  */
#define MSG_CMD_STATUS_REQ  3       /* Request status update                      */
#define MSG_CMD_PRIORITY_UP 4       /* Increase job priority (preemption)         */

/*
 * Worker Information Structure
 * Detailed information about each worker for monitoring.
 */
typedef struct {
    pid_t           pid;                        /* Process ID of the worker       */
    WorkerStatus    status;                     /* Current operational status     */
    int             current_job_id;             /* ID of job being processed      */
    int             progress;                   /* Completion percentage (0-100)  */
    int             jobs_completed;             /* Total jobs completed by worker */
    time_t          last_active;                /* Last activity timestamp        */
} WorkerInfo;

/*
 * System State Structure (Shared Memory)
 * ═══════════════════════════════════════════════════════════════════════════════
 * This is the CENTRAL data structure shared between all processes.
 * It resides in shared memory and is accessed by:
 *   - Master Process: Updates heartbeat, manages job queue
 *   - Worker Processes: Update their status and progress
 *   - UI Process: Reads all data for display (every 0.1s)
 *
 * CRITICAL: All access to this structure MUST be protected by mutexes
 *           to prevent race conditions and ensure data consistency.
 *
 * Memory Layout:
 * ┌─────────────────────────────────────────────────────────────────────┐
 * │  Heartbeat & Health Monitoring                                      │
 * ├─────────────────────────────────────────────────────────────────────┤
 * │  Job Queue (Ready Queue) - 20 slots                                 │
 * ├─────────────────────────────────────────────────────────────────────┤
 * │  Worker Status Arrays - 3 workers                                   │
 * ├─────────────────────────────────────────────────────────────────────┤
 * │  Statistics & Counters                                              │
 * ├─────────────────────────────────────────────────────────────────────┤
 * │  Synchronization Primitives (Mutex)                                 │
 * └─────────────────────────────────────────────────────────────────────┘
 */
typedef struct {
    /* ═══════════════════════════════════════════════════════════════════════════
     * SECTION 1: HEARTBEAT & SYSTEM HEALTH MONITORING
     * ═══════════════════════════════════════════════════════════════════════════
     * The Master process updates 'last_heartbeat' every 500ms.
     * If UI detects (current_time - last_heartbeat) > HEARTBEAT_TIMEOUT_MS,
     * it displays "System Hang" or "Kernel Panic" warning.
     */
    time_t              last_heartbeat;         /* Master's heartbeat timestamp   */
    SystemHealthStatus  system_health;          /* Overall system health status   */
    volatile int        running;                /* System running flag (1=yes)    */
    pid_t               master_pid;             /* Master process ID              */

    /* ═══════════════════════════════════════════════════════════════════════════
     * SECTION 2: JOB QUEUE (READY QUEUE)
     * ═══════════════════════════════════════════════════════════════════════════
     * Visual representation of the Ready Queue for the UI.
     * Jobs are added by Master, removed when assigned to Workers.
     * The UI displays this queue in real-time.
     */
    Job                 job_queue[MAX_JOBS_IN_QUEUE];   /* Ready Queue array      */
    int                 queue_head;             /* Front of circular queue        */
    int                 queue_tail;             /* Rear of circular queue         */
    int                 queue_count;            /* Number of jobs in queue        */
    int                 next_job_id;            /* Auto-increment job ID counter  */

    /* ═══════════════════════════════════════════════════════════════════════════
     * SECTION 3: WORKER STATUS TRACKING
     * ═══════════════════════════════════════════════════════════════════════════
     * Real-time status of all worker processes.
     * Updated by workers themselves, read by UI for visualization.
     *
     * Parallel arrays indexed by worker_id (0 to NUM_WORKERS-1):
     *   worker_status[i]   - Current status enum
     *   current_job_id[i]  - Job ID being processed (-1 if idle)
     *   worker_progress[i] - Completion percentage (0-100)
     */
    WorkerStatus        worker_status[NUM_WORKERS];     /* Status of each worker  */
    int                 current_job_id[NUM_WORKERS];    /* Current job per worker */
    int                 worker_progress[NUM_WORKERS];   /* Progress (0-100%)      */
    pid_t               worker_pids[NUM_WORKERS];       /* PIDs of worker procs   */
    int                 worker_jobs_done[NUM_WORKERS];  /* Jobs done per worker   */

    /* ═══════════════════════════════════════════════════════════════════════════
     * SECTION 4: GLOBAL STATISTICS
     * ═══════════════════════════════════════════════════════════════════════════
     * System-wide counters for performance monitoring and UI display.
     */
    int                 total_jobs_submitted;   /* Total jobs entered system      */
    int                 total_jobs_done;        /* Total jobs completed           */
    int                 total_jobs_failed;      /* Total jobs that failed         */
    double              avg_wait_time;          /* Average waiting time           */
    double              avg_turnaround_time;    /* Average turnaround time        */
    time_t              system_start_time;      /* When scheduler started         */

    /* ═══════════════════════════════════════════════════════════════════════════
     * SECTION 5: SYNCHRONIZATION PRIMITIVES
     * ═══════════════════════════════════════════════════════════════════════════
     * CRITICAL: These mutexes MUST be initialized with PTHREAD_PROCESS_SHARED
     *           attribute to work across multiple processes.
     *
     * Mutex Usage:
     *   log_lock        - Protects console logging (prevents interleaved output)
     *   queue_lock      - Protects job_queue modifications
     *   stats_lock      - Protects statistics updates
     *
     * Initialization Example:
     *   pthread_mutexattr_t attr;
     *   pthread_mutexattr_init(&attr);
     *   pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
     *   pthread_mutex_init(&state->log_lock, &attr);
     */
    pthread_mutex_t     log_lock;               /* Cross-process logging mutex    */
    pthread_mutex_t     queue_lock;             /* Job queue access mutex         */
    pthread_mutex_t     stats_lock;             /* Statistics update mutex        */

} SystemState;


/* ═══════════════════════════════════════════════════════════════════════════════
 *                              UTILITY MACROS
 * ═══════════════════════════════════════════════════════════════════════════════ */

/*
 * Safe Minimum/Maximum Macros
 * Avoid side effects by not evaluating arguments multiple times.
 */
#define MIN(a, b)   ((a) < (b) ? (a) : (b))
#define MAX(a, b)   ((a) > (b) ? (a) : (b))

/*
 * Progress Bar Width for UI Display
 */
#define PROGRESS_BAR_WIDTH  30

/*
 * Logging Macros with Timestamps and Colors
 * These provide consistent, colored logging across all system components.
 *
 * Usage:
 *   LOG_INFO("Server started on port %d", port);
 *   LOG_ERROR("Failed to allocate memory");
 *   LOG_WORKER(1, "Processing job %d", job_id);
 */
#define LOG_TIMESTAMP() do { \
    time_t _t = time(NULL); \
    struct tm *_tm = localtime(&_t); \
    printf("[%02d:%02d:%02d] ", _tm->tm_hour, _tm->tm_min, _tm->tm_sec); \
} while(0)

#define LOG_INFO(fmt, ...) do { \
    LOG_TIMESTAMP(); \
    printf(COLOR_CYAN "[INFO] " COLOR_RESET fmt "\n", ##__VA_ARGS__); \
} while(0)

#define LOG_SUCCESS(fmt, ...) do { \
    LOG_TIMESTAMP(); \
    printf(COLOR_GREEN "[SUCCESS] " COLOR_RESET fmt "\n", ##__VA_ARGS__); \
} while(0)

#define LOG_WARNING(fmt, ...) do { \
    LOG_TIMESTAMP(); \
    printf(COLOR_YELLOW "[WARNING] " COLOR_RESET fmt "\n", ##__VA_ARGS__); \
} while(0)

#define LOG_ERROR(fmt, ...) do { \
    LOG_TIMESTAMP(); \
    printf(COLOR_RED "[ERROR] " COLOR_RESET fmt "\n", ##__VA_ARGS__); \
} while(0)

#define LOG_MASTER(fmt, ...) do { \
    LOG_TIMESTAMP(); \
    printf(COLOR_MAGENTA "[MASTER] " COLOR_RESET fmt "\n", ##__VA_ARGS__); \
} while(0)

#define LOG_WORKER(id, fmt, ...) do { \
    LOG_TIMESTAMP(); \
    printf(COLOR_BLUE "[WORKER-%d] " COLOR_RESET fmt "\n", id, ##__VA_ARGS__); \
} while(0)

#define LOG_UI(fmt, ...) do { \
    LOG_TIMESTAMP(); \
    printf(COLOR_BRIGHT_CYAN "[UI] " COLOR_RESET fmt "\n", ##__VA_ARGS__); \
} while(0)

/*
 * Screen Control Macros
 * For creating dynamic, updating terminal displays.
 */
#define CLEAR_SCREEN()      printf("\033[2J\033[H")     /* Clear & home cursor    */
#define CURSOR_HOME()       printf("\033[H")           /* Move cursor to home    */
#define CURSOR_HIDE()       printf("\033[?25l")        /* Hide cursor            */
#define CURSOR_SHOW()       printf("\033[?25h")        /* Show cursor            */
#define CURSOR_MOVE(r, c)   printf("\033[%d;%dH", r, c) /* Move to row, col      */
#define CLEAR_LINE()        printf("\033[2K")          /* Clear current line     */

/*
 * Priority to Color String Conversion
 * Returns the appropriate ANSI color code for a given priority level.
 */
#define PRIORITY_TO_COLOR(p) \
    ((p) == PRIORITY_HIGH ? PRIORITY_HIGH_COLOR : \
     (p) == PRIORITY_MEDIUM ? PRIORITY_MED_COLOR : PRIORITY_LOW_COLOR)

/*
 * Priority to String Conversion
 */
#define PRIORITY_TO_STRING(p) \
    ((p) == PRIORITY_HIGH ? "HIGH" : \
     (p) == PRIORITY_MEDIUM ? "MED " : "LOW ")

/*
 * Worker Status to String Conversion
 */
#define WORKER_STATUS_TO_STRING(s) \
    ((s) == WORKER_IDLE ? "IDLE" : \
     (s) == WORKER_BUSY ? "BUSY" : \
     (s) == WORKER_STARTING ? "INIT" : \
     (s) == WORKER_STOPPING ? "STOP" : "ERR!")

/*
 * Worker Status to Color Conversion
 */
#define WORKER_STATUS_TO_COLOR(s) \
    ((s) == WORKER_IDLE ? STATUS_IDLE_COLOR : \
     (s) == WORKER_BUSY ? STATUS_BUSY_COLOR : STATUS_ERROR_COLOR)


/* ═══════════════════════════════════════════════════════════════════════════════
 *                          FUNCTION PROTOTYPES
 * ═══════════════════════════════════════════════════════════════════════════════
 * Common utility functions that may be implemented in a separate common.c file.
 * These are declared here for use across all system components.
 */

/*
 * Shared Memory Management
 */
static inline SystemState* attach_shared_memory(int shmid) {
    SystemState *state = (SystemState *)shmat(shmid, NULL, 0);
    if (state == (void *)-1) {
        perror("shmat failed");
        return NULL;
    }
    return state;
}

static inline void detach_shared_memory(SystemState *state) {
    if (state != NULL && state != (void *)-1) {
        shmdt(state);
    }
}

/*
 * Mutex Initialization for Cross-Process Sharing
 * IMPORTANT: Call this once during system initialization.
 *
 * Returns: 0 on success, -1 on failure
 */
static inline int init_shared_mutex(pthread_mutex_t *mutex) {
    pthread_mutexattr_t attr;

    if (pthread_mutexattr_init(&attr) != 0) {
        perror("pthread_mutexattr_init failed");
        return -1;
    }

    /* CRITICAL: Set PTHREAD_PROCESS_SHARED for cross-process synchronization */
    if (pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED) != 0) {
        perror("pthread_mutexattr_setpshared failed");
        pthread_mutexattr_destroy(&attr);
        return -1;
    }

    if (pthread_mutex_init(mutex, &attr) != 0) {
        perror("pthread_mutex_init failed");
        pthread_mutexattr_destroy(&attr);
        return -1;
    }

    pthread_mutexattr_destroy(&attr);
    return 0;
}

/*
 * Safe Mutex Lock with Timeout Detection
 * Prevents deadlocks by logging long waits.
 */
static inline void safe_mutex_lock(pthread_mutex_t *mutex, const char *name) {
    int result = pthread_mutex_lock(mutex);
    if (result != 0) {
        fprintf(stderr, COLOR_RED "[MUTEX ERROR] Failed to lock %s: %s\n" COLOR_RESET,
                name, strerror(result));
    }
}

static inline void safe_mutex_unlock(pthread_mutex_t *mutex, const char *name) {
    int result = pthread_mutex_unlock(mutex);
    if (result != 0) {
        fprintf(stderr, COLOR_RED "[MUTEX ERROR] Failed to unlock %s: %s\n" COLOR_RESET,
                name, strerror(result));
    }
}

/*
 * Time Difference Calculation (in milliseconds)
 * Used for heartbeat timeout detection.
 */
static inline long time_diff_ms(time_t start, time_t end) {
    return (long)difftime(end, start) * 1000;
}

/*
 * Get Current Time as Formatted String
 * Returns a static buffer - not thread-safe, use only for display.
 */
static inline const char* get_time_string(void) {
    static char buffer[32];
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    strftime(buffer, sizeof(buffer), "%H:%M:%S", tm_info);
    return buffer;
}

/*
 * Generate Progress Bar String
 * Creates a visual progress bar for UI display.
 *
 * Parameters:
 *   progress - Percentage complete (0-100)
 *   width    - Width of the progress bar in characters
 *
 * Returns: Static buffer containing the progress bar string
 */
static inline const char* generate_progress_bar(int progress, int width) {
    static char bar[128];
    int filled = (progress * width) / 100;
    int empty = width - filled;

    int pos = 0;
    bar[pos++] = '[';

    for (int i = 0; i < filled && pos < 126; i++) {
        bar[pos++] = '=';
    }

    if (filled < width && pos < 126) {
        bar[pos++] = '>';
    }

    for (int i = 0; i < empty - 1 && pos < 126; i++) {
        bar[pos++] = ' ';
    }

    bar[pos++] = ']';
    bar[pos] = '\0';

    return bar;
}

/*
 * Initialize System State to Default Values
 * Should be called once by the Master process after creating shared memory.
 */
static inline void init_system_state(SystemState *state) {
    /* Initialize heartbeat and health */
    state->last_heartbeat = time(NULL);
    state->system_health = SYSTEM_OK;
    state->running = 1;
    state->master_pid = getpid();

    /* Initialize job queue */
    memset(state->job_queue, 0, sizeof(state->job_queue));
    state->queue_head = 0;
    state->queue_tail = 0;
    state->queue_count = 0;
    state->next_job_id = 1;

    /* Initialize worker arrays */
    for (int i = 0; i < NUM_WORKERS; i++) {
        state->worker_status[i] = WORKER_IDLE;
        state->current_job_id[i] = -1;
        state->worker_progress[i] = 0;
        state->worker_pids[i] = 0;
        state->worker_jobs_done[i] = 0;
    }

    /* Initialize statistics */
    state->total_jobs_submitted = 0;
    state->total_jobs_done = 0;
    state->total_jobs_failed = 0;
    state->avg_wait_time = 0.0;
    state->avg_turnaround_time = 0.0;
    state->system_start_time = time(NULL);

    /* Initialize mutexes with PTHREAD_PROCESS_SHARED attribute */
    init_shared_mutex(&state->log_lock);
    init_shared_mutex(&state->queue_lock);
    init_shared_mutex(&state->stats_lock);
}


/* ═══════════════════════════════════════════════════════════════════════════════
 *                              ERROR HANDLING
 * ═══════════════════════════════════════════════════════════════════════════════ */

/*
 * Error Codes for System Operations
 */
#define ERR_SHM_CREATE      -1      /* Failed to create shared memory             */
#define ERR_SHM_ATTACH      -2      /* Failed to attach shared memory             */
#define ERR_MSG_CREATE      -3      /* Failed to create message queue             */
#define ERR_MSG_SEND        -4      /* Failed to send message                     */
#define ERR_MSG_RECV        -5      /* Failed to receive message                  */
#define ERR_FORK            -6      /* Failed to fork process                     */
#define ERR_MUTEX           -7      /* Mutex operation failed                     */
#define ERR_QUEUE_FULL      -8      /* Job queue is full                          */
#define ERR_QUEUE_EMPTY     -9      /* Job queue is empty                         */

/*
 * Error Handling Macro
 * Prints error and exits if condition is true.
 */
#define CHECK_ERROR(cond, msg) do { \
    if (cond) { \
        perror(msg); \
        exit(EXIT_FAILURE); \
    } \
} while(0)


#endif /* COMMON_H */

/*
 * ╔══════════════════════════════════════════════════════════════════════════════╗
 * ║                              END OF FILE                                     ║
 * ╠══════════════════════════════════════════════════════════════════════════════╣
 * ║  This header file is the foundation of the OS Resource Scheduler.           ║
 * ║  All processes (Master, Workers, UI) must include this file.                ║
 * ║                                                                              ║
 * ║  Compilation Example:                                                        ║
 * ║    gcc -o master master.c -lpthread -lrt                                    ║
 * ║    gcc -o worker worker.c -lpthread -lrt                                    ║
 * ║    gcc -o ui ui.c -lpthread -lrt                                            ║
 * ║                                                                              ║
 * ║  Memory Map:                                                                 ║
 * ║    ┌──────────────┐                                                          ║
 * ║    │   Master     │──┐                                                       ║
 * ║    └──────────────┘  │    ┌───────────────────┐                              ║
 * ║    ┌──────────────┐  ├───>│  Shared Memory    │                              ║
 * ║    │   Worker 1   │──┤    │  (SystemState)    │                              ║
 * ║    └──────────────┘  │    └───────────────────┘                              ║
 * ║    ┌──────────────┐  │                                                       ║
 * ║    │   Worker 2   │──┤    ┌───────────────────┐                              ║
 * ║    └──────────────┘  ├───>│  Message Queue    │                              ║
 * ║    ┌──────────────┐  │    │  (Job Dispatch)   │                              ║
 * ║    │   Worker 3   │──┤    └───────────────────┘                              ║
 * ║    └──────────────┘  │                                                       ║
 * ║    ┌──────────────┐  │                                                       ║
 * ║    │     UI       │──┘                                                       ║
 * ║    └──────────────┘                                                          ║
 * ╚══════════════════════════════════════════════════════════════════════════════╝
 */
