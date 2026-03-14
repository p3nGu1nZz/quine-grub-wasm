#include "app.h"
#include "constants.h"
#include "base64.h"
#include "util.h"
#include "exporter.h"
#include "nn/feature.h"
#include "wasm/evolution.h"
#include "wasm/parser.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <filesystem>
#include <functional>

// ── Training-phase constants ─────────────────────────────────────────────────
// Shared by both the constructor (to compute m_trainingTotal) and tickTraining.
static constexpr int kTrainMinAnimSteps = 30;
static constexpr int kTrainEpochs       = 5;
#include <atomic>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

// Global pointer used by the signal handler to notify the running App
static App* g_appInstance = nullptr;
// also keep an atomic flag for simple checks
static std::atomic<bool> g_shouldExitFlag{false};

// Forwarded function called from external code (e.g. signal handler)
// to request the application cleanly terminate.
void requestAppExit() {
    g_shouldExitFlag.store(true);
    if (g_appInstance) {
        g_appInstance->requestExit();
    }
}


// ─── App ─────────────────────────────────────────────────────────────────────

App::App()
    : App(CliOptions{})
{
}

App::~App() {
    // persist blacklist on shutdown
    saveBlacklist();
    // clear global pointer so signal handler won't dereference it
    if (g_appInstance == this) g_appInstance = nullptr;
}

App::App(const CliOptions& opts, std::function<uint64_t()> nowFn)
    : m_opts(opts)
{
    // register global pointer immediately so signals can be handled
    g_appInstance = this;

    // choose time source
    if (nowFn) {
        m_nowFn = std::move(nowFn);
    } else {
        m_nowFn = [](){ return static_cast<uint64_t>(SDL_GetTicks()); };
    }
    if (m_opts.kernelType == KernelType::SEQ) {
        m_stableKernel   = KERNEL_SEQ;
        m_currentKernel  = KERNEL_SEQ;
    } else {
        m_stableKernel   = KERNEL_GLOB;
        m_currentKernel  = KERNEL_GLOB;
    }
    m_lastFrameTicks = now();

    // initialise telemetry bounds
    m_kernelSizeMin = INT_MAX;
    m_kernelSizeMax = 0;

    // session identifier used to group exports
    m_runId = nowFileStamp();

    // sanitize telemetry directory override early, warn if invalid
    if (!m_opts.telemetryDir.empty()) {
        std::string clean = sanitizeRelativePath(m_opts.telemetryDir);
        if (clean.empty()) {
            m_logger.log("WARNING: invalid telemetryDir '" + m_opts.telemetryDir + "', falling back to default", "warning");
            m_opts.telemetryDir.clear();
        } else {
            m_opts.telemetryDir = clean;
        }
    }

    // compute the base directory where bin/logs and bin/seq live.  By
    // default this is derived from the executable path rather than the
    // current working directory, which prevents stray "bin/" folders when
    // launching from the repo root.  Users may override the telemetry
    // subdirectory via --telemetry-dir; the override is interpreted relative
    // to the executable directory.
    namespace fs = std::filesystem;
    fs::path exeDir = fs::path(executableDir());
    // if we're running from a "test" or "bin" subdirectory, step up one
    // level so telemetry lands alongside the primary binary rather than
    // nested under build/<target>/test or build/<target>/bin/bin.
    fs::path root = exeDir;
    auto fname = exeDir.filename().string();
    if (fname == "test" || fname == "bin")
        root = exeDir.parent_path();

    fs::path logsDir = root / "bin" / "logs";
    fs::path seqBase = root / "bin" / "seq";
    if (!m_opts.telemetryDir.empty())
        seqBase = root / m_opts.telemetryDir;

    // remember for tests
    m_logsDir = logsDir;
    m_seqBase = seqBase;

    // create advisor using the telemetry base (without runId appended)
    m_advisor = Advisor(seqBase.string());
    // if heuristic blacklist is active we expect to store entries; reserve
    // some initial capacity to avoid rehash churn.
    if (m_opts.heuristic != HeuristicMode::NONE) {
        m_blacklist.reserve(512);
    }
    // load model if requested, or auto-load the most recent checkpoint
    if (!m_opts.loadModelPath.empty()) {
        if (!m_trainer.load(m_opts.loadModelPath)) {
            m_logger.log("WARNING: failed to load model from " + m_opts.loadModelPath, "warning");
        } else {
            m_logger.log("Loaded model from " + m_opts.loadModelPath, "info");
        }
    } else {
        // look for an auto-saved checkpoint from a previous run
        auto cpPath = telemetryRoot() / "model_checkpoint.dat";
        if (fs::exists(cpPath)) {
            if (m_trainer.load(cpPath.string())) {
                m_logger.log("Auto-loaded model checkpoint from " + cpPath.string(), "info");
            }
        }
    }

    // Determine training steps based on advisor entries.  This logic is
    // used at startup and when we reload telemetry after an evolution run.
    prepareTrainingSteps();

    // In headless mode (no GUI) there is no training dashboard: complete
    // training synchronously and allow evolution to begin immediately.
    if (!m_opts.useGui) {
        for (const auto& e : m_advisor.entries())
            m_trainer.observe(e);
        m_trainingPhase    = TrainingPhase::COMPLETE;
        m_evolutionEnabled = true;
    }

    // remove any stray files left in the working directory by older versions
    // (bootloader_*.log, quine_telemetry_gen*.txt)
    {
        namespace fs = std::filesystem;
        for (auto& entry : fs::directory_iterator(fs::current_path())) {
            if (!entry.is_regular_file()) continue;
            auto name = entry.path().filename().string();
            if ((name.rfind("bootloader_", 0) == 0 && name.find(".log") != std::string::npos) ||
                (name.rfind("quine_telemetry_gen", 0) == 0 && name.find(".txt") != std::string::npos)) {
                fs::remove(entry.path());
            }
        }
    }

    // Ensure necessary directories exist beneath the executable root
    namespace fs = std::filesystem;
    fs::create_directories(logsDir);
    fs::create_directories(seqBase / m_runId);

    // load any persisted heuristic blacklist from previous sessions
    loadBlacklist();

    // Open buffered log file (flushes every ~1 s; always flushed on exit/signal)
    m_logger.init((logsDir / ("bootloader_" + nowFileStamp() + ".log")).string());

    // Parse initial kernel and populate the instruction list; this also
    // fills the byte cache used by `kernelBytes()`.
    updateKernelData();
}

