#include "m64p_loader.h"

#include <libretro.h>
#include <libco.h>

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define MODULAR_FRONTEND_API_VERSION 0x020102
#define GAME_THREAD_STACK_SIZE (8 * 1024 * 1024)

static retro_environment_t environ_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_log_printf_t log_cb;

static struct retro_hw_render_callback hw;

static m64p_modular_core core;
static m64p_modular_plugin gfx;
static m64p_modular_plugin audio;
static m64p_modular_plugin input;
static m64p_modular_plugin rsp;

static cothread_t retro_thread;
static cothread_t game_thread;

static bool core_started;
static bool rom_open;
static bool context_ready;
static bool emulating;
static bool emu_finished;
static bool frame_ready;
static bool game_loaded;

static unsigned screen_width = 640;
static unsigned screen_height = 480;
static int gl_attrs[32];
static void lr_log(enum retro_log_level level, const char *fmt, ...);
static uint32_t vidext_default_fbo(void);

typedef void (*gl_bind_framebuffer_t)(unsigned int target, unsigned int framebuffer);
typedef void (*gl_blit_framebuffer_t)(int srcX0, int srcY0, int srcX1, int srcY1,
                                      int dstX0, int dstY0, int dstX1, int dstY1,
                                      unsigned int mask, unsigned int filter);

typedef void (*gl_read_buffer_t)(unsigned int mode);
typedef void (*gl_draw_buffer_t)(unsigned int mode);
typedef void (*gl_get_integerv_t)(unsigned int pname, int *data);
typedef unsigned int (*gl_get_error_t)(void);
typedef void (*gl_use_program_t)(unsigned int program);
typedef void (*gl_active_texture_t)(unsigned int texture);
typedef void (*gl_bind_texture_t)(unsigned int target, unsigned int texture);
typedef void (*gl_viewport_t)(int x, int y, int width, int height);
typedef void (*gl_read_pixels_t)(int x, int y, int width, int height,
                                     unsigned int format, unsigned int type,
                                     void *pixels);

typedef unsigned char (*gl_is_enabled_t)(unsigned int cap);
typedef void (*gl_enable_t)(unsigned int cap);
typedef void (*gl_disable_t)(unsigned int cap);
typedef void (*gl_scissor_t)(int x, int y, int width, int height);

static gl_bind_framebuffer_t gl_bind_framebuffer;
static gl_blit_framebuffer_t gl_blit_framebuffer;
static gl_read_buffer_t gl_read_buffer;
static gl_draw_buffer_t gl_draw_buffer;
static gl_get_integerv_t gl_get_integerv;
static gl_get_error_t gl_get_error;
static gl_use_program_t gl_use_program;
static gl_active_texture_t gl_active_texture;
static gl_bind_texture_t gl_bind_texture;

static int saved_gl_program;
static int saved_gl_active_texture;
static int saved_gl_texture_2d;
static int saved_gl_texture0_2d;
static bool saved_gl_handoff;
static gl_viewport_t gl_viewport;
static gl_read_pixels_t gl_read_pixels;
static gl_is_enabled_t gl_is_enabled;
static gl_enable_t gl_enable;
static gl_disable_t gl_disable;
static gl_scissor_t gl_scissor;
static bool gl_present_checked;
static bool gl_present_logged;

#define MOD_GL_FRAMEBUFFER       0x8D40u
#define MOD_GL_READ_FRAMEBUFFER  0x8CA8u
#define MOD_GL_DRAW_FRAMEBUFFER  0x8CA9u
#define MOD_GL_COLOR_BUFFER_BIT  0x00004000u
#define MOD_GL_NEAREST           0x2600u
#define MOD_GL_BACK              0x0405u
#define MOD_GL_COLOR_ATTACHMENT0 0x8CE0u
#define MOD_GL_VIEWPORT                 0x0BA2u
#define MOD_GL_READ_BUFFER              0x0C02u
#define MOD_GL_DRAW_BUFFER              0x0C01u
#define MOD_GL_READ_FRAMEBUFFER_BINDING 0x8CAAu
#define MOD_GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6u
#define MOD_GL_CURRENT_PROGRAM    0x8B8Du
#define MOD_GL_ACTIVE_TEXTURE     0x84E0u
#define MOD_GL_TEXTURE_BINDING_2D 0x8069u
#define MOD_GL_TEXTURE0           0x84C0u
#define MOD_GL_TEXTURE_2D         0x0DE1u
#define MOD_GL_RGBA          0x1908u
#define MOD_GL_UNSIGNED_BYTE 0x1401u
#define MOD_GL_SCISSOR_TEST  0x0C11u
#define MOD_GL_DEPTH_TEST    0x0B71u
#define MOD_GL_BLEND         0x0BE2u
#define MOD_GL_CULL_FACE     0x0B44u
#define MOD_GL_STENCIL_TEST  0x0B90u
#define MOD_GL_SCISSOR_BOX   0x0C10u

