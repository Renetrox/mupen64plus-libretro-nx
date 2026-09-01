#include "m64p_loader.h"

#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static char g_last_error[512];

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
    core->override_vidext = (ptr_CoreOverrideVidExt) load_symbol(core->handle, "CoreOverrideVidExt");

    if (core->error_message == NULL || core->startup == NULL ||
        core->shutdown == NULL || core->attach_plugin == NULL ||
        core->detach_plugin == NULL || core->do_command == NULL ||
        core->override_vidext == NULL)
        goto invalid_core;

    return M64ERR_SUCCESS;

invalid_core:
    dlclose(core->handle);
    memset(core, 0, sizeof(*core));
    return M64ERR_INPUT_INVALID;
}

void m64p_modular_core_unload(m64p_modular_core *core)
{
    if (core == NULL)
        return;

    if (core->handle != NULL)
        dlclose(core->handle);

    memset(core, 0, sizeof(*core));
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
                         &plugin->name, NULL);
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
