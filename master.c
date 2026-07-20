/*
 * ╔══════════════════════════════════════════════════════════════════════════════╗
 * ║                    NEXUS OS SCHEDULER - MASTER PROCESS                       ║
 * ║                        The Heart of the System                               ║
 * ╠══════════════════════════════════════════════════════════════════════════════╣
 * ║  Author      : Zain Iqbal                                                    ║
 * ║  Description : Master process responsible for IPC initialization, worker    ║
 * ║                process management, job scheduling, and system heartbeat.    ║
 * ║                Implements core OS concepts: Process Creation, IPC, and      ║
 * ║                Priority-based Scheduling.                                   ║
 * ╚══════════════════════════════════════════════════════════════════════════════╝
 */

#include "common.h"
#include <sys/wait.h>

/* ═══════════════════════════════════════════════════════════════════════════════
 *                              MASTER CONFIGURATION
 * ═══════════════════════════════════════════════════════════════════════════════ */

#define LOG_FILE            "system_log.txt"    /* System log file path           */
#define BURST_MIN_JOBS      3                   /* Minimum jobs per burst         */
#define BURST_MAX_JOBS      5                   /* Maximum jobs per burst         */
#define BURST_INTERVAL_MIN  2                   /* Minimum seconds between bursts */
#define BURST_INTERVAL_MAX  3                   /* Maximum seconds between bursts */
#define WORKER_BINARY       "./worker"          /* Worker executable path         */

/* ═══════════════════════════════════════════════════════════════════════════════
 *                              GLOBAL STATE
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* IPC Identifiers */
static int          g_shmid = -1;               /* Shared Memory ID               */
static int          g_msgid = -1;               /* Message Queue ID               */
static SystemState  *g_state = NULL;            /* Pointer to shared memory       */

/* Process Management */
static pid_t        g_worker_pids[NUM_WORKERS]; /* Worker process IDs             */
static volatile sig_atomic_t g_running = 1;     /* Main loop control flag         */

/* Logging */
static FILE         *g_log_file = NULL;         /* Log file handle                */
static pthread_mutex_t g_file_log_lock = PTHREAD_MUTEX_INITIALIZER;


/* ═══════════════════════════════════════════════════════════════════════════════
 *                              FORWARD DECLARATIONS
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Initialization Functions */
static int  init_logging(void);
static int  init_shared_memory(void);
static int  init_message_queue(void);
static int  spawn_workers(void);

/* Scheduling Functions */
static void generate_job_burst(void);
static int  add_job_to_queue(Job *job);
static int  send_job_to_workers(Job *job);
static Job  create_random_job(void);

/* Heartbeat & Main Loop */
static void heartbeat_loop(void);
static void update_heartbeat(void);

/* Signal Handling & Cleanup */
static void signal_handler(int sig);
static void cleanup(void);
static void terminate_workers(void);

/* Logging Utilities */
static void log_event(const char *format, ...);
static void log_job(const Job *job, const char *action);


/* ═══════════════════════════════════════════════════════════════════════════════
 *                              LOGGING FUNCTIONS
 * ═══════════════════════════════════════════════════════════════════════════════ */

/*
 * Initialize the logging system.
 * Opens the log file and writes a startup header.
 * Returns: 0 on success, -1 on failure
 */
static int init_logging(void) {
    g_log_file = fopen(LOG_FILE, "a");
    if (g_log_file == NULL) {
        perror("fopen(LOG_FILE)");
        return -1;
    }

    /* Write startup header */
    time_t now = time(NULL);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));

    fprintf(g_log_file, "\n");
    fprintf(g_log_file, "════════════════════════════════════════════════════════════════════\n");
    fprintf(g_log_file, "  NEXUS OS SCHEDULER - SESSION START\n");
    fprintf(g_log_file, "  Timestamp: %s\n", time_str);
    fprintf(g_log_file, "  PID: %d\n", getpid());
    fprintf(g_log_file, "════════════════════════════════════════════════════════════════════\n");
    fflush(g_log_file);

    return 0;
}

/*
 * Thread-safe logging function.
 * Writes timestamped messages to both console and log file.
 */
