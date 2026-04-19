/*
 * ============================================================================
 *                    NEXUS OS SCHEDULER - AEGIS TUI DASHBOARD
 *                        High-Performance Terminal Interface
 * ============================================================================
 *  Author      : Zain Iqbal,Mohsin Ali,Tahir Nawaz
 *  Description : A stable, sleek ncurses-based dashboard that visualizes
 *                the OS Resource Scheduler state in real-time. Uses only
 *                standard ncurses ACS characters for maximum compatibility.
 * ============================================================================
 */

#include "common.h"
#include <ncurses.h>
#include <locale.h>

/* ============================================================================
 *                              UI CONFIGURATION
 * ============================================================================ */

#define MAX_LOG_ENTRIES     10          /* Number of log entries to display       */
#define LOG_BUFFER_SIZE     256         /* Max length of each log message         */
#define PROGRESS_WIDTH      25          /* Width of progress bars                 */

/* Minimum terminal dimensions */
#define MIN_TERM_COLS       80
#define MIN_TERM_ROWS       24

/* Layout constants */
#define HEADER_HEIGHT       5           /* Height of header section               */
#define FOOTER_HEIGHT       3           /* Height of footer section               */
#define LOG_HEIGHT          8           /* Height of log section                  */

/* ============================================================================
 *                              COLOR PAIR DEFINITIONS
 * ============================================================================ */

enum ColorPairs {
    PAIR_DEFAULT = 1,
    PAIR_HEADER,
    PAIR_BORDER,
    PAIR_HIGH_PRIORITY,
    PAIR_MED_PRIORITY,
    PAIR_LOW_PRIORITY,
    PAIR_WORKER_IDLE,
    PAIR_WORKER_BUSY,
    PAIR_PROGRESS_FILL,
    PAIR_PROGRESS_EMPTY,
    PAIR_EMERGENCY,
    PAIR_SUCCESS,
    PAIR_WARNING,
    PAIR_INFO,
    PAIR_TITLE,
    PAIR_HEARTBEAT_OK,
    PAIR_HEARTBEAT_LOST
};

/* ============================================================================
 *                              GLOBAL STATE
 * ============================================================================ */

/* Shared Memory Connection */
static int              g_shmid = -1;
static SystemState      *g_state = NULL;

/* NCurses Windows */
static WINDOW   *win_header = NULL;
static WINDOW   *win_queue = NULL;
static WINDOW   *win_workers = NULL;
static WINDOW   *win_logs = NULL;
static WINDOW   *win_footer = NULL;

/* UI State */
static int      g_running = 1;
static int      g_emergency_mode = 0;
static int      g_heartbeat_pulse = 0;      /* Toggles for heartbeat animation    */
static time_t   g_last_pulse_time = 0;      /* Track pulse timing                 */
static int      g_term_rows = 0;
static int      g_term_cols = 0;

/* Log Ring Buffer */
static char     g_log_buffer[MAX_LOG_ENTRIES][LOG_BUFFER_SIZE];
static int      g_log_head = 0;
static int      g_log_count = 0;


/* ============================================================================
 *                           FORWARD DECLARATIONS
 * ============================================================================ */

/* Initialization & Cleanup */
static int  init_ncurses(void);
static int  init_shared_memory_connection(void);
static void cleanup(void);

/* Window Management */
static void create_windows(void);
static void destroy_windows(void);
static void resize_handler(void);

/* Drawing Functions */
static void draw_header(void);
static void draw_queue(void);
static void draw_workers(void);
static void draw_logs(void);
static void draw_footer(void);
static void draw_all(void);

/* Data Functions */
static int  check_heartbeat(void);
static void update_heartbeat_pulse(void);
static void add_log_entry(const char *format, ...);

/* Utility Drawing Helpers */
static void draw_box_with_title(WINDOW *win, const char *title, int color_pair);
static void draw_progress_bar(WINDOW *win, int y, int x, int width, int progress);
static void draw_horizontal_line(WINDOW *win, int y, int x, int width);

/* Input Handling */
static void handle_input(void);


/* ============================================================================
 *                         INITIALIZATION FUNCTIONS
 * ============================================================================ */

