/*
 * IBM pSeries operator panel
 *
 * The panel has a power light and a display of 2 lines of 16 characters.
 * The operating system writes to the display with the RTAS call
 * display-character. A form feed clears it, a carriage return goes back
 * to the start of the line, and a line feed goes to the next line or,
 * from the last one, scrolls the display up. AIX shows its boot progress
 * codes there.
 *
 * SLOF prints its checkpoint codes on the console: a C, four hex digits
 * and a carriage return. Until the operating system first writes to the
 * display, the panel takes these codes out of the console output and
 * shows them instead.
 *
 * The panel has a window of its own with the SDL display.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "qapi/error.h"
#include "hw/core/sysbus.h"
#include "hw/ppc/spapr_panel.h"
#include "system/system.h"
#include "ui/console.h"
#include "ui/input.h"
#include "ui/vgafont.h"
#include "trace.h"

OBJECT_DECLARE_SIMPLE_TYPE(SpaprPanelState, SPAPR_PANEL)

/* C, four hex digits and a carriage return */
#define CODE_LEN    6

/* pass on a code that stops halfway after this long */
#define CODE_WAIT_MS    50

struct SpaprPanelState {
    SysBusDevice parent_obj;

    char text[SPAPR_PANEL_ROWS][SPAPR_PANEL_COLS];
    int row, col;

    /* the firmware runs: look for its codes in the console output */
    bool firmware;
    uint8_t held[CODE_LEN];
    int nheld;
    SpaprVioDevice *held_vty;
    QEMUTimer *code_timer;

    QemuConsole *con;
    QemuInputHandlerState *input;
    Notifier machine_done;
    int pointer_x, pointer_y;
    bool redraw;
};

#define PANEL_W     440
#define PANEL_H     120

/* light: centre and radius, label to the right */
#define LIGHT_Y     60
#define LIGHT_R     7
#define POWER_X     28
#define LABEL_DX    14

/* display: each pixel of the font is a DOT by DOT dot */
#define DOT         2
#define CHAR_STEP   (FONT_WIDTH * DOT + 2)
#define ROW_STEP    (FONT_HEIGHT * DOT + 8)
#define GLASS_X     116
#define GLASS_Y     14
#define GLASS_W     308
#define GLASS_H     92
#define BEZEL       4
#define TEXT_X      (GLASS_X + 11)
#define TEXT_Y      (GLASS_Y + 10)

#define COL_CASE        0x2a2a2a
#define COL_EDGE        0x5a5a5a
#define COL_BEZEL       0x050505
#define COL_LABEL       0x808080
#define COL_POWER_ON    0x30e030
#define COL_GLASS       0x9db44a
#define COL_DOT_OFF     0x94aa45
#define COL_DOT_ON      0x1c2610

static const char *const panel_arrow[] = {
    "X", "XX", "X.X", "X..X", "X...X", "X....X", "X.....X", "X......X",
    "X.......X", "X........X", "X.....XXXXX", "X..X..X", "X.X X..X",
    "XX  X..X", "X    X..X", "     X..X", "      XX",
};

static void panel_fill(uint32_t *fb, int x, int y, int w, int h, uint32_t c)
{
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            fb[j * PANEL_W + i] = c;
        }
    }
}

static void panel_label(uint32_t *fb, int x, int y, const char *s)
{
    for (; *s; s++, x += FONT_WIDTH) {
        const uint8_t *glyph = vgafont16 +
                               (uint8_t)g_ascii_toupper(*s) * FONT_HEIGHT;

        for (int j = 0; j < FONT_HEIGHT; j++) {
            for (int i = 0; i < FONT_WIDTH; i++) {
                if (glyph[j] & (0x80 >> i)) {
                    fb[(y + j) * PANEL_W + x + i] = COL_LABEL;
                }
            }
        }
    }
}

static void panel_light(uint32_t *fb, int x, const char *label, uint32_t c)
{
    for (int dy = -LIGHT_R; dy <= LIGHT_R; dy++) {
        for (int dx = -LIGHT_R; dx <= LIGHT_R; dx++) {
            int r2 = dx * dx + dy * dy;

            if (r2 <= LIGHT_R * LIGHT_R) {
                fb[(LIGHT_Y + dy) * PANEL_W + x + dx] =
                    r2 > (LIGHT_R - 2) * (LIGHT_R - 2) ? COL_EDGE : c;
            }
        }
    }
    panel_label(fb, x + LABEL_DX, LIGHT_Y - FONT_HEIGHT / 2, label);
}

/* one character, with the unlit dots of its cell a shade darker */
static void panel_char(uint32_t *fb, int x, int y, uint8_t c)
{
    const uint8_t *glyph = vgafont16 + c * FONT_HEIGHT;

    for (int j = 0; j < FONT_HEIGHT; j++) {
        for (int i = 0; i < FONT_WIDTH; i++) {
            panel_fill(fb, x + i * DOT, y + j * DOT, DOT, DOT,
                       glyph[j] & (0x80 >> i) ? COL_DOT_ON : COL_DOT_OFF);
        }
    }
}