static void log_event(const char *format, ...) {
    pthread_mutex_lock(&g_file_log_lock);

    /* Get current timestamp */
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%H:%M:%S", tm_info);

    /* Format the message */
    char message[512];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    /* Write to console with color */
    printf(COLOR_MAGENTA "[%s] [MASTER] " COLOR_RESET "%s\n", timestamp, message);

    /* Write to log file */
    if (g_log_file != NULL) {
        fprintf(g_log_file, "[%s] [MASTER] %s\n", timestamp, message);
        fflush(g_log_file);
    }

    pthread_mutex_unlock(&g_file_log_lock);
}

/*
 * Log job-specific events with detailed information.
 */
static void log_job(const Job *job, const char *action) {
    const char *priority_str;
    switch (job->priority) {
        case PRIORITY_HIGH:   priority_str = "HIGH"; break;
        case PRIORITY_MEDIUM: priority_str = "MED";  break;
        case PRIORITY_LOW:    priority_str = "LOW";  break;
        default:              priority_str = "???";  break;
    }

    log_event("%s Job #%03d [Priority: %s, Burst: %ds]",
              action, job->id, priority_str, job->burst_time);
}


/* ═══════════════════════════════════════════════════════════════════════════════
 *                         IPC INITIALIZATION FUNCTIONS
 * ═══════════════════════════════════════════════════════════════════════════════ */

/*
 * Initialize Shared Memory segment.
 * Creates the shared memory, attaches it, and initializes the SystemState.
 * Returns: 0 on success, -1 on failure
 */
static int init_shared_memory(void) {
    log_event("Initializing Shared Memory (Key: 0x%04X)...", SHM_KEY);

    /*
     * Create shared memory segment
     * IPC_CREAT: Create if doesn't exist
     * IPC_EXCL:  Fail if already exists (ensures fresh start)
     * 0666:      Read/write permissions for all
     */
    g_shmid = shmget(SHM_KEY, sizeof(SystemState), IPC_CREAT | IPC_EXCL | 0666);
    
    if (g_shmid == -1) {
        if (errno == EEXIST) {
            /* Segment exists - try to remove and recreate */
            log_event("Existing shared memory found, attempting cleanup...");
            
            int old_shmid = shmget(SHM_KEY, sizeof(SystemState), 0666);
            if (old_shmid != -1) {
                shmctl(old_shmid, IPC_RMID, NULL);
            }
            
            /* Retry creation */
            g_shmid = shmget(SHM_KEY, sizeof(SystemState), IPC_CREAT | IPC_EXCL | 0666);
        }
        
        if (g_shmid == -1) {
            perror("shmget");
            return -1;
        }
    }

    /*
     * Attach shared memory to process address space
     * shmat returns pointer to the shared segment
     */
    g_state = (SystemState *)shmat(g_shmid, NULL, 0);
    if (g_state == (void *)-1) {
        perror("shmat");
        shmctl(g_shmid, IPC_RMID, NULL);
        return -1;
    }

    /*
     * Initialize the SystemState structure
     * Uses init_system_state() from common.h which properly
     * initializes mutexes with PTHREAD_PROCESS_SHARED attribute
     */
    init_system_state(g_state);

    log_event("Shared Memory initialized successfully (SHMID: %d, Size: %zu bytes)",
              g_shmid, sizeof(SystemState));

    return 0;
}

/*
 * Initialize Message Queue for job dispatch.
 * Returns: 0 on success, -1 on failure
 */
static int init_message_queue(void) {
    log_event("Initializing Message Queue (Key: 0x%04X)...", MSG_KEY);

    /*
     * Create message queue
     * Similar flags to shared memory creation
     */
    g_msgid = msgget(MSG_KEY, IPC_CREAT | IPC_EXCL | 0666);
    
    if (g_msgid == -1) {
        if (errno == EEXIST) {
            /* Queue exists - try to remove and recreate */
            log_event("Existing message queue found, attempting cleanup...");
            
            int old_msgid = msgget(MSG_KEY, 0666);
            if (old_msgid != -1) {
                msgctl(old_msgid, IPC_RMID, NULL);
            }
            
            /* Retry creation */
            g_msgid = msgget(MSG_KEY, IPC_CREAT | IPC_EXCL | 0666);
        }
        
        if (g_msgid == -1) {
            perror("msgget");
            return -1;
        }
    }

    log_event("Message Queue initialized successfully (MSGID: %d)", g_msgid);
    return 0;
}


/* ═══════════════════════════════════════════════════════════════════════════════
 *                         PROCESS MANAGEMENT FUNCTIONS
 * ═══════════════════════════════════════════════════════════════════════════════ */

