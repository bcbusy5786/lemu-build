/*
 * QEMU VMMouse MMIO transport
 *
 * Absolute ("seamless") mouse backdoor for machines that have no x86
 * vmport - MIPS (Jazz, RM200), Alpha (Jensen) and PowerPC (40p/PReP).
 *
 * The x86 vmmouse (hw/i386/vmmouse.c) is reached with a single `in eax, dx`
 * that passes six CPU registers in and out at once. RISC guests cannot do
 * that, so this device exposes the very same protocol through a small
 * memory-mapped register window: the guest writes the six "registers"
 * (eax..edi), strobes COMMAND, then reads the registers back. The queue,
 * the input handler and the command dispatch are identical to the x86
 * device - only the transport differs - so one guest driver design serves
 * every CPU.
 *
 * Copyright (C) 2007 Anthony Liguori <anthony@codemonkey.ws>  (x86 original)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "ui/console.h"
#include "ui/input.h"
#include "hw/core/sysbus.h"
#include "hw/input/i8042.h"
#include "hw/input/ps2.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qom/object.h"
#include "hw/input/vmmouse-mmio.h"

/* debug only - noisy */
/* #define DEBUG_VMMOUSE_MMIO */

/* --- protocol, identical to hw/i386/vmmouse.c ------------------------- */
#define VMMOUSE_READ_ID             0x45414552
#define VMMOUSE_DISABLE             0x000000f5
#define VMMOUSE_REQUEST_RELATIVE    0x4c455252
#define VMMOUSE_REQUEST_ABSOLUTE    0x53424152

#define VMMOUSE_QUEUE_SIZE          1024
#define VMMOUSE_VERSION             0x3442554a
#define VMMOUSE_RELATIVE_PACKET     0x00010000

#define VMMOUSE_LEFT_BUTTON         0x20
#define VMMOUSE_RIGHT_BUTTON        0x10
#define VMMOUSE_MIDDLE_BUTTON       0x08

#define VMMOUSE_MIN_X 0
#define VMMOUSE_MIN_Y 0
#define VMMOUSE_MAX_X 0xFFFF
#define VMMOUSE_MAX_Y 0xFFFF

/* command numbers, same values as the x86 vmport enum */
#define CMD_VMMOUSE_DATA            39
#define CMD_VMMOUSE_STATUS          40
#define CMD_VMMOUSE_COMMAND         41

OBJECT_DECLARE_SIMPLE_TYPE(VMMouseMMIOState, VMMOUSE_MMIO)

struct VMMouseMMIOState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t reg[6];                    /* eax, ebx, ecx, edx, esi, edi */

    uint32_t queue[VMMOUSE_QUEUE_SIZE];
    int32_t queue_size;
    uint16_t nb_queue;
    uint16_t status;
    uint8_t absolute;
    QemuInputHandlerState *hs;
    bool hs_is_absolute;                /* polarity the live handler was made with */
    QEMUBH *handler_bh;                 /* reconciles the handler on the main loop */
    int axis[INPUT_AXIS__MAX];
    int dz;
    bool btns[INPUT_BUTTON__MAX];

    DeviceState *i8042;                 /* link: i8042 or i8042-mmio */
    PS2MouseState *ps2mouse;            /* resolved from the link at realize */
};

static uint32_t vmmouse_get_status(VMMouseMMIOState *s)
{
    return (s->status << 16) | s->nb_queue;
}

static void vmmouse_input_event(DeviceState *dev, QemuConsole *src,
                                QemuInputEvent *evt)
{
    VMMouseMMIOState *s = VMMOUSE_MMIO(dev);

    switch (evt->type) {
    case INPUT_EVENT_KIND_BTN:
        if (evt->btn.down) {
            if (evt->btn.button == INPUT_BUTTON_WHEEL_UP) {
                s->dz--;
            } else if (evt->btn.button == INPUT_BUTTON_WHEEL_DOWN) {
                s->dz++;
            }
        }
        s->btns[evt->btn.button] = evt->btn.down;
        break;
    case INPUT_EVENT_KIND_ABS:
        if (evt->abs.axis == INPUT_AXIS_X) {
            s->axis[INPUT_AXIS_X] =
                qemu_input_scale_axis(evt->abs.value,
                                      INPUT_EVENT_ABS_MIN, INPUT_EVENT_ABS_MAX,
                                      VMMOUSE_MIN_X, VMMOUSE_MAX_X);
        } else if (evt->abs.axis == INPUT_AXIS_Y) {
            s->axis[INPUT_AXIS_Y] =
                qemu_input_scale_axis(evt->abs.value,
                                      INPUT_EVENT_ABS_MIN, INPUT_EVENT_ABS_MAX,
                                      VMMOUSE_MIN_Y, VMMOUSE_MAX_Y);
        }
        break;
    case INPUT_EVENT_KIND_REL:
        s->axis[evt->rel.axis] += evt->rel.value;
        break;
    default:
        break;
    }
}

