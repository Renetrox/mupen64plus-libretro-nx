#include "m64p_loader.h"

#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static char g_last_error[512];

/*
 * The NX snapshot in this repository predates the Mupen64Plus 2.6 VidExt
 * expansion.  Its public m64p_video_extension_functions has 12 callbacks,
 * while the standalone 2.6 core requires a 17-callback table.  Keep the old
 * headers isolated and translate the table here when a 2.6+ core is loaded.
 */
typedef enum
{
    MODULAR_RENDER_OPENGL = 0,
    MODULAR_RENDER_VULKAN = 1
} modular_render_mode;

typedef struct
{
    unsigned int Functions;
    m64p_error    (*VidExtFuncInit)(void);
    m64p_error    (*VidExtFuncQuit)(void);
    m64p_error    (*VidExtFuncListModes)(m64p_2d_size *, int *);
    m64p_error    (*VidExtFuncListRates)(m64p_2d_size, int *, int *);
    m64p_error    (*VidExtFuncSetMode)(int, int, int, int, int);
    m64p_error    (*VidExtFuncSetModeWithRate)(int, int, int, int, int, int);
    m64p_function (*VidExtFuncGLGetProc)(const char *);
    m64p_error    (*VidExtFuncGLSetAttr)(m64p_GLattr, int);
    m64p_error    (*VidExtFuncGLGetAttr)(m64p_GLattr, int *);
    m64p_error    (*VidExtFuncGLSwapBuf)(void);
    m64p_error    (*VidExtFuncSetCaption)(const char *);
    m64p_error    (*VidExtFuncToggleFS)(void);
    m64p_error    (*VidExtFuncResizeWindow)(int, int);
    uint32_t      (*VidExtFuncGLGetDefaultFramebuffer)(void);
    m64p_error    (*VidExtFuncInitWithRenderMode)(modular_render_mode);
    m64p_error    (*VidExtFuncVKGetSurface)(void **, void *);
    m64p_error    (*VidExtFuncVKGetInstanceExtensions)(const char **[], uint32_t *);
} modular_vidext_v26;

typedef m64p_error (*raw_override_vidext_t)(void *);

static raw_override_vidext_t g_override_vidext_raw;
static m64p_video_extension_functions g_legacy_vidext;

static m64p_error compat_list_rates(m64p_2d_size size, int *num_rates, int *rates)
{
    (void) size;

    if (num_rates == NULL)
        return M64ERR_INPUT_ASSERT;

    if (rates != NULL && *num_rates > 0)
    {
        rates[0] = 60;
        *num_rates = 1;
    }
    else
    {
        *num_rates = 0;
    }

    return M64ERR_SUCCESS;
}

static m64p_error compat_set_mode_with_rate(int width, int height, int refresh_rate,
                                            int bpp, int mode, int flags)
{
    (void) refresh_rate;

    if (g_legacy_vidext.VidExtFuncSetMode == NULL)
        return M64ERR_INPUT_ASSERT;

    return g_legacy_vidext.VidExtFuncSetMode(width, height, bpp, mode, flags);
}

static m64p_error compat_init_with_render_mode(modular_render_mode mode)
{
    if (mode != MODULAR_RENDER_OPENGL)
        return M64ERR_UNSUPPORTED;

    if (g_legacy_vidext.VidExtFuncInit == NULL)
        return M64ERR_INPUT_ASSERT;

    return g_legacy_vidext.VidExtFuncInit();
}

static m64p_error compat_vk_get_surface(void **surface, void *instance)
{
    (void) surface;
    (void) instance;
    return M64ERR_UNSUPPORTED;
}

static m64p_error compat_vk_get_instance_extensions(const char **extensions[],
                                                    uint32_t *num_extensions)
{
    (void) extensions;
    (void) num_extensions;
    return M64ERR_UNSUPPORTED;
}