/*
 * Spawn worker processes using fork() and execl().
 * Creates NUM_WORKERS child processes, each running the worker binary.
 * Returns: 0 on success, -1 on failure
 */
static int spawn_workers(void) {
    log_event("Spawning %d Worker Processes...", NUM_WORKERS);

    for (int i = 0; i < NUM_WORKERS; i++) {
        /*
         * fork() creates a child process
         * Returns:
         *   > 0 in parent (child's PID)
         *   = 0 in child
         *   < 0 on error
         */
        pid_t pid = fork();

        if (pid < 0) {
            /* Fork failed */
            perror("fork");
            log_event("ERROR: Failed to fork Worker %d", i);
            
            /* Kill any workers we've already spawned */
            for (int j = 0; j < i; j++) {
                kill(g_worker_pids[j], SIGTERM);
            }
            return -1;
        }
        else if (pid == 0) {
            /*
             * CHILD PROCESS
             * Replace this process image with the worker binary
             * execl() only returns on error
             */
            char worker_id[8];
            snprintf(worker_id, sizeof(worker_id), "%d", i);

            /*
             * execl(path, arg0, arg1, ..., NULL)
             * arg0 is conventionally the program name
             * arg1 is our worker ID
             */
            execl(WORKER_BINARY, "worker", worker_id, (char *)NULL);

            /* If execl returns, it failed */
            perror("execl");
            fprintf(stderr, "ERROR: Failed to exec worker binary '%s'\n", WORKER_BINARY);
            _exit(EXIT_FAILURE);
        }
        else {
            /*
             * PARENT PROCESS
             * Store child PID for management
             */
            g_worker_pids[i] = pid;
            g_state->worker_pids[i] = pid;
            g_state->worker_status[i] = WORKER_STARTING;

            log_event("Worker %d spawned successfully (PID: %d)", i, pid);
        }

        /* Small delay between forks for clean startup */
        usleep(100000);  /* 100ms */
    }

    log_event("All %d workers spawned successfully", NUM_WORKERS);
    return 0;
}

/*
 * Terminate all worker processes gracefully.
 * Sends shutdown message first, then SIGTERM, finally SIGKILL if needed.
 */
static void terminate_workers(void) {
    log_event("Initiating worker termination sequence...");

    /* First, send shutdown command via message queue */
    if (g_msgid != -1) {
        Message shutdown_msg;
        memset(&shutdown_msg, 0, sizeof(shutdown_msg));
        shutdown_msg.command = MSG_CMD_SHUTDOWN;

        for (int i = 0; i < NUM_WORKERS; i++) {
            shutdown_msg.mtype = i + 1;  /* Worker-specific message type */
            msgsnd(g_msgid, &shutdown_msg, sizeof(shutdown_msg) - sizeof(long), IPC_NOWAIT);
        }
    }

    /* Give workers time to shut down gracefully */
    usleep(500000);  /* 500ms */

    /* Send SIGTERM to any remaining workers */
    for (int i = 0; i < NUM_WORKERS; i++) {
        if (g_worker_pids[i] > 0) {
            int status;
            pid_t result = waitpid(g_worker_pids[i], &status, WNOHANG);
            
            if (result == 0) {
                /* Worker still running - send SIGTERM */
                log_event("Sending SIGTERM to Worker %d (PID: %d)", i, g_worker_pids[i]);
                kill(g_worker_pids[i], SIGTERM);
            }
        }
    }

    /* Wait for workers with timeout */
    usleep(500000);  /* 500ms */

    /* Force kill any stubborn workers */
    for (int i = 0; i < NUM_WORKERS; i++) {
        if (g_worker_pids[i] > 0) {
            int status;
            pid_t result = waitpid(g_worker_pids[i], &status, WNOHANG);
            
            if (result == 0) {
                /* Worker STILL running - force kill */
                log_event("Force killing Worker %d (PID: %d)", i, g_worker_pids[i]);
                kill(g_worker_pids[i], SIGKILL);
                waitpid(g_worker_pids[i], &status, 0);
            }
            
            g_worker_pids[i] = 0;
        }
    }

    log_event("All workers terminated");
}


/* ═══════════════════════════════════════════════════════════════════════════════
 *                         JOB SCHEDULING FUNCTIONS
 * ═══════════════════════════════════════════════════════════════════════════════ */

