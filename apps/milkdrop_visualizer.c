/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Trimpod: Milkdrop-style music visualizer.
 *
 * Wraps libprojectM (vendored in lib/projectm) and renders it fullscreen into
 * Trimpod's OpenGL ES window (see firmware/target/hosted/sdl/window-sdl.c).
 * Audio is tapped from the SDL PCM output (pcm-sdl.c: pcm_sdl_viz_latest) so the
 * visuals react to the beat. Presets are .milk files shipped in
 * ROCKBOX_DIR/presets and auto-transition over time (projectM fires a
 * "switch requested" callback when a preset's duration elapses or on a beat).
 *
 * Entered from the Now Playing menu (A -> "Start Visualizer"); Back (B) returns.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/
#include "config.h"
#include "milkdrop_visualizer.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <SDL.h>
#include <GLES2/gl2.h>
#include <projectM-4/projectM.h>

#include "file.h"
#include "dir.h"
#include "pathfuncs.h"          /* path_append (preset basename -> full path) */
#include "string-extra.h"       /* strlcpy */
#include "strnatcmp.h"          /* natural alphabetical sort of the preset list */
#include "action.h"
#include "lcd.h"
#include "backlight.h"         /* backlight_set_timeout: suspend Auto Screen Off while visualizing */
#include "powermgmt.h"         /* reset_poweroff_timer: restart idle timer on exit */
#include "kernel.h"
#include "tick.h"
#include "thread.h"
#include "button.h"
#include "settings.h"
#include "lang.h"
#include "splash.h"
#include "misc.h"
#include "icon.h"
#include "root_menu.h"
#include "gui/list.h"
#include "window-sdl.h"
#include <SDL_thread.h>          /* render loop runs on its own OS thread (core) */

/* The SDL window owning the shared GL context (defined in window-sdl.c).  Stock
 * window-sdl.h does not export it, so declare it here as button-sdl.c does. */
extern SDL_Window *sdlWindow;

/* Audio tap implemented in firmware/target/hosted/sdl/pcm-sdl.c */
extern unsigned pcm_sdl_viz_latest(int16_t *out, unsigned max_frames);

/* projectM renders at 1/4 screen resolution into an offscreen FBO, which is then
 * nearest-neighbour upscaled to fill the window.  Our libprojectM is patched to
 * composite into the caller's bound framebuffer, so render_frame() lands in our
 * FBO. */
#define VIZ_DIVISOR     4
static GLuint viz_fbo, viz_tex, viz_prog, viz_vbo;
static GLint  viz_u_fade;     /* u_fade uniform location (fade-to-black) */
static int    viz_w, viz_h;   /* low-res render size */

static const char *viz_vs_src =
    "attribute vec2 a_pos;\n"
    "attribute vec2 a_uv;\n"
    "varying vec2 v_uv;\n"
    "void main(){ v_uv = a_uv; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
static const char *viz_fs_src =
    "precision mediump float;\n"
    "varying vec2 v_uv;\n"
    "uniform sampler2D u_tex;\n"
    "uniform float u_fade;\n"   /* 1.0 = full brightness, 0.0 = black */
    "void main(){ gl_FragColor = vec4(texture2D(u_tex, v_uv).rgb * u_fade, 1.0); }\n";

static GLuint viz_compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    return s;
}

/* Build the upscale shader/quad and the low-res render target (viz_w x viz_h).
 * UVs are in GL orientation (v=0 at bottom) so the FBO texture draws upright. */