static void resolve_present_functions(void)
{
    if (gl_present_checked)
        return;

    gl_present_checked = true;

    if (hw.get_proc_address == NULL)
        return;

    gl_bind_framebuffer =
        (gl_bind_framebuffer_t) hw.get_proc_address("glBindFramebuffer");

    if (gl_bind_framebuffer == NULL)
        gl_bind_framebuffer =
            (gl_bind_framebuffer_t) hw.get_proc_address("glBindFramebufferEXT");

    gl_blit_framebuffer =
        (gl_blit_framebuffer_t) hw.get_proc_address("glBlitFramebuffer");

    if (gl_blit_framebuffer == NULL)
        gl_blit_framebuffer =
            (gl_blit_framebuffer_t) hw.get_proc_address("glBlitFramebufferEXT");

    gl_read_buffer =
        (gl_read_buffer_t) hw.get_proc_address("glReadBuffer");

    gl_draw_buffer =
        (gl_draw_buffer_t) hw.get_proc_address("glDrawBuffer");

    gl_get_integerv =
        (gl_get_integerv_t) hw.get_proc_address("glGetIntegerv");

    gl_get_error =
        (gl_get_error_t) hw.get_proc_address("glGetError");

    gl_use_program =
        (gl_use_program_t) hw.get_proc_address("glUseProgram");

    gl_active_texture =
        (gl_active_texture_t) hw.get_proc_address("glActiveTexture");

    gl_bind_texture =
        (gl_bind_texture_t) hw.get_proc_address("glBindTexture");

    gl_viewport =
        (gl_viewport_t) hw.get_proc_address("glViewport");

    gl_read_pixels =
        (gl_read_pixels_t) hw.get_proc_address("glReadPixels");

    gl_is_enabled =
        (gl_is_enabled_t) hw.get_proc_address("glIsEnabled");

    gl_enable =
        (gl_enable_t) hw.get_proc_address("glEnable");

    gl_disable =
        (gl_disable_t) hw.get_proc_address("glDisable");

    gl_scissor =
        (gl_scissor_t) hw.get_proc_address("glScissor");
}


static unsigned long sample_rgb_sum(void)
{
    unsigned char pixels[32 * 32 * 4];
    unsigned long sum = 0;
    unsigned i;

    if (gl_read_pixels == NULL)
        return 0;

    gl_read_pixels(
        (int)screen_width / 2 - 16,
        (int)screen_height / 2 - 16,
        32, 32,
        MOD_GL_RGBA,
        MOD_GL_UNSIGNED_BYTE,
        pixels);

    for (i = 0; i < 32 * 32; i++)
    {
        sum += pixels[i * 4 + 0];
        sum += pixels[i * 4 + 1];
        sum += pixels[i * 4 + 2];
    }

    return sum;
}