uint64_t App::now() const {
    // use injected callback (or SDL ticks by default)
    return m_nowFn();
}

// decode the current base64 kernel and refresh the parsed instruction
// list.  callers must call this whenever m_currentKernel mutates.
void App::updateKernelData() {
    m_currentKernelBytes = base64_decode(m_currentKernel);
    m_instructions = extractCodeSection(m_currentKernelBytes);
}

// Recalculate the training/animation step counts from current advisor
// entries and reset the training phase to LOADING.  This is invoked at
// startup and also whenever we reload telemetry midway through a run.
void App::prepareTrainingSteps() {
    int nEntries = (int)m_advisor.size();
    int loadSteps  = std::max(kTrainMinAnimSteps, nEntries);
    int trainSteps = std::max(kTrainMinAnimSteps, nEntries * kTrainEpochs);
    m_trainingLoadEnd = loadSteps;
    m_trainingTotal   = loadSteps + trainSteps;
    m_trainingStep    = 0;
    m_trainingPhase   = TrainingPhase::LOADING;
}

size_t App::kernelBytes() const {
    return base64_decode(m_currentKernel).size();
}

// ─── FSM helpers ─────────────────────────────────────────────────────────────

void App::transitionTo(SystemState s) {
    m_fsm.transition(s);
}


// ─── Main update (called every frame) ────────────────────────────────────────

bool App::update() {
    if (m_shouldExit) return false;

    uint64_t t  = now();
    uint64_t dt = t - m_lastFrameTicks;
    m_lastFrameTicks = t;

    if (!m_paused)
        m_uptimeMs += static_cast<double>(dt);

    // enforce run-time limit if requested.  we check here so that both GUI
    // and headless loops will eventually see `update()` return false (and
    // `m_shouldExit` will be set).  the check is made before the state
    // machine so we abort as soon as the budget is exceeded.
    if (m_opts.maxRunMs > 0 && m_uptimeMs >= m_opts.maxRunMs) {
        m_logger.log("Max-run-ms limit reached (" + std::to_string(m_opts.maxRunMs) + " ms)", "info");
        m_shouldExit = true;
        return false;
    }

    if (m_memGrowing && t >= m_memGrowFlashUntil)
        m_memGrowing = false;

    if (m_paused) return true;

    // Advance the startup training phase before running the evolution FSM.
    // In GUI mode the FSM is gated behind m_evolutionEnabled (set when the
    // user clicks "Start Evolution").  In headless mode both flags are set
    // in the constructor so this path is a no-op.
    if (!m_evolutionEnabled) {
        tickTraining();
        // only re-enable evolution after training is complete AND the
        // model checkpoint has been saved (or attempted).  This ensures
        // the save countdown inside tickTraining() runs to completion
        // before the FSM takes over.
        if (m_trainingPhase == TrainingPhase::COMPLETE && m_modelSaved) {
            m_evolutionEnabled = true;
        }
        return true;
    }

    switch (m_fsm.current()) {
        case SystemState::IDLE:             startBoot();      break;
        case SystemState::BOOTING:          tickBooting();    break;
        case SystemState::LOADING_KERNEL:   tickLoading();    break;
        case SystemState::EXECUTING:        tickExecuting();  break;
        case SystemState::VERIFYING_QUINE:  tickVerifying();  break;
        case SystemState::REPAIRING:        tickRepairing();  break;
        case SystemState::SYSTEM_HALT:                        break;
    }

    return true;
}

