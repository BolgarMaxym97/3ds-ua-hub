/*
 * 3DS UA Hub: installs, updates and removes Ukrainian translations of games from one app on
 * the HOME Menu. Plan: IDEA.md; design: https://claude.ai/artifact/FE7cw2U38XgMqKk3mu7ftn.
 */
#include <3ds.h>
#include <stdlib.h>
#include <citro2d.h>
#include <citro3d.h>

#include "app.h"
#include "image.h"
#include "net.h"
#include "ui.h"

/* Linear memory for the screens; the rest of the 64 MB region stays heap, for downloads. */
u32 __ctru_linear_heap_size = 8 * 1024 * 1024;

/* The screens are drawn at twice their size and the transfer to the display averages each 2x2
 * block: citro2d has no antialiasing, and on a console (Azahar renders upscaled and hides it) the
 * focus ring, the thin strokes of the icons and text shrunk from the 24 px font came out ragged
 * and blurred. */
#define SCREEN_TRANSFER                                                                                    \
    (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |                       \
     GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |         \
     GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_XY))

static C3D_RenderTarget *screen_target(gfxScreen_t screen, u16 width)
{
    C3D_RenderTarget *target = C3D_RenderTargetCreate(240 * 2, width * 2, GPU_RB_RGBA8, GPU_RB_DEPTH16);
    if (target)
        C3D_RenderTargetSetOutput(target, screen, GFX_LEFT, SCREEN_TRANSFER);
    return target;
}

/* Drawing in screen pixels on the double-size target. */
static void scene_begin(C3D_RenderTarget *target, u16 width)
{
    C2D_SceneBegin(target);
    C2D_SceneSize(240, width, true);
}

int main(void)
{
    romfsInit();
    cfguInit();
    ptmuInit();
    amInit();
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS * 4);
    C2D_Prepare();
    C3D_RenderTarget *top = screen_target(GFX_TOP, TOP_W);
    C3D_RenderTarget *bottom = screen_target(GFX_BOTTOM, BOTTOM_W);

    static App app;
    net_init();
    image_init();
    bool ready = top && bottom && ui_init();
    app_init(&app);

    /* A tap is the stylus lifted close to where it came down; further is a drag (a list scrolls). */
    bool touching = false, dragged = false;
    touchPosition first = {0, 0}, last = {0, 0};
    while (ready && aptMainLoop() && !app.quit) {
        hidScanInput();
        Input in = {.down = hidKeysDown(), .held = hidKeysHeld()};
        if (hidKeysHeld() & KEY_TOUCH) {
            touchPosition now;
            hidTouchRead(&now);
            if (!touching) {
                first = last = now;
                dragged = false;
            }
            if (abs(now.py - first.py) > 6 || abs(now.px - first.px) > 6)
                dragged = true;
            in.touching = true;
            in.touch_x = now.px;
            in.touch_y = now.py;
            in.drag_dy = touching ? now.py - last.py : 0;
            last = now;
            touching = true;
        } else if (touching) {
            touching = false;
            in.tapped = !dragged;
            in.tap_x = last.px;
            in.tap_y = last.py;
        }
        app_update(&app, &in);

        ui_frame_begin();
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(top, COL_BG);
        scene_begin(top, TOP_W);
        app_draw_top(&app);
        C2D_TargetClear(bottom, COL_BG);
        scene_begin(bottom, BOTTOM_W);
        app_draw_bottom(&app);
        C3D_FrameEnd(0);
    }

    app_exit(&app);
    image_exit();
    ui_exit();
    net_exit();
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    amExit();
    ptmuExit();
    cfguExit();
    romfsExit();
    return 0;
}