/*
 * The UI hides the host pointer over a window with absolute input and
 * shows it again once the pointer reaches the edge, so draw one inside.
 */
static void panel_draw_pointer(SpaprPanelState *s, uint32_t *fb)
{
    int x = s->pointer_x, y = s->pointer_y;

    if (x <= 0 || y <= 0 || x >= PANEL_W - 1 || y >= PANEL_H - 1) {
        return;
    }
    for (unsigned j = 0; j < ARRAY_SIZE(panel_arrow) && y + j < PANEL_H; j++) {
        for (unsigned i = 0; panel_arrow[j][i] && x + i < PANEL_W; i++) {
            char c = panel_arrow[j][i];

            if (c != ' ') {
                fb[(y + j) * PANEL_W + x + i] = c == 'X' ? 0 : 0xffffff;
            }
        }
    }
}

static void panel_draw(SpaprPanelState *s, uint32_t *fb)
{
    panel_fill(fb, 0, 0, PANEL_W, PANEL_H, COL_CASE);
    panel_light(fb, POWER_X, "power", COL_POWER_ON);
    panel_fill(fb, GLASS_X - BEZEL - 1, GLASS_Y - BEZEL - 1,
               GLASS_W + 2 * BEZEL + 2, GLASS_H + 2 * BEZEL + 2, COL_EDGE);
    panel_fill(fb, GLASS_X - BEZEL, GLASS_Y - BEZEL,
               GLASS_W + 2 * BEZEL, GLASS_H + 2 * BEZEL, COL_BEZEL);
    panel_fill(fb, GLASS_X, GLASS_Y, GLASS_W, GLASS_H, COL_GLASS);
    for (int r = 0; r < SPAPR_PANEL_ROWS; r++) {
        for (int c = 0; c < SPAPR_PANEL_COLS; c++) {
            panel_char(fb, TEXT_X + c * CHAR_STEP, TEXT_Y + r * ROW_STEP,
                       s->text[r][c]);
        }
    }
    panel_draw_pointer(s, fb);
}

static bool panel_update_display(void *opaque)
{
    SpaprPanelState *s = opaque;
    DisplaySurface *surface = qemu_console_surface(s->con);

    if (!s->redraw || surface_bits_per_pixel(surface) != 32) {
        return true;
    }
    panel_draw(s, (uint32_t *)surface_data(surface));
    qemu_console_update(s->con, 0, 0, PANEL_W, PANEL_H);
    s->redraw = false;
    return true;
}

static void panel_invalidate_display(void *opaque)
{
    SpaprPanelState *s = opaque;

    s->redraw = true;
}

static const GraphicHwOps panel_gfx_ops = {
    .gfx_update = panel_update_display,
    .invalidate = panel_invalidate_display,
};

static int panel_axis(int value, int size)
{
    return qemu_input_scale_axis(value, INPUT_EVENT_ABS_MIN,
                                 INPUT_EVENT_ABS_MAX, 0, size);
}

/* clicks on the panel go nowhere, not to the guest's mouse */
static void panel_input_event(DeviceState *dev, QemuConsole *src,
                              QemuInputEvent *evt)
{
    SpaprPanelState *s = SPAPR_PANEL(dev);

    if (evt->type != INPUT_EVENT_KIND_ABS) {
        return;
    }
    if (evt->abs.axis == INPUT_AXIS_X) {
        s->pointer_x = panel_axis(evt->abs.value, PANEL_W);
    } else {
        s->pointer_y = panel_axis(evt->abs.value, PANEL_H);
    }
    s->redraw = true;
}

static const QemuInputHandler panel_input = {
    .name = "pSeries operator panel",
    .mask = INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_ABS,
    .event = panel_input_event,
};

static void panel_clear(SpaprPanelState *s)
{
    memset(s->text, ' ', sizeof(s->text));
    s->row = 0;
    s->col = 0;
    s->redraw = true;
}

static void panel_putc(SpaprPanelState *s, uint8_t c)
{
    switch (c) {
    case '\f':
        panel_clear(s);
        break;
    case '\r':
        s->col = 0;
        break;
    case '\n':
        if (s->row < SPAPR_PANEL_ROWS - 1) {
            s->row++;
            break;
        }
        memmove(s->text[0], s->text[1],
                sizeof(s->text) - sizeof(s->text[0]));
        memset(s->text[SPAPR_PANEL_ROWS - 1], ' ', SPAPR_PANEL_COLS);
        s->redraw = true;
        break;
    default:
        /* the rest of a line that is too long is lost */
        if (c >= ' ' && c < 0x7f && s->col < SPAPR_PANEL_COLS) {
            s->text[s->row][s->col++] = c;
            s->redraw = true;
        }
        break;
    }
}

