#pragma once

#include "types.h"
#include "fsm.h"
#include "log.h"
#include "wasm/kernel.h"
#include "wasm/parser.h"
#include "cli.h"
#include "nn/advisor.h"
#include "nn/train.h"
#include <climits>
#include <functional>
#include <map>
#include <unordered_map>
#include <filesystem>

#include <string>
#include <vector>
#include <cstdint>

// automatic transition threshold; tests reference this value to drive
// evolution/training cycles without hard-coding the number.
static constexpr int kAutoTrainGen = 50;

// ── App ───────────────────────────────────────────────────────────────────────
//
// Top-level orchestrator.  Drives the BootFsm, coordinates the WasmKernel,
// mutation engine, and AppLogger.  Call update() once per frame.
// ─────────────────────────────────────────────────────────────────────────────

class App {
public:
    App();
    // constructor with optional custom time source (used by unit tests)
    explicit App(const CliOptions& opts,
                 std::function<uint64_t()> nowFn = nullptr);

    // Drive the state machine.  Returns false when the app should exit.
    bool update();

    // ── Accessors for the renderer ────────────────────────────────────────────
    SystemState  state()               const { return m_fsm.current(); }
    int          generation()          const { return m_generation; }
    double       uptimeSec()           const { return m_uptimeMs / 1000.0; }
    int          retryCount()          const { return m_retryCount; }
    int          evolutionAttempts()   const { return m_evolutionAttempts; }
    int          programCounter()      const { return m_programCounter; }
    bool         isPaused()            const { return m_paused; }
    int          focusAddr()           const { return m_focusAddr; }
    int          focusLen()            const { return m_focusLen; }
    bool         isMemoryGrowing()     const { return m_memGrowing; }
    bool         isSystemReading()     const { return m_sysReading; }
    size_t       kernelBytes()         const;

    const std::deque<LogEntry>&               logs()         const { return m_logger.logs(); }
    const std::vector<Instruction>&           instructions() const { return m_instructions; }
    const std::string&                        currentKernel() const { return m_currentKernel; }
    const std::string&                        stableKernel()  const { return m_stableKernel; }
    const std::vector<std::vector<uint8_t>>&  knownInstructions() const { return m_knownInstructions; }
    int                                       knownInstructionCount() const { return (int)m_knownInstructions.size(); }

    void togglePause() { m_paused = !m_paused; }

    // multi-instance support
    int instanceCount() const { return (int)m_instances.size(); }
    const std::vector<std::string>& instances() const { return m_instances; }
    // helper that directly records a spawned kernel (used by tests or host)
    void spawnInstance(const std::string& kernel);

    // remove the instance at the given index (no-op if out of range)
    void killInstance(int index);

    // invoked by the WasmKernel host import when a kernel requests a kill
    void handleKillRequest(int32_t idx);

    // convenient logging wrapper for UI and tests; preferring this avoids
    // callers having to grab the internal logger and bypass the const
    // guarantee of `logs()`.
    void log(const std::string& msg, const std::string& type = "info") {
        m_logger.log(msg, type);
    }

    // Build and return a telemetry report string.
    std::string exportHistory() const;

    // Execute a callback with a per-run timeout.  On platforms that support
    // it this will fork a child process and kill it if it exceeds the
    // configured `CliOptions::maxExecMs`.  Returns true if the callback
    // completed successfully before the deadline; false otherwise.
    bool runWithTimeout(const std::function<void()>& fn);

    // Request that the application shut itself down at the next convenient
    // opportunity.  This sets an internal flag so that `update()` will return
    // false soon after the current generation completes.  It is safe to call
    // from a signal handler (invoked via `requestAppExit()` below).
    void requestExit();

    // manually trigger telemetry export for the current generation
    void exportNow();

    // Unique identifier for this run; used to organise exported data.
    const std::string& runId() const { return m_runId; }

    // access CLI options
    const CliOptions& options() const { return m_opts; }

    // expose trainer for tests
    const Trainer& trainer() const { return m_trainer; }

    // expose advisor for GUI or tests
    const Advisor& advisor() const { return m_advisor; }

    // public wrapper to the protected telemetryRoot() helper; used by the
    // GUI to display the effective directory being scanned for telemetry.
    std::filesystem::path telemetryRootPublic() const { return telemetryRoot(); }

