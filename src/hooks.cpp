#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/system/format.h>
#include <rex/ppc.h>
#include <rex/runtime.h>
#include <rex/system/gpu_plugin.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/thread/mutex.h>

#include <cstring>
#include <atomic>

// //https://github.com/rexglue/rexglue-sdk/issues/121
// void FixBrokenSwitchTable_82812FA4(PPCRegister& r3) {

// 	switch (r3.u32) {
// 	case 0:
// 		r3.s64 = 2;
// 		break;
// 	case 1:
// 		r3.s64 = 1;
// 		break;
// 	default:
// 		r3.s64 = 1;
// 		break;
// 	case 3:
// 		r3.s64 = 0;
// 		break;
// 	case 2:
// 		r3.s64 = 3;
// 		break;
// 	case 0xc:
// 		r3.s64 = 3;
// 		break;
// 	}
// }

REXCVAR_DEFINE_BOOL(c2_fps_display, false, "Debug", "Displays the game's debug fps overlay.");
REXCVAR_DEFINE_BOOL(c2_perf_graph_display, false, "Debug", "Displays the game's debug performance graph overlay.");
REXCVAR_DEFINE_BOOL(c2_gdb_console_output_log, false, "Debug", "Log GDb_ConsoleOutput calls");
REXCVAR_DEFINE_BOOL(c2_resource_log, false, "Debug", "Log what resources get loaded and what functions handle them.");
REXCVAR_DEFINE_BOOL(c2_gfx_draw_log, false, "Debug", "Log Scaleform GFx renderer draw counts every 60 UI frames.");
REXCVAR_DEFINE_BOOL(c2_force_texture_invalidate, false, "Debug",
                    "Experiment: trigger GPU memory-watch callbacks over the physical heap whenever the GFx "
                    "renderer unmaps or updates a texture (tests glyph-atlas invalidation).");
REXCVAR_DEFINE_STRING(c2_gpu_backend, "any", "GPU",
                      "Backend to request from the GPU plugin: any, d3d12 or vulkan. 'any' takes the plugin's "
                      "preference (D3D12 on Windows, Vulkan elsewhere).");

bool force_fps_display() { 
    return REXCVAR_GET(c2_fps_display);
}

bool force_perf_graph_display() {
    return REXCVAR_GET(c2_perf_graph_display);
}

REX_HOOK_RAW(GDb_ConsoleOutput) {
    uint32_t format_ptr = ctx.r3.u32;
    if (!format_ptr) {
        ctx.r3.s64 = -1;
        return;
    }

    auto format = rex::memory::GuestPtr<const u16*>(base, format_ptr);

    rex::system::format::StackArgList args(ctx, base, 1);   // varargs start at r4 (index 1)  
    rex::system::format::WideStringFormatData data(format);

    int32_t count = format_core(base, data, args, true);  // true = wide  
    if (REXCVAR_GET(c2_gdb_console_output_log) && count > 0) {
        auto wstr = data.wstr();
        // wstr is std::wstring; narrow for logging (fine for ASCII debug output)  
        std::string narrow(wstr.begin(), wstr.end());
        while (!narrow.empty() && std::isspace((unsigned char)narrow.back()))
            narrow.pop_back();
        REXLOG_DEBUG("GDb_ConsoleOutput: {}", narrow);
    }
}

void LogLoadResourceArgs(PPCRegister& r3, PPCRegister& r4, PPCRegister& r5,
    PPCRegister& r6, PPCRegister& r7, PPCRegister& r8) {

    if (!REXCVAR_GET(c2_resource_log))
        return;

    uint8_t* base = rex::system::kernel_state()->memory()->virtual_membase();

    auto pStrName = rex::memory::GuestPtr<const char*>(base, r3.u32);
    auto pResId = rex::memory::GuestPtr<const char*>(base, r4.u32);
    auto pStrType = rex::memory::GuestPtr<const char*>(base, r5.u32);
    auto pResourcePath = rex::memory::GuestPtr<const char*>(base, r6.u32);

    REXLOG_DEBUG(
        "RWS::LoadResource: Loading Resource: pStrName='{}' pResId={:#x} (guid[0]={:#x}) pStrType='{}' "
        "pResourcePath='{}' pStream={:#x} uiStreamSize={}",
        pStrName ? pStrName : "(null)", r4.u32,
        pResId ? *pResId : 0,
        pStrType ? pStrType : "(null)",
        pResourcePath ? pResourcePath : "(null)", r7.u32, r8.u32);
}