/*
 * Initialize ncurses library with all required settings.
 * Returns: 0 on success, -1 on failure
 */
static int init_ncurses(void) {
    /* Set locale for proper character handling */
    setlocale(LC_ALL, "");

    /* Initialize ncurses */
    if (initscr() == NULL) {
        fprintf(stderr, "Error: Failed to initialize ncurses\n");
        return -1;
    }

    /* Check for color support */
    if (!has_colors()) {
        endwin();
        fprintf(stderr, "Error: Terminal does not support colors\n");
        return -1;
    }

    /* Enable color mode */
    start_color();
    use_default_colors();

    /* Configure ncurses behavior */
    cbreak();               /* Disable line buffering                         */
    noecho();               /* Don't echo typed characters                    */
    keypad(stdscr, TRUE);   /* Enable special keys                            */
    curs_set(0);            /* Hide cursor                                    */
    nodelay(stdscr, TRUE);  /* Non-blocking getch()                           */

    /* Get terminal dimensions */
    getmaxyx(stdscr, g_term_rows, g_term_cols);

    /* Check minimum size */
    if (g_term_cols < MIN_TERM_COLS || g_term_rows < MIN_TERM_ROWS) {
        endwin();
        fprintf(stderr, "Error: Terminal too small. Minimum: %dx%d\n", 
                MIN_TERM_COLS, MIN_TERM_ROWS);
        return -1;
    }

    /* Initialize color pairs */
    init_pair(PAIR_DEFAULT,         COLOR_WHITE,    -1);
    init_pair(PAIR_HEADER,          COLOR_CYAN,     -1);
    init_pair(PAIR_BORDER,          COLOR_BLUE,     -1);
    init_pair(PAIR_HIGH_PRIORITY,   COLOR_RED,      -1);
    init_pair(PAIR_MED_PRIORITY,    COLOR_YELLOW,   -1);
    init_pair(PAIR_LOW_PRIORITY,    COLOR_CYAN,     -1);
    init_pair(PAIR_WORKER_IDLE,     COLOR_BLUE,     -1);
    init_pair(PAIR_WORKER_BUSY,     COLOR_GREEN,    -1);
    init_pair(PAIR_PROGRESS_FILL,   COLOR_GREEN,    -1);
    init_pair(PAIR_PROGRESS_EMPTY,  COLOR_WHITE,    -1);
    init_pair(PAIR_EMERGENCY,       COLOR_RED,      -1);
    init_pair(PAIR_SUCCESS,         COLOR_GREEN,    -1);
    init_pair(PAIR_WARNING,         COLOR_YELLOW,   -1);
    init_pair(PAIR_INFO,            COLOR_CYAN,     -1);
    init_pair(PAIR_TITLE,           COLOR_MAGENTA,  -1);
    init_pair(PAIR_HEARTBEAT_OK,    COLOR_GREEN,    -1);
    init_pair(PAIR_HEARTBEAT_LOST,  COLOR_RED,      -1);

    /* Initialize pulse timing */
    g_last_pulse_time = time(NULL);

    add_log_entry("SYSTEM: Aegis Dashboard initialized");
    add_log_entry("SYSTEM: Terminal size %dx%d", g_term_cols, g_term_rows);

    return 0;
}

/*
 * Connect to shared memory segment.
 * Returns: 0 on success, -1 on failure
 */
static int init_shared_memory_connection(void) {
    /* Get existing shared memory segment */
    g_shmid = shmget(SHM_KEY, sizeof(SystemState), 0666);
    if (g_shmid == -1) {
        add_log_entry("ERROR: Cannot find shared memory (0x%04X)", SHM_KEY);
        add_log_entry("ERROR: Is the Master process running?");
        return -1;
    }

    /* Attach to shared memory */
    g_state = (SystemState *)shmat(g_shmid, NULL, 0);
    if (g_state == (void *)-1) {
        add_log_entry("ERROR: Failed to attach shared memory");
        g_state = NULL;
        return -1;
    }

    add_log_entry("SYSTEM: Connected to shared memory (0x%04X)", SHM_KEY);
    return 0;
}