static void vmmouse_input_sync(DeviceState *dev)
{
    VMMouseMMIOState *s = VMMOUSE_MMIO(dev);
    int buttons = 0;

    if (s->nb_queue > (VMMOUSE_QUEUE_SIZE - 4)) {
        return;
    }

    if (s->btns[INPUT_BUTTON_LEFT]) {
        buttons |= VMMOUSE_LEFT_BUTTON;
    }
    if (s->btns[INPUT_BUTTON_RIGHT]) {
        buttons |= VMMOUSE_RIGHT_BUTTON;
    }
    if (s->btns[INPUT_BUTTON_MIDDLE]) {
        buttons |= VMMOUSE_MIDDLE_BUTTON;
    }
    if (!s->absolute) {
        buttons |= VMMOUSE_RELATIVE_PACKET;
    }

    s->queue[s->nb_queue++] = buttons;
    s->queue[s->nb_queue++] = s->axis[INPUT_AXIS_X];
    s->queue[s->nb_queue++] = s->axis[INPUT_AXIS_Y];
    s->queue[s->nb_queue++] = s->dz;
    s->dz = 0;

    if (!s->absolute) {
        s->axis[INPUT_AXIS_X] = 0;
        s->axis[INPUT_AXIS_Y] = 0;
    }

    /* Still generate a PS/2 mouse event as a doorbell, so the guest's mouse
     * ISR runs and drains our queue - exactly like the x86 device. */
    if (s->ps2mouse) {
        ps2_mouse_fake_event(s->ps2mouse);
    }
}

static const QemuInputHandler vmmouse_abs_handler = {
    .name  = "vmmouse",
    .mask  = INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_ABS,
    .event = vmmouse_input_event,
    .sync  = vmmouse_input_sync,
};

static const QemuInputHandler vmmouse_rel_handler = {
    .name  = "vmmouse",
    .mask  = INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_REL,
    .event = vmmouse_input_event,
    .sync  = vmmouse_input_sync,
};

static void vmmouse_remove_handler(VMMouseMMIOState *s)
{
    g_clear_pointer(&s->hs, qemu_input_handler_unregister);
}

/*
 * Registering/activating/unregistering the input handler has to happen on the
 * main loop. Activating the handler - or dropping it while it is the active
 * one - fires the mouse-mode-change notifier, and a UI backend may answer that
 * by touching the host windowing system (SDL, for one, updates the window
 * title through Cocoa), which aborts the process if it runs off the main
 * thread. MMIO writes are serviced on the vCPU thread, so the command handlers
 * only publish the desired state (s->status, s->absolute) and schedule this
 * bottom half, which reconciles the live handler with that state.
 */
static void vmmouse_handler_bh(void *opaque)
{
    VMMouseMMIOState *s = opaque;

    if (s->status != 0) {
        /* disabled: there must be no handler */
        vmmouse_remove_handler(s);
        return;
    }
    /* enabled: ensure a handler of the right polarity is registered + active */
    if (s->hs != NULL && s->hs_is_absolute != (bool)s->absolute) {
        vmmouse_remove_handler(s);
    }
    if (s->hs == NULL) {
        const QemuInputHandler *h = s->absolute ?
            &vmmouse_abs_handler : &vmmouse_rel_handler;
        s->hs = qemu_input_handler_register(DEVICE(s), h);
        s->hs_is_absolute = s->absolute;
        qemu_input_handler_activate(s->hs);
    }
}

static void vmmouse_update_handler(VMMouseMMIOState *s, int absolute)
{
    if (s->status != 0) {
        return;
    }
    s->absolute = absolute;
    qemu_bh_schedule(s->handler_bh);
}