    // helper used by tests to apply a telemetry entry and optionally
    // persist the model via CLI flag.  The optional `mutSeq` carries the raw
    // bytes of the mutation that produced this kernel so the log can show
    // which instructions actually changed.
    void trainAndMaybeSave(const TelemetryEntry& te,
                           const std::vector<uint8_t>& mutSeq = {});

    // expose reboot helper for tests
    void doReboot(bool success);

    // ── Training phase (shown in GUI before evolution begins) ─────────────────
    TrainingPhase trainingPhase()    const { return m_trainingPhase; }
    bool          trainingDone()     const { return m_trainingPhase == TrainingPhase::COMPLETE; }
    // 0.0 = not started, 1.0 = fully complete
    float         trainingProgress() const;
    // call from GUI when the user clicks "Start Evolution"
    void          enableEvolution();

    // query whether the evolution FSM is currently enabled (used by GUI)
    bool          evolutionEnabled() const { return m_evolutionEnabled; }

    // test helpers
    // simulate a boot failure triggered by the given mutation sequence
    // (calls private handleBootFailure internally)
    void test_simulateFailure(const std::string& reason,
                               const std::vector<uint8_t>& mutation) {
        m_pendingMutation = mutation;
        handleBootFailure(reason);
    }

    // Test-only helpers to manipulate internal state directly.
    void test_forceEvolutionEnabled(bool v) { m_evolutionEnabled = v; }
    void test_forceTrainingPhase(TrainingPhase p) { m_trainingPhase = p; }
    void test_forceModelSaved(bool v) { m_modelSaved = v; }
    // expose internal counters for unit tests
    int test_trainingStep() const { return m_trainingStep; }
    int test_trainingLoadEnd() const { return m_trainingLoadEnd; }

    // model saving info (bridge to private members defined later)
    bool savingModel() const;
    bool modelSaved() const;
    // progress of the countdown before writing a checkpoint; ranges from 0.0
    // (not started) to 1.0 (ready to save or finished).  This value is useful
    // for the GUI to display a secondary progress bar during save.
    float saveProgress() const;
    int  test_savePhase() const;

    // Blacklist management
    bool isBlacklisted(const std::vector<uint8_t>& seq) const;
    void addToBlacklist(const std::vector<uint8_t>& seq);

    // return advisor safety score in [0,1] for a candidate mutation sequence.
    // This convenience wrapper exists so callers (tests/gui) don't need to
    // reach through `advisor()` themselves.
    float scoreSequence(const std::vector<uint8_t>& seq) const;

    // decay all weights by one; entries reaching zero are removed
    void decayBlacklist();

    // telemetry accessors (for tests or GUI)
    int mutationsApplied() const { return m_mutationsApplied; }
    int mutationInsertCount() const { return m_mutationInsert; }
    int mutationDeleteCount() const { return m_mutationDelete; }
    int mutationModifyCount() const { return m_mutationModify; }
    int mutationAddCount() const { return m_mutationAdd; }
    double lastGenDurationMs() const { return m_lastGenDurationMs; }
    int kernelSizeMin() const { return m_kernelSizeMin; }
    int kernelSizeMax() const { return m_kernelSizeMax; }
    const std::string& lastTrapReason() const { return m_lastTrapReason; }

    // Persist blacklist across runs
    ~App();                             // flush blacklist on destruction
    void loadBlacklist();
    void saveBlacklist() const;

    // helpers used by tests to validate constructor path decisions

    // recompute training step counts from the current advisor entries
    // and reset the phase to LOADING; used when telemetry is reloaded.
    void          prepareTrainingSteps();
    std::filesystem::path logsDir() const { return m_logsDir; }
    std::filesystem::path seqBaseDir() const { return m_seqBase; }

protected:
    // compute base directory for telemetry/logs using executable path;
    // CLI override (telemetryDir) is appended to this location.
    // e.g. returns <exe_dir>/bin/seq when runId is empty.
    std::filesystem::path telemetryRoot() const;

private:

private:
    // FSM helpers
    void transitionTo(SystemState s);

    // Training tick: advance the startup RL training phase by one step.
    // Called every frame from update() until training is complete.
    void tickTraining();

    // Boot sequence steps (called from update())
    void startBoot();
    void tickBooting();
    void tickLoading();
    void tickExecuting();
    void tickVerifying();
    void tickRepairing();

    // Export helpers
    void autoExport();

    // WASM host callbacks
    void onWasmLog(uint32_t ptr, uint32_t len, const uint8_t* mem, uint32_t memSize);
    void onGrowMemory(uint32_t pages);
    // called when the kernel requests a spawn; ptr/len refer to base64 in
    // kernel memory.
    void handleSpawnRequest(uint32_t ptr, uint32_t len);
    void handleBootFailure(const std::string& reason);