/*
 * Cleanup function - safely detach and release resources.
 */
static void cleanup(void) {
    destroy_windows();
    endwin();

    if (g_state != NULL && g_state != (void *)-1) {
        shmdt(g_state);
        g_state = NULL;
    }

    printf("\n[AEGIS] Dashboard terminated safely.\n");
}


/* ============================================================================
 *                         WINDOW MANAGEMENT FUNCTIONS
 * ============================================================================ */

/*
 * Create all ncurses windows based on current terminal dimensions.
 * Uses adaptive layout calculations.
 */
static void create_windows(void) {
    /* Recalculate dimensions */
    getmaxyx(stdscr, g_term_rows, g_term_cols);

    /* Calculate panel dimensions */
    int queue_width = g_term_cols / 2;
    int workers_width = g_term_cols - queue_width;
    int main_height = g_term_rows - HEADER_HEIGHT - LOG_HEIGHT - FOOTER_HEIGHT;

    /* Ensure minimum heights */
    if (main_height < 10) main_height = 10;

    /* Create windows with calculated dimensions */
    win_header = newwin(HEADER_HEIGHT, g_term_cols, 0, 0);
    win_queue = newwin(main_height, queue_width, HEADER_HEIGHT, 0);
    win_workers = newwin(main_height, workers_width, HEADER_HEIGHT, queue_width);
    win_logs = newwin(LOG_HEIGHT, g_term_cols, HEADER_HEIGHT + main_height, 0);
    win_footer = newwin(FOOTER_HEIGHT, g_term_cols, g_term_rows - FOOTER_HEIGHT, 0);

    /* Enable keypad for all windows */
    if (win_header)  keypad(win_header, TRUE);
    if (win_queue)   keypad(win_queue, TRUE);
    if (win_workers) keypad(win_workers, TRUE);
    if (win_logs)    keypad(win_logs, TRUE);
    if (win_footer)  keypad(win_footer, TRUE);
}

/*
 * Destroy all ncurses windows.
 */
static void destroy_windows(void) {
    if (win_header)  { delwin(win_header);  win_header = NULL; }
    if (win_queue)   { delwin(win_queue);   win_queue = NULL; }
    if (win_workers) { delwin(win_workers); win_workers = NULL; }
    if (win_logs)    { delwin(win_logs);    win_logs = NULL; }
    if (win_footer)  { delwin(win_footer);  win_footer = NULL; }
}

/*
 * Handle terminal resize events.
 */
static void resize_handler(void) {
    /* Get new dimensions */
    getmaxyx(stdscr, g_term_rows, g_term_cols);

    /* Recreate windows */
    destroy_windows();
    clear();
    refresh();
    create_windows();

    add_log_entry("UI: Resized to %dx%d", g_term_cols, g_term_rows);
}


/* ============================================================================
 *                         DATA FUNCTIONS
 * ============================================================================ */

/*
 * Check heartbeat and determine if system is responsive.
 * Returns: 1 if heartbeat is healthy, 0 if lost
 */
static int check_heartbeat(void) {
    if (g_state == NULL) return 0;

    time_t current_time = time(NULL);
    long diff_sec = (long)difftime(current_time, g_state->last_heartbeat);

    /* Heartbeat timeout is 2 seconds */
    return (diff_sec < 3);
}

/*
 * Update heartbeat pulse animation (toggles every 500ms).
 */
static void update_heartbeat_pulse(void) {
    if (g_state == NULL) return;

    time_t current_time = time(NULL);
    
    /* Toggle pulse based on heartbeat timestamp */
    if (current_time != g_last_pulse_time) {
        g_heartbeat_pulse = !g_heartbeat_pulse;
        g_last_pulse_time = current_time;
    }

    /* Update emergency mode */
    g_emergency_mode = !check_heartbeat();
}

/*
 * Add a log entry to the ring buffer.
 */