static void vmmouse_read_id(VMMouseMMIOState *s)
{
    if (s->nb_queue == VMMOUSE_QUEUE_SIZE) {
        return;
    }
    s->queue[s->nb_queue++] = VMMOUSE_VERSION;
    s->status = 0;
    vmmouse_update_handler(s, s->absolute);
}

static void vmmouse_request_relative(VMMouseMMIOState *s)
{
    vmmouse_update_handler(s, 0);
}

static void vmmouse_request_absolute(VMMouseMMIOState *s)
{
    vmmouse_update_handler(s, 1);
}

static void vmmouse_disable(VMMouseMMIOState *s)
{
    s->status = 0xffff;
    qemu_bh_schedule(s->handler_bh);
}

static void vmmouse_data(VMMouseMMIOState *s, uint64_t *data, uint32_t size)
{
    int i;

    if (size == 0 || size > 6 || size > s->nb_queue) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "vmmouse-mmio: driver requested too much data %u\n", size);
        vmmouse_disable(s);
        return;
    }

    for (i = 0; i < size; i++) {
        data[i] = s->queue[i];
    }

    s->nb_queue -= size;
    if (s->nb_queue) {
        memmove(s->queue, &s->queue[size],
                sizeof(s->queue[0]) * s->nb_queue);
    }
}

/* The transport-independent protocol dispatch - a copy of the switch in the
 * x86 vmmouse_ioport_read(), operating on data[0..5] = eax..edi. */
static void vmmouse_mmio_command(VMMouseMMIOState *s, uint64_t *data)
{
    uint16_t command = data[2] & 0xFFFF;

    switch (command) {
    case CMD_VMMOUSE_STATUS:
        data[0] = vmmouse_get_status(s);
        break;
    case CMD_VMMOUSE_COMMAND:
        switch (data[1]) {
        case VMMOUSE_DISABLE:
            vmmouse_disable(s);
            break;
        case VMMOUSE_READ_ID:
            vmmouse_read_id(s);
            break;
        case VMMOUSE_REQUEST_RELATIVE:
            vmmouse_request_relative(s);
            break;
        case VMMOUSE_REQUEST_ABSOLUTE:
            vmmouse_request_absolute(s);
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "vmmouse-mmio: unknown command %" PRIx64 "\n",
                          data[1]);
            break;
        }
        break;
    case CMD_VMMOUSE_DATA:
        vmmouse_data(s, data, data[1]);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "vmmouse-mmio: unknown command %x\n", command);
        break;
    }
}

static uint64_t vmmouse_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    VMMouseMMIOState *s = opaque;

    switch (addr) {
    case VMMOUSE_MMIO_REG_MAGIC:
        return VMMOUSE_MMIO_MAGIC_VAL;
    case VMMOUSE_MMIO_REG_VERSION:
        return VMMOUSE_MMIO_IFACE_VER;
    case VMMOUSE_MMIO_REG_EAX:
        return s->reg[0];
    case VMMOUSE_MMIO_REG_EBX:
        return s->reg[1];
    case VMMOUSE_MMIO_REG_ECX:
        return s->reg[2];
    case VMMOUSE_MMIO_REG_EDX:
        return s->reg[3];
    case VMMOUSE_MMIO_REG_ESI:
        return s->reg[4];
    case VMMOUSE_MMIO_REG_EDI:
        return s->reg[5];
    default:
        return 0;
    }
}

static void vmmouse_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned size)
{
    VMMouseMMIOState *s = opaque;

    switch (addr) {
    case VMMOUSE_MMIO_REG_EAX:
        s->reg[0] = val;
        break;
    case VMMOUSE_MMIO_REG_EBX:
        s->reg[1] = val;
        break;
    case VMMOUSE_MMIO_REG_ECX:
        s->reg[2] = val;
        break;
    case VMMOUSE_MMIO_REG_EDX:
        s->reg[3] = val;
        break;
    case VMMOUSE_MMIO_REG_ESI:
        s->reg[4] = val;
        break;
    case VMMOUSE_MMIO_REG_EDI:
        s->reg[5] = val;
        break;
    case VMMOUSE_MMIO_REG_COMMAND: {
        uint64_t data[6];
        int i;

        for (i = 0; i < 6; i++) {
            data[i] = s->reg[i];
        }
        vmmouse_mmio_command(s, data);
        for (i = 0; i < 6; i++) {
            s->reg[i] = (uint32_t)data[i];
        }
        break;
    }
    default:
        break;
    }
}