/* pass on what was held as the start of a code */
static void panel_pass_held(SpaprPanelState *s)
{
    timer_del(s->code_timer);
    if (s->nheld) {
        vty_putchars(s->held_vty, s->held, s->nheld);
        s->nheld = 0;
    }
}

static void panel_code_timer(void *opaque)
{
    panel_pass_held(opaque);
}

static bool panel_code_char(int pos, uint8_t c)
{
    return pos == 0 ? c == 'C' :
           pos < CODE_LEN - 1 ? g_ascii_isxdigit(c) : c == '\r';
}

/* take one byte of console output, return how many bytes to pass on */
static int panel_vty_byte(SpaprPanelState *s, uint8_t c, uint8_t *out)
{
    int n = 0;

    if (s->nheld && !panel_code_char(s->nheld, c)) {
        /* not a code after all */
        memcpy(out, s->held, s->nheld);
        n = s->nheld;
        s->nheld = 0;
    }
    if (!panel_code_char(s->nheld, c)) {
        out[n++] = c;
        return n;
    }
    s->held[s->nheld++] = c;
    if (s->nheld == CODE_LEN) {
        char code[CODE_LEN];

        memcpy(code, s->held, CODE_LEN - 1);
        code[CODE_LEN - 1] = '\0';
        trace_spapr_panel_checkpoint(code);
        panel_clear(s);
        for (int i = 0; i < CODE_LEN - 1; i++) {
            panel_putc(s, code[i]);
        }
        s->nheld = 0;
    }
    return n;
}

void spapr_panel_vty_write(DeviceState *dev, SpaprVioDevice *sdev,
                           uint8_t *buf, int len)
{
    SpaprPanelState *s = SPAPR_PANEL(dev);
    uint8_t out[32];
    int n = 0;

    if (!s->firmware) {
        vty_putchars(sdev, buf, len);
        return;
    }
    if (s->nheld && s->held_vty != sdev) {
        panel_pass_held(s);
    }
    s->held_vty = sdev;
    for (int i = 0; i < len; i++) {
        if (n > (int)sizeof(out) - CODE_LEN) {
            vty_putchars(sdev, out, n);
            n = 0;
        }
        n += panel_vty_byte(s, buf[i], out + n);
    }
    if (n) {
        vty_putchars(sdev, out, n);
    }
    if (s->nheld) {
        timer_mod(s->code_timer,
                  qemu_clock_get_ms(QEMU_CLOCK_REALTIME) + CODE_WAIT_MS);
    } else {
        timer_del(s->code_timer);
    }
}

void spapr_panel_display_character(DeviceState *dev, uint8_t c)
{
    SpaprPanelState *s = SPAPR_PANEL(dev);

    trace_spapr_panel_display_character(c);
    if (s->firmware) {
        /* the operating system has taken over */
        panel_pass_held(s);
        s->firmware = false;
    }
    panel_putc(s, c);
}

/* The window follows the graphics adapter, which stays the main display */
static void panel_machine_done(Notifier *notifier, void *data)
{
    SpaprPanelState *s = container_of(notifier, SpaprPanelState,
                                      machine_done);
    DeviceState *dev = DEVICE(s);

    s->con = qemu_graphic_console_create(dev, 0, &panel_gfx_ops, s);
    qemu_console_resize(s->con, PANEL_W, PANEL_H);
    s->input = qemu_input_handler_register(dev, &panel_input);
    qemu_input_handler_bind(s->input, dev->id, 0, &error_abort);
}

static void panel_reset(DeviceState *dev)
{
    SpaprPanelState *s = SPAPR_PANEL(dev);

    panel_pass_held(s);
    panel_clear(s);
    s->firmware = true;
}

static void panel_realize(DeviceState *dev, Error **errp)
{
    SpaprPanelState *s = SPAPR_PANEL(dev);

    panel_clear(s);
    s->firmware = true;
    s->code_timer = timer_new_ms(QEMU_CLOCK_REALTIME, panel_code_timer, s);
    s->machine_done.notify = panel_machine_done;
    qemu_add_machine_init_done_notifier(&s->machine_done);
}

static void panel_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "IBM pSeries operator panel";
    dc->realize = panel_realize;
    device_class_set_legacy_reset(dc, panel_reset);
    /* the machine hands it the display and the console output */
    dc->user_creatable = false;
}

static const TypeInfo panel_info = {
    .name = TYPE_SPAPR_PANEL,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SpaprPanelState),
    .class_init = panel_class_init,
};

static void panel_register_types(void)
{
    type_register_static(&panel_info);
}

type_init(panel_register_types)