// ─── Training phase ──────────────────────────────────────────────────────────

float App::trainingProgress() const {
    if (m_trainingPhase == TrainingPhase::COMPLETE) return 1.0f;
    if (m_trainingTotal <= 0) return 1.0f;
    float p = static_cast<float>(m_trainingStep) / static_cast<float>(m_trainingTotal);
    return p > 1.0f ? 1.0f : p;
}

bool App::savingModel() const { return m_savingModel; }
bool App::modelSaved() const { return m_modelSaved; }
int App::test_savePhase() const { return m_savePhase; }

float App::saveProgress() const {
    // if model has already been saved, report full progress
    if (m_modelSaved) return 1.0f;
    // mirror the countdown logic in tickTraining(); the hard-coded saveWait
    // value must stay in sync.
    const int saveWait = 3; // number of update calls to wait
    if (!m_savingModel) return 0.0f;
    // m_savePhase is incremented *after* first entering the save state, so
    // progress ranges from 1..saveWait; normalise to [0,1].
    float prog = static_cast<float>(m_savePhase) / static_cast<float>(saveWait);
    return prog > 1.0f ? 1.0f : prog;
}

void App::enableEvolution() {
    m_trainingPhase    = TrainingPhase::COMPLETE;
    m_evolutionEnabled = true;
}

void App::tickTraining() {
    if (m_trainingPhase == TrainingPhase::COMPLETE) {
        if (!m_modelSaved) {
            // begin or advance the save countdown
            if (!m_savingModel) {
                m_savingModel = true;
                m_savePhase = 0;
            }
            const int saveWait = 3; // number of update calls to wait
            if (m_savePhase < saveWait) {
                m_savePhase++;
            } else {
                // perform actual save once countdown finishes
                // build a filesystem path for the checkpoint and convert to
                // string only when logging.  assign to `auto` so we keep the
                // path type and can call `.string()` later.
                auto path = telemetryRoot() / "model_checkpoint.dat";
                if (m_trainer.save(path.string())) {
                    m_logger.log("Saved model checkpoint to " + path.string(), "info");
                } else {
                    m_logger.log("WARNING: failed to save model checkpoint", "warning");
                }
                m_modelSaved = true;
                m_savingModel = false;
            }
        }
        return;
    }

    const auto& entries = m_advisor.entries();
    int nEntries = (int)entries.size();

    m_trainingStep++;

    if (m_trainingPhase == TrainingPhase::LOADING) {
        if (m_trainingStep >= m_trainingLoadEnd) {
            m_trainingPhase = TrainingPhase::TRAINING;
        }
    } else if (m_trainingPhase == TrainingPhase::TRAINING) {
        // Observe one entry per step (cycling through entries)
        if (!entries.empty()) {
            // trainIdx is 1-based within the TRAINING phase
            int trainIdx = m_trainingStep - m_trainingLoadEnd;
            int idx = (trainIdx - 1) % nEntries;
            m_trainer.observe(entries[idx]);
        }
        if (m_trainingStep >= m_trainingTotal) {
            m_trainingPhase = TrainingPhase::COMPLETE;
        }
    }
}

// ─── Boot sequence steps ─────────────────────────────────────────────────────

void App::startBoot() {
    transitionTo(SystemState::BOOTING);
    m_logger.log("--- BOOT SEQUENCE INITIATED ---", "system");
    m_instrIndex     = 0;
    m_callExecuted   = false;
    m_quineSuccess   = false;
    m_programCounter = -1;
    m_focusAddr      = 0;
    m_focusLen       = 0;
    m_sysReading     = false;
    m_kernel.terminate();
}

void App::tickBooting() {
    uint64_t bootSpeed = static_cast<uint64_t>(std::max(50, 400 - m_generation * 5));
    if (m_fsm.elapsedMs() >= bootSpeed) {
        transitionTo(SystemState::LOADING_KERNEL);
        m_loadingProgress = 0;
        int kbytes = static_cast<int>(kernelBytes());
        m_logger.log("Loading Kernel Image: " + std::to_string(kbytes) + " bytes", "info");
    }
}

