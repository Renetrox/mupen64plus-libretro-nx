/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-rsp-hle - plugin.c                                        *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *   Copyright (C) 2014 Bobby Smiles                                       *
 *   Copyright (C) 2009 Richard Goedeken                                   *
 *   Copyright (C) 2002 Hacktarux                                          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "common.h"
#include "hle.h"
#include "hle_internal.h"
#include "hle_external.h"

#define M64P_PLUGIN_PROTOTYPES 1
#include "m64p_common.h"
#include "m64p_config.h"
#include "m64p_frontend.h"
#include "m64p_plugin.h"
#include "m64p_types.h"
#include "mupen64plus-next_common.h"

#define CONFIG_API_VERSION       0x020100
#define CONFIG_PARAM_VERSION     1.00

#define RSP_API_VERSION          0x20000
#define RSP_HLE_VERSION          0x020509
#define RSP_PLUGIN_API_VERSION   0x020000

#ifndef MI_INTR_DP
#define MI_INTR_DP 0x20
#endif

#define HLE_FRAMESKIP_AUTO_MAX 2u

/* libretro globals owned by libretro/libretro.c */
extern struct retro_perf_callback perf_cb;
extern struct retro_core_option_v2_definition option_defs_us[];
unsigned retro_get_region(void);

/* local variables */
static struct hle_t g_hle;
static void (*l_CheckInterrupts)(void) = NULL;
static void (*l_ProcessDlistList)(void) = NULL;
static void (*l_ProcessAlistList)(void) = NULL;
static void (*l_ProcessRdpList)(void) = NULL;
static void (*l_ShowCFB)(void) = NULL;
static void (*l_DebugCallback)(void *, int, const char *) = NULL;
static void *l_DebugCallContext = NULL;
static uint32_t *l_MI_INTR_REG = NULL;
static int l_PluginInit = 0;

/*
 * Experimental HLE frameskip ported from the FZ/ReARMed work.
 *
 * This deliberately lives at the HLE DList boundary instead of suppressing
 * video_cb() after the renderer has already done its work. When a frame is
 * skipped we still raise MI_INTR_DP; mupen64plus-core consumes that bit after
 * the RSP task and schedules DP_INT through its normal timing path.
 *
 * The first prototype reuses the existing Frame Duplication core-option slot
 * at shared-library load time. IMPORTANT: the option key intentionally stays
 * CORE_NAME "-FrameDuping" because libretro.c still queries that key. Only the
 * visible label/values are repurposed here:
 *
 *     HLE Frameskip: Disabled / Auto
 *
 * Keeping the original key avoids an invalid GET_VARIABLE request while this
 * prototype is being validated. Auto also leaves NX frame duplication enabled
 * on skipped presentations, which is acceptable for this experimental stage.
 */
static int hle_frameskip_enabled = 0;
static unsigned hle_frameskip_skip_pending = 0;
static unsigned hle_frameskip_consecutive = 0;
static uint64_t hle_frameskip_initial_usec = 0;
static uint64_t hle_frameskip_virtual_count = 0;

static void hle_frameskip_reset(void)
{
    hle_frameskip_skip_pending = 0;
    hle_frameskip_consecutive = 0;
    hle_frameskip_initial_usec = 0;
    hle_frameskip_virtual_count = 0;
}

static void hle_frameskip_patch_core_option(void)
{
    size_t i;

    for (i = 0; option_defs_us[i].key != NULL; ++i)
    {
        if (strcmp(option_defs_us[i].key, CORE_NAME "-FrameDuping") == 0)
        {
            option_defs_us[i].desc = "HLE Frameskip";
            option_defs_us[i].desc_categorized = NULL;
            option_defs_us[i].info =
                "Skip HLE graphics display lists automatically when emulation falls behind. "
                "Only active with the HLE RSP. LLE RSP paths are not skipped.";
            option_defs_us[i].info_categorized = NULL;
            option_defs_us[i].category_key = NULL;
            option_defs_us[i].values[0].value = "False";
            option_defs_us[i].values[0].label = "Disabled";
            option_defs_us[i].values[1].value = "Auto";
            option_defs_us[i].values[1].label = "Auto";
            option_defs_us[i].values[2].value = NULL;
            option_defs_us[i].values[2].label = NULL;
            option_defs_us[i].default_value = "False";
            break;
        }
    }
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor))
static void hle_frameskip_option_constructor(void)
{
    hle_frameskip_patch_core_option();
}
#endif