static void present_default_framebuffer(void)
{
    static unsigned long present_count = 0;
    uint32_t frontend_fbo;
    bool trace;

    present_count++;
    trace = (present_count <= 10 || (present_count % 60) == 0);

    if (!context_ready || hw.get_current_framebuffer == NULL)
        return;

    frontend_fbo = (uint32_t) hw.get_current_framebuffer();

    if (frontend_fbo == 0)
        return;

    resolve_present_functions();

    if (gl_bind_framebuffer == NULL || gl_blit_framebuffer == NULL)
    {
        if (!gl_present_logged)
        {
            lr_log(RETRO_LOG_WARN,
                   "OpenGL framebuffer blit functions unavailable\n");
            gl_present_logged = true;
        }
        return;
    }

    if (trace && gl_get_integerv != NULL)
    {
        int rfbo = -1, dfbo = -1, rb = -1, db = -1;
        int vp[4] = {0, 0, 0, 0};

        gl_get_integerv(MOD_GL_READ_FRAMEBUFFER_BINDING, &rfbo);
        gl_get_integerv(MOD_GL_DRAW_FRAMEBUFFER_BINDING, &dfbo);
        gl_get_integerv(MOD_GL_READ_BUFFER, &rb);
        gl_get_integerv(MOD_GL_DRAW_BUFFER, &db);
        gl_get_integerv(MOD_GL_VIEWPORT, vp);

        lr_log(RETRO_LOG_INFO,
               "GL before #%lu: readFBO=%d drawFBO=%d readBuf=0x%x drawBuf=0x%x viewport=%d,%d %dx%d\n",
               present_count, rfbo, dfbo, rb, db,
               vp[0], vp[1], vp[2], vp[3]);
    }

    if (gl_get_error != NULL)
        while (gl_get_error() != 0) {}

    gl_bind_framebuffer(MOD_GL_READ_FRAMEBUFFER, 0);

    if (gl_read_buffer != NULL)
        gl_read_buffer(MOD_GL_BACK);

    if (trace && gl_get_error != NULL)
        lr_log(RETRO_LOG_INFO, "GL source #%lu error=0x%x\n",
               present_count, gl_get_error());

    if (trace && gl_read_pixels != NULL)
        lr_log(RETRO_LOG_INFO, "PIX source #%lu rgb_sum=%lu\n",
               present_count, sample_rgb_sum());

    gl_bind_framebuffer(MOD_GL_DRAW_FRAMEBUFFER, frontend_fbo);

    if (gl_draw_buffer != NULL)
        gl_draw_buffer(MOD_GL_COLOR_ATTACHMENT0);

    if (trace && gl_get_error != NULL)
        lr_log(RETRO_LOG_INFO, "GL destination #%lu error=0x%x\n",
               present_count, gl_get_error());

    gl_blit_framebuffer(
        0, 0, (int) screen_width, (int) screen_height,
        0, 0, (int) screen_width, (int) screen_height,
        MOD_GL_COLOR_BUFFER_BIT, MOD_GL_NEAREST);

    if (trace && gl_get_error != NULL)
        lr_log(RETRO_LOG_INFO, "GL blit #%lu error=0x%x\n",
               present_count, gl_get_error());

    if (trace && gl_read_pixels != NULL)
    {
        gl_bind_framebuffer(MOD_GL_READ_FRAMEBUFFER, frontend_fbo);

        if (gl_read_buffer != NULL)
            gl_read_buffer(MOD_GL_COLOR_ATTACHMENT0);

        lr_log(RETRO_LOG_INFO, "PIX dest #%lu rgb_sum=%lu\n",
               present_count, sample_rgb_sum());
    }

    gl_bind_framebuffer(MOD_GL_FRAMEBUFFER, frontend_fbo);

    if (trace && gl_is_enabled != NULL && gl_get_integerv != NULL)
    {
        int scissor[4] = {0, 0, 0, 0};

        gl_get_integerv(MOD_GL_SCISSOR_BOX, scissor);

        lr_log(RETRO_LOG_INFO,
               "GL state #%lu: scissor=%d depth=%d blend=%d cull=%d stencil=%d box=%d,%d %dx%d\n",
               present_count,
               gl_is_enabled(MOD_GL_SCISSOR_TEST),
               gl_is_enabled(MOD_GL_DEPTH_TEST),
               gl_is_enabled(MOD_GL_BLEND),
               gl_is_enabled(MOD_GL_CULL_FACE),
               gl_is_enabled(MOD_GL_STENCIL_TEST),
               scissor[0], scissor[1], scissor[2], scissor[3]);
    }

    if (!gl_present_logged)
    {
        lr_log(RETRO_LOG_INFO,
               "presenting framebuffer 0 -> RetroArch FBO %u\n",
               frontend_fbo);
        gl_present_logged = true;
    }
}

static char runtime_dir[PATH_MAX];
static char core_path[PATH_MAX];
static char gfx_path[PATH_MAX];
static char audio_path[PATH_MAX];
static char input_path[PATH_MAX];
static char rsp_path[PATH_MAX];