void App::tickLoading() {
    static constexpr int LOAD_STEP = 8;
    int kbytes = static_cast<int>(kernelBytes());

    if (m_loadingProgress < kbytes) {
        m_focusAddr        = m_loadingProgress;
        m_focusLen         = LOAD_STEP;
        m_loadingProgress += LOAD_STEP;
        return;
    }

    m_focusAddr = 0;
    m_focusLen  = 0;

    m_logger.log("Instantiating Module...", "info");
    try {
        m_kernel.bootDynamic(
            m_currentKernel,
            [this](uint32_t ptr, uint32_t len, const uint8_t* mem, uint32_t msz) {
                onWasmLog(ptr, len, mem, msz);
            },
            [this](uint32_t pages) {
                onGrowMemory(pages);
            },
            [this](uint32_t ptr, uint32_t len) {
                handleSpawnRequest(ptr, len);
            },
            {}, // weight callback is unused here
            [this](int32_t idx) {
                handleKillRequest(idx);
            }
        );
    } catch (const std::exception& e) {
        handleBootFailure(std::string("Module load failed: ") + e.what());
        return;
    }

    if (!m_kernel.isLoaded()) {
        handleBootFailure("Instance lost during boot");
        return;
    }

    transitionTo(SystemState::EXECUTING);
    m_instrIndex   = 0;
    m_callExecuted = false;
}

void App::tickExecuting() {
    uint64_t stepSpeed   = static_cast<uint64_t>(std::max(80, 200 - m_generation * 2));
    uint64_t elapsed     = m_fsm.elapsedMs();
    int      expectedIdx = static_cast<int>(elapsed / stepSpeed);
    if (m_instrIndex > expectedIdx) return;

    if (m_instructions.empty()) {
        if (!m_callExecuted) {
            m_logger.log("EXEC: Blind Run (Parser unavailable)", "warning");
            try {
                m_kernel.runDynamic(m_currentKernel);
            } catch (const std::exception& e) {
                handleBootFailure(e.what());
            }
            m_callExecuted = true;
        }
        return;
    }

    if (m_instrIndex >= static_cast<int>(m_instructions.size())) {
        if (!m_callExecuted) {
            m_logger.log("EXEC: end of instruction stream, executing kernel", "info");
            try {
                m_kernel.runDynamic(m_currentKernel);
            } catch (const std::exception& e) {
                handleBootFailure(e.what());
            }
            m_callExecuted = true;
        }
        return;
    }

    const auto& inst = m_instructions[m_instrIndex];
    m_programCounter  = m_instrIndex;
    m_focusAddr       = inst.originalOffset;
    m_focusLen        = std::max(1, inst.length);

    // Previous versions used the presence of a CALL opcode as a signal to
    // invoke `runDynamic`.  This proved fragile: mutations often insert
    // extraneous `call` instructions with invalid indices, causing the
    // WASM runtime to trap early and the host to reboot prematurely.  The
    // kernel no longer relies on this mechanism; the host will execute the
    // `run` export once the instruction stream has been fully stepped.
    // Therefore we ignore CALL opcodes here and handle execution below.

    m_instrIndex++;
}

bool App::runWithTimeout(const std::function<void()>& fn) {
    if (m_opts.maxExecMs <= 0) {
        fn();
        return true;
    }

#ifndef _WIN32
    pid_t pid = fork();
    if (pid == 0) {
        // child
        try {
            fn();
            _exit(0);
        } catch (...) {
            _exit(1);
        }
    } else if (pid > 0) {
        int status = 0;
        int waited = 0;
        while (waited < m_opts.maxExecMs) {
            pid_t r = waitpid(pid, &status, WNOHANG);
            if (r == pid) {
                if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return true;
                return false;
            }
            usleep(1000);
            waited++;
        }
        // timeout
        m_logger.log("EXECUTION: kernel timeout, killing child", "error");
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        return false;
    } else {
        m_logger.log("EXECUTION: fork failed for timeout watchdog", "error");
        return false;
    }
#else
    // Windows: no fork, just run and hope for the best
    fn();
    return true;
#endif
}

void App::requestExit() {
    m_shouldExit = true;
}