static bool viz_gl_init(void)
{
    int win_w, win_h;
    static const GLfloat verts[] = {
        -1.f, -1.f, 0.f, 0.f,
         1.f, -1.f, 1.f, 0.f,
        -1.f,  1.f, 0.f, 1.f,
         1.f,  1.f, 1.f, 1.f,
    };

    SDL_GL_GetDrawableSize(sdlWindow, &win_w, &win_h);
    viz_w = win_w / VIZ_DIVISOR;
    viz_h = win_h / VIZ_DIVISOR;

    viz_prog = glCreateProgram();
    glAttachShader(viz_prog, viz_compile(GL_VERTEX_SHADER, viz_vs_src));
    glAttachShader(viz_prog, viz_compile(GL_FRAGMENT_SHADER, viz_fs_src));
    glBindAttribLocation(viz_prog, 0, "a_pos");
    glBindAttribLocation(viz_prog, 1, "a_uv");
    glLinkProgram(viz_prog);
    viz_u_fade = glGetUniformLocation(viz_prog, "u_fade");

    glGenBuffers(1, &viz_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, viz_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

    glGenTextures(1, &viz_tex);
    glBindTexture(GL_TEXTURE_2D, viz_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, viz_w, viz_h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &viz_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, viz_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, viz_tex, 0);
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return ok;
}

/* Draw the low-res viz texture to fill the window (nearest-neighbour upscale). */
static void viz_gl_present(float fade)
{
    int win_w, win_h;
    SDL_GL_GetDrawableSize(sdlWindow, &win_w, &win_h);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, win_w, win_h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(viz_prog);
    glUniform1f(viz_u_fade, fade);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, viz_tex);
    glBindBuffer(GL_ARRAY_BUFFER, viz_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat),
                          (void *)(2 * sizeof(GLfloat)));
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

#define PRESET_DIR      ROCKBOX_DIR "/presets"
/* Per-preset on/off state persists here: one DISABLED basename per line (absence
 * = enabled, so a missing file means "all on").  Both the Visualizers toggle
 * list and the Now Playing auto-cycle read/write it. */
#define VIZ_STATE_FILE  ROCKBOX_DIR "/visualizers.txt"
#define MAX_PRESETS     256                  /* the shipped curated set (~100) */
#define NAMEBUF_SZ      (MAX_PRESETS * 256)  /* packed basenames incl ".milk" */
#define PCM_CHUNK       512                  /* stereo frames fed per frame */

static projectm_handle pm;                   /* created lazily, kept alive */
static char  name_buf[NAMEBUF_SZ];           /* packed preset basenames */
static int   name_off[MAX_PRESETS];          /* offset of each basename */
static bool  preset_enabled[MAX_PRESETS];    /* toggled on the Visualizers screen */
static int   preset_count;
static int   preset_cur = -1;

/* Fade-to-black transition state. Instead of projectM's soft-cut (which renders
 * two presets at once and spikes the CPU), we fade the current preset to black
 * over VIZ_FADE_TICKS, hard-load the next preset while black, then fade it in. */
enum viz_fade_state { VIZ_NORMAL, VIZ_FADE_OUT, VIZ_FADE_IN };
#define VIZ_FADE_TICKS  (HZ / 2)             /* 0.5s per fade phase */
static enum viz_fade_state viz_fade;
static bool viz_entry_faded;   /* set by the entry fade-to-black -> fade IN on start */
static long  viz_fade_start;                 /* current_tick when this phase began */
static int   viz_pending_preset = -1;        /* preset to load at black (-1 = random) */
static volatile bool viz_switch_req;         /* set by projectM's switch callback */
static long  viz_preset_tick;                /* current_tick when the shown preset began */

static bool ends_with_milk(const char *name)
{
    size_t n = strlen(name);
    return n > 5 && strcasecmp(name + n - 5, ".milk") == 0;
}

static int name_off_cmp(const void *a, const void *b)
{
    return strnatcasecmp(name_buf + *(const int *)a, name_buf + *(const int *)b);
}

/* Apply the saved on/off state (a list of DISABLED basenames; absence = on, so
 * a missing/empty file means everything is enabled).  Call after scan + sort. */