/*
 * Create a random job with varied priority and burst time.
 * Returns: A new Job struct with random parameters
 */
static Job create_random_job(void) {
    Job job;
    memset(&job, 0, sizeof(Job));

    /* Assign unique ID */
    job.id = g_state->next_job_id++;

    /* Generate job name */
    snprintf(job.name, MAX_JOB_NAME_LEN, "Task_%03d", job.id);

    /*
     * Random priority distribution:
     *   20% HIGH (1)
     *   50% MEDIUM (2)
     *   30% LOW (3)
     */
    int rand_val = rand() % 100;
    if (rand_val < 20) {
        job.priority = PRIORITY_HIGH;
    } else if (rand_val < 70) {
        job.priority = PRIORITY_MEDIUM;
    } else {
        job.priority = PRIORITY_LOW;
    }

    /* Random burst time between MIN and MAX */
    job.burst_time = MIN_BURST_TIME + (rand() % (MAX_BURST_TIME - MIN_BURST_TIME + 1));
    job.remaining_time = job.burst_time;

    /* Set state and timestamps */
    job.state = JOB_STATE_WAITING;
    job.arrival_time = time(NULL);
    job.start_time = 0;
    job.completion_time = 0;
    job.assigned_worker = -1;

    return job;
}

/*
 * Add a job to the shared memory job queue for UI visualization.
 * Returns: 0 on success, -1 if queue is full
 */
static int add_job_to_queue(Job *job) {
    /* Lock the queue for thread-safe access */
    pthread_mutex_lock(&g_state->queue_lock);

    if (g_state->queue_count >= MAX_JOBS_IN_QUEUE) {
        pthread_mutex_unlock(&g_state->queue_lock);
        log_event("WARNING: Job queue full, cannot add Job #%03d", job->id);
        return -1;
    }

    /* Add job to circular queue */
    int tail = g_state->queue_tail;
    memcpy(&g_state->job_queue[tail], job, sizeof(Job));
    g_state->queue_tail = (tail + 1) % MAX_JOBS_IN_QUEUE;
    g_state->queue_count++;
    g_state->total_jobs_submitted++;

    pthread_mutex_unlock(&g_state->queue_lock);

    return 0;
}

/*
 * Send a job to workers via message queue.
 * Uses round-robin or priority-based dispatch.
 * Returns: 0 on success, -1 on failure
 */