void App::trainAndMaybeSave(const TelemetryEntry& te,
                            const std::vector<uint8_t>& mutSeq) {
    // run the full sequence through a policy copy so the LSTM processes
    // every opcode, not just the last one.  The copy is necessary because
    // we need resetState() which is non-const.
    float predBefore = 0.0f;
    int seqLen = 0;
    if (!te.kernelBase64.empty()) {
        auto seq = Feature::extractSequence(te);
        seqLen = (int)seq.size();
        if (!seq.empty()) {
            Policy pol = m_trainer.policy(); // copy
            pol.resetState();
            for (auto op : seq) {
                std::vector<float> feat(kFeatSize, 0.0f);
                if (op < kFeatSize) feat[op] = 1.0f;
                auto out = pol.forward(feat);
                predBefore = out.empty() ? 0.0f : out[0];
            }
        } else {
            auto feat = Feature::extract(te);
            auto out = m_trainer.policy().forward(feat);
            predBefore = out.empty() ? 0.0f : out[0];
        }
    }

    m_trainer.observe(te);

    // build a compact description of the mutation opcodes
    std::string mutDesc;
    if (!mutSeq.empty()) {
        auto insts = parseInstructions(mutSeq.data(), mutSeq.size());
        for (size_t i = 0; i < insts.size(); ++i) {
            if (i) mutDesc += ",";
            mutDesc += getOpcodeName(insts[i].opcode);
        }
    }

    // log NN feedback: generation, mutation, model prediction, loss
    {
        float reward = static_cast<float>(te.generation);
        float loss = m_trainer.lastLoss();
        float avgLoss = m_trainer.avgLoss();
        int obs = m_trainer.observations();
        std::ostringstream ss;
        ss << std::fixed;
        ss << "NN: gen=" << te.generation
           << " seq=" << seqLen;
        if (!mutDesc.empty()) {
            ss << " mut=[" << mutDesc << "]";
        }
        ss << " pred=" << std::setprecision(4) << predBefore
           << " reward=" << std::setprecision(1) << reward
           << " loss=" << std::setprecision(6) << loss
           << " avgLoss=" << std::setprecision(6) << avgLoss
           << " obs=" << obs
           << (m_trainer.test_lastUsedSequence() ? " [SEQ]" : " [HIST]");
        m_logger.log(ss.str(), "info");
    }

    if (!m_opts.saveModelPath.empty()) {
        m_trainer.save(m_opts.saveModelPath);
    }
}

void App::tickVerifying() {
    if (m_fsm.elapsedMs() >= static_cast<uint64_t>(DEFAULT_BOOT_CONFIG.rebootDelayMs))
        doReboot(true);
}

void App::tickRepairing() {
    if (m_fsm.elapsedMs() >= 1500)
        doReboot(false);
}

// ─── WASM callbacks ───────────────────────────────────────────────────────────

void App::onWasmLog(uint32_t ptr, uint32_t len,
                    const uint8_t* mem, uint32_t memSize)
{
    if (ptr + len > memSize) {
        handleBootFailure("WASM log out of bounds");
        return;
    }

    std::string output(reinterpret_cast<const char*>(mem + ptr), len);

    m_logger.log("STDOUT: Received " + std::to_string(len) +
                 " bytes from 0x" + [&]{
                     std::ostringstream ss;
                     ss << std::uppercase << std::hex << std::setw(4)
                        << std::setfill('0') << ptr;
                     return ss.str();
                 }(), "info");

    if (output == m_currentKernel) {
        m_logger.log("VERIFICATION: MEMORY INTEGRITY CONFIRMED", "success");
        m_logger.log("EXEC: QUINE SUCCESS -> INITIATING REBOOT...", "system");

        m_stableKernel = m_currentKernel;
        m_retryCount   = 0;
        m_logger.addHistory({ m_generation, nowIso(), (int)kernelBytes(),
                               "EXECUTE", "Verification Success", true });

        // Evolve
        try {
            int seed = m_generation + 1;
            auto evo = evolveBinary(m_currentKernel, m_knownInstructions, seed,
                                        m_opts.mutationStrategy);
            // ask the advisor to score the candidate sequence
            {
                float sc = m_advisor.score(evo.mutationSequence);
                m_logger.log("ADVISOR SCORE: " + std::to_string(sc), "info");
                if (sc < 0.05f) {
                    m_logger.log("ADVISOR: extremely low score, rerolling", "warning");
                    seed++;
                    evo = evolveBinary(m_currentKernel, m_knownInstructions, seed,
                                        m_opts.mutationStrategy);
                }
            }
            // if the mutation is blacklisted and heuristic enabled, retry a few times
            int tries = 0;
            while (m_opts.heuristic != HeuristicMode::NONE &&
                   !evo.mutationSequence.empty() &&
                   isBlacklisted(evo.mutationSequence) && tries < 8) {
                m_logger.log("EVOLUTION: mutation sequence blacklisted, reroll", "warning");
                seed++;
                evo = evolveBinary(m_currentKernel, m_knownInstructions, seed,
                                        m_opts.mutationStrategy);
                tries++;
            }
            auto evolved = base64_decode(evo.binary);
            if (evolved.size() < 8 || evolved[0] != 0x00 || evolved[1] != 0x61 ||
                evolved[2] != 0x73 || evolved[3] != 0x6D)
                throw std::runtime_error("Invalid WASM magic after evolution");

            m_nextKernel      = evo.binary;
            m_pendingMutation = evo.mutationSequence;

            m_evolutionAttempts++;
            if (!evo.mutationSequence.empty()) {
                m_mutationsApplied++;
                switch (evo.actionUsed) {
                    case EvolutionAction::MODIFY: m_mutationModify++; break;
                    case EvolutionAction::INSERT: m_mutationInsert++; break;
                    case EvolutionAction::ADD   : m_mutationAdd++; break;
                    case EvolutionAction::DELETE: m_mutationDelete++; break;
                }
            }
            m_logger.log("EVOLUTION: " + evo.description, "mutation");
            m_logger.addHistory({ m_generation, nowIso(), (int)kernelBytes(),
                                   "EVOLVE", evo.description, true });
            // train on this generation
            {
                TelemetryEntry te;
                te.generation = m_generation;
                te.kernelBase64 = m_currentKernel;
                te.trapCode = m_lastTrapReason;
                trainAndMaybeSave(te, evo.mutationSequence);
            }
        } catch (const EvolutionException& ee) {
            std::string msg = std::string("EVOLUTION REJECTED: ") + ee.what();
            if (!ee.binary.empty())
                msg += " candidate=" + ee.binary;
            m_logger.log(msg, "warning");
            m_nextKernel.clear();
            m_pendingMutation.clear();
        } catch (const std::exception& e) {
            m_logger.log(std::string("EVOLUTION REJECTED: ") + e.what(), "warning");
            m_nextKernel.clear();
            m_pendingMutation.clear();
        }

        transitionTo(SystemState::VERIFYING_QUINE);
    } else {
        handleBootFailure("Output checksum mismatch (Self-Replication Failed)");
    }
}