static m64p_error compat_override_vidext_v26(m64p_video_extension_functions *legacy)
{
    modular_vidext_v26 table;

    if (g_override_vidext_raw == NULL)
        return M64ERR_NOT_INIT;
    if (legacy == NULL)
        return M64ERR_INPUT_ASSERT;

    memcpy(&g_legacy_vidext, legacy, sizeof(g_legacy_vidext));
    memset(&table, 0, sizeof(table));

    table.Functions = 17;
    table.VidExtFuncInit = g_legacy_vidext.VidExtFuncInit;
    table.VidExtFuncQuit = g_legacy_vidext.VidExtFuncQuit;
    table.VidExtFuncListModes = g_legacy_vidext.VidExtFuncListModes;
    table.VidExtFuncListRates = compat_list_rates;
    table.VidExtFuncSetMode = g_legacy_vidext.VidExtFuncSetMode;
    table.VidExtFuncSetModeWithRate = compat_set_mode_with_rate;
    table.VidExtFuncGLGetProc = g_legacy_vidext.VidExtFuncGLGetProc;
    table.VidExtFuncGLSetAttr = g_legacy_vidext.VidExtFuncGLSetAttr;
    table.VidExtFuncGLGetAttr = g_legacy_vidext.VidExtFuncGLGetAttr;
    table.VidExtFuncGLSwapBuf = g_legacy_vidext.VidExtFuncGLSwapBuf;
    table.VidExtFuncSetCaption = g_legacy_vidext.VidExtFuncSetCaption;
    table.VidExtFuncToggleFS = g_legacy_vidext.VidExtFuncToggleFS;
    table.VidExtFuncResizeWindow = g_legacy_vidext.VidExtFuncResizeWindow;
    table.VidExtFuncGLGetDefaultFramebuffer = g_legacy_vidext.VidExtFuncGLGetDefaultFramebuffer;
    table.VidExtFuncInitWithRenderMode = compat_init_with_render_mode;
    table.VidExtFuncVKGetSurface = compat_vk_get_surface;
    table.VidExtFuncVKGetInstanceExtensions = compat_vk_get_instance_extensions;

    return g_override_vidext_raw(&table);
}

static void set_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_last_error, sizeof(g_last_error), fmt, ap);
    va_end(ap);
}

const char *m64p_modular_last_error(void)
{
    return g_last_error;
}

static void *load_symbol(void *handle, const char *name)
{
    const char *error;
    void *symbol;

    dlerror();
    symbol = dlsym(handle, name);
    error = dlerror();
    if (error != NULL)
    {
        set_error("missing symbol %s: %s", name, error);
        return NULL;
    }

    return symbol;
}

static void *open_core_library(const char *path)
{
    void *handle = NULL;
    const char *error = NULL;

    if (path != NULL && path[0] != '\0')
    {
        handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (handle == NULL)
        {
            error = dlerror();
            set_error("cannot open core %s: %s", path, error ? error : "unknown dlopen error");
        }
        return handle;
    }

    handle = dlopen("libmupen64plus.so.2", RTLD_NOW | RTLD_LOCAL);
    if (handle != NULL)
        return handle;

    dlerror();
    handle = dlopen("libmupen64plus.so", RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL)
    {
        error = dlerror();
        set_error("cannot find libmupen64plus.so.2 or libmupen64plus.so: %s",
                  error ? error : "unknown dlopen error");
    }

    return handle;
}

m64p_error m64p_modular_core_load(m64p_modular_core *core, const char *path)
{
    ptr_PluginGetVersion get_version;
    m64p_plugin_type type = M64PLUGIN_NULL;
    m64p_error result;
    void *override_symbol;

    if (core == NULL)
        return M64ERR_INPUT_ASSERT;

    memset(core, 0, sizeof(*core));
    g_last_error[0] = '\0';

    core->handle = open_core_library(path);
    if (core->handle == NULL)
        return M64ERR_INPUT_NOT_FOUND;

    get_version = (ptr_PluginGetVersion) load_symbol(core->handle, "PluginGetVersion");
    if (get_version == NULL)
        goto invalid_core;

    result = get_version(&type, &core->version, &core->api_version,
                         &core->name, &core->capabilities);
    if (result != M64ERR_SUCCESS)
    {
        set_error("PluginGetVersion failed for core (error %d)", result);
        goto invalid_core;
    }

    if (type != M64PLUGIN_CORE)
    {
        set_error("loaded library is not a Mupen64Plus core (type %d)", type);
        goto invalid_core;
    }

    if ((core->api_version & 0xffff0000) !=
        (M64P_MODULAR_CORE_API_VERSION & 0xffff0000))
    {
        set_error("incompatible core API 0x%06x; expected major API 0x%06x",
                  core->api_version, M64P_MODULAR_CORE_API_VERSION);
        dlclose(core->handle);
        memset(core, 0, sizeof(*core));
        return M64ERR_INCOMPATIBLE;
    }

    core->error_message = (ptr_CoreErrorMessage) load_symbol(core->handle, "CoreErrorMessage");
    core->startup = (ptr_CoreStartup) load_symbol(core->handle, "CoreStartup");
    core->shutdown = (ptr_CoreShutdown) load_symbol(core->handle, "CoreShutdown");
    core->attach_plugin = (ptr_CoreAttachPlugin) load_symbol(core->handle, "CoreAttachPlugin");
    core->detach_plugin = (ptr_CoreDetachPlugin) load_symbol(core->handle, "CoreDetachPlugin");
    core->do_command = (ptr_CoreDoCommand) load_symbol(core->handle, "CoreDoCommand");
    override_symbol = load_symbol(core->handle, "CoreOverrideVidExt");

    if (core->error_message == NULL || core->startup == NULL ||
        core->shutdown == NULL || core->attach_plugin == NULL ||
        core->detach_plugin == NULL || core->do_command == NULL ||
        override_symbol == NULL)
        goto invalid_core;

    if (core->version >= 0x020600)
    {
        g_override_vidext_raw = (raw_override_vidext_t) override_symbol;
        core->override_vidext = compat_override_vidext_v26;
    }
    else
    {
        core->override_vidext = (ptr_CoreOverrideVidExt) override_symbol;
    }

    return M64ERR_SUCCESS;

invalid_core:
    if (core->handle != NULL)
        dlclose(core->handle);
    memset(core, 0, sizeof(*core));
    g_override_vidext_raw = NULL;
    memset(&g_legacy_vidext, 0, sizeof(g_legacy_vidext));
    return M64ERR_INPUT_INVALID;
}