void LogHandlerFound(PPCRegister& r3, PPCRegister& r30) {

	if (!REXCVAR_GET(c2_resource_log))
        return;

    uint8_t* base = rex::system::kernel_state()->memory()->virtual_membase();
    auto pStrName = rex::memory::GuestPtr<const char*>(base, r30.u32);

    if (r3.u32 != 0) {
        REXLOG_DEBUG("RWS::LoadResource: Handler for '{}' found: {:#x}", pStrName ? pStrName : "(null)", r3.u32);
    }
    else {
        REXLOG_DEBUG("RWS::LoadResource: Handler for '{}' NOT found. Ignoring Resource.", pStrName ? pStrName : "(null)");
    }
}

void LogUsingStreaming(PPCRegister& r30) {

    if (!REXCVAR_GET(c2_resource_log))
        return;

    uint8_t* base = rex::system::kernel_state()->memory()->virtual_membase();
    auto pStrName = rex::memory::GuestPtr<const char*>(base, r30.u32);

	REXLOG_DEBUG("RWS::LoadResource: Loading '{}' as a streaming resource.", pStrName ? pStrName : "(null)");
}

void LogCustomResourceHandler(PPCRegister& r30, PPCRegister& ctr) {

    if (!REXCVAR_GET(c2_resource_log))
        return;

    uint8_t* base = rex::system::kernel_state()->memory()->virtual_membase();
    const char* pStrName = rex::memory::GuestPtr<const char*>(base, r30.u32);
    REXLOG_DEBUG("RWS::LoadResource: Custom resource loading function is being used for '{}', address of the function is: 0x{:08X}",
        pStrName ? pStrName : "(null)", ctr.u32);
}

void LogResourceAddCheck(PPCRegister& r30, PPCRegister& r29) {

    if (!REXCVAR_GET(c2_resource_log))
        return;

    if (r29.u32 == 0) {
        return;  // Add is not called when the vtable load returned null  
    }

    uint8_t* base = rex::system::kernel_state()->memory()->virtual_membase();
    auto pStrName = rex::memory::GuestPtr<const char*>(base, r30.u32);

    REXLOG_DEBUG("RWS::LoadResource: RWS::CResourceManager::Add will be called for '{}'", pStrName ? pStrName : "(null)");
}
//=============================================================================
// Quiet replacements for kernel exports the SDK only stubs.
//
// The game calls KeSaveFloatingPointState / KeRestoreFloatingPointState every
// frame from its render loop. The SDK only has logging stubs for them, and the
// generated code calls the __imp__ symbols directly, so defining them here
// makes the linker bind the game's calls to these instead of the runtime
// library's copies. The export registry (used for ordinal lookups) is patched
// as well from Crackdown2App::OnPreSetup. Both are no-ops on the host; the
// save variant reports STATUS_SUCCESS like the real kernel.
//=============================================================================

extern "C" REX_FUNC(__imp__KeSaveFloatingPointState) {
    (void)base;
    ctx.r3.u64 = 0;  // STATUS_SUCCESS
}

extern "C" REX_FUNC(__imp__KeRestoreFloatingPointState) {
    (void)base;
    ctx.r3.u64 = 0;
}

void InstallQuietKernelStubs() {
    struct { const char* name; PPCFunc* func; } replacements[] = {
        { "__imp__KeSaveFloatingPointState", __imp__KeSaveFloatingPointState },
        { "__imp__KeRestoreFloatingPointState", __imp__KeRestoreFloatingPointState },
    };
    for (auto& r : replacements) {
        bool found = false;
        for (auto& [name, func] : rex::ppc::GetPPCFuncRegistry()) {
            if (std::strcmp(name, r.name) == 0) {
                func = r.func;
                found = true;
            }
        }
        if (!found) {
            rex::ppc::GetPPCFuncRegistry().emplace_back(r.name, r.func);
        }
    }
}