static int send_job_to_workers(Job *job) {
    Message msg;
    memset(&msg, 0, sizeof(msg));

    /*
     * Message type determines which worker receives it:
     * mtype = 1, 2, or 3 for specific workers
     * Using 0 would be broadcast (not supported by System V)
     * 
     * We use round-robin dispatch based on job ID
     */
    static int next_worker = 0;
    msg.mtype = (next_worker % NUM_WORKERS) + 1;
    next_worker++;

    msg.command = MSG_CMD_NEW_JOB;
    memcpy(&msg.job, job, sizeof(Job));

    /*
     * msgsnd() sends message to queue
     * sizeof(msg) - sizeof(long) excludes mtype from size
     * 0 = block if queue is full
     */
    if (msgsnd(g_msgid, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
        perror("msgsnd");
        log_event("ERROR: Failed to send Job #%03d to message queue", job->id);
        return -1;
    }

    return 0;
}

/*
 * Generate a burst of random jobs.
 * Creates 3-5 jobs and dispatches them to workers.
 */
static void generate_job_burst(void) {
    /* Random number of jobs in burst */
    int num_jobs = BURST_MIN_JOBS + (rand() % (BURST_MAX_JOBS - BURST_MIN_JOBS + 1));

    log_event("╔════════════════════════════════════════╗");
    log_event("║     GENERATING JOB BURST: %d JOBS      ║", num_jobs);
    log_event("╚════════════════════════════════════════╝");

    int jobs_added = 0;
    int jobs_sent = 0;

    for (int i = 0; i < num_jobs; i++) {
        /* Create random job */
        Job job = create_random_job();

        /* Add to visual queue */
        if (add_job_to_queue(&job) == 0) {
            jobs_added++;
        }

        /* Send to workers via message queue */
        if (send_job_to_workers(&job) == 0) {
            jobs_sent++;
            log_job(&job, "DISPATCHED");
        }

        /* Small delay between job creations */
        usleep(50000);  /* 50ms */
    }

    log_event("Burst complete: %d/%d jobs queued, %d/%d jobs dispatched",
              jobs_added, num_jobs, jobs_sent, num_jobs);
}


/* ═══════════════════════════════════════════════════════════════════════════════
 *                         HEARTBEAT & MAIN LOOP
 * ═══════════════════════════════════════════════════════════════════════════════ */

/*
 * Update the heartbeat timestamp in shared memory.
 * Called every 500ms to indicate master is alive.
 */
static void update_heartbeat(void) {
    if (g_state != NULL) {
        g_state->last_heartbeat = time(NULL);
    }
}

/*
 * Main heartbeat and scheduling loop.
 * Updates heartbeat every 500ms.
 * Generates job bursts every 2-3 seconds.
 */
static void heartbeat_loop(void) {
    log_event("Starting main heartbeat loop...");
    log_event("Heartbeat interval: %dms, Burst interval: %d-%ds",
              HEARTBEAT_INTERVAL_MS, BURST_INTERVAL_MIN, BURST_INTERVAL_MAX);

    /* Timing variables */
    time_t last_burst_time = time(NULL);
    int next_burst_interval = BURST_INTERVAL_MIN + 
                              (rand() % (BURST_INTERVAL_MAX - BURST_INTERVAL_MIN + 1));

    /* Heartbeat counter for status updates */
    int heartbeat_count = 0;

    while (g_running) {
        /* Update heartbeat */
        update_heartbeat();
        heartbeat_count++;

        /* Log periodic status (every 10 heartbeats = 5 seconds) */
        if (heartbeat_count % 10 == 0) {
            log_event("STATUS: Uptime=%lds, Jobs=%d submitted/%d completed, Queue=%d/%d",
                      time(NULL) - g_state->system_start_time,
                      g_state->total_jobs_submitted,
                      g_state->total_jobs_done,
                      g_state->queue_count,
                      MAX_JOBS_IN_QUEUE);
        }

        /* Check if it's time for a job burst */
        time_t current_time = time(NULL);
        if (difftime(current_time, last_burst_time) >= next_burst_interval) {
            generate_job_burst();
            last_burst_time = current_time;
            
            /* Randomize next burst interval */
            next_burst_interval = BURST_INTERVAL_MIN + 
                                  (rand() % (BURST_INTERVAL_MAX - BURST_INTERVAL_MIN + 1));
        }

        /* Check for terminated workers and respawn if needed */
        for (int i = 0; i < NUM_WORKERS; i++) {
            if (g_worker_pids[i] > 0) {
                int status;
                pid_t result = waitpid(g_worker_pids[i], &status, WNOHANG);
                
                if (result > 0) {
                    /* Worker terminated unexpectedly */
                    log_event("WARNING: Worker %d (PID: %d) terminated unexpectedly",
                              i, g_worker_pids[i]);
                    g_state->worker_status[i] = WORKER_ERROR;
                    g_worker_pids[i] = 0;
                    
                    /* Could implement auto-respawn here */
                }
            }
        }

        /* Sleep for heartbeat interval (500ms) */
        usleep(HEARTBEAT_INTERVAL_MS * 1000);
    }

    log_event("Heartbeat loop terminated");
}


/* ═══════════════════════════════════════════════════════════════════════════════
 *                         SIGNAL HANDLING & CLEANUP
 * ═══════════════════════════════════════════════════════════════════════════════ */

/*
 * Signal handler for graceful shutdown.
 * Triggered by SIGINT (Ctrl+C), SIGTERM, etc.
 */
static void signal_handler(int sig) {
    /* Avoid re-entrance */
    static volatile sig_atomic_t handling_signal = 0;
    if (handling_signal) return;
    handling_signal = 1;

    /* Suppress unused parameter warning */
    (void)sig;

    /* Use write() instead of printf() in signal handler (async-signal-safe) */
    const char *msg = "\n[SIGNAL] Received shutdown signal, cleaning up...\n";
    write(STDOUT_FILENO, msg, strlen(msg));

    /* Set flag to stop main loop */
    g_running = 0;
}

/*
 * Comprehensive cleanup function.
 * Releases all IPC resources and terminates workers.
 */
static void cleanup(void) {
    log_event("╔════════════════════════════════════════╗");
    log_event("║     INITIATING SYSTEM SHUTDOWN         ║");
    log_event("╚════════════════════════════════════════╝");

    /* Mark system as not running */
    if (g_state != NULL) {
        g_state->running = 0;
    }

    /* Terminate worker processes */
    terminate_workers();

    /* Destroy mutexes in shared memory */
    if (g_state != NULL) {
        pthread_mutex_destroy(&g_state->log_lock);
        pthread_mutex_destroy(&g_state->queue_lock);
        pthread_mutex_destroy(&g_state->stats_lock);
    }

    /* Detach shared memory */
    if (g_state != NULL && g_state != (void *)-1) {
        log_event("Detaching shared memory...");
        if (shmdt(g_state) == -1) {
            perror("shmdt");
        }
        g_state = NULL;
    }

    /* Remove shared memory segment */
    if (g_shmid != -1) {
        log_event("Removing shared memory segment (SHMID: %d)...", g_shmid);
        if (shmctl(g_shmid, IPC_RMID, NULL) == -1) {
            perror("shmctl(IPC_RMID)");
        }
        g_shmid = -1;
    }

    /* Remove message queue */
    if (g_msgid != -1) {
        log_event("Removing message queue (MSGID: %d)...", g_msgid);
        if (msgctl(g_msgid, IPC_RMID, NULL) == -1) {
            perror("msgctl(IPC_RMID)");
        }
        g_msgid = -1;
    }

    /* Close log file */
    if (g_log_file != NULL) {
        time_t now = time(NULL);
        char time_str[64];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));

        fprintf(g_log_file, "════════════════════════════════════════════════════════════════════\n");
        fprintf(g_log_file, "  SESSION END: %s\n", time_str);
        fprintf(g_log_file, "════════════════════════════════════════════════════════════════════\n\n");
        fclose(g_log_file);
        g_log_file = NULL;
    }

    printf(COLOR_GREEN "\n[MASTER] Cleanup complete. Goodbye!\n" COLOR_RESET);
}


