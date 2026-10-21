/* Scenes: the Mode 7 plane under channel 0's camera.
 *
 * Channel 0's registers hold the Mode 7 plane's camera, as the scripts and the
 * scenes' tasks set them (scene.h names them): how high it is, which scales the
 * picture down; which way it is turned; where the plane is scrolled to, and
 * the point it turns about. Two scenes show a picture on the plane flat,
 * turned and scaled by one matrix: the Beyond Time ending's flight can show
 * the world's map so (beyond_time.c), and Lavos falls through space spinning
 * so (lavos_falls.c). For those a frame's task works the matrix the turn and
 * the height make out into the registers beside them, and in the vblank the
 * plane is sent: its matrix, its centre and its scroll. The Epoch's dive into
 * Lavos shows the plane in perspective instead (perspective.c builds its
 * matrix line by line), and in the vblank only the plane's scroll and centre
 * are sent. */
#include "scene/scene.h"

#include "original_effects.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

static uint16_t plane(const GameState *g, unsigned reg) { return scene_channel_register(g, SCENE_CAMERA_CHANNEL, reg); }
static void set_plane(GameState *g, unsigned reg, uint16_t v) { set_scene_channel_register(g, SCENE_CAMERA_CHANNEL, reg, v); }

// $7E:56B4, $7E:56D1
/* The matrix: the picture turned by the camera's turn and scaled by its
 * height, the cosine's part on the diagonal and the sine's across it. */
void scene_turn_the_flat_plane(GameState *g)
{
    SceneProgramPage page = scene_program_channel(SCENE_CAMERA_CHANNEL);
    uint16_t turn = plane(g, SCENE_PLANE_TURN), height = plane(g, SCENE_PLANE_HEIGHT);
    int16_t along = scene_scale_by_256ths(g, page, scene_cosine(g, turn), height);
    set_plane(g, SCENE_PLANE_A, (uint16_t)along);
    set_plane(g, SCENE_PLANE_D, (uint16_t)along);
    int16_t across = scene_scale_by_256ths(g, page, scene_sine(g, turn), height);
    set_plane(g, SCENE_PLANE_B, (uint16_t)across);
    set_plane(g, SCENE_PLANE_C, (uint16_t)-across);
}

// $7E:5654
/* In the vblank, the plane shown flat: its matrix, its centre and its scroll
 * sent. */
void scene_send_the_flat_plane(const GameState *g, Platform *pf)
{
    VidMode7 m7 = {
        .a = (int16_t)plane(g, SCENE_PLANE_A), .b = (int16_t)plane(g, SCENE_PLANE_B), .c = (int16_t)plane(g, SCENE_PLANE_C),
        .d = (int16_t)plane(g, SCENE_PLANE_D), .cx = (int16_t)plane(g, SCENE_PLANE_CENTRE_X),
        .cy = (int16_t)plane(g, SCENE_PLANE_CENTRE_Y), .hofs = (int16_t)plane(g, SCENE_PLANE_X),
        .vofs = (int16_t)plane(g, SCENE_PLANE_Y),
    };
    vid_set_mode7(pf, PF_AT(0x7E565A), &m7, VID_M7_MATRIX | VID_M7_CENTER | VID_M7_SCROLL);
}

// $7E:81C4
/* In the vblank, the plane in perspective: its scroll and its centre sent. */
void scene_send_the_plane_s_place(const GameState *g, Platform *pf)
{
    VidMode7 m7 = {
        .cx = (int16_t)plane(g, SCENE_PLANE_CENTRE_X), .cy = (int16_t)plane(g, SCENE_PLANE_CENTRE_Y),
        .hofs = (int16_t)plane(g, SCENE_PLANE_X), .vofs = (int16_t)plane(g, SCENE_PLANE_Y),
    };
    vid_set_mode7(pf, PF_AT(0x7E81CA), &m7, VID_M7_SCROLL);
    vid_set_mode7(pf, PF_AT(0x7E81DE), &m7, VID_M7_CENTER);
}

/* ---- Entries ---------------------------------------------------------------------------------- */

/* Vblank tasks (the flat plane's of Lavos's fall, which the Beyond Time
 * ending's vblank calls too; the plane's place of the Epoch's dive): each
 * moves the direct page to channel 0's, which it leaves there, and nothing
 * they leave is read. */
enum { CAMERA_PAGE = 0x0A00 };

void scene_send_the_flat_plane_entry(Snes *s)
{
    scene_send_the_flat_plane(state_of(s), m_platform(s));
    ORIGINAL_EFFECT(s->cpu.d = CAMERA_PAGE);
    m_rts(s);
}

void scene_send_the_plane_s_place_entry(Snes *s)
{
    scene_send_the_plane_s_place(state_of(s), m_platform(s));
    ORIGINAL_EFFECT(s->cpu.d = CAMERA_PAGE);
    m_rts(s);
}

/* Each is sent through the data bank, which must reach the registers. */
bool scene_send_the_flat_plane_present(Snes *s)
{
    return m_bank_reaches_io(s) && m_code_matches(s, 0x7E5654, 0x55, 0x93E05BF3D717E712ull);
}

bool scene_send_the_plane_s_place_present(Snes *s)
{
    return m_bank_reaches_io(s) && m_code_matches(s, 0x7E81C4, 0x2D, 0xF9B68B76E19640AEull);
}
