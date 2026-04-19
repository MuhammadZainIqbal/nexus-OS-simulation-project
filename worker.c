/*
 * ╔══════════════════════════════════════════════════════════════════════════════╗
 * ║                    NEXUS OS SCHEDULER - WORKER PROCESS                       ║
 * ║                         The Executor Engine                                  ║
 * ╠══════════════════════════════════════════════════════════════════════════════╣
 * ║  Author      : Zain Iqbal                                                    ║
 * ║  Description : Worker process that consumes jobs from the IPC Message Queue ║
 * ║                and executes them using POSIX threads. Demonstrates the key  ║
 * ║                difference between Processes and Threads in OS design.       ║
 * ╚══════════════════════════════════════════════════════════════════════════════╝
 *
 * ╔══════════════════════════════════════════════════════════════════════════════╗
 * ║                    VIVA-READY: PROCESS vs THREAD EXPLANATION                 ║
 * ╠══════════════════════════════════════════════════════════════════════════════╣
 * ║                                                                              ║
 * ║  WORKER PROCESS (this file):                                                 ║
 * ║  ─────────────────────────────                                               ║
 * ║  • Created by Master using fork() + execl()                                  ║
 * ║  • Has its OWN address space (separate memory)                               ║
 * ║  • Communicates via IPC (Shared Memory, Message Queues)                      ║
 * ║  • Heavier to create (~1ms) due to memory copying                            ║
 * ║  • Crash in one worker doesn't affect others (isolation)                     ║
 * ║  • Each worker has unique PID from the kernel                                ║
 * ║                                                                              ║
 * ║  TASK THREAD (created within worker):                                        ║
 * ║  ─────────────────────────────────────                                       ║
 * ║  • Created using pthread_create()                                            ║
 * ║  • SHARES address space with parent (same memory)                            ║
 * ║  • Communicates via shared variables (needs mutexes!)                        ║
 * ║  • Lightweight to create (~10μs)                                             ║
 * ║  • Can corrupt shared data if not synchronized                               ║
 * ║  • Shares PID but has unique TID (Thread ID)                                 ║
 * ║                                                                              ║
 * ║  WHY BOTH?                                                                   ║
 * ║  ──────────                                                                  ║
 * ║  • Processes provide ISOLATION (fault tolerance)                             ║
 * ║  • Threads provide CONCURRENCY within each worker                            ║
 * ║  • Worker process handles IPC, threads handle actual execution               ║
 * ║  • This hybrid model is used in production servers (e.g., Apache, Nginx)     ║
 * ║                                                                              ║
 * ╚══════════════════════════════════════════════════════════════════════════════╝
 */

#include "common.h"

/* ═══════════════════════════════════════════════════════════════════════════════
 *                              WORKER CONFIGURATION
 * ═══════════════════════════════════════════════════════════════════════════════ */

#define PROGRESS_UPDATE_INTERVAL_MS     100     /* Update progress every 100ms    */
#define MAX_CONCURRENT_TASKS            2       /* Max threads per worker         */

/* ═══════════════════════════════════════════════════════════════════════════════
 *                              GLOBAL STATE
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Worker Identity */
static int              g_worker_id = -1;       /* Worker ID (0, 1, or 2)         */
static pid_t            g_worker_pid;           /* This worker's PID              */

/* IPC Connections */
static int              g_shmid = -1;           /* Shared Memory ID               */
static int              g_msgid = -1;           /* Message Queue ID               */
static SystemState      *g_state = NULL;        /* Pointer to shared memory       */

/* Thread Management */
static pthread_t        g_current_thread = 0;   /* Current task thread            */
static volatile int     g_thread_active = 0;    /* Is a thread currently running? */
static pthread_mutex_t  g_thread_lock = PTHREAD_MUTEX_INITIALIZER;

/* Shutdown Control */
static volatile sig_atomic_t g_running = 1;     /* Main loop control flag         */


/* ═══════════════════════════════════════════════════════════════════════════════
 *                              DATA STRUCTURES
 * ═══════════════════════════════════════════════════════════════════════════════ */

/*
 * Task Context - passed to each task thread
 * Contains all information needed to execute a job
 */
typedef struct {
    Job         job;            /* Copy of the job to execute                     */
    int         worker_id;      /* Which worker is executing this                 */
} TaskContext;


/* ═══════════════════════════════════════════════════════════════════════════════
 *                              FORWARD DECLARATIONS
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Initialization */
static int  parse_arguments(int argc, char *argv[]);
static int  connect_shared_memory(void);
static int  connect_message_queue(void);
static void register_worker(void);