/* ═══════════════════════════════════════════════════════════════════════════════
 *                              MAIN FUNCTION
 * ═══════════════════════════════════════════════════════════════════════════════ */

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    /* Print startup banner */
    printf("\n");
    printf(COLOR_CYAN "╔══════════════════════════════════════════════════════════════╗\n");
    printf("║            NEXUS OS SCHEDULER - MASTER PROCESS              ║\n");
    printf("║                     Version 1.0.0                           ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n" COLOR_RESET);
    printf("\n");

    /* Seed random number generator */
    srand(time(NULL) ^ getpid());

    /* Initialize worker PIDs array */
    memset(g_worker_pids, 0, sizeof(g_worker_pids));

    /*
     * Register signal handlers for graceful shutdown
     * SIGINT  - Ctrl+C
     * SIGTERM - kill command
     */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction(SIGINT)");
        return EXIT_FAILURE;
    }
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction(SIGTERM)");
        return EXIT_FAILURE;
    }

    /* Register cleanup function for exit() */
    atexit(cleanup);

    /* Initialize logging */
    if (init_logging() != 0) {
        fprintf(stderr, "ERROR: Failed to initialize logging\n");
        return EXIT_FAILURE;
    }

    log_event("Master process starting (PID: %d)...", getpid());

    /* Initialize Shared Memory */
    if (init_shared_memory() != 0) {
        log_event("FATAL: Failed to initialize shared memory");
        return EXIT_FAILURE;
    }

    /* Initialize Message Queue */
    if (init_message_queue() != 0) {
        log_event("FATAL: Failed to initialize message queue");
        return EXIT_FAILURE;
    }

    /* Spawn worker processes */
    if (spawn_workers() != 0) {
        log_event("FATAL: Failed to spawn workers");
        return EXIT_FAILURE;
    }

    /* Give workers time to initialize */
    log_event("Waiting for workers to initialize...");
    sleep(1);

    /* Mark all workers as idle (they should update their own status) */
    for (int i = 0; i < NUM_WORKERS; i++) {
        if (g_state->worker_status[i] == WORKER_STARTING) {
            g_state->worker_status[i] = WORKER_IDLE;
        }
    }

    log_event("╔════════════════════════════════════════╗");
    log_event("║     SYSTEM INITIALIZATION COMPLETE     ║");
    log_event("╠════════════════════════════════════════╣");
    log_event("║  Shared Memory: READY                  ║");
    log_event("║  Message Queue: READY                  ║");
    log_event("║  Workers:       %d/%d ONLINE             ║", NUM_WORKERS, NUM_WORKERS);
    log_event("╚════════════════════════════════════════╝");

    printf("\n" COLOR_YELLOW "Press Ctrl+C to shutdown gracefully\n" COLOR_RESET "\n");

    /* Enter main heartbeat loop */
    heartbeat_loop();

    return EXIT_SUCCESS;
}