static const MemoryRegionOps vmmouse_mmio_ops = {
    .read = vmmouse_mmio_read,
    .write = vmmouse_mmio_write,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static int vmmouse_post_load(void *opaque, int version_id)
{
    VMMouseMMIOState *s = opaque;

    /* Reconcile the input handler with the restored state on the main loop. */
    qemu_bh_schedule(s->handler_bh);
    return 0;
}

static const VMStateDescription vmstate_vmmouse_mmio = {
    .name = "vmmouse-mmio",
    .version_id = 0,
    .minimum_version_id = 0,
    .post_load = vmmouse_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_INT32_EQUAL(queue_size, VMMouseMMIOState),
        VMSTATE_UINT32_ARRAY(queue, VMMouseMMIOState, VMMOUSE_QUEUE_SIZE),
        VMSTATE_UINT16(nb_queue, VMMouseMMIOState),
        VMSTATE_UINT16(status, VMMouseMMIOState),
        VMSTATE_UINT8(absolute, VMMouseMMIOState),
        VMSTATE_UINT32_ARRAY(reg, VMMouseMMIOState, 6),
        VMSTATE_END_OF_LIST()
    }
};

static void vmmouse_mmio_reset(DeviceState *d)
{
    VMMouseMMIOState *s = VMMOUSE_MMIO(d);

    s->queue_size = VMMOUSE_QUEUE_SIZE;
    s->nb_queue = 0;
    memset(s->reg, 0, sizeof(s->reg));

    vmmouse_disable(s);
}

static void vmmouse_mmio_realize(DeviceState *dev, Error **errp)
{
    VMMouseMMIOState *s = VMMOUSE_MMIO(dev);

    if (!s->i8042) {
        /* No explicit link: adopt the machine's single i8042 (handy when it
         * is buried in a super-I/O chip, as on the 40p). */
        bool ambiguous = false;
        Object *o = object_resolve_path_type("", TYPE_I8042, &ambiguous);

        if (o == NULL && !ambiguous) {
            o = object_resolve_path_type("", TYPE_I8042_MMIO, &ambiguous);
        }
        if (o != NULL && !ambiguous) {
            s->i8042 = DEVICE(o);
        }
    }
    if (!s->i8042) {
        error_setg(errp, "'i8042' link is not set and no i8042 was found");
        return;
    }
    if (object_dynamic_cast(OBJECT(s->i8042), TYPE_I8042)) {
        s->ps2mouse = &I8042(s->i8042)->kbd.ps2mouse;
    } else if (object_dynamic_cast(OBJECT(s->i8042), TYPE_I8042_MMIO)) {
        s->ps2mouse = &I8042_MMIO(s->i8042)->kbd.ps2mouse;
    } else {
        error_setg(errp,
                   "'i8042' link must be an i8042 or i8042-mmio device");
        return;
    }

    memory_region_init_io(&s->iomem, OBJECT(s), &vmmouse_mmio_ops, s,
                          "vmmouse-mmio", VMMOUSE_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);

    s->handler_bh = qemu_bh_new(vmmouse_handler_bh, s);
}

static void vmmouse_mmio_unrealize(DeviceState *dev)
{
    VMMouseMMIOState *s = VMMOUSE_MMIO(dev);

    vmmouse_remove_handler(s);
    if (s->handler_bh) {
        qemu_bh_delete(s->handler_bh);
        s->handler_bh = NULL;
    }
}

static const Property vmmouse_mmio_properties[] = {
    DEFINE_PROP_LINK("i8042", VMMouseMMIOState, i8042, TYPE_DEVICE,
                     DeviceState *),
};

static void vmmouse_mmio_class_initfn(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = vmmouse_mmio_realize;
    dc->unrealize = vmmouse_mmio_unrealize;
    device_class_set_legacy_reset(dc, vmmouse_mmio_reset);
    dc->vmsd = &vmstate_vmmouse_mmio;
    device_class_set_props(dc, vmmouse_mmio_properties);
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo vmmouse_mmio_info = {
    .name          = TYPE_VMMOUSE_MMIO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(VMMouseMMIOState),
    .class_init    = vmmouse_mmio_class_initfn,
};

static void vmmouse_mmio_register_types(void)
{
    type_register_static(&vmmouse_mmio_info);
}

type_init(vmmouse_mmio_register_types)
