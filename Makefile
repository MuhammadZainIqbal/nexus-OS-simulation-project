# ╔══════════════════════════════════════════════════════════════════════════════╗
# ║                    NEXUS OS SCHEDULER - MAKEFILE                             ║
# ║                      Build Automation Script                                 ║
# ╠══════════════════════════════════════════════════════════════════════════════╣
# ║  Author      : Zain Iqbal                                                    ║
# ║  Description : Makefile for compiling the OS Resource Scheduler project      ║
# ║                demonstrating IPC, Shared Memory, Message Queues, and         ║
# ║                Multi-threading concepts.                                     ║
# ╚══════════════════════════════════════════════════════════════════════════════╝

# ═══════════════════════════════════════════════════════════════════════════════
#                              COMPILER SETTINGS
# ═══════════════════════════════════════════════════════════════════════════════

CC          = gcc
CFLAGS      = -Wall -Wextra -pedantic -std=c11 -g
LDFLAGS_COMMON = -lpthread
LDFLAGS_UI  = -lncurses -lpthread

# ═══════════════════════════════════════════════════════════════════════════════
#                              TARGET BINARIES
# ═══════════════════════════════════════════════════════════════════════════════

MASTER      = master
WORKER      = worker
UI          = ui

# All targets
ALL_TARGETS = $(MASTER) $(WORKER) $(UI)

# ═══════════════════════════════════════════════════════════════════════════════
#                              SOURCE FILES
# ═══════════════════════════════════════════════════════════════════════════════

MASTER_SRC  = master.c
WORKER_SRC  = worker.c
UI_SRC      = ui.c
HEADER      = common.h

# ═══════════════════════════════════════════════════════════════════════════════
#                              BUILD RULES
# ═══════════════════════════════════════════════════════════════════════════════

# Default target - build everything
.PHONY: all
all: $(ALL_TARGETS)
	@echo ""
	@echo "╔══════════════════════════════════════════════════════════════╗"
	@echo "║            BUILD SUCCESSFUL - ALL TARGETS READY             ║"
	@echo "╠══════════════════════════════════════════════════════════════╣"
	@echo "║  Binaries created:                                          ║"
	@echo "║    • ./master  - Master scheduler process                   ║"
	@echo "║    • ./worker  - Worker executor process                    ║"
	@echo "║    • ./ui      - TUI dashboard                              ║"
	@echo "╠══════════════════════════════════════════════════════════════╣"
	@echo "║  To run:                                                    ║"
	@echo "║    Terminal 1: make run                                     ║"
	@echo "║    Terminal 2: ./ui                                         ║"
	@echo "╚══════════════════════════════════════════════════════════════╝"
	@echo ""

# Master process
$(MASTER): $(MASTER_SRC) $(HEADER)
	@echo "[CC] Compiling $(MASTER)..."
	$(CC) $(CFLAGS) -o $@ $(MASTER_SRC) $(LDFLAGS_COMMON)
	@echo "[OK] $(MASTER) built successfully"

# Worker process
$(WORKER): $(WORKER_SRC) $(HEADER)
	@echo "[CC] Compiling $(WORKER)..."
	$(CC) $(CFLAGS) -o $@ $(WORKER_SRC) $(LDFLAGS_COMMON)
	@echo "[OK] $(WORKER) built successfully"

# UI process (requires ncurses)
$(UI): $(UI_SRC) $(HEADER)
	@echo "[CC] Compiling $(UI)..."
	$(CC) $(CFLAGS) -o $@ $(UI_SRC) $(LDFLAGS_UI)
	@echo "[OK] $(UI) built successfully"

# ═══════════════════════════════════════════════════════════════════════════════
#                              RUN TARGETS
# ═══════════════════════════════════════════════════════════════════════════════

# Run master (spawns workers automatically)
.PHONY: run
run: all clean-ipc
	@echo ""
	@echo "╔══════════════════════════════════════════════════════════════╗"
	@echo "║              STARTING NEXUS OS SCHEDULER                    ║"
	@echo "╠══════════════════════════════════════════════════════════════╣"
	@echo "║  Press Ctrl+C to shutdown gracefully                        ║"
	@echo "║  Run './ui' in another terminal for the dashboard           ║"
	@echo "╚══════════════════════════════════════════════════════════════╝"
	@echo ""
	./$(MASTER)

# Run UI only (master must be running)
.PHONY: run-ui
run-ui: $(UI)
	./$(UI)

# Run everything in demo mode (requires tmux or multiple terminals)
.PHONY: demo
demo: all clean-ipc
	@echo "Starting demo mode..."
	@echo "Run './ui' in a separate terminal to see the dashboard"
	./$(MASTER)