static void add_log_entry(const char *format, ...) {
    va_list args;
    char timestamp[16];
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);

    strftime(timestamp, sizeof(timestamp), "%H:%M:%S", tm_info);

    char message[200];  /* Leave room for timestamp + brackets */
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    snprintf(g_log_buffer[g_log_head], LOG_BUFFER_SIZE, "[%s] %s", timestamp, message);

    g_log_head = (g_log_head + 1) % MAX_LOG_ENTRIES;
    if (g_log_count < MAX_LOG_ENTRIES) {
        g_log_count++;
    }
}


/* ============================================================================
 *                         UTILITY DRAWING HELPERS
 * ============================================================================ */

/*
 * Draw a box with standard ACS characters and centered title.
 */
static void draw_box_with_title(WINDOW *win, const char *title, int color_pair) {
    int height, width;
    getmaxyx(win, height, width);
    (void)height;

    wattron(win, COLOR_PAIR(color_pair));
    box(win, 0, 0);

    if (title != NULL && strlen(title) > 0) {
        int title_len = (int)strlen(title);
        int title_x = (width - title_len - 4) / 2;
        if (title_x < 1) title_x = 1;

        wattron(win, A_BOLD);
        mvwprintw(win, 0, title_x, "[ %s ]", title);
        wattroff(win, A_BOLD);
    }

    wattroff(win, COLOR_PAIR(color_pair));
}

/*
 * Draw a horizontal line using ACS characters.
 */
static void draw_horizontal_line(WINDOW *win, int y, int x, int width) {
    mvwhline(win, y, x, ACS_HLINE, width);
}

/*
 * Draw a solid progress bar using ACS_CKBOARD.
 */
static void draw_progress_bar(WINDOW *win, int y, int x, int width, int progress) {
    if (progress < 0) progress = 0;
    if (progress > 100) progress = 100;

    int filled = (progress * width) / 100;

    /* Draw brackets */
    mvwaddch(win, y, x, '[');
    mvwaddch(win, y, x + width + 1, ']');

    /* Draw filled portion */
    wattron(win, COLOR_PAIR(PAIR_PROGRESS_FILL) | A_BOLD);
    for (int i = 0; i < filled; i++) {
        mvwaddch(win, y, x + 1 + i, ACS_CKBOARD);
    }
    wattroff(win, COLOR_PAIR(PAIR_PROGRESS_FILL) | A_BOLD);

    /* Draw empty portion */
    wattron(win, COLOR_PAIR(PAIR_PROGRESS_EMPTY) | A_DIM);
    for (int i = filled; i < width; i++) {
        mvwaddch(win, y, x + 1 + i, ACS_BULLET);
    }
    wattroff(win, COLOR_PAIR(PAIR_PROGRESS_EMPTY) | A_DIM);

    /* Draw percentage */
    mvwprintw(win, y, x + width + 3, "%3d%%", progress);
}


/* ============================================================================
 *                         MAIN DRAWING FUNCTIONS
 * ============================================================================ */

/*
 * Draw the header section with title, uptime, and status.
 */
static void draw_header(void) {
    if (!win_header) return;
    werase(win_header);

    int height, width;
    getmaxyx(win_header, height, width);
    (void)height;

    int border_color = g_emergency_mode ? PAIR_EMERGENCY : PAIR_HEADER;
    draw_box_with_title(win_header, "NEXUS OS SCHEDULER - AEGIS DASHBOARD", border_color);

    /* Draw title line */
    wattron(win_header, COLOR_PAIR(PAIR_TITLE) | A_BOLD);
    mvwprintw(win_header, 2, (width - 42) / 2, 
              "=== REAL-TIME RESOURCE SCHEDULER MONITOR ===");
    wattroff(win_header, COLOR_PAIR(PAIR_TITLE) | A_BOLD);

    /* System Status indicator */
    if (g_state != NULL && g_state->running) {
        wattron(win_header, COLOR_PAIR(PAIR_SUCCESS) | A_BOLD);
        mvwprintw(win_header, 3, 3, "[*] SYSTEM ACTIVE");
        wattroff(win_header, COLOR_PAIR(PAIR_SUCCESS) | A_BOLD);
    } else {
        wattron(win_header, COLOR_PAIR(PAIR_WARNING) | A_BOLD);
        mvwprintw(win_header, 3, 3, "[ ] SYSTEM HALTED");
        wattroff(win_header, COLOR_PAIR(PAIR_WARNING) | A_BOLD);
    }

    /* System Uptime clock */
    if (g_state != NULL) {
        time_t uptime = time(NULL) - g_state->system_start_time;
        int hours = (int)(uptime / 3600);
        int minutes = (int)((uptime % 3600) / 60);
        int seconds = (int)(uptime % 60);

        wattron(win_header, COLOR_PAIR(PAIR_INFO) | A_BOLD);
        mvwprintw(win_header, 3, width - 25, "UPTIME: %02d:%02d:%02d", 
                  hours, minutes, seconds);
        wattroff(win_header, COLOR_PAIR(PAIR_INFO) | A_BOLD);
    }

    wnoutrefresh(win_header);
}