static void hle_frameskip_read_option(void)
{
    struct retro_variable var;
    int enabled = 0;

    if (environ_cb != NULL)
    {
        var.key = CORE_NAME "-FrameDuping";
        var.value = NULL;
        if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value != NULL)
            enabled = (strcmp(var.value, "Auto") == 0);
    }

    if (hle_frameskip_enabled != enabled)
    {
        hle_frameskip_enabled = enabled;
        hle_frameskip_reset();
    }
}

static int hle_frameskip_should_skip(void)
{
    if (!hle_frameskip_enabled || l_MI_INTR_REG == NULL)
        return 0;

    if (!hle_frameskip_skip_pending)
    {
        hle_frameskip_consecutive = 0;
        return 0;
    }

    if (hle_frameskip_consecutive >= HLE_FRAMESKIP_AUTO_MAX)
    {
        /* Force one rendered frame, then keep the pending catch-up request. */
        hle_frameskip_consecutive = 0;
        return 0;
    }

    hle_frameskip_skip_pending = 0;
    hle_frameskip_consecutive++;
    return 1;
}

static void hle_frameskip_update(void)
{
    uint64_t now;
    uint64_t elapsed;
    uint64_t real_count;
    unsigned target_fps;

    if (!hle_frameskip_enabled || perf_cb.get_time_usec == NULL)
        return;

    now = (uint64_t) perf_cb.get_time_usec();
    target_fps = (retro_get_region() == RETRO_REGION_PAL) ? 50u : 60u;

    if (hle_frameskip_initial_usec == 0 || now <= hle_frameskip_initial_usec)
    {
        hle_frameskip_initial_usec = now;
        hle_frameskip_virtual_count = 0;
        hle_frameskip_skip_pending = 0;
        hle_frameskip_consecutive = 0;
        return;
    }

    elapsed = now - hle_frameskip_initial_usec;
    real_count = (elapsed * target_fps) / 1000000u;

    hle_frameskip_virtual_count++;

    if (real_count > hle_frameskip_virtual_count)
    {
        hle_frameskip_skip_pending = 1;
    }
    else if (real_count < hle_frameskip_virtual_count)
    {
        hle_frameskip_virtual_count = real_count;
    }
}

EXPORT m64p_error CALL hlePluginGetVersion(m64p_plugin_type *PluginType, int *PluginVersion, int *APIVersion, const char **PluginNamePtr, int *Capabilities)
{
    /* set version info */
    if (PluginType != NULL)
        *PluginType = M64PLUGIN_RSP;

    if (PluginVersion != NULL)
        *PluginVersion = RSP_HLE_VERSION;

    if (APIVersion != NULL)
        *APIVersion = RSP_PLUGIN_API_VERSION;

    if (PluginNamePtr != NULL)
        *PluginNamePtr = "Hacktarux/Azimer High-Level Emulation RSP Plugin";

    if (Capabilities != NULL)
        *Capabilities = 0;

    return M64ERR_SUCCESS;
}

/* local function */
static void DebugMessage(int level, const char *message, va_list args)
{
    char msgbuf[1024];

    if (l_DebugCallback == NULL)
        return;

    vsprintf(msgbuf, message, args);

    (*l_DebugCallback)(l_DebugCallContext, level, msgbuf);
}

/* Global functions needed by HLE core */
void HleVerboseMessage(void* UNUSED(user_defined), const char *message, ...)
{
}

void HleErrorMessage(void* UNUSED(user_defined), const char *message, ...)
{
}

void HleWarnMessage(void* UNUSED(user_defined), const char *message, ...)
{
}

void HleCheckInterrupts(void* UNUSED(user_defined))
{
    if (l_CheckInterrupts == NULL)
        return;

    (*l_CheckInterrupts)();
}

void HleProcessDlistList(void* UNUSED(user_defined))
{
    if (l_ProcessDlistList == NULL)
        return;

    /* Allow Disabled/Auto to be changed from Core Options at runtime. */
    hle_frameskip_read_option();

    if (hle_frameskip_should_skip())
    {
        /*
         * hle.c has already marked the RSP task TASKDONE/BROKE/HALT.
         * Raising DP here mirrors a completed graphics task without invoking
         * the renderer. mupen64plus-core turns MI_INTR_DP into the normal
         * delayed DP_INT immediately after rsp.doRspCycles() returns.
         */
        *l_MI_INTR_REG |= MI_INTR_DP;
    }
    else
    {
        (*l_ProcessDlistList)();
    }

    /* The current graphics task has now completed; decide catch-up for next. */
    hle_frameskip_update();
}