void App::onGrowMemory(uint32_t /*pages*/) {
    // We only need the flash effect; the page count itself is not tracked.
    m_memGrowing        = true;
    m_memGrowFlashUntil = now() + 800;
}

void App::spawnInstance(const std::string& kernel) {
    if (!kernel.empty()) {
        m_instances.push_back(kernel);
        m_logger.log("SPAWN: recorded new instance (total=" + std::to_string(m_instances.size()) + ")", "info");
    }
}

void App::killInstance(int index) {
    if (index < 0 || index >= (int)m_instances.size()) return;
    m_logger.log("KILL: removing instance " + std::to_string(index), "info");
    m_instances.erase(m_instances.begin() + index);
}

void App::handleKillRequest(int32_t idx) {
    killInstance((int)idx);
}

void App::handleSpawnRequest(uint32_t ptr, uint32_t len) {
    uint32_t memSize = 0;
    const uint8_t* wMem = m_kernel.rawMemory(&memSize);
    if (wMem && ptr + len <= memSize) {
        std::string s(reinterpret_cast<const char*>(wMem + ptr), len);
        spawnInstance(s);
    }
}

bool App::isBlacklisted(const std::vector<uint8_t>& seq) const {
    auto it = m_blacklist.find(seq);
    return it != m_blacklist.end() && it->second > 0;
}

float App::scoreSequence(const std::vector<uint8_t>& seq) const {
    // simply forward to our internal Advisor instance.  We expose this
    // because a few clients (notably the GUI) prefer to call directly on
    // App rather than plumbing their own Advisor reference.
    return m_advisor.score(seq);
}

void App::addToBlacklist(const std::vector<uint8_t>& seq) {
    if (seq.empty()) return;
    if (m_opts.heuristic == HeuristicMode::NONE) return;
    // assign initial weight if not present, otherwise bump
    auto it = m_blacklist.find(seq);
    if (it == m_blacklist.end()) {
        // choose starting weight; a few generations should elapse before retry
        m_blacklist[seq] = 3;
    } else {
        it->second = std::max(it->second, 3);
    }
}

void App::decayBlacklist() {
    for (auto it = m_blacklist.begin(); it != m_blacklist.end();) {
        if (--(it->second) <= 0) {
            it = m_blacklist.erase(it);
        } else {
            ++it;
        }
    }
}

// ─── Failure / Repair ────────────────────────────────────────────────────────

void App::handleBootFailure(const std::string& reason) {
    m_logger.log("CRITICAL: " + reason, "error");
    m_logger.addHistory({ m_generation, nowIso(), (int)kernelBytes(),
                          "REPAIR", reason, false });

    // record trap reason for telemetry
    m_lastTrapReason = reason;
    // add the mutation that just produced the failing kernel to blacklist
    if (!m_pendingMutation.empty() && m_opts.heuristic != HeuristicMode::NONE) {
        addToBlacklist(m_pendingMutation);
        m_logger.log("HEURISTIC: blacklisted mutation sequence", "warning");
    }

    m_retryCount++;

    try {
        auto evo      = evolveBinary(m_stableKernel, m_knownInstructions,
                                       m_retryCount, m_opts.mutationStrategy);
        m_currentKernel  = evo.binary;
        m_nextKernel.clear();
        m_pendingMutation = evo.mutationSequence;
        m_logger.log("ADAPTATION: " + evo.description, "mutation");
        updateKernelData();
    } catch (...) {
        m_currentKernel = m_stableKernel;
        m_pendingMutation.clear();
        m_logger.log("ADAPTATION: Fallback to base stable kernel", "system");
        updateKernelData();
    }

    transitionTo(SystemState::REPAIRING);
    m_programCounter = -1;
    m_focusAddr      = 0;
    m_focusLen       = 0;
    m_sysReading     = false;
}