/*
 * Draw the Ready Queue panel.
 */
static void draw_queue(void) {
    if (!win_queue) return;
    werase(win_queue);

    int height, width;
    getmaxyx(win_queue, height, width);

    int border_color = g_emergency_mode ? PAIR_EMERGENCY : PAIR_BORDER;
    draw_box_with_title(win_queue, "READY QUEUE", border_color);

    if (g_state == NULL) {
        wattron(win_queue, COLOR_PAIR(PAIR_WARNING) | A_BOLD);
        mvwprintw(win_queue, height/2, (width-15)/2, "NO CONNECTION");
        wattroff(win_queue, COLOR_PAIR(PAIR_WARNING) | A_BOLD);
        wnoutrefresh(win_queue);
        return;
    }

    /* Column headers */
    wattron(win_queue, A_BOLD | A_UNDERLINE);
    mvwprintw(win_queue, 2, 2, "ID    PRIORITY   BURST   STATUS");
    wattroff(win_queue, A_BOLD | A_UNDERLINE);

    draw_horizontal_line(win_queue, 3, 1, width - 2);

    int jobs_displayed = 0;
    int max_display = height - 6;

    /* Empty queue message */
    if (g_state->queue_count == 0) {
        wattron(win_queue, COLOR_PAIR(PAIR_INFO) | A_DIM);
        mvwprintw(win_queue, height/2, (width-20)/2, "-- Queue Empty --");
        wattroff(win_queue, COLOR_PAIR(PAIR_INFO) | A_DIM);
        wnoutrefresh(win_queue);
        return;
    }

    /* Display jobs */
    for (int i = 0; i < MAX_JOBS_IN_QUEUE && jobs_displayed < max_display; i++) {
        int idx = (g_state->queue_head + i) % MAX_JOBS_IN_QUEUE;
        Job *job = &g_state->job_queue[idx];

        if (job->state == JOB_STATE_EMPTY) continue;

        int y = 4 + jobs_displayed;

        /* Job ID */
        wattron(win_queue, A_BOLD);
        mvwprintw(win_queue, y, 2, "%03d", job->id);
        wattroff(win_queue, A_BOLD);

        /* Priority with color */
        int pcolor;
        const char *plabel;
        switch (job->priority) {
            case PRIORITY_HIGH:
                pcolor = PAIR_HIGH_PRIORITY;
                plabel = "HIGH";
                break;
            case PRIORITY_MEDIUM:
                pcolor = PAIR_MED_PRIORITY;
                plabel = "MED ";
                break;
            default:
                pcolor = PAIR_LOW_PRIORITY;
                plabel = "LOW ";
                break;
        }

        wattron(win_queue, COLOR_PAIR(pcolor) | A_BOLD);
        mvwprintw(win_queue, y, 8, "[%s]", plabel);
        wattroff(win_queue, COLOR_PAIR(pcolor) | A_BOLD);

        /* Burst time */
        mvwprintw(win_queue, y, 19, "%2ds", job->burst_time);

        /* Status */
        const char *status;
        int scolor;
        if (job->state == JOB_STATE_RUNNING) {
            status = "ACTIVE";
            scolor = PAIR_SUCCESS;
        } else {
            status = "QUEUED";
            scolor = PAIR_INFO;
        }

        wattron(win_queue, COLOR_PAIR(scolor));
        mvwprintw(win_queue, y, 26, "%s", status);
        wattroff(win_queue, COLOR_PAIR(scolor));

        jobs_displayed++;
    }

    /* Queue count at bottom */
    wattron(win_queue, COLOR_PAIR(PAIR_INFO) | A_DIM);
    mvwprintw(win_queue, height - 2, 2, "Total: %d/%d", 
              g_state->queue_count, MAX_JOBS_IN_QUEUE);
    wattroff(win_queue, COLOR_PAIR(PAIR_INFO) | A_DIM);

    wnoutrefresh(win_queue);
}

