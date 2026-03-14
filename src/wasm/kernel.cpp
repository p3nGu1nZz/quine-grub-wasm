#include "wasm/kernel.h"
#include "base64.h"

#include <wasm3.h>
#include <m3_env.h>

#include <stdexcept>
#include <cstring>

static const uint32_t WASM3_STACK_SLOTS = 4096;

// Per-runtime user data passed to host functions
struct KernelUserData {
    WasmKernel*     kernel;
    LogCallback*    logCb;
    GrowMemCallback* growCb;
    SpawnCallback*  spawnCb;
    WeightCallback* weightCb;
    KillCallback*   killCb;
};

// ── Host function: env.log(ptr i32, len i32) ─────────────────────────────────
m3ApiRawFunction(hostLogImpl) {
    m3ApiGetArg(uint32_t, ptr)
    m3ApiGetArg(uint32_t, len)

    uint32_t  memSize = 0;
    uint8_t*  wMem    = m3_GetMemory(runtime, &memSize, 0);

    auto* ud = reinterpret_cast<KernelUserData*>(m3_GetUserData(runtime));
    if (ud && ud->logCb && *ud->logCb && wMem)
        (*ud->logCb)(ptr, len, wMem, memSize);

    m3ApiSuccess()
}

// ── Host function: env.grow_memory(pages i32) ────────────────────────────────
m3ApiRawFunction(hostGrowImpl) {
    m3ApiGetArg(uint32_t, pages)

    auto* ud = reinterpret_cast<KernelUserData*>(m3_GetUserData(runtime));
    if (ud && ud->growCb && *ud->growCb)
        (*ud->growCb)(pages);

    m3ApiSuccess()
}

// new host function: env.spawn(ptr,len)
m3ApiRawFunction(hostSpawnImpl) {
    m3ApiGetArg(uint32_t, ptr)
    m3ApiGetArg(uint32_t, len)
    auto* ud = reinterpret_cast<KernelUserData*>(m3_GetUserData(runtime));
    if (ud && ud->spawnCb && *ud->spawnCb) {
        (*ud->spawnCb)(ptr, len);
    }
    m3ApiSuccess()
}

// new host function: env.record_weight(ptr,len)
// this is used by the kernel to transmit its serialized neural matrix or
// weight update; the host may ignore or persist the blob.
m3ApiRawFunction(hostRecordWeightImpl) {
    m3ApiGetArg(uint32_t, ptr)
    m3ApiGetArg(uint32_t, len)
    auto* ud = reinterpret_cast<KernelUserData*>(m3_GetUserData(runtime));
    if (ud && ud->weightCb && *ud->weightCb) {
        (*ud->weightCb)(ptr, len);
    }
    m3ApiSuccess()
}

// new host function: env.kill_instance(idx)
m3ApiRawFunction(hostKillImpl) {
    m3ApiGetArg(int32_t, idx)
    auto* ud = reinterpret_cast<KernelUserData*>(m3_GetUserData(runtime));
    if (ud && ud->killCb && *ud->killCb) {
        (*ud->killCb)(idx);
    }
    m3ApiSuccess()
}

// ─────────────────────────────────────────────────────────────────────────────

WasmKernel::WasmKernel()  = default;
WasmKernel::~WasmKernel() { terminate(); }

bool WasmKernel::isLoaded() const {
    return m_runtime != nullptr && m_runFunc != nullptr;
}

void WasmKernel::terminate() {
    m_runFunc  = nullptr;
    m_module   = nullptr;
    if (m_runtime) { m3_FreeRuntime(m_runtime);     m_runtime = nullptr; }
    if (m_env)     { m3_FreeEnvironment(m_env);     m_env     = nullptr; }
    if (m_userData) {
        delete m_userData->spawnCb;
        delete m_userData->weightCb;
        delete m_userData;
    }
    m_userData = nullptr;
    m_wasmBytes.clear();
    m_logCb  = {};
    m_growCb = {};
}