# ═══════════════════════════════════════════════════════════════════════════════
#                              CLEANUP TARGETS
# ═══════════════════════════════════════════════════════════════════════════════

# Clean compiled binaries
.PHONY: clean
clean:
	@echo "[CLEAN] Removing binaries..."
	rm -f $(ALL_TARGETS)
	@echo "[CLEAN] Removing log files..."
	rm -f system_log.txt
	@echo "[OK] Clean complete"

# Clean IPC resources (shared memory and message queues)
.PHONY: clean-ipc
clean-ipc:
	@echo "[IPC] Cleaning shared memory segments..."
	-ipcrm --all=shm 2>/dev/null || true
	@echo "[IPC] Cleaning message queues..."
	-ipcrm --all=msg 2>/dev/null || true
	@echo "[OK] IPC resources cleaned"

# Full clean - binaries, logs, and IPC
.PHONY: distclean
distclean: clean clean-ipc
	@echo "[OK] Full cleanup complete"

# Reset everything and rebuild
.PHONY: rebuild
rebuild: distclean all

# ═══════════════════════════════════════════════════════════════════════════════
#                              UTILITY TARGETS
# ═══════════════════════════════════════════════════════════════════════════════

# Show IPC status
.PHONY: ipc-status
ipc-status:
	@echo ""
	@echo "╔══════════════════════════════════════════════════════════════╗"
	@echo "║                    IPC RESOURCE STATUS                      ║"
	@echo "╚══════════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "=== Shared Memory Segments ==="
	@ipcs -m
	@echo ""
	@echo "=== Message Queues ==="
	@ipcs -q
	@echo ""

# Show help
.PHONY: help
help:
	@echo ""
	@echo "╔══════════════════════════════════════════════════════════════╗"
	@echo "║              NEXUS OS SCHEDULER - MAKEFILE HELP             ║"
	@echo "╚══════════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "  Build Targets:"
	@echo "    make            - Build all binaries (master, worker, ui)"
	@echo "    make master     - Build master process only"
	@echo "    make worker     - Build worker process only"
	@echo "    make ui         - Build UI dashboard only"
	@echo ""
	@echo "  Run Targets:"
	@echo "    make run        - Clean IPC and run master (spawns workers)"
	@echo "    make run-ui     - Run UI dashboard (master must be running)"
	@echo "    make demo       - Same as 'make run'"
	@echo ""
	@echo "  Cleanup Targets:"
	@echo "    make clean      - Remove binaries and log files"
	@echo "    make clean-ipc  - Remove IPC resources (shm, msg queues)"
	@echo "    make distclean  - Full cleanup (binaries + IPC)"
	@echo "    make rebuild    - Clean everything and rebuild"
	@echo ""
	@echo "  Utility Targets:"
	@echo "    make ipc-status - Show current IPC resources"
	@echo "    make help       - Show this help message"
	@echo ""
	@echo "  Quick Start:"
	@echo "    1. make                 # Build everything"
	@echo "    2. make run             # Start scheduler (Terminal 1)"
	@echo "    3. ./ui                 # Start dashboard (Terminal 2)"
	@echo ""

# ═══════════════════════════════════════════════════════════════════════════════
#                              DEBUG TARGETS
# ═══════════════════════════════════════════════════════════════════════════════

# Build with debug symbols and no optimization
.PHONY: debug
debug: CFLAGS += -O0 -DDEBUG
debug: clean all
	@echo "[DEBUG] Debug build complete"

# Build with extra warnings
.PHONY: paranoid
paranoid: CFLAGS += -Werror -Wshadow -Wconversion -Wformat=2
paranoid: clean all
	@echo "[PARANOID] Strict build complete"

# ═══════════════════════════════════════════════════════════════════════════════
#                              INSTALL TARGET
# ═══════════════════════════════════════════════════════════════════════════════

PREFIX ?= /usr/local/bin

.PHONY: install
install: all
	@echo "[INSTALL] Installing to $(PREFIX)..."
	install -m 755 $(MASTER) $(PREFIX)/nexus-master
	install -m 755 $(WORKER) $(PREFIX)/nexus-worker
	install -m 755 $(UI) $(PREFIX)/nexus-ui
	@echo "[OK] Installation complete"

.PHONY: uninstall
uninstall:
	@echo "[UNINSTALL] Removing from $(PREFIX)..."
	rm -f $(PREFIX)/nexus-master
	rm -f $(PREFIX)/nexus-worker
	rm -f $(PREFIX)/nexus-ui
	@echo "[OK] Uninstallation complete"

# ═══════════════════════════════════════════════════════════════════════════════
#                              END OF MAKEFILE
# ═══════════════════════════════════════════════════════════════════════════════