void m64p_modular_core_unload(m64p_modular_core *core)
{
    if (core == NULL)
        return;

    if (core->handle != NULL)
        dlclose(core->handle);

    memset(core, 0, sizeof(*core));
    g_override_vidext_raw = NULL;
    memset(&g_legacy_vidext, 0, sizeof(g_legacy_vidext));
}

m64p_error m64p_modular_plugin_load(m64p_modular_core *core,
                                    m64p_modular_plugin *plugin,
                                    m64p_plugin_type expected_type,
                                    const char *path,
                                    ptr_DebugCallback debug_cb)
{
    ptr_PluginGetVersion get_version;
    ptr_PluginStartup startup;
    m64p_error result;
    const char *error;

    if (core == NULL || core->handle == NULL || plugin == NULL ||
        path == NULL || path[0] == '\0')
        return M64ERR_INPUT_ASSERT;

    memset(plugin, 0, sizeof(*plugin));
    g_last_error[0] = '\0';

    plugin->handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (plugin->handle == NULL)
    {
        error = dlerror();
        set_error("cannot open plugin %s: %s", path,
                  error ? error : "unknown dlopen error");
        return M64ERR_INPUT_NOT_FOUND;
    }

    get_version = (ptr_PluginGetVersion) load_symbol(plugin->handle, "PluginGetVersion");
    startup = (ptr_PluginStartup) load_symbol(plugin->handle, "PluginStartup");
    plugin->shutdown = (ptr_PluginShutdown) load_symbol(plugin->handle, "PluginShutdown");

    if (get_version == NULL || startup == NULL || plugin->shutdown == NULL)
        goto fail;

    result = get_version(&plugin->type, &plugin->version, &plugin->api_version,
                         &plugin->name, &plugin->capabilities);
    if (result != M64ERR_SUCCESS)
    {
        set_error("PluginGetVersion failed for %s (error %d)", path, result);
        goto fail;
    }

    if (plugin->type != expected_type)
    {
        set_error("plugin %s has type %d, expected %d",
                  path, plugin->type, expected_type);
        goto fail;
    }

    result = startup(core->handle, plugin, debug_cb);
    if (result != M64ERR_SUCCESS)
    {
        set_error("PluginStartup failed for %s (error %d)", path, result);
        goto fail;
    }

    result = core->attach_plugin(expected_type, plugin->handle);
    if (result != M64ERR_SUCCESS)
    {
        set_error("CoreAttachPlugin failed for %s (error %d)", path, result);
        plugin->shutdown();
        goto fail;
    }

    plugin->attached = 1;
    return M64ERR_SUCCESS;

fail:
    if (plugin->handle != NULL)
        dlclose(plugin->handle);
    memset(plugin, 0, sizeof(*plugin));
    return M64ERR_PLUGIN_FAIL;
}

void m64p_modular_plugin_unload(m64p_modular_core *core,
                                m64p_modular_plugin *plugin)
{
    if (plugin == NULL || plugin->handle == NULL)
        return;

    if (plugin->attached && core != NULL && core->detach_plugin != NULL)
        core->detach_plugin(plugin->type);

    if (plugin->shutdown != NULL)
        plugin->shutdown();

    dlclose(plugin->handle);
    memset(plugin, 0, sizeof(*plugin));
}