/* Job Processing */
static void *task_thread(void *arg);
static int  remove_job_from_queue(int job_id);
static void execute_job(Job *job);
static void update_progress(int progress);

/* Main Loop */
static void worker_loop(void);

/* Cleanup */
static void signal_handler(int sig);
static void cleanup(void);

/* Logging */
static void worker_log(const char *format, ...);


/* ═══════════════════════════════════════════════════════════════════════════════
 *                              LOGGING FUNCTIONS
 * ═══════════════════════════════════════════════════════════════════════════════ */

/*
 * Worker-specific logging function.
 * Uses the shared memory log_lock for cross-process safety.
 */
static void worker_log(const char *format, ...) {
    /* Lock using shared memory mutex for cross-process synchronization */
    if (g_state != NULL) {
        pthread_mutex_lock(&g_state->log_lock);
    }

    /* Get timestamp */
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%H:%M:%S", tm_info);

    /* Format message */
    char message[256];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    /* Print with worker-specific color */
    const char *colors[] = {COLOR_BLUE, COLOR_GREEN, COLOR_YELLOW};
    const char *color = colors[g_worker_id % 3];

    printf("%s[%s] [WORKER-%d] " COLOR_RESET "%s\n", 
           color, timestamp, g_worker_id, message);

    if (g_state != NULL) {
        pthread_mutex_unlock(&g_state->log_lock);
    }
}


/* ═══════════════════════════════════════════════════════════════════════════════
 *                         INITIALIZATION FUNCTIONS
 * ═══════════════════════════════════════════════════════════════════════════════ */

/*
 * Parse command line arguments.
 * Expects worker_id as argv[1].
 * Returns: 0 on success, -1 on failure
 */
static int parse_arguments(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "ERROR: Worker ID not provided\n");
        fprintf(stderr, "Usage: %s <worker_id>\n", argv[0]);
        return -1;
    }

    g_worker_id = atoi(argv[1]);

    if (g_worker_id < 0 || g_worker_id >= NUM_WORKERS) {
        fprintf(stderr, "ERROR: Invalid worker ID %d (must be 0-%d)\n", 
                g_worker_id, NUM_WORKERS - 1);
        return -1;
    }

    g_worker_pid = getpid();

    return 0;
}

/*
 * Connect to existing shared memory segment.
 * The Master process creates this, we just attach.
 * Returns: 0 on success, -1 on failure
 */
static int connect_shared_memory(void) {
    /*
     * Get existing shared memory segment
     * Note: We use 0666 (no IPC_CREAT) since Master already created it
     */
    g_shmid = shmget(SHM_KEY, sizeof(SystemState), 0666);
    if (g_shmid == -1) {
        perror("shmget");
        fprintf(stderr, "ERROR: Failed to find shared memory (Key: 0x%04X)\n", SHM_KEY);
        fprintf(stderr, "       Is the Master process running?\n");
        return -1;
    }

    /*
     * Attach to shared memory
     * shmat() maps the shared segment into our address space
     */
    g_state = (SystemState *)shmat(g_shmid, NULL, 0);
    if (g_state == (void *)-1) {
        perror("shmat");
        g_state = NULL;
        return -1;
    }

    return 0;
}

/*
 * Connect to existing message queue.
 * Returns: 0 on success, -1 on failure
 */
static int connect_message_queue(void) {
    /*
     * Get existing message queue
     * Similar to shared memory - Master creates, we just access
     */
    g_msgid = msgget(MSG_KEY, 0666);
    if (g_msgid == -1) {
        perror("msgget");
        fprintf(stderr, "ERROR: Failed to find message queue (Key: 0x%04X)\n", MSG_KEY);
        return -1;
    }

    return 0;
}

/*
 * Register this worker in shared memory.
 * Sets initial status and stores PID.
 */
static void register_worker(void) {
    if (g_state == NULL) return;

    /*
     * Update worker information in shared memory
     * This makes the worker visible to the UI
     */
    g_state->worker_pids[g_worker_id] = g_worker_pid;
    g_state->worker_status[g_worker_id] = WORKER_IDLE;
    g_state->current_job_id[g_worker_id] = -1;
    g_state->worker_progress[g_worker_id] = 0;

    worker_log("Registered successfully (PID: %d)", g_worker_pid);
}