/*
 * Draw the CPU Workers panel.
 */
static void draw_workers(void) {
    if (!win_workers) return;
    werase(win_workers);

    int height, width;
    getmaxyx(win_workers, height, width);

    int border_color = g_emergency_mode ? PAIR_EMERGENCY : PAIR_BORDER;
    draw_box_with_title(win_workers, "CPU WORKERS", border_color);

    if (g_state == NULL) {
        wattron(win_workers, COLOR_PAIR(PAIR_WARNING) | A_BOLD);
        mvwprintw(win_workers, height/2, (width-15)/2, "NO CONNECTION");
        wattroff(win_workers, COLOR_PAIR(PAIR_WARNING) | A_BOLD);
        wnoutrefresh(win_workers);
        return;
    }

    /* Calculate worker box dimensions */
    int worker_height = (height - 4) / NUM_WORKERS;
    if (worker_height < 5) worker_height = 5;

    for (int i = 0; i < NUM_WORKERS; i++) {
        int box_y = 2 + i * worker_height;
        int box_x = 2;
        int box_w = width - 4;
        int box_h = worker_height - 1;

        /* Draw worker sub-box using ACS characters */
        wattron(win_workers, COLOR_PAIR(PAIR_BORDER));
        
        /* Top border */
        mvwaddch(win_workers, box_y, box_x, ACS_ULCORNER);
        mvwhline(win_workers, box_y, box_x + 1, ACS_HLINE, box_w - 2);
        mvwaddch(win_workers, box_y, box_x + box_w - 1, ACS_URCORNER);
        
        /* Side borders */
        for (int r = 1; r < box_h - 1; r++) {
            mvwaddch(win_workers, box_y + r, box_x, ACS_VLINE);
            mvwaddch(win_workers, box_y + r, box_x + box_w - 1, ACS_VLINE);
        }
        
        /* Bottom border */
        mvwaddch(win_workers, box_y + box_h - 1, box_x, ACS_LLCORNER);
        mvwhline(win_workers, box_y + box_h - 1, box_x + 1, ACS_HLINE, box_w - 2);
        mvwaddch(win_workers, box_y + box_h - 1, box_x + box_w - 1, ACS_LRCORNER);
        
        wattroff(win_workers, COLOR_PAIR(PAIR_BORDER));

        /* Worker title */
        wattron(win_workers, COLOR_PAIR(PAIR_HEADER) | A_BOLD);
        mvwprintw(win_workers, box_y, box_x + 2, "[ WORKER %d ]", i);
        wattroff(win_workers, COLOR_PAIR(PAIR_HEADER) | A_BOLD);

        /* Worker PID */
        if (g_state->worker_pids[i] > 0) {
            mvwprintw(win_workers, box_y + 1, box_x + 2, "PID: %d", 
                      g_state->worker_pids[i]);
        } else {
            wattron(win_workers, A_DIM);
            mvwprintw(win_workers, box_y + 1, box_x + 2, "PID: -----");
            wattroff(win_workers, A_DIM);
        }

        /* Worker status */
        const char *status_str;
        int status_color;
        char status_char;

        switch (g_state->worker_status[i]) {
            case WORKER_BUSY:
                status_str = "BUSY";
                status_color = PAIR_WORKER_BUSY;
                status_char = '*';
                break;
            case WORKER_STARTING:
                status_str = "INIT";
                status_color = PAIR_INFO;
                status_char = '~';
                break;
            case WORKER_STOPPING:
                status_str = "STOP";
                status_color = PAIR_WARNING;
                status_char = '!';
                break;
            case WORKER_ERROR:
                status_str = "ERR!";
                status_color = PAIR_EMERGENCY;
                status_char = 'X';
                break;
            default:
                status_str = "IDLE";
                status_color = PAIR_WORKER_IDLE;
                status_char = 'o';
                break;
        }

        wattron(win_workers, COLOR_PAIR(status_color) | A_BOLD);
        mvwprintw(win_workers, box_y + 1, box_x + box_w - 12, "[%c] %s", 
                  status_char, status_str);
        wattroff(win_workers, COLOR_PAIR(status_color) | A_BOLD);

        /* Current job info */
        if (g_state->current_job_id[i] >= 0) {
            wattron(win_workers, COLOR_PAIR(PAIR_SUCCESS));
            mvwprintw(win_workers, box_y + 2, box_x + 2, "Job #%03d", 
                      g_state->current_job_id[i]);
            wattroff(win_workers, COLOR_PAIR(PAIR_SUCCESS));
        } else {
            wattron(win_workers, A_DIM);
            mvwprintw(win_workers, box_y + 2, box_x + 2, "No active job");
            wattroff(win_workers, A_DIM);
        }

        /* Jobs completed count */
        mvwprintw(win_workers, box_y + 2, box_x + box_w - 12, "Done: %d", 
                  g_state->worker_jobs_done[i]);

        /* Progress bar */
        int progress = g_state->worker_progress[i];
        int bar_width = box_w - 12;
        if (bar_width > PROGRESS_WIDTH) bar_width = PROGRESS_WIDTH;
        if (bar_width < 10) bar_width = 10;

        if (box_h > 4) {
            draw_progress_bar(win_workers, box_y + 3, box_x + 2, bar_width, progress);
        }
    }

    wnoutrefresh(win_workers);
}