void HleProcessAlistList(void* UNUSED(user_defined))
{
    if (l_ProcessAlistList == NULL)
        return;

    (*l_ProcessAlistList)();
}

void HleProcessRdpList(void* UNUSED(user_defined))
{
    if (l_ProcessRdpList == NULL)
        return;

    (*l_ProcessRdpList)();
}

void HleShowCFB(void* UNUSED(user_defined))
{
    if (l_ShowCFB == NULL)
        return;

    (*l_ShowCFB)();
}

int HleForwardTask(void* user_defined)
{
    return -1;
}

/* DLL-exported functions */
EXPORT m64p_error CALL hlePluginStartup(m64p_dynlib_handle CoreLibHandle, void *Context,
                                     void (*DebugCallback)(void *, int, const char *))
{
    if (l_PluginInit)
        return M64ERR_ALREADY_INIT;

    /* first thing is to set the callback function for debug info */
    l_DebugCallback = DebugCallback;
    l_DebugCallContext = Context;

    /* On non-GNU toolchains there is no constructor above; patching here is
     * still useful, although frontends may already have consumed the option
     * table by this point. */
    hle_frameskip_patch_core_option();
    hle_frameskip_read_option();

    /* this plugin doesn't use any Core library functions (ex for Configuration), so no need to keep the CoreLibHandle */

    l_PluginInit = 1;
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL hlePluginShutdown(void)
{
    if (!l_PluginInit)
        return M64ERR_NOT_INIT;

    /* reset some local variable */
    l_DebugCallback = NULL;
    l_DebugCallContext = NULL;
    l_MI_INTR_REG = NULL;
    hle_frameskip_enabled = 0;
    hle_frameskip_reset();

    l_PluginInit = 0;
    return M64ERR_SUCCESS;
}

EXPORT unsigned int CALL hleDoRspCycles(unsigned int Cycles)
{
    hle_execute(&g_hle);
    return Cycles;
}

EXPORT void CALL hleInitiateRSP(RSP_INFO Rsp_Info, unsigned int* CycleCount)
{
    hle_init(&g_hle,
             Rsp_Info.RDRAM,
             Rsp_Info.DMEM,
             Rsp_Info.IMEM,
             Rsp_Info.MI_INTR_REG,
             Rsp_Info.SP_MEM_ADDR_REG,
             Rsp_Info.SP_DRAM_ADDR_REG,
             Rsp_Info.SP_RD_LEN_REG,
             Rsp_Info.SP_WR_LEN_REG,
             Rsp_Info.SP_STATUS_REG,
             Rsp_Info.SP_DMA_FULL_REG,
             Rsp_Info.SP_DMA_BUSY_REG,
             Rsp_Info.SP_PC_REG,
             Rsp_Info.SP_SEMAPHORE_REG,
             Rsp_Info.DPC_START_REG,
             Rsp_Info.DPC_END_REG,
             Rsp_Info.DPC_CURRENT_REG,
             Rsp_Info.DPC_STATUS_REG,
             Rsp_Info.DPC_CLOCK_REG,
             Rsp_Info.DPC_BUFBUSY_REG,
             Rsp_Info.DPC_PIPEBUSY_REG,
             Rsp_Info.DPC_TMEM_REG,
             NULL);

    l_CheckInterrupts = Rsp_Info.CheckInterrupts;
    l_ProcessDlistList = Rsp_Info.ProcessDlistList;
    l_ProcessAlistList = Rsp_Info.ProcessAlistList;
    l_ProcessRdpList = Rsp_Info.ProcessRdpList;
    l_ShowCFB = Rsp_Info.ShowCFB;
    l_MI_INTR_REG = Rsp_Info.MI_INTR_REG;

    hle_frameskip_read_option();
    hle_frameskip_reset();

    // Is the DoCommand really needed? It's upstream
    m64p_rom_header rom_header;
    CoreDoCommand(M64CMD_ROM_GET_HEADER, sizeof(rom_header), &rom_header);

    g_hle.hle_gfx = 1;
    g_hle.hle_aud = 0;
    
    /* notify fallback plugin */
    /*if (l_InitiateRSP) {
        l_InitiateRSP(Rsp_Info, CycleCount);
    }*/
}

EXPORT void CALL hleRomClosed(void)
{
     g_hle.cached_ucodes.count = 0;
     hle_frameskip_reset();
     
    /* notify fallback plugin */
    /*if (l_RomClosed) {
        l_RomClosed();
    }*/
}