//=============================================================================
// Scaleform GFx draw diagnostics (midasm hooks in default_hooks.toml).
//
// Counts what the game's GFx renderer (GRendererXbox360Impl) submits per UI
// frame, to tell "UI not drawn by the game" apart from "UI drawn but lost in
// GPU emulation".
//=============================================================================

static std::atomic<uint32_t> g_gfx_tri_count{0};
static std::atomic<uint32_t> g_gfx_bitmap_count{0};
static std::atomic<uint32_t> g_gfx_line_count{0};
static std::atomic<uint32_t> g_gfx_tri_calls{0};
static std::atomic<uint32_t> g_gfx_bitmap_calls{0};
static std::atomic<uint32_t> g_gfx_frames{0};

void CountGfxDrawIndexedTriList(PPCRegister& r8) {
    g_gfx_tri_count += r8.u32;   // triangleCount
    ++g_gfx_tri_calls;
}

void CountGfxDrawBitmaps(PPCRegister& r7) {
    g_gfx_bitmap_count += r7.u32;   // count
    ++g_gfx_bitmap_calls;
}

void CountGfxDrawLineStrip(PPCRegister& r5) {
    g_gfx_line_count += r5.u32;   // lineCount
}

static std::atomic<uint32_t> g_gfx_tex_updates{0};

static void ForceGpuTextureInvalidate() {
    ++g_gfx_tex_updates;
    if (!REXCVAR_GET(c2_force_texture_invalidate))
        return;
    auto* memory = rex::system::kernel_memory();
    if (!memory)
        return;
    // Physical-heap virtual mirrors; snapped to pages inside, cheap when nothing is watched.
    static const uint32_t ranges[] = { 0xA0000000u, 0xC0000000u, 0xE0000000u };
    for (uint32_t start : ranges) {
        auto lock = rex::thread::global_critical_region::AcquireDirect();
        memory->TriggerPhysicalMemoryCallbacks(std::move(lock), start, 0x1FFFFFFFu, /*is_write=*/true,
                                               /*unwatch_exact_range=*/false);
    }
}

void OnGfxTextureUnmap(PPCRegister& r3) { (void)r3; ForceGpuTextureInvalidate(); }
void OnGfxTextureUpdate(PPCRegister& r3) { (void)r3; ForceGpuTextureInvalidate(); }

void LogGfxEndDisplay(PPCRegister& r3) {
    (void)r3;
    if (!REXCVAR_GET(c2_gfx_draw_log))
        return;
    uint32_t frames = ++g_gfx_frames;
    if (frames % 60 != 0)
        return;
    REXLOG_INFO("GFx: 60 UI frames: {} DrawIndexedTriList calls ({} tris), {} DrawBitmaps calls ({} bitmaps), {} line strip lines, {} texture unmap/updates",
                g_gfx_tri_calls.exchange(0), g_gfx_tri_count.exchange(0),
                g_gfx_bitmap_calls.exchange(0), g_gfx_bitmap_count.exchange(0),
                g_gfx_line_count.exchange(0), g_gfx_tex_updates.exchange(0));
}

//=============================================================================
// GPU backend override.
//
// ReXApp loads the plugin named by --gpu_plugin with backend "any", which picks
// D3D12 on Windows. --c2_gpu_backend=vulkan|d3d12 loads it here instead so the
// same build can be compared across backends (e.g. Vulkan on a Windows box).
//=============================================================================

void LoadRequestedGpuBackend(rex::RuntimeConfig& config) {
    const std::string& backend = REXCVAR_GET(c2_gpu_backend);
    if (config.graphics || config.gpu_plugin.empty() || backend.empty() || backend == "any")
        return;
    config.graphics = rex::system::LoadGpuPlugin(config.gpu_plugin, backend);
    if (!config.graphics) {
        REXLOG_ERROR("c2_gpu_backend: plugin '{}' has no '{}' backend; falling back to the plugin default",
                     config.gpu_plugin, backend);
    }
}