/*
 * Draw the diagnostics/logs panel.
 */
static void draw_logs(void) {
    if (!win_logs) return;
    werase(win_logs);

    int height, width;
    getmaxyx(win_logs, height, width);

    int border_color = g_emergency_mode ? PAIR_EMERGENCY : PAIR_BORDER;
    draw_box_with_title(win_logs, "SYSTEM DIAGNOSTICS", border_color);

    /* Display log entries */
    int display_count = (g_log_count < height - 2) ? g_log_count : height - 2;
    int start_idx = (g_log_count <= MAX_LOG_ENTRIES) ? 0 : g_log_head;

    for (int i = 0; i < display_count; i++) {
        int idx = (start_idx + g_log_count - display_count + i) % MAX_LOG_ENTRIES;
        if (idx < 0) idx += MAX_LOG_ENTRIES;
        
        char *entry = g_log_buffer[idx];

        /* Color based on log type */
        int color = PAIR_DEFAULT;
        if (strstr(entry, "ERROR") || strstr(entry, "CRITICAL")) {
            color = PAIR_EMERGENCY;
        } else if (strstr(entry, "WARNING")) {
            color = PAIR_WARNING;
        } else if (strstr(entry, "SYSTEM") || strstr(entry, "UI")) {
            color = PAIR_INFO;
        }

        wattron(win_logs, COLOR_PAIR(color));
        mvwprintw(win_logs, 1 + i, 2, "%.*s", width - 4, entry);
        wattroff(win_logs, COLOR_PAIR(color));
    }

    wnoutrefresh(win_logs);
}

/*
 * Draw the footer with controls and heartbeat indicator.
 */