    // ── Components ────────────────────────────────────────────────────────────
    BootFsm   m_fsm;
    AppLogger m_logger;
    WasmKernel m_kernel;

    // directory paths computed during construction (exposed for tests)
    std::filesystem::path m_logsDir;
    std::filesystem::path m_seqBase;

    // for learning & advice
    Advisor m_advisor;
    Trainer m_trainer;

    // ── State ─────────────────────────────────────────────────────────────────
    // era tracking removed; visual themes not required
    bool      m_paused = false;

    int    m_generation        = 0;
    double m_uptimeMs          = 0.0;
    int    m_retryCount        = 0;
    int    m_evolutionAttempts = 0;
    // telemetry counters
    int    m_mutationsApplied = 0;
    int    m_mutationInsert   = 0;
    int    m_mutationDelete   = 0;
    int    m_mutationModify   = 0;
    int    m_mutationAdd      = 0;
    // checkpoint/save state (values used by App::tickTraining())
    bool   m_modelSaved       = false;
    bool   m_savingModel      = false;
    int    m_savePhase        = 0;
    // heuristic blacklist: sequences that previously caused traps, mapped to decay weight
    // weight >0 means entry is still blacklisted; DECAY mode will decrement after each success
    struct VecHash {
        size_t operator()(const std::vector<uint8_t>& v) const noexcept {
            // FNV-1a 64-bit
            size_t h = 1469598103934665603ULL;
            for (uint8_t b : v) {
                h ^= b;
                h *= 1099511628211ULL;
            }
            return h;
        }
    };
    std::unordered_map<std::vector<uint8_t>, int, VecHash> m_blacklist;
    // profiling / telemetry timing
    uint64_t m_genStartTime   = 0; // steady ticks at generation start
    double   m_lastGenDurationMs = 0.0;
    int      m_kernelSizeMin  = INT_MAX;
    int      m_kernelSizeMax  = 0;
    std::string m_lastTrapReason;
    int    m_programCounter    = -1;

    std::string m_stableKernel;
    std::string m_currentKernel;
    std::string m_nextKernel;    // deferred on successful quine

    // cache the decoded bytes for the current kernel; updated whenever
    // `m_currentKernel` changes.  This avoids repeated base64 decoding in
    // kernelBytes() and other accessors.
    std::vector<uint8_t> m_currentKernelBytes;

    std::vector<Instruction>              m_instructions;
    std::vector<std::vector<uint8_t>>     m_knownInstructions;
    std::vector<uint8_t>                  m_pendingMutation;
    std::vector<std::string>              m_instances;

    int  m_focusAddr  = 0;
    int  m_focusLen   = 0;
    bool m_memGrowing = false;
    bool m_sysReading = false;

    // Per-tick timing
    uint64_t m_lastFrameTicks  = 0;
    int      m_loadingProgress = 0;
    int      m_instrIndex      = 0;
    bool     m_callExecuted    = false;
    bool     m_quineSuccess    = false;

    // Identifier for this session; created at startup
    std::string m_runId;

    uint64_t m_memGrowFlashUntil = 0;

    // CLI options supplied at startup
    CliOptions m_opts;

    // internal flag used by requestExit() and signal handlers
    bool m_shouldExit = false;

    // ── Startup training phase ────────────────────────────────────────────────
    // In GUI mode the FSM is held in IDLE until the user clicks "Start
    // Evolution".  Training advances one step per update() call.
    TrainingPhase m_trainingPhase    = TrainingPhase::LOADING;
    bool          m_evolutionEnabled = false; // set by enableEvolution()
    int           m_trainingStep     = 0;     // monotonically-increasing step counter
    int           m_trainingTotal    = 0;     // total steps (set in constructor)
    int           m_trainingLoadEnd  = 0;     // step at which LOADING phase ends


    // optional time source (default uses SDL_GetTicks).  tests inject a fake
    // clock so that run-time limits can be verified deterministically.
    std::function<uint64_t()> m_nowFn;


    uint64_t now() const;

    // utility used internally whenever the base64 kernel string is updated.
    // decodes into `m_currentKernelBytes` and refreshes `m_instructions`.
    void updateKernelData();
};

// helper invoked by the signal handler or tests to request a graceful shutdown.
void requestAppExit();