static void lr_log(enum retro_log_level level, const char *fmt, ...)
{
    char buffer[1024];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);

    if (log_cb)
        log_cb(level, "mupen64plus-modular: %s", buffer);
    else
        fprintf(stderr, "mupen64plus-modular: %s", buffer);
}

static void m64p_debug(void *context, int level, const char *message)
{
    enum retro_log_level lr_level = RETRO_LOG_INFO;
    (void) context;

    if (level == M64MSG_ERROR)
        lr_level = RETRO_LOG_ERROR;
    else if (level == M64MSG_WARNING)
        lr_level = RETRO_LOG_WARN;
    else if (level == M64MSG_VERBOSE)
        lr_level = RETRO_LOG_DEBUG;

    lr_log(lr_level, "%s\n", message ? message : "(null)");
}

static void state_callback(void *context, m64p_core_param param, int value)
{
    (void) context;
    (void) param;
    (void) value;
}

static void dirname_copy(const char *path, char *out, size_t out_size)
{
    const char *slash;
    size_t len;

    if (out_size == 0)
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

    len = (size_t)(slash - path);
    if (len == 0)
        len = 1;
    if (len >= out_size)
        len = out_size - 1;

    memcpy(out, path, len);
    out[len] = '\0';
}

static bool file_exists(const char *path)
{
    return path != NULL && access(path, R_OK) == 0;
}

static void join_path(char *out, size_t out_size, const char *a, const char *b)
{
    size_t len = strlen(a);
    snprintf(out, out_size, "%s%s%s", a,
             (len > 0 && a[len - 1] == '/') ? "" : "/", b);
}

static bool find_runtime(void)
{
    const char *loaded_path = NULL;
    char core_dir[PATH_MAX];
    char candidate[PATH_MAX];
    char probe[PATH_MAX];

    if (environ_cb && environ_cb(RETRO_ENVIRONMENT_GET_LIBRETRO_PATH, &loaded_path) && loaded_path)
        dirname_copy(loaded_path, core_dir, sizeof(core_dir));
    else
        snprintf(core_dir, sizeof(core_dir), ".");

    join_path(candidate, sizeof(candidate), core_dir, "runtime");
    join_path(probe, sizeof(probe), candidate, "libmupen64plus.so.2");
    if (file_exists(probe))
    {
        snprintf(runtime_dir, sizeof(runtime_dir), "%s", candidate);
        snprintf(core_path, sizeof(core_path), "%s", probe);
        return true;
    }

    snprintf(candidate, sizeof(candidate), "%s/../runtime", core_dir);
    join_path(probe, sizeof(probe), candidate, "libmupen64plus.so.2");
    if (file_exists(probe))
    {
        snprintf(runtime_dir, sizeof(runtime_dir), "%s", candidate);
        snprintf(core_path, sizeof(core_path), "%s", probe);
        return true;
    }

    join_path(probe, sizeof(probe), core_dir, "libmupen64plus.so.2");
    if (file_exists(probe))
    {
        snprintf(runtime_dir, sizeof(runtime_dir), "%s", core_dir);
        snprintf(core_path, sizeof(core_path), "%s", probe);
        return true;
    }

    lr_log(RETRO_LOG_ERROR,
           "runtime not found next to core; expected runtime/libmupen64plus.so.2 or ../runtime/libmupen64plus.so.2\n");
    return false;
}