/* ═══════════════════════════════════════════════════════════════════════════════
 *                         JOB PROCESSING FUNCTIONS
 * ═══════════════════════════════════════════════════════════════════════════════ */

/*
 * Remove a job from the visual queue in shared memory.
 * This is called when a worker picks up a job for execution.
 *
 * SYNCHRONIZATION: Uses queue_lock mutex
 *
 * Returns: 0 on success, -1 if job not found
 */
static int remove_job_from_queue(int job_id) {
    if (g_state == NULL) return -1;

    /*
     * CRITICAL SECTION: Modifying shared job queue
     * Must lock to prevent race conditions with Master adding jobs
     * or other workers removing jobs simultaneously
     */
    pthread_mutex_lock(&g_state->queue_lock);

    int found = 0;

    /* Search for the job in the circular queue */
    for (int i = 0; i < MAX_JOBS_IN_QUEUE; i++) {
        int idx = (g_state->queue_head + i) % MAX_JOBS_IN_QUEUE;
        Job *job = &g_state->job_queue[idx];

        if (job->id == job_id && job->state != JOB_STATE_EMPTY) {
            /* Mark as running (will be visible in UI momentarily) */
            job->state = JOB_STATE_RUNNING;
            job->assigned_worker = g_worker_id;
            job->start_time = time(NULL);

            /*
             * For visual queue, we mark it as empty after brief delay
             * This creates a "picked up" animation effect in the UI
             */
            found = 1;
            break;
        }
    }

    /* If found, also decrement the queue count */
    if (found && g_state->queue_count > 0) {
        /* 
         * Note: We don't actually remove from array (would require shifting)
         * Instead, we mark it and let it be overwritten when queue wraps
         * This is acceptable for a visual-only queue
         */
    }

    pthread_mutex_unlock(&g_state->queue_lock);

    return found ? 0 : -1;
}

/*
 * Mark a job as completed and remove from visual queue.
 */
static void complete_job_in_queue(int job_id) {
    if (g_state == NULL) return;

    pthread_mutex_lock(&g_state->queue_lock);

    /* Find and mark job as completed/empty */
    for (int i = 0; i < MAX_JOBS_IN_QUEUE; i++) {
        Job *job = &g_state->job_queue[i];
        if (job->id == job_id) {
            job->state = JOB_STATE_EMPTY;  /* Free the slot */
            job->completion_time = time(NULL);
            
            /* Decrement queue count */
            if (g_state->queue_count > 0) {
                g_state->queue_count--;
            }
            break;
        }
    }

    pthread_mutex_unlock(&g_state->queue_lock);
}

/*
 * Update the worker's progress in shared memory.
 * Called frequently during job execution for smooth UI updates.
 */
static void update_progress(int progress) {
    if (g_state == NULL) return;

    /* Clamp progress to valid range */
    if (progress < 0) progress = 0;
    if (progress > 100) progress = 100;

    /* Update progress in shared memory (atomic for int) */
    g_state->worker_progress[g_worker_id] = progress;
}

/*
 * Simulate job execution with progress updates.
 * This is the core "work" simulation.
 */
static void execute_job(Job *job) {
    /*
     * Calculate timing for smooth progress updates
     * 
     * Example: BurstTime = 5 seconds
     *          Update every 100ms = 50 updates total
     *          Progress increment = 100 / 50 = 2% per update
     */
    int total_updates = (job->burst_time * 1000) / PROGRESS_UPDATE_INTERVAL_MS;
    if (total_updates < 1) total_updates = 1;

    float progress_increment = 100.0f / total_updates;
    float current_progress = 0.0f;

    worker_log("Executing Job #%03d (Burst: %ds, Priority: %d)",
               job->id, job->burst_time, job->priority);

    /*
     * WORK SIMULATION LOOP
     * This loop simulates CPU-bound work
     * In a real system, this would be actual computation
     */
    for (int i = 0; i < total_updates && g_running; i++) {
        /* Update progress */
        current_progress += progress_increment;
        update_progress((int)current_progress);

        /* Simulate work by sleeping */
        usleep(PROGRESS_UPDATE_INTERVAL_MS * 1000);

        /*
         * Optional: Add some CPU work to make it more realistic
         * volatile int dummy = 0;
         * for (int j = 0; j < 100000; j++) dummy++;
         */
    }

    /* Ensure we hit exactly 100% */
    update_progress(100);
}