void WasmKernel::bootDynamic(const std::string& glob,
                               LogCallback        logCb,
                               GrowMemCallback    growCb,
                               SpawnCallback      spawnCb,
                               WeightCallback     weightCb,
                               KillCallback       killCb)
{
    terminate();

    m_wasmBytes = base64_decode(glob);
    m_logCb     = std::move(logCb);
    m_growCb    = std::move(growCb);
    // spawn and weight callbacks optional; store pointers

    m_env = m3_NewEnvironment();
    if (!m_env) throw std::runtime_error("wasm3: failed to create environment");

    // Allocate user-data; lifetime managed by this WasmKernel instance
    // allocate user data including spawnCb, weightCb and killCb pointers set later
    SpawnCallback* scb = new SpawnCallback();
    WeightCallback* wcb = new WeightCallback();
    KillCallback*   kcb = new KillCallback();
    // copy provided callbacks into heap objects so they outlive this scope
    if (spawnCb) *scb = std::move(spawnCb);
    if (weightCb) *wcb = std::move(weightCb);
    if (killCb)   *kcb = std::move(killCb);
    m_userData  = new KernelUserData{ this, &m_logCb, &m_growCb, scb, wcb, kcb };
    m_runtime   = m3_NewRuntime(m_env, WASM3_STACK_SLOTS, m_userData);
    if (!m_runtime) {
        delete m_userData; m_userData = nullptr;
        m3_FreeEnvironment(m_env); m_env = nullptr;
        throw std::runtime_error("wasm3: failed to create runtime");
    }

    M3Result err = m3_ParseModule(m_env, &m_module,
                                   m_wasmBytes.data(),
                                   (uint32_t)m_wasmBytes.size());
    if (err) {
        terminate();
        throw std::runtime_error(std::string("wasm3 parse: ") + err);
    }

    err = m3_LoadModule(m_runtime, m_module);
    if (err) {
        terminate();
        throw std::runtime_error(std::string("wasm3 load: ") + err);
    }

    // Link host functions
    err = m3_LinkRawFunction(m_module, "env", "log", "v(ii)", hostLogImpl);
    if (err && err != m3Err_functionLookupFailed)
        throw std::runtime_error(std::string("wasm3 link log: ") + err);

    err = m3_LinkRawFunction(m_module, "env", "grow_memory", "v(i)", hostGrowImpl);
    if (err && err != m3Err_functionLookupFailed)
        throw std::runtime_error(std::string("wasm3 link grow_memory: ") + err);

    // link spawn if requested later (always link so kernel can import even if
    // callback is empty)
    err = m3_LinkRawFunction(m_module, "env", "spawn", "v(ii)", hostSpawnImpl);
    if (err && err != m3Err_functionLookupFailed)
        throw std::runtime_error(std::string("wasm3 link spawn: ") + err);
    // link weight logger; this will call the weightCb if one was provided
    err = m3_LinkRawFunction(m_module, "env", "record_weight", "v(ii)", hostRecordWeightImpl);
    if (err && err != m3Err_functionLookupFailed)
        throw std::runtime_error(std::string("wasm3 link record_weight: ") + err);
    // link kill_instance so kernels can request their own termination
    err = m3_LinkRawFunction(m_module, "env", "kill_instance", "v(i)", hostKillImpl);
    if (err && err != m3Err_functionLookupFailed)
        throw std::runtime_error(std::string("wasm3 link kill_instance: ") + err);

    err = m3_FindFunction(&m_runFunc, m_runtime, "run");
    if (err) {
        terminate();
        throw std::runtime_error(std::string("wasm3 find 'run': ") + err);
    }
}

void WasmKernel::runDynamic(const std::string& sourceGlob) {
    if (!isLoaded())
        throw std::runtime_error("Kernel Panic: Not loaded. Boot first.");

    // Write source into WASM memory at offset 0
    uint32_t memSize  = 0;
    uint8_t* wMem     = m3_GetMemory(m_runtime, &memSize, 0);
    if (!wMem)
        throw std::runtime_error("Kernel Panic: WASM memory unavailable.");

    uint32_t srcLen = (uint32_t)sourceGlob.size();
    if (srcLen > memSize)
        throw std::runtime_error("Kernel Panic: Source larger than WASM memory.");

    memcpy(wMem, sourceGlob.data(), srcLen);

    // Call run(0, srcLen)
    M3Result err = m3_CallV(m_runFunc, (uint32_t)0, srcLen);
    if (err)
        throw std::runtime_error(std::string("wasm3 call 'run': ") + err);
}

const uint8_t* WasmKernel::rawMemory(uint32_t* size) const {
    if (!m_runtime) return nullptr;
    return m3_GetMemory(m_runtime, size, 0);
}