static void select_plugin_paths(void)
{
    struct retro_variable var = { "mupen64plus_modular_video_plugin", NULL };
    const char *gfx_name = "mupen64plus-video-glide64mk2.so";
    char plugin_dir[PATH_MAX];

    if (environ_cb && environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
    {
        if (strcmp(var.value, "GLideN64") == 0)
            gfx_name = "mupen64plus-video-GLideN64.so";
    }

    join_path(plugin_dir, sizeof(plugin_dir), runtime_dir, "plugins");
    join_path(gfx_path, sizeof(gfx_path), plugin_dir, gfx_name);
    join_path(audio_path, sizeof(audio_path), plugin_dir, "mupen64plus-audio-sdl.so");
    join_path(input_path, sizeof(input_path), plugin_dir, "mupen64plus-input-sdl.so");
    join_path(rsp_path, sizeof(rsp_path), plugin_dir, "mupen64plus-rsp-hle.so");
}

static m64p_error vidext_init(void)
{
    return M64ERR_SUCCESS;
}

static m64p_error vidext_quit(void)
{
    return M64ERR_SUCCESS;
}

static m64p_error vidext_list_modes(m64p_2d_size *sizes, int *count)
{
    if (count == NULL)
        return M64ERR_INPUT_ASSERT;

    if (sizes != NULL && *count > 0)
    {
        sizes[0].uiWidth = screen_width;
        sizes[0].uiHeight = screen_height;
        *count = 1;
    }
    else
        *count = 0;

    return M64ERR_SUCCESS;
}

static m64p_error vidext_set_mode(int width, int height, int bpp, int mode, int flags)
{
    (void) bpp;
    (void) mode;
    (void) flags;

    if (width > 0)
        screen_width = (unsigned) width;
    if (height > 0)
        screen_height = (unsigned) height;

    return M64ERR_SUCCESS;
}

static m64p_function vidext_get_proc(const char *name)
{
    if (!context_ready || hw.get_proc_address == NULL || name == NULL)
        return NULL;

    return (m64p_function) hw.get_proc_address(name);
}

static m64p_error vidext_set_attr(m64p_GLattr attr, int value)
{
    unsigned index = (unsigned) attr;
    if (index < (sizeof(gl_attrs) / sizeof(gl_attrs[0])))
        gl_attrs[index] = value;
    return M64ERR_SUCCESS;
}

static m64p_error vidext_get_attr(m64p_GLattr attr, int *value)
{
    unsigned index = (unsigned) attr;

    if (value == NULL)
        return M64ERR_INPUT_ASSERT;

    if (index < (sizeof(gl_attrs) / sizeof(gl_attrs[0])))
        *value = gl_attrs[index];
    else
        *value = 0;

    return M64ERR_SUCCESS;
}

static m64p_error vidext_swap_buffers(void)
{
    static unsigned long swap_count = 0;

    swap_count++;

    if (swap_count <= 10 || (swap_count % 60) == 0)
        lr_log(RETRO_LOG_INFO,
               "VidExt_GL_SwapBuffers #%lu, frontend FBO=%u\n",
               swap_count,
               vidext_default_fbo());
    typedef void (*gl_finish_t)(void);
    static gl_finish_t finish_cb = NULL;

    present_default_framebuffer();

    if (finish_cb == NULL && hw.get_proc_address != NULL)
        finish_cb = (gl_finish_t) hw.get_proc_address("glFinish");

    if (finish_cb != NULL)
        finish_cb();

    /*
     * Glide64mk2 leaves GL state active. RetroArch expects the HW
     * renderer to give the context back in a neutral state.
     */
    if (gl_disable != NULL)
    {
        gl_disable(MOD_GL_SCISSOR_TEST);
        gl_disable(MOD_GL_BLEND);
        gl_disable(MOD_GL_DEPTH_TEST);
        gl_disable(MOD_GL_CULL_FACE);
        gl_disable(MOD_GL_STENCIL_TEST);
    }

    /*
     * Save Glide64mk2 shader/texture state, then give RetroArch
     * a neutral GL state. RetroArch keeps its own GL state cache
     * and must not inherit the plugin's current program.
     */
    if (gl_get_integerv != NULL &&
        gl_use_program != NULL &&
        gl_active_texture != NULL &&
        gl_bind_texture != NULL)
    {
        gl_get_integerv(MOD_GL_CURRENT_PROGRAM, &saved_gl_program);
        gl_get_integerv(MOD_GL_ACTIVE_TEXTURE, &saved_gl_active_texture);
        gl_get_integerv(MOD_GL_TEXTURE_BINDING_2D, &saved_gl_texture_2d);

        /*
         * RetroArch expects texture unit 0 in a neutral state.
         * Preserve Glide64mk2's binding there before clearing it.
         */
        gl_active_texture(MOD_GL_TEXTURE0);
        gl_get_integerv(MOD_GL_TEXTURE_BINDING_2D,
                        &saved_gl_texture0_2d);

        gl_use_program(0);
        gl_bind_texture(MOD_GL_TEXTURE_2D, 0);

        saved_gl_handoff = true;
    }

    frame_ready = true;

    if (emulating && game_thread != NULL && retro_thread != NULL)
        co_switch(retro_thread);

    return M64ERR_SUCCESS;
}

static m64p_error vidext_set_caption(const char *caption)
{
    (void) caption;
    return M64ERR_SUCCESS;
}

static m64p_error vidext_toggle_fs(void)
{
    return M64ERR_SUCCESS;
}

static m64p_error vidext_resize(int width, int height)
{
    if (width > 0)
        screen_width = (unsigned) width;
    if (height > 0)
        screen_height = (unsigned) height;
    return M64ERR_SUCCESS;
}

static uint32_t vidext_default_fbo(void)
{
    if (!context_ready || hw.get_current_framebuffer == NULL)
        return 0;
    return (uint32_t) hw.get_current_framebuffer();
}

static m64p_video_extension_functions vidext = {
    12,
    vidext_init,
    vidext_quit,
    vidext_list_modes,
    vidext_set_mode,
    vidext_get_proc,
    vidext_set_attr,
    vidext_get_attr,
    vidext_swap_buffers,
    vidext_set_caption,
    vidext_toggle_fs,
    vidext_resize,
    vidext_default_fbo
};

static void context_reset(void)
{
    context_ready = true;
    lr_log(RETRO_LOG_INFO, "OpenGL context ready\n");
}

static void context_destroy(void)
{
    context_ready = false;
}

static bool request_hw_context(void)
{
    memset(&hw, 0, sizeof(hw));
    hw.context_type = RETRO_HW_CONTEXT_OPENGL;
    hw.context_reset = context_reset;
    hw.context_destroy = context_destroy;
    hw.depth = true;
    hw.stencil = true;
    hw.bottom_left_origin = true;
    hw.cache_context = false;
    hw.debug_context = false;

    if (!environ_cb || !environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw))
    {
        lr_log(RETRO_LOG_ERROR, "frontend rejected OpenGL hardware rendering\n");
        return false;
    }

    return true;
}

