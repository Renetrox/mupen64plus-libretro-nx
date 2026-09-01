#ifndef M64P_MODULAR_LOADER_H
#define M64P_MODULAR_LOADER_H

#include "m64p_common.h"
#include "m64p_frontend.h"
#include "m64p_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define M64P_MODULAR_CORE_API_VERSION 0x020001

typedef struct m64p_modular_core
{
    m64p_dynlib_handle handle;

    ptr_CoreErrorMessage error_message;
    ptr_CoreStartup startup;
    ptr_CoreShutdown shutdown;
    ptr_CoreAttachPlugin attach_plugin;
    ptr_CoreDetachPlugin detach_plugin;
    ptr_CoreDoCommand do_command;
    ptr_CoreOverrideVidExt override_vidext;

    int version;
    int api_version;
    int capabilities;
    const char *name;
} m64p_modular_core;

typedef struct m64p_modular_plugin
{
    m64p_dynlib_handle handle;
    ptr_PluginShutdown shutdown;

    m64p_plugin_type type;
    int version;
    int api_version;
    const char *name;
    int attached;
} m64p_modular_plugin;

m64p_error m64p_modular_core_load(m64p_modular_core *core, const char *path);
void m64p_modular_core_unload(m64p_modular_core *core);

m64p_error m64p_modular_plugin_load(m64p_modular_core *core,
                                    m64p_modular_plugin *plugin,
                                    m64p_plugin_type expected_type,
                                    const char *path,
                                    ptr_DebugCallback debug_cb);
void m64p_modular_plugin_unload(m64p_modular_core *core,
                                m64p_modular_plugin *plugin);

const char *m64p_modular_last_error(void);

#ifdef __cplusplus
}
#endif

#endif