/* ═══════════════════════════════════════════════════════════════════════════════
 *                              END OF FILE
 * ═══════════════════════════════════════════════════════════════════════════════
 *
 * COMPILATION:
 *   gcc -o master master.c -lpthread
 *
 * EXECUTION ORDER:
 *   1. Compile worker.c first: gcc -o worker worker.c -lpthread
 *   2. Run master: ./master
 *   3. (Optional) Run UI in separate terminal: ./ui
 *
 * FEATURES:
 *   ✓ IPC Initialization (Shared Memory + Message Queue)
 *   ✓ Process Creation (fork + execl)
 *   ✓ Heartbeat mechanism (500ms interval)
 *   ✓ Job burst scheduling (2-3 second intervals)
 *   ✓ Signal handling (SIGINT/SIGTERM)
 *   ✓ Comprehensive cleanup (no memory leaks)
 *   ✓ Thread-safe logging to file and console
 *   ✓ Worker health monitoring
 *
 * IPC CLEANUP:
 *   If the program crashes, manually clean IPC resources:
 *   $ ipcs                    # List IPC resources
 *   $ ipcrm -m <shmid>        # Remove shared memory
 *   $ ipcrm -q <msgid>        # Remove message queue
 *
 * ═══════════════════════════════════════════════════════════════════════════════ */


 import asyncio
import databases
import sqlalchemy
from fastapi import FastAPI, BackgroundTasks, Request

DATABASE_URL = "postgresql://user:password@localhost/testdb"
database = databases.Database(DATABASE_URL)
metadata = sqlalchemy.MetaData()

app = FastAPI()

# A mock internal cache that the AI must realize holds dirty user data
processing_queue = []

@app.on_event("startup")
async def startup():
    await database.connect()

@app.on_event("shutdown")
async def shutdown():
    await database.disconnect()

@app.post("/register-telemetry")
async def register_telemetry(request: Request, background_tasks: BackgroundTasks):
    # Payload: {"device_id": "DEV-99", "patch_notes": "1'; DROP TABLE users;--"}
    payload = await request.json()
    
    # Context 1: Looks safe. Data is appended to an in-memory queue.
    processing_queue.append(payload)
    
    # Context 2: Trigger async background processing
    background_tasks.add_task(flush_telemetry_pipeline)
    return {"status": "queued"}

async def flush_telemetry_pipeline():
    if not processing_queue:
        return
    
    # Dequeue the raw user payload
    item = processing_queue.pop(0)
    notes = item.get("patch_notes")
    
    # VULNERABILITY: Second-Order Flow.
    # The AI must trace 'patch_notes' from the API request, into the list, 
    # out of the list, and into this raw string interpolation across async threads.
    query = f"UPDATE device_logs SET logs = 'Processed' WHERE notes = '{notes}'"
    
    await database.execute(query=query)
const express = require('express');
const { exec } = require('child_process');
const app = express();
app.use(express.json());

// Unsafe deep merge utility common in legacy code or custom utilities
function unsafeMerge(target, source) {
    for (let key in source) {
        if (source.hasOwnProperty(key)) {
            
            if (typeof target[key] === 'object' && typeof source[key] === 'object') {
                unsafeMerge(target[key], source[key]);
            } else {
                target[key] = source[key];
            }
        }
    }
    return target;
}

app.post('/api/config', (req, res) => {
    let userSessionConfig = {};
    
    
    Payload to pollute the global Object prototype:
    {
        "__proto__": {
            "shell": "node",
            "env": { "NODE_OPTIONS": "--inspect-brk=0.0.0.0:4444" }
        }
    }
    
    unsafeMerge(userSessionConfig, req.body);
    
    res.json({ status: "Config updated" });
});

app.get('/api/system-status', (req, res) => {
    
    exec('echo "System Active"', (error, stdout, stderr) => {
        res.json({ output: stdout.trim() });
    });
});

app.listen(3000);