void App::doReboot(bool success) {
    m_kernel.terminate();
    m_programCounter = -1;
    m_focusAddr      = 0;
    m_focusLen       = 0;
    m_sysReading     = false;

    if (success && m_genStartTime != 0) {
        // compute duration for previous generation
        uint64_t nowTicks = now();
        m_lastGenDurationMs = (nowTicks - m_genStartTime) / 1000.0; // ticks are us?
        if (m_opts.profile) {
            m_logger.log("PROFILE: gen " + std::to_string(m_generation) +
                          " took " + std::to_string(m_lastGenDurationMs) + " ms", "info");
        }
    }

    if (success) {
        m_generation++;
        // record generation start time
        m_genStartTime = now();

        // if we have reached the automatic training generation threshold,
        // or a later multiple of it, disable evolution and prepare to load the
        // new telemetry data.  this allows endless alternating cycles of
        // evolution and training.
        if (m_generation > 0 && m_generation % kAutoTrainGen == 0) {
            m_logger.log("AUTO: reached generation " + std::to_string(m_generation) +
                          ", switching to training", "info");
            m_evolutionEnabled = false;
            // clear any previous checkpoint flag so we will save again later
            m_modelSaved = false;
            // wipe the trainer statistics/replay buffer so the next train cycle
            // starts from a clean slate (weights remain unchanged)
            m_trainer.reset();
            // re-scan telemetry so the advisor sees the latest entries
            namespace fs = std::filesystem;
            fs::path seqBase = telemetryRoot();
            m_advisor = Advisor(seqBase.string());
            prepareTrainingSteps();
        }

        // check against user-specified generation limit as well
        if (m_opts.maxGen > 0 && m_generation >= m_opts.maxGen) {
            m_shouldExit = true;
        }

        // wall-clock limit duplicate check; see comment above
        if (m_opts.maxRunMs > 0 && m_uptimeMs >= m_opts.maxRunMs) {
            m_shouldExit = true;
        }

        // decay blacklist entries if requested
        if (m_opts.heuristic == HeuristicMode::DECAY) {
            decayBlacklist();
        }

        if (!m_nextKernel.empty()) {
            m_currentKernel = m_nextKernel;
            m_nextKernel.clear();
            updateKernelData();
            // update size min/max using cached bytes
            int sz = (int)m_currentKernelBytes.size();
            m_kernelSizeMin = std::min(m_kernelSizeMin, sz);
            m_kernelSizeMax = std::max(m_kernelSizeMax, sz);
        }

        if (!m_pendingMutation.empty()) {
            bool isNop = (m_pendingMutation.size() == 1 && m_pendingMutation[0] == 0x01);
            if (!isNop) {
                bool exists = false;
                for (const auto& seq : m_knownInstructions)
                    if (seq == m_pendingMutation) { exists = true; break; }
                if (!exists)
                    m_knownInstructions.push_back(m_pendingMutation);
            }
            m_pendingMutation.clear();
        }
    } else {
        m_pendingMutation.clear();
    }

    // automatically export telemetry for this generation/session
    autoExport();

    transitionTo(SystemState::IDLE);
}

// ─── Export ──────────────────────────────────────────────────────────────────

#include <filesystem>  // for auto-export


std::string App::exportHistory() const {
    ExportData d;
    d.generation    = m_generation;
    d.currentKernel = m_currentKernel;
    d.instructions  = m_instructions;
    d.logs          = m_logger.logs();
    d.history       = m_logger.history();
    // telemetry metrics
    d.mutationsAttempted = m_evolutionAttempts;
    d.mutationsApplied   = m_mutationsApplied;
    // include any kernels that have been spawned (alive instances)
    d.instances = m_instances;
    d.mutationInsert     = m_mutationInsert;
    d.mutationDelete     = m_mutationDelete;
    d.mutationModify     = m_mutationModify;
    d.mutationAdd        = m_mutationAdd;
    d.trapCode           = m_lastTrapReason;
    d.genDurationMs      = m_lastGenDurationMs;
    d.kernelSizeMin      = m_kernelSizeMin;
    d.kernelSizeMax      = m_kernelSizeMax;
    // heuristic summary
    d.heuristicBlacklistCount = (int)m_blacklist.size();
    d.advisorEntryCount       = (int)m_advisor.entryCount();
    return buildReport(d);
}

// Write the current telemetry and kernel blob to the session directory.

