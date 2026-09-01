#include "m64p_loader.h"

#include <stdio.h>
#include <string.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static void debug_callback(void *context, int level, const char *message)
{
    (void) context;
    fprintf(stderr, "[m64p:%d] %s\n", level, message ? message : "(null)");
}

static void print_version(const char *label, const char *name, int version, int api)
{
    printf("%-7s %-32s version %d.%d.%d  API %d.%d.%d\n",
           label,
           name ? name : "(unnamed)",
           (version >> 16) & 0xffff, (version >> 8) & 0xff, version & 0xff,
           (api >> 16) & 0xffff, (api >> 8) & 0xff, api & 0xff);
}

static void dirname_copy(const char *path, char *out, size_t out_size)
{
    const char *slash;
    size_t len;

    if (out == NULL || out_size == 0)
        return;

    if (path == NULL || path[0] == '\0')
    {
        snprintf(out, out_size, ".");
        return;
    }

    slash = strrchr(path, '/');
    if (slash == NULL)
    {
        snprintf(out, out_size, ".");
        return;
    }

    len = (size_t) (slash - path);
    if (len == 0)
        len = 1;
    if (len >= out_size)
        len = out_size - 1;

    memcpy(out, path, len);
    out[len] = '\0';
}

static int load_plugin(m64p_modular_core *core,
                       m64p_modular_plugin *slot,
                       m64p_plugin_type type,
                       const char *label,
                       const char *path)
{
    m64p_error result;

    if (path == NULL || strcmp(path, "-") == 0)
        return 0;

    result = m64p_modular_plugin_load(core, slot, type, path, debug_callback);
    if (result != M64ERR_SUCCESS)
    {
        fprintf(stderr, "%s load failed: %s\n", label, m64p_modular_last_error());
        return -1;
    }

    print_version(label, slot->name, slot->version, slot->api_version);
    return 0;
}

int main(int argc, char **argv)
{
    m64p_modular_core core;
    m64p_modular_plugin gfx = {0};
    m64p_modular_plugin audio = {0};
    m64p_modular_plugin input = {0};
    m64p_modular_plugin rsp = {0};
    const char *core_path;
    const char *data_path;
    char core_dir[PATH_MAX];
    m64p_error result;
    int started = 0;
    int status = 1;

    if (argc < 2 || argc > 6)
    {
        fprintf(stderr,
                "Usage: %s CORE [GFX|-] [AUDIO|-] [INPUT|-] [RSP|-]\n"
                "\n"
                "CORE may be '-' to search libmupen64plus.so.2/libmupen64plus.so.\n"
                "When CORE is an explicit path, its directory is also used as the\n"
                "Mupen64Plus shared-data directory (Glide64mk2.ini, mupen64plus.ini, etc.).\n"
                "The probe validates ABI loading, PluginStartup and CoreAttachPlugin.\n",
                argv[0]);
        return 2;
    }

    core_path = strcmp(argv[1], "-") == 0 ? NULL : argv[1];
    dirname_copy(core_path, core_dir, sizeof(core_dir));
    data_path = core_path ? core_dir : ".";

    result = m64p_modular_core_load(&core, core_path);
    if (result != M64ERR_SUCCESS)
    {
        fprintf(stderr, "Core load failed: %s\n", m64p_modular_last_error());
        return 1;
    }

    print_version("CORE", core.name, core.version, core.api_version);
    printf("Core capabilities: 0x%x\n", core.capabilities);
    printf("Shared data path: %s\n", data_path);

    result = core.startup(M64P_MODULAR_CORE_API_VERSION,
                          ".", data_path,
                          NULL, debug_callback,
                          NULL, NULL);
    if (result != M64ERR_SUCCESS)
    {
        fprintf(stderr, "CoreStartup failed (%d): %s\n",
                result,
                core.error_message ? core.error_message(result) : "unknown error");
        goto cleanup;
    }
    started = 1;

    if (argc > 2 && load_plugin(&core, &gfx, M64PLUGIN_GFX, "GFX", argv[2]) != 0)
        goto cleanup;
    if (argc > 3 && load_plugin(&core, &audio, M64PLUGIN_AUDIO, "AUDIO", argv[3]) != 0)
        goto cleanup;
    if (argc > 4 && load_plugin(&core, &input, M64PLUGIN_INPUT, "INPUT", argv[4]) != 0)
        goto cleanup;
    if (argc > 5 && load_plugin(&core, &rsp, M64PLUGIN_RSP, "RSP", argv[5]) != 0)
        goto cleanup;

    puts("OK: external Mupen64Plus core and requested plugins attached successfully.");
    status = 0;

cleanup:
    m64p_modular_plugin_unload(&core, &rsp);
    m64p_modular_plugin_unload(&core, &input);
    m64p_modular_plugin_unload(&core, &audio);
    m64p_modular_plugin_unload(&core, &gfx);

    if (started)
        core.shutdown();

    m64p_modular_core_unload(&core);
    return status;
}