static bool load_plugin_required(m64p_modular_plugin *slot,
                                 m64p_plugin_type type,
                                 const char *label,
                                 const char *path)
{
    m64p_error result;

    if (!file_exists(path))
    {
        lr_log(RETRO_LOG_ERROR, "%s plugin not found: %s\n", label, path);
        return false;
    }

    result = m64p_modular_plugin_load(&core, slot, type, path, m64p_debug);
    if (result != M64ERR_SUCCESS)
    {
        lr_log(RETRO_LOG_ERROR, "%s plugin load failed: %s\n",
               label, m64p_modular_last_error());
        return false;
    }

    lr_log(RETRO_LOG_INFO, "%s: %s\n", label,
           slot->name ? slot->name : path);
    return true;
}

static void unload_plugins(void)
{
    m64p_modular_plugin_unload(&core, &rsp);
    m64p_modular_plugin_unload(&core, &input);
    m64p_modular_plugin_unload(&core, &audio);
    m64p_modular_plugin_unload(&core, &gfx);
}

static void game_thread_main(void)
{
    m64p_error result;

    emulating = true;
    result = core.do_command(M64CMD_EXECUTE, 0, NULL);
    emulating = false;
    emu_finished = true;

    if (result != M64ERR_SUCCESS)
        lr_log(RETRO_LOG_ERROR, "M64CMD_EXECUTE returned error %d\n", result);

    for (;;)
        co_switch(retro_thread);
}

static void cleanup_game(void)
{
    if (game_thread != NULL)
    {
        if (emulating && core.do_command)
        {
            core.do_command(M64CMD_STOP, 0, NULL);
            co_switch(game_thread);
        }

        co_delete(game_thread);
        game_thread = NULL;
    }

    unload_plugins();

    if (rom_open && core.do_command)
    {
        core.do_command(M64CMD_ROM_CLOSE, 0, NULL);
        rom_open = false;
    }

    if (core_started && core.shutdown)
    {
        core.shutdown();
        core_started = false;
    }

    m64p_modular_core_unload(&core);

    memset(&gfx, 0, sizeof(gfx));
    memset(&audio, 0, sizeof(audio));
    memset(&input, 0, sizeof(input));
    memset(&rsp, 0, sizeof(rsp));

    context_ready = false;
    emulating = false;
    emu_finished = false;
    frame_ready = false;
    game_loaded = false;
}

RETRO_API void retro_set_environment(retro_environment_t cb)
{
    static const struct retro_variable variables[] = {
        { "mupen64plus_modular_video_plugin", "Video Plugin; glide64mk2|GLideN64" },
        { NULL, NULL }
    };

    environ_cb = cb;
    if (environ_cb)
        environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void *) variables);
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb)
{
    video_cb = cb;
}

RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb)
{
    audio_cb = cb;
    (void) audio_cb;
}

RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
    audio_batch_cb = cb;
    (void) audio_batch_cb;
}

RETRO_API void retro_set_input_poll(retro_input_poll_t cb)
{
    input_poll_cb = cb;
}

RETRO_API void retro_set_input_state(retro_input_state_t cb)
{
    input_state_cb = cb;
    (void) input_state_cb;
}

RETRO_API unsigned retro_api_version(void)
{
    return RETRO_API_VERSION;
}

RETRO_API void retro_get_system_info(struct retro_system_info *info)
{
    memset(info, 0, sizeof(*info));
    info->library_name = "Mupen64Plus Modular";
    info->library_version = "0.1";
    info->valid_extensions = "n64|v64|z64|bin";
    info->need_fullpath = false;
    info->block_extract = false;
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info)
{
    memset(info, 0, sizeof(*info));
    info->geometry.base_width = screen_width;
    info->geometry.base_height = screen_height;
    info->geometry.max_width = 1920;
    info->geometry.max_height = 1440;
    info->geometry.aspect_ratio = 4.0f / 3.0f;
    info->timing.fps = 60.0;
    info->timing.sample_rate = 44100.0;
}

RETRO_API void retro_init(void)
{
    struct retro_log_callback log;

    memset(&log, 0, sizeof(log));
    if (environ_cb && environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
        log_cb = log.log;

    retro_thread = co_active();
}

RETRO_API void retro_deinit(void)
{
    if (game_loaded)
        cleanup_game();
    retro_thread = NULL;
}

RETRO_API bool retro_load_game(const struct retro_game_info *game)
{
    m64p_error result;

    if (game == NULL || game->data == NULL || game->size == 0)
        return false;

    cleanup_game();

    if (!request_hw_context())
        return false;

    if (!find_runtime())
        return false;

    select_plugin_paths();

    result = m64p_modular_core_load(&core, core_path);
    if (result != M64ERR_SUCCESS)
    {
        lr_log(RETRO_LOG_ERROR, "core load failed: %s\n", m64p_modular_last_error());
        goto fail;
    }

    lr_log(RETRO_LOG_INFO, "core: %s\n", core.name ? core.name : core_path);

    result = core.startup(MODULAR_FRONTEND_API_VERSION,
                          runtime_dir, runtime_dir,
                          NULL, m64p_debug,
                          NULL, state_callback);
    if (result != M64ERR_SUCCESS)
    {
        lr_log(RETRO_LOG_ERROR, "CoreStartup failed: %d\n", result);
        goto fail;
    }
    core_started = true;

    result = core.override_vidext(&vidext);
    if (result != M64ERR_SUCCESS)
    {
        lr_log(RETRO_LOG_ERROR, "CoreOverrideVidExt failed: %d\n", result);
        goto fail;
    }

    result = core.do_command(M64CMD_ROM_OPEN, (int) game->size, (void *) game->data);
    if (result != M64ERR_SUCCESS)
    {
        lr_log(RETRO_LOG_ERROR, "M64CMD_ROM_OPEN failed: %d\n", result);
        goto fail;
    }
    rom_open = true;

    if (!load_plugin_required(&gfx, M64PLUGIN_GFX, "GFX", gfx_path))
        goto fail;
    if (!load_plugin_required(&audio, M64PLUGIN_AUDIO, "AUDIO", audio_path))
        goto fail;
    if (!load_plugin_required(&input, M64PLUGIN_INPUT, "INPUT", input_path))
        goto fail;
    if (!load_plugin_required(&rsp, M64PLUGIN_RSP, "RSP", rsp_path))
        goto fail;

    game_thread = co_create(GAME_THREAD_STACK_SIZE, game_thread_main);
    if (game_thread == NULL)
    {
        lr_log(RETRO_LOG_ERROR, "failed to create emulation coroutine\n");
        goto fail;
    }

    game_loaded = true;
    emu_finished = false;
    frame_ready = false;

    lr_log(RETRO_LOG_INFO, "runtime: %s\n", runtime_dir);
    lr_log(RETRO_LOG_INFO, "waiting for RetroArch OpenGL context\n");
    return true;

fail:
    cleanup_game();
    return false;
}

RETRO_API void retro_unload_game(void)
{
    cleanup_game();
}

RETRO_API void retro_run(void)
{
    if (input_poll_cb)
        input_poll_cb();

    if (!game_loaded || emu_finished)
    {
        if (video_cb)
            video_cb(NULL, screen_width, screen_height, 0);
        return;
    }

    if (!context_ready)
    {
        if (video_cb)
            video_cb(NULL, screen_width, screen_height, 0);
        return;
    }

    /*
     * RetroArch needed its own FBO bound while video_cb() presented
     * the previous frame.  Glide64mk2 expects the normal/default
     * framebuffer again when emulation resumes.
     */
    resolve_present_functions();
    if (gl_bind_framebuffer != NULL)
        gl_bind_framebuffer(MOD_GL_FRAMEBUFFER, 0);

    if (gl_draw_buffer != NULL)
        gl_draw_buffer(MOD_GL_BACK);

    if (gl_read_buffer != NULL)
        gl_read_buffer(MOD_GL_BACK);

    /*
     * RetroArch changes the GL viewport while presenting the HW frame.
     * Glide64mk2 expects its original 640x480 viewport when it resumes.
     */
    if (gl_viewport != NULL)
        gl_viewport(0, 0, (int) screen_width, (int) screen_height);

    if (gl_scissor != NULL)
        gl_scissor(0, 0, (int) screen_width, (int) screen_height);

    if (gl_enable != NULL)
    {
        gl_enable(MOD_GL_SCISSOR_TEST);
        gl_enable(MOD_GL_BLEND);
    }

    if (gl_disable != NULL)
    {
        gl_disable(MOD_GL_DEPTH_TEST);
        gl_disable(MOD_GL_CULL_FACE);
        gl_disable(MOD_GL_STENCIL_TEST);
    }

    /*
     * RetroArch has finished presenting. Restore exactly the GL
     * program/texture state Glide64mk2 had when it yielded.
     */
    if (saved_gl_handoff &&
        gl_use_program != NULL &&
        gl_active_texture != NULL &&
        gl_bind_texture != NULL)
    {
        /*
         * Restore texture unit 0 first.
         */
        gl_active_texture(MOD_GL_TEXTURE0);
        gl_bind_texture(MOD_GL_TEXTURE_2D,
                        (unsigned int) saved_gl_texture0_2d);

        /*
         * Restore the texture unit Glide64mk2 actually had active.
         */
        gl_active_texture((unsigned int) saved_gl_active_texture);
        gl_bind_texture(MOD_GL_TEXTURE_2D,
                        (unsigned int) saved_gl_texture_2d);

        gl_use_program((unsigned int) saved_gl_program);

        saved_gl_handoff = false;
    }

    frame_ready = false;
    co_switch(game_thread);

    if (video_cb)
    {
        if (frame_ready)
            video_cb(RETRO_HW_FRAME_BUFFER_VALID, screen_width, screen_height, 0);
        else
            video_cb(NULL, screen_width, screen_height, 0);
    }
}

RETRO_API void retro_reset(void)
{
    if (game_loaded && core.do_command)
        core.do_command(M64CMD_RESET, 0, NULL);
}

RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device)
{
    (void) port;
    (void) device;
}

RETRO_API size_t retro_serialize_size(void)
{
    return 0;
}

RETRO_API bool retro_serialize(void *data, size_t size)
{
    (void) data;
    (void) size;
    return false;
}

RETRO_API bool retro_unserialize(const void *data, size_t size)
{
    (void) data;
    (void) size;
    return false;
}

RETRO_API void retro_cheat_reset(void)
{
}

RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
    (void) index;
    (void) enabled;
    (void) code;
}

RETRO_API bool retro_load_game_special(unsigned game_type,
                                        const struct retro_game_info *info,
                                        size_t num_info)
{
    (void) game_type;
    (void) info;
    (void) num_info;
    return false;
}

RETRO_API unsigned retro_get_region(void)
{
    return RETRO_REGION_NTSC;
}

RETRO_API void *retro_get_memory_data(unsigned id)
{
    (void) id;
    return NULL;
}

RETRO_API size_t retro_get_memory_size(unsigned id)
{
    (void) id;
    return 0;
}
