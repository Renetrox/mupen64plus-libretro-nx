#include "m64p_loader.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
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

static int read_rom(const char *path, void **data_out, int *size_out)
{
    FILE *fp = NULL;
    long length;
    void *data = NULL;

    if (path == NULL || data_out == NULL || size_out == NULL)
        return -1;

    *data_out = NULL;
    *size_out = 0;

    fp = fopen(path, "rb");
    if (fp == NULL)
    {
        fprintf(stderr, "Cannot open ROM: %s\n", path);
        return -1;
    }

    if (fseek(fp, 0, SEEK_END) != 0)
        goto fail;

    length = ftell(fp);
    if (length < 4096 || length > INT_MAX || (length & 3) != 0)
    {
        fprintf(stderr, "Invalid ROM size: %ld bytes\n", length);
        goto fail;
    }

    if (fseek(fp, 0, SEEK_SET) != 0)
        goto fail;

    data = malloc((size_t) length);
    if (data == NULL)
    {
        fprintf(stderr, "Out of memory reading ROM (%ld bytes)\n", length);
        goto fail;
    }

    if (fread(data, 1, (size_t) length, fp) != (size_t) length)
    {
        fprintf(stderr, "Short read while loading ROM: %s\n", path);
        free(data);
        goto fail;
    }

    fclose(fp);
    *data_out = data;
    *size_out = (int) length;
    return 0;

fail:
    fclose(fp);
    return -1;
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
    const char *rom_path;
    const char *data_path;
    char core_dir[PATH_MAX];
    void *rom_data = NULL;
    int rom_size = 0;
    m64p_error result;
    int started = 0;
    int rom_open = 0;
    int status = 1;

    if (argc < 3 || argc > 7)
    {
        fprintf(stderr,
                "Usage: %s CORE ROM [GFX|-] [AUDIO|-] [INPUT|-] [RSP|-]\n"
                "\n"
                "CORE may be '-' to search libmupen64plus.so.2/libmupen64plus.so.\n"
                "ROM must be a valid N64 ROM image. The official Mupen64Plus API\n"
                "requires a ROM (or disk) to be open before CoreAttachPlugin().\n"
                "When CORE is an explicit path, its directory is also used as the\n"
                "Mupen64Plus config/shared-data directory (Glide64mk2.ini, etc.).\n",
                argv[0]);
        return 2;
    }

    core_path = strcmp(argv[1], "-") == 0 ? NULL : argv[1];
    rom_path = argv[2];
    dirname_copy(core_path, core_dir, sizeof(core_dir));
    data_path = core_path ? core_dir : ".";

    if (read_rom(rom_path, &rom_data, &rom_size) != 0)
        return 1;

    result = m64p_modular_core_load(&core, core_path);
    if (result != M64ERR_SUCCESS)
    {
        fprintf(stderr, "Core load failed: %s\n", m64p_modular_last_error());
        free(rom_data);
        return 1;
    }

    print_version("CORE", core.name, core.version, core.api_version);
    printf("Core capabilities: 0x%x\n", core.capabilities);
    printf("Shared data path: %s\n", data_path);
    printf("ROM: %s (%d bytes)\n", rom_path, rom_size);

    result = core.startup(M64P_MODULAR_CORE_API_VERSION,
                          data_path, data_path,
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

    result = core.do_command(M64CMD_ROM_OPEN, rom_size, rom_data);
    if (result != M64ERR_SUCCESS)
    {
        fprintf(stderr, "M64CMD_ROM_OPEN failed (%d): %s\n",
                result,
                core.error_message ? core.error_message(result) : "unknown error");
        goto cleanup;
    }
    rom_open = 1;

    if (argc > 3 && load_plugin(&core, &gfx, M64PLUGIN_GFX, "GFX", argv[3]) != 0)
        goto cleanup;
    if (argc > 4 && load_plugin(&core, &audio, M64PLUGIN_AUDIO, "AUDIO", argv[4]) != 0)
        goto cleanup;
    if (argc > 5 && load_plugin(&core, &input, M64PLUGIN_INPUT, "INPUT", argv[5]) != 0)
        goto cleanup;
    if (argc > 6 && load_plugin(&core, &rsp, M64PLUGIN_RSP, "RSP", argv[6]) != 0)
        goto cleanup;

    puts("OK: ROM opened and external Mupen64Plus plugins attached successfully.");
    status = 0;

cleanup:
    m64p_modular_plugin_unload(&core, &rsp);
    m64p_modular_plugin_unload(&core, &input);
    m64p_modular_plugin_unload(&core, &audio);
    m64p_modular_plugin_unload(&core, &gfx);

    if (rom_open)
        core.do_command(M64CMD_ROM_CLOSE, 0, NULL);

    if (started)
        core.shutdown();

    m64p_modular_core_unload(&core);
    free(rom_data);
    return status;
}