/*
 * Task Thread Function
 * ═══════════════════════════════════════════════════════════════════════════════
 * This is the thread entry point for executing a job.
 * Each job runs in its own thread, allowing the main loop to continue
 * receiving messages (though we limit concurrency for simplicity).
 *
 * VIVA NOTE: This demonstrates POSIX threads (pthreads)
 *            - Shares memory with parent process
 *            - Lightweight compared to fork()
 *            - Requires careful synchronization (mutexes)
 */
static void *task_thread(void *arg) {
    TaskContext *ctx = (TaskContext *)arg;
    Job job = ctx->job;
    int worker_id = ctx->worker_id;

    /*
     * STEP 1: Remove job from visual queue (SYNC with queue_lock)
     * This updates the UI to show the job has been picked up
     */
    remove_job_from_queue(job.id);

    /*
     * STEP 2: Update worker status to BUSY (SYNC with stats_lock)
     * This is critical for the UI to show correct worker state
     */
    pthread_mutex_lock(&g_state->stats_lock);
    
    g_state->worker_status[worker_id] = WORKER_BUSY;
    g_state->current_job_id[worker_id] = job.id;
    g_state->worker_progress[worker_id] = 0;
    
    pthread_mutex_unlock(&g_state->stats_lock);

    worker_log("Started Job #%03d [Priority: %s, Burst: %ds]",
               job.id,
               job.priority == PRIORITY_HIGH ? "HIGH" :
               job.priority == PRIORITY_MEDIUM ? "MED" : "LOW",
               job.burst_time);

    /*
     * STEP 3: Execute the job
     * This is the actual "work" simulation
     */
    execute_job(&job);

    /*
     * STEP 4: Mark job as completed (SYNC with stats_lock)
     * Update statistics and reset worker state
     */
    pthread_mutex_lock(&g_state->stats_lock);

    /* Increment completion counters */
    g_state->total_jobs_done++;
    g_state->worker_jobs_done[worker_id]++;

    /* Calculate and update average turnaround time */
    time_t turnaround = time(NULL) - job.arrival_time;
    double old_avg = g_state->avg_turnaround_time;
    int total_done = g_state->total_jobs_done;
    g_state->avg_turnaround_time = ((old_avg * (total_done - 1)) + turnaround) / total_done;

    /* Reset worker status to IDLE */
    g_state->worker_status[worker_id] = WORKER_IDLE;
    g_state->current_job_id[worker_id] = -1;
    g_state->worker_progress[worker_id] = 0;

    pthread_mutex_unlock(&g_state->stats_lock);

    /*
     * STEP 5: Remove job from visual queue completely
     */
    complete_job_in_queue(job.id);

    worker_log("Completed Job #%03d (Turnaround: %lds)", job.id, (long)turnaround);

    /* Mark thread as inactive */
    pthread_mutex_lock(&g_thread_lock);
    g_thread_active = 0;
    pthread_mutex_unlock(&g_thread_lock);

    /* Free the context */
    free(ctx);

    /*
     * pthread_exit() terminates only this thread, not the entire process
     * This is different from exit() which would terminate everything
     */
    pthread_exit(NULL);
}


/* ═══════════════════════════════════════════════════════════════════════════════
 *                              MAIN WORKER LOOP
 * ═══════════════════════════════════════════════════════════════════════════════ */

/*
 * Main worker loop - consumes jobs from message queue.
 * Uses blocking msgrcv() to wait efficiently for jobs.
 */