static void draw_footer(void) {
    if (!win_footer) return;
    werase(win_footer);

    int height, width;
    getmaxyx(win_footer, height, width);
    (void)height;

    /* Top border */
    int border_color = g_emergency_mode ? PAIR_EMERGENCY : PAIR_BORDER;
    wattron(win_footer, COLOR_PAIR(border_color));
    mvwhline(win_footer, 0, 0, ACS_HLINE, width);
    wattroff(win_footer, COLOR_PAIR(border_color));

    /* Controls */
    wattron(win_footer, COLOR_PAIR(PAIR_WARNING) | A_BOLD);
    mvwprintw(win_footer, 1, 2, "[Q] Quit  [R] Reconnect");
    wattroff(win_footer, COLOR_PAIR(PAIR_WARNING) | A_BOLD);

    /* Statistics */
    if (g_state != NULL) {
        mvwprintw(win_footer, 1, width/2 - 20, 
                  "Jobs: %d submitted | %d completed",
                  g_state->total_jobs_submitted,
                  g_state->total_jobs_done);
    }

    /* Heartbeat indicator with pulsing effect */
    int hb_ok = check_heartbeat();
    int hb_color = hb_ok ? PAIR_HEARTBEAT_OK : PAIR_HEARTBEAT_LOST;
    
    /* Pulse character: filled or empty circle based on toggle */
    char pulse_char = (g_heartbeat_pulse) ? '*' : 'o';
    
    if (!hb_ok) {
        /* Lost heartbeat - blink effect */
        pulse_char = (g_heartbeat_pulse) ? '!' : ' ';
    }

    wattron(win_footer, COLOR_PAIR(hb_color) | A_BOLD);
    if (hb_ok) {
        mvwprintw(win_footer, 1, width - 18, "[%c] HEARTBEAT OK", pulse_char);
    } else {
        wattron(win_footer, A_BLINK);
        mvwprintw(win_footer, 1, width - 20, "[%c] HEARTBEAT LOST", pulse_char);
        wattroff(win_footer, A_BLINK);
    }
    wattroff(win_footer, COLOR_PAIR(hb_color) | A_BOLD);

    wnoutrefresh(win_footer);
}

/*
 * Master draw function - refreshes all windows.
 */
static void draw_all(void) {
    /* Update heartbeat pulse state */
    update_heartbeat_pulse();

    /* Draw all panels */
    draw_header();
    draw_queue();
    draw_workers();
    draw_logs();
    draw_footer();

    /* Commit all updates */
    doupdate();
}


/* ============================================================================
 *                         INPUT HANDLING
 * ============================================================================ */

/*
 * Handle keyboard input.
 */
static void handle_input(void) {
    int ch = getch();

    switch (ch) {
        case 'q':
        case 'Q':
            g_running = 0;
            add_log_entry("UI: Shutdown requested by user");
            break;

        case KEY_RESIZE:
            resize_handler();
            break;

        case 'r':
        case 'R':
            /* Force reconnect to shared memory */
            if (g_state != NULL) {
                shmdt(g_state);
                g_state = NULL;
            }
            add_log_entry("UI: Reconnecting to shared memory...");
            if (init_shared_memory_connection() == 0) {
                add_log_entry("UI: Reconnection successful");
            }
            break;

        default:
            break;
    }
}


/* ============================================================================
 *                              MAIN FUNCTION
 * ============================================================================ */

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    /* Register cleanup on exit */
    atexit(cleanup);

    /* Initialize ncurses */
    if (init_ncurses() != 0) {
        fprintf(stderr, "Failed to initialize ncurses\n");
        return EXIT_FAILURE;
    }

    /* Create windows */
    create_windows();

    /* Initial draw */
    add_log_entry("UI: Attempting to connect to scheduler...");
    draw_all();

    /* Connect to shared memory */
    if (init_shared_memory_connection() != 0) {
        add_log_entry("WARNING: Running without shared memory connection");
    }

    /* Main loop - 100ms refresh rate */
    while (g_running) {
        handle_input();
        draw_all();
        usleep(100000);  /* 100ms */
    }

    return EXIT_SUCCESS;
}


/* ============================================================================
 *                              END OF FILE
 * ============================================================================
 *
 * COMPILATION:
 *   gcc -o ui ui.c -lncurses -lpthread
 *
 * FEATURES:
 *   - 100% ncurses ACS characters (no encoding issues)
 *   - Pulsing heartbeat indicator
 *   - Adaptive window layout
 *   - Solid progress bars using ACS_CKBOARD
 *   - Professional borders
 *   - Real-time shared memory visualization
 *   - Terminal resize support
 *
 * ============================================================================ */