// return the directory that should contain telemetry data (without runId)
std::filesystem::path App::telemetryRoot() const {
    namespace fs = std::filesystem;
    fs::path exe = fs::path(executableDir());
    fs::path root = exe;
    // if the executable lives inside a "test" folder (unit tests) or a
    // "bin" folder (normal build) treat the parent as the logical root so
    // telemetry exports land alongside the build directory rather than
    // nested one level deeper.
    auto fname = exe.filename().string();
    if (fname == "test" || fname == "bin")
        root = exe.parent_path();
    if (!m_opts.telemetryDir.empty())
        return root / m_opts.telemetryDir;
    return root / "bin" / "seq";
}

void App::loadBlacklist() {
    namespace fs = std::filesystem;
    fs::path base = telemetryRoot();
    fs::path file = base / "blacklist.txt";
    if (!fs::exists(file)) return;
    std::ifstream f(file);
    if (!f) return;
    while (f) {
        int weight;
        std::string hex;
        if (!(f >> weight >> hex)) break;
        std::vector<uint8_t> seq;
        for (size_t i = 0; i + 1 < hex.size(); i += 2) {
            unsigned int byte;
            std::stringstream ss;
            ss << std::hex << hex.substr(i, 2);
            ss >> byte;
            seq.push_back((uint8_t)byte);
        }
        if (!seq.empty() && weight > 0) {
            m_blacklist[seq] = weight;
        }
    }
}

void App::saveBlacklist() const {
    namespace fs = std::filesystem;
    fs::path base = telemetryRoot();
    fs::create_directories(base);
    fs::path file = base / "blacklist.txt";
    std::ofstream f(file);
    if (!f) return;
    for (const auto& [seq, weight] : m_blacklist) {
        if (weight <= 0) continue;
        f << weight << " ";
        for (auto b : seq) {
            f << std::hex << std::setw(2) << std::setfill('0') << (int)b;
        }
        f << "\n";
    }
}

void App::exportNow() {
    // same as autoExport but callable directly; ignore exceptions
    try {
        autoExport();
    } catch (...) {}
}

void App::autoExport() {
    namespace fs = std::filesystem;
    try {
        fs::path base = telemetryRoot() / m_runId;
        fs::create_directories(base);

        if (m_opts.telemetryLevel == TelemetryLevel::NONE) {
            return;
        }

        // export header/full report or JSON
        fs::path reportFile = base / ("gen_" + std::to_string(m_generation) + ".txt");
        // acquire lock file to prevent concurrent writers from other processes
        int lockfd = -1;
        fs::path lockPath = base / "export.lock";
        lockfd = ::open(lockPath.string().c_str(), O_CREAT | O_RDWR, 0666);
        if (lockfd >= 0) flock(lockfd, LOCK_EX);
        std::ofstream r(reportFile);
        if (r) {
            if (m_opts.telemetryFormat == TelemetryFormat::JSON) {
                // simple JSON object; callers can parse as needed
                r << "{\n";
                r << "  \"generation\": " << m_generation << ",\n";
                r << "  \"kernel\": \"" << m_currentKernel << "\",\n";
                r << "  \"mutationsAttempted\": " << m_evolutionAttempts << ",\n";
                r << "  \"mutationsApplied\": " << m_mutationsApplied << ",\n";
                r << "  \"trapCode\": \"" << m_lastTrapReason << "\",\n";
                r << "  \"genDurationMs\": " << m_lastGenDurationMs << ",\n";
                r << "  \"kernelSizeMin\": " << m_kernelSizeMin << ",\n";
                r << "  \"kernelSizeMax\": " << m_kernelSizeMax << ",\n";
                r << "  \"heuristicBlacklistCount\": " << (int)m_blacklist.size() << "\n";
                r << "  \"advisorEntryCount\": " << (int)m_advisor.entryCount() << "\n";
                r << "}\n";
            } else {
                if (m_opts.telemetryLevel == TelemetryLevel::BASIC) {
                    r << "WASM QUINE BOOTLOADER - SYSTEM HISTORY EXPORT\n";
                    r << "Generated: " << nowIso() << "\n";
                    r << "Final Generation: " << m_generation << "\n";
                } else {
                    r << exportHistory();
                }
            }
        }
        // also dump raw kernel base64 for easier consumption if full
        if (m_opts.telemetryLevel == TelemetryLevel::FULL) {
            fs::path kernelFile = base / ("kernel_" + std::to_string(m_generation) + ".b64");
            std::ofstream k(kernelFile);
            if (k) k << m_currentKernel;
        }
        if (lockfd >= 0) {
            flock(lockfd, LOCK_UN);
            close(lockfd);
        }
    } catch (const std::exception& e) {
        // logging may not be initialized yet
        m_logger.log(std::string("autoExport failed: ") + e.what(), "error");
    }
}