static void worker_loop(void) {
    worker_log("Entering main job consumer loop...");
    worker_log("Listening for jobs on message type %d", g_worker_id + 1);

    Message msg;

    while (g_running && g_state->running) {
        /*
         * BLOCKING MESSAGE RECEIVE
         * ═══════════════════════════════════════════════════════════════════════
         * msgrcv() blocks until a message arrives or is interrupted
         *
         * Parameters:
         *   g_msgid  - Message queue identifier
         *   &msg     - Buffer to receive message
         *   size     - Size of message (excluding mtype)
         *   mtype    - Message type to receive (worker_id + 1 for our worker)
         *   0        - Flags (0 = blocking)
         *
         * The mtype allows each worker to receive only messages intended for it
         * This implements a simple load distribution mechanism
         */
        ssize_t bytes = msgrcv(g_msgid, &msg, sizeof(msg) - sizeof(long),
                               g_worker_id + 1, 0);

        if (bytes == -1) {
            if (errno == EINTR) {
                /* Interrupted by signal - check if we should exit */
                continue;
            }
            if (errno == EIDRM) {
                /* Message queue removed - Master shutting down */
                worker_log("Message queue removed, shutting down...");
                break;
            }
            perror("msgrcv");
            continue;
        }

        /*
         * Process received message based on command type
         */
        switch (msg.command) {
            case MSG_CMD_SHUTDOWN:
                /*
                 * Graceful shutdown command from Master
                 */
                worker_log("Received SHUTDOWN command");
                g_running = 0;
                break;

            case MSG_CMD_NEW_JOB:
                /*
                 * New job to process
                 * Check if we can accept it (not already processing)
                 */
                pthread_mutex_lock(&g_thread_lock);
                int can_process = !g_thread_active;
                if (can_process) {
                    g_thread_active = 1;
                }
                pthread_mutex_unlock(&g_thread_lock);

                if (can_process) {
                    /*
                     * SPAWN TASK THREAD
                     * ═══════════════════════════════════════════════════════════
                     * We create a new thread to handle job execution
                     * This allows the main loop to continue receiving messages
                     *
                     * pthread_create() parameters:
                     *   &g_current_thread - Thread handle storage
                     *   NULL              - Default thread attributes
                     *   task_thread       - Thread function
                     *   ctx               - Argument to thread function
                     */
                    TaskContext *ctx = malloc(sizeof(TaskContext));
                    if (ctx == NULL) {
                        worker_log("ERROR: Failed to allocate task context");
                        pthread_mutex_lock(&g_thread_lock);
                        g_thread_active = 0;
                        pthread_mutex_unlock(&g_thread_lock);
                        break;
                    }

                    ctx->job = msg.job;
                    ctx->worker_id = g_worker_id;

                    int result = pthread_create(&g_current_thread, NULL, 
                                               task_thread, ctx);
                    if (result != 0) {
                        worker_log("ERROR: pthread_create failed: %s", 
                                  strerror(result));
                        free(ctx);
                        pthread_mutex_lock(&g_thread_lock);
                        g_thread_active = 0;
                        pthread_mutex_unlock(&g_thread_lock);
                    } else {
                        /*
                         * Detach thread - we don't need to join it
                         * Resources will be automatically freed on exit
                         */
                        pthread_detach(g_current_thread);
                        
                        worker_log("Spawned task thread for Job #%03d", msg.job.id);
                    }
                } else {
                    /*
                     * Worker is busy - in a real system we might:
                     * 1. Queue the job locally
                     * 2. Send it back to the queue
                     * 3. Let another worker pick it up
                     *
                     * For this demo, we log and continue
                     */
                    worker_log("WARNING: Busy, cannot process Job #%03d", msg.job.id);
                }
                break;

            default:
                worker_log("WARNING: Unknown command %d", msg.command);
                break;
        }
    }

    worker_log("Exited main loop");
}


/* ═══════════════════════════════════════════════════════════════════════════════
 *                         SIGNAL HANDLING & CLEANUP
 * ═══════════════════════════════════════════════════════════════════════════════ */

/*
 * Signal handler for graceful shutdown.
 */
static void signal_handler(int sig) {
    (void)sig;  /* Suppress unused warning */
    g_running = 0;
}

/*
 * Cleanup function - detach from IPC resources.
 */
static void cleanup(void) {
    worker_log("Cleaning up...");

    /* Wait for any running task thread to complete */
    pthread_mutex_lock(&g_thread_lock);
    int thread_running = g_thread_active;
    pthread_mutex_unlock(&g_thread_lock);

    if (thread_running) {
        worker_log("Waiting for active task to complete...");
        /* Give it a moment to finish */
        usleep(500000);  /* 500ms */
    }

    /* Update status in shared memory before detaching */
    if (g_state != NULL) {
        g_state->worker_status[g_worker_id] = WORKER_STOPPING;
        g_state->worker_pids[g_worker_id] = 0;
        g_state->current_job_id[g_worker_id] = -1;
        g_state->worker_progress[g_worker_id] = 0;
    }

    /*
     * DETACH from shared memory
     * Note: We only DETACH, not REMOVE
     * The Master process is responsible for removing IPC resources
     */
    if (g_state != NULL && g_state != (void *)-1) {
        worker_log("Detaching from shared memory...");
        if (shmdt(g_state) == -1) {
            perror("shmdt");
        }
        g_state = NULL;
    }

    /*
     * Note: We don't remove the message queue either
     * Just stop using it - Master handles cleanup
     */

    worker_log("Shutdown complete. Goodbye!");
}