static char viz_state_buf[NAMEBUF_SZ];
static void load_enabled_state(void)
{
    for (int i = 0; i < preset_count; i++)
        preset_enabled[i] = true;            /* default: all on */

    int fd = open(VIZ_STATE_FILE, O_RDONLY);
    if (fd < 0)
        return;
    int len = read(fd, viz_state_buf, sizeof(viz_state_buf) - 1);
    close(fd);
    if (len <= 0)
        return;
    viz_state_buf[len] = '\0';

    char *line = viz_state_buf, *nl;
    while (line && *line)
    {
        nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        size_t L = strlen(line);
        if (L && line[L - 1] == '\r') line[--L] = '\0';   /* tolerate CRLF */
        if (*line)
            for (int i = 0; i < preset_count; i++)
                if (strcmp(name_buf + name_off[i], line) == 0)
                {
                    preset_enabled[i] = false;
                    break;
                }
        line = nl ? nl + 1 : NULL;
    }
}

/* Kept for Task 6's stock-Rockbox toggle list (its only caller, the trimpod-UI
 * menu, was stubbed out); marked unused so it doesn't trip -Werror meanwhile. */
static void save_enabled_state(void) __attribute__((unused));
static void save_enabled_state(void)
{
    int fd = open(VIZ_STATE_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return;
    for (int i = 0; i < preset_count; i++)
        if (!preset_enabled[i])
        {
            const char *nm = name_buf + name_off[i];
            write(fd, nm, strlen(nm));
            write(fd, "\n", 1);
        }
    close(fd);
}

/* Scan the flat preset dir (the shipped curated set) into name_buf, sort it,
 * then apply the saved on/off toggles.  Re-run on every entry so toggle changes
 * take effect immediately (in the list and in the Now Playing auto-cycle). */
static void scan_presets(void)
{
    preset_count = 0;
    preset_cur = -1;
    int used = 0;

    DIR *d = opendir(PRESET_DIR);
    if (d)
    {
        struct DIRENT *e;
        while ((e = readdir(d)) != NULL && preset_count < MAX_PRESETS)
        {
            if (e->d_name[0] == '.' || !ends_with_milk(e->d_name))
                continue;
            int len = strlen(e->d_name) + 1;
            if (used + len > (int)sizeof(name_buf))
                break;
            name_off[preset_count++] = used;
            memcpy(name_buf + used, e->d_name, len);
            used += len;
        }
        closedir(d);
    }
    qsort(name_off, preset_count, sizeof name_off[0], name_off_cmp);
    load_enabled_state();
}

/* Load preset by index, smoothly blending unless a hard (beat) cut was asked. */
static void load_preset(int idx, bool hard_cut)
{
    if (idx < 0 || idx >= preset_count)
        return;
    preset_cur = idx;
    char path[MAX_PATH];
    path_append(path, PRESET_DIR, name_buf + name_off[idx], sizeof(path));
    projectm_load_preset_file(pm, path, !hard_cut);
}

static int enabled_count(void)
{
    int c = 0;
    for (int i = 0; i < preset_count; i++)
        if (preset_enabled[i]) c++;
    return c;
}

/* Pick a random preset from the enabled pool (whatever's toggled on).  Falls
 * back to the whole set if nothing is enabled, so X is never a black screen.
 * Avoids repeating the current preset when there's more than one to choose. */
static void next_preset(bool hard_cut)
{
    if (preset_count <= 0)
        return;
    bool use_enabled = enabled_count() > 0;
    int pool = use_enabled ? enabled_count() : preset_count;
    int target = rand() % pool;

    int idx = -1, seen = 0;
    for (int i = 0; i < preset_count; i++)
        if (!use_enabled || preset_enabled[i])
        {
            if (seen == target) { idx = i; break; }
            seen++;
        }
    if (idx < 0)
        return;
    if (idx == preset_cur && pool > 1)        /* advance to the next in-pool one */
        for (int j = 1; j < preset_count; j++)
        {
            int c = (idx + j) % preset_count;
            if (!use_enabled || preset_enabled[c]) { idx = c; break; }
        }
    load_preset(idx, hard_cut);
}

/* Begin a fade-to-black transition to preset idx (idx < 0 = random next).
 * Ignored if a transition is already running. */
static void viz_request_switch(int idx)
{
    if (viz_fade != VIZ_NORMAL)
        return;
    viz_pending_preset = idx;
    viz_fade = VIZ_FADE_OUT;
    viz_fade_start = current_tick;
}

/* projectM asks for the next preset when the current one's duration elapses or
 * a strong beat triggers a cut. We defer to the main loop, which fades to
 * black, hard-loads, and fades in (rather than projectM's two-preset blend). */
static void preset_switch_cb(bool is_hard_cut, void *user_data)
{
    (void)user_data;
    (void)is_hard_cut;
    viz_switch_req = true;
}

/* Apply the "Visualization Transition" setting: seconds each preset shows before
 * auto-switching.  0 = "Never" -> hold the preset (no time- OR beat-triggered
 * switch) until the user manually changes it (A) or restarts the visualizer. */
static void apply_viz_transition(void)
{
    /* We cycle presets ourselves on a wall-clock timer (see the session loop),
     * so projectM must never auto-switch on its own.  Its preset clock counts
     * wall time across the whole app -- including the gap while the visualizer
     * is closed -- with no API to reset it, so letting it drive the switch made
     * a long interval fire the instant the visualizer re-opened. */
    projectm_set_preset_duration(pm, 9999999.0);   /* never -- we drive switching */
    projectm_set_hard_cut_enabled(pm, false);
}

static bool ensure_init(void)
{
    if (pm)
        return true;

    sdl_gl_make_current();

    if (!viz_gl_init())
        return false;

    pm = projectm_create();
    if (!pm)
        return false;

    /* Render at 1/4 resolution; the FBO texture is upscaled nearest-neighbour. */
    projectm_set_window_size(pm, viz_w, viz_h);
    projectm_set_mesh_size(pm, 48, 36);
    projectm_set_fps(pm, 60);
    projectm_set_aspect_correction(pm, true);
    /* We run our own fade-to-black on switch, so disable projectM's soft-cut
     * blend (it renders two presets at once and spikes the CPU). */
    projectm_set_soft_cut_duration(pm, 0.0);
    projectm_set_hard_cut_duration(pm, 8.0);
    projectm_set_hard_cut_sensitivity(pm, 1.0f);
    projectm_set_beat_sensitivity(pm, 1.2f);
    apply_viz_transition();   /* preset_duration + hard-cut on/off (0 = Never) */
    projectm_set_preset_switch_requested_event_callback(pm, preset_switch_cb, NULL);

    srand((unsigned)current_tick);
    return true;
}

/* Run the visualizer fullscreen until Back (B). If locked_path != NULL the
 * visualizer stays on that one preset (a path chosen from the browser);
 * otherwise presets auto-cycle from the pool and A jumps to another one. */
/* The projectM render loop runs on its own OS thread so it lands on a free core,
 * in parallel with audio decode/buffering/input -- those stay on the Rockbox
 * cooperative scheduler on the main thread.  The render thread owns the GL
 * context for the whole session (the main thread releases it before the thread
 * starts and reclaims it after join). */
static volatile bool viz_thread_stop;
static bool          viz_locked;        /* preset pinned -> no auto-cycle */

static int viz_render_thread(void *param)
{
    (void)param;
    int16_t pcm[PCM_CHUNK * 2];

    SDL_GL_MakeCurrent(sdlWindow, sdl_gl_get_context());
    SDL_GL_SetSwapInterval(1);          /* vsync paces the loop -- no busy spin */

    while (!viz_thread_stop)
    {
        unsigned frames = pcm_sdl_viz_latest(pcm, PCM_CHUNK);
        if (frames > 0)
            projectm_pcm_add_int16(pm, pcm, frames, PROJECTM_STEREO);

        /* A pending projectM switch request kicks off a fade-to-black. */
        if (viz_switch_req)
        {
            viz_switch_req = false;
            viz_request_switch(-1);
        }

        /* Time-based preset cycling on a wall-clock that only runs while the
         * visualizer is on screen: switch after the Visualization Transition
         * interval of real seconds.  0 = "Never" holds the preset. */
        /* replaced by global_settings.viz_transition in Task 7 */
        int transition_s = 20;
        if (!viz_locked && transition_s > 0 &&
            viz_fade == VIZ_NORMAL &&
            current_tick - viz_preset_tick >= (long)transition_s * HZ)
            viz_request_switch(-1);

        /* Advance the fade transition (timed off current_tick so the 0.5s phases
         * are smooth regardless of frame rate).  At full black, hard-load the
         * pending preset, then fade the new one back in. */
        float fade = 1.f;
        if (viz_fade == VIZ_FADE_OUT)
        {
            long dt = current_tick - viz_fade_start;
            if (dt >= VIZ_FADE_TICKS)
            {
                if (viz_pending_preset >= 0)
                    load_preset(viz_pending_preset, true);
                else
                    next_preset(true);
                viz_pending_preset = -1;
                viz_preset_tick = current_tick;   /* restart the cycle clock */
                viz_fade = VIZ_FADE_IN;
                viz_fade_start = current_tick;
                fade = 0.f;
            }
            else
                fade = 1.f - (float)dt / (float)VIZ_FADE_TICKS;
        }
        else if (viz_fade == VIZ_FADE_IN)
        {
            long dt = current_tick - viz_fade_start;
            if (dt >= VIZ_FADE_TICKS)
                viz_fade = VIZ_NORMAL;
            else
                fade = (float)dt / (float)VIZ_FADE_TICKS;
        }

        /* Render projectM into the low-res FBO (libprojectM is patched to
         * composite into the bound draw framebuffer), then upscale to window. */
        glBindFramebuffer(GL_FRAMEBUFFER, viz_fbo);
        glViewport(0, 0, viz_w, viz_h);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        projectm_opengl_render_frame(pm);

        viz_gl_present(fade);           /* nearest-neighbour upscale + fade */
        SDL_GL_SwapWindow(sdlWindow);   /* blocks on vsync -> paces the thread */
    }

    SDL_GL_MakeCurrent(sdlWindow, NULL);   /* release so the main thread can reclaim */
    return 0;
}

static void visualizer_session(const char *locked_path)
{
    viz_locked = locked_path != NULL;

    if (!ensure_init())
    {
        trimpod_viz_active = false;   /* the entry fade may have set it */
        viz_entry_faded = false;
        lcd_update();                 /* hand the window back, restore the screen */
        return;
    }

    /* Set projectM up and load the first preset on the main thread (it holds the
     * GL context here), then hand the context to the render thread. */
    button_clear_queue();
    sdl_gl_make_current();
    trimpod_viz_active = true;

    /* The visualizer owns the screen for as long as it runs -- that is active
     * use, not idle.  Suspend Auto Screen Off for the session (restored on exit). */
    backlight_set_timeout(0);

    apply_viz_transition();   /* pick up the current "Visualization Transition" setting */

    projectm_set_preset_locked(pm, viz_locked);
    if (viz_locked)
    {
        projectm_load_preset_file(pm, locked_path, false);   /* hard cut */
    }
    else
    {
        scan_presets();                  /* refresh the enabled pool (toggles) */
        if (preset_count > 0)
            next_preset(true);
    }

    /* Entered via the fade-to-black (auto-start) -> fade the first preset IN. */
    if (viz_entry_faded)
    {
        viz_fade = VIZ_FADE_IN;
        viz_fade_start = current_tick;
        viz_entry_faded = false;
    }
    else
        viz_fade = VIZ_NORMAL;
    viz_switch_req = false;
    viz_preset_tick = current_tick;   /* start the cycle clock from on-screen now */

    /* Hand the GL context to the render thread (its own core); the main thread
     * keeps the Rockbox scheduler (codec/buffering) running and watches for the
     * exit button.  get_action() yields to the Rockbox threads and blocks briefly
     * between polls so the main thread doesn't busy-spin; the volume rocker is
     * handled globally (ACTION_NONE here) so it adjusts volume without exiting. */
    SDL_GL_MakeCurrent(sdlWindow, NULL);
    viz_thread_stop = false;
    SDL_Thread *rt = SDL_CreateThread(viz_render_thread, "trimpod_viz", NULL);
    if (rt)
    {
        while (get_action(CONTEXT_STD, HZ/20) == ACTION_NONE)
            ;
        viz_thread_stop = true;
        SDL_WaitThread(rt, NULL);
    }
    sdl_gl_make_current();   /* reclaim the context the render thread released */

    projectm_set_preset_locked(pm, false);
    trimpod_viz_active = false;
    backlight_set_timeout(global_settings.backlight_timeout);  /* restore Auto Screen Off */
    button_clear_queue();
    /* The user was just interacting (they pressed B to exit); stamp activity so
     * idle timers restart instead of immediately re-firing.  Trimpod called its
     * own button_touch_activity() (for a WPS auto-start timer absent in stock
     * Rockbox); the stock idle-timer equivalent is reset_poweroff_timer(). */
    reset_poweroff_timer();

    /* Repaint the screen we came from (its LCD framebuffer is intact). */
    lcd_update();
}

void milkdrop_visualizer_run(void)
{
    visualizer_session(NULL);   /* Now Playing menu entry: auto-cycling presets */
}

/* Fade the current (Now Playing) screen to black over one fade phase, reusing the
 * same fade technique as the visualizer's preset transitions (applied here to the
 * LCD image).  A keypress DURING the fade snaps the screen back and returns true
 * (cancel -- don't launch).  On completion returns false and arms the fade-IN so
 * the visualizer (loaded while black) fades in from black. */
bool milkdrop_visualizer_fade_to_black(void)
{
    sdl_gl_make_current();
    SDL_GL_SetSwapInterval(1);   /* vsync-pace the fade so it's smooth (else the
                                  * first fade, before viz_gl_init, runs un-paced) */
    /* Own the window for the fade + load so the normal LCD presenter can't repaint
     * Now Playing at full brightness over us (gwps_leave_wps does exactly that). */
    trimpod_viz_active = true;

    long start = current_tick;
    for (;;)
    {
        long dt = current_tick - start;
        float fade = (dt >= VIZ_FADE_TICKS) ? 0.f
                                            : 1.f - (float)dt / (float)VIZ_FADE_TICKS;
        sdl_gl_present_lcd_fade(fade);

        int act = get_action(CONTEXT_STD, TIMEOUT_NOBLOCK);
        if (act != ACTION_NONE && act != ACTION_REDRAW)   /* real button -> cancel */
        {
            sdl_gl_present_lcd_fade(1.0f);   /* snap Now Playing back to full */
            trimpod_viz_active = false;      /* hand the window back to the LCD */
            return true;                     /* cancelled */
        }
        if (dt >= VIZ_FADE_TICKS)
            break;
        yield();
    }
    viz_entry_faded = true;   /* visualizer_session keeps trimpod_viz_active set,
                               * then fades IN from black */
    return false;
}

/* ---- Settings -> Visualizers: the on/off toggle list -----------------------
 * The interactive list UI lived here in the trimpod fork, built on trimpod's
 * bespoke page framework (trimpod_page / trimpod_ui).  Those deps don't exist in
 * stock Rockbox, so the list is stubbed for now; Task 6 reimplements it with the
 * stock Rockbox UI.  The preset-management helpers it relied on (scan_presets,
 * load/save_enabled_state, the name_buf/name_off/preset_enabled statics) are kept
 * above for that reimplementation -- some are temporarily unreferenced here. */

int milkdrop_visualizer_menu(void) { return 0; }   /* reimplemented in Task 6 */