/* ═══════════════════════════════════════════════════════════════════════════════
 *                              MAIN FUNCTION
 * ═══════════════════════════════════════════════════════════════════════════════ */

int main(int argc, char *argv[]) {
    /*
     * STEP 1: Parse command line arguments
     * Worker ID is passed by Master via execl()
     */
    if (parse_arguments(argc, argv) != 0) {
        return EXIT_FAILURE;
    }

    /* Print startup banner */
    printf(COLOR_CYAN "\n═══════════════════════════════════════════════════════════════\n");
    printf("  NEXUS OS SCHEDULER - WORKER %d STARTING\n", g_worker_id);
    printf("  PID: %d\n", g_worker_pid);
    printf("═══════════════════════════════════════════════════════════════\n" COLOR_RESET);

    /*
     * STEP 2: Set up signal handlers
     */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Register cleanup on exit */
    atexit(cleanup);

    /*
     * STEP 3: Connect to Shared Memory
     * Must be done before we can update our status
     */
    worker_log("Connecting to shared memory (Key: 0x%04X)...", SHM_KEY);
    if (connect_shared_memory() != 0) {
        fprintf(stderr, "FATAL: Cannot connect to shared memory\n");
        return EXIT_FAILURE;
    }
    worker_log("Shared memory connected successfully");

    /*
     * STEP 4: Connect to Message Queue
     * This is how we receive jobs from Master
     */
    worker_log("Connecting to message queue (Key: 0x%04X)...", MSG_KEY);
    if (connect_message_queue() != 0) {
        fprintf(stderr, "FATAL: Cannot connect to message queue\n");
        return EXIT_FAILURE;
    }
    worker_log("Message queue connected successfully");

    /*
     * STEP 5: Register this worker in shared memory
     * Makes us visible to the UI
     */
    register_worker();

    /*
     * STEP 6: Enter main job processing loop
     */
    worker_log("╔════════════════════════════════════════╗");
    worker_log("║        WORKER %d ONLINE & READY        ║", g_worker_id);
    worker_log("╚════════════════════════════════════════╝");

    worker_loop();

    return EXIT_SUCCESS;
}


/* ═══════════════════════════════════════════════════════════════════════════════
 *                              END OF FILE
 * ═══════════════════════════════════════════════════════════════════════════════
 *
 * COMPILATION:
 *   gcc -o worker worker.c -lpthread
 *
 * MANUAL EXECUTION (for testing):
 *   ./worker 0    # Start as Worker 0
 *   ./worker 1    # Start as Worker 1
 *   ./worker 2    # Start as Worker 2
 *
 * NORMAL OPERATION:
 *   Workers are spawned automatically by the Master process
 *   Do not run manually in production
 *
 * FEATURES:
 *   ✓ Multi-threaded job execution (pthread_create)
 *   ✓ Blocking message queue receive (msgrcv)
 *   ✓ Proper mutex synchronization for shared memory
 *   ✓ Smooth progress updates (every 100ms)
 *   ✓ Graceful shutdown handling
 *   ✓ Worker isolation from Master crashes
 *
 * SYNCHRONIZATION POINTS:
 *   1. queue_lock  - When removing jobs from visual queue
 *   2. stats_lock  - When updating worker status and job counters
 *   3. log_lock    - When writing log messages
 *   4. g_thread_lock - Internal thread state management
 *
 * THREAD MODEL:
 *   ┌─────────────────────────────────────────────────────────────────┐
 *   │                     WORKER PROCESS                              │
 *   │  ┌─────────────────────────────────────────────────────────────┐ │
 *   │  │  MAIN THREAD                                                │ │
 *   │  │  - msgrcv() blocking wait                                   │ │
 *   │  │  - Spawns task threads                                      │ │
 *   │  │  - Handles shutdown                                         │ │
 *   │  └─────────────────────────────────────────────────────────────┘ │
 *   │                           │                                     │
 *   │                           ▼                                     │
 *   │  ┌─────────────────────────────────────────────────────────────┐ │
 *   │  │  TASK THREAD (detached)                                     │ │
 *   │  │  - Removes job from queue                                   │ │
 *   │  │  - Updates worker status                                    │ │
 *   │  │  - Executes job (simulated)                                 │ │
 *   │  │  - Updates progress                                         │ │
 *   │  │  - Increments counters                                      │ │
 *   │  └─────────────────────────────────────────────────────────────┘ │
 *   └─────────────────────────────────────────────────────────────────┘
 *
 * ═══════════════════════════════════════════════════════════════════════════════ */
