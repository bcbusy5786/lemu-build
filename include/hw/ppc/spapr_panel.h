/*
 * IBM pSeries operator panel: power light and a display of 2 lines of
 * 16 characters
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_PPC_SPAPR_PANEL_H
#define HW_PPC_SPAPR_PANEL_H

#include "hw/ppc/spapr_vio.h"

#define TYPE_SPAPR_PANEL "spapr-panel"

#define SPAPR_PANEL_ROWS 2
#define SPAPR_PANEL_COLS 16

/* the RTAS call display-character */
void spapr_panel_display_character(DeviceState *dev, uint8_t c);

/* console output, which may hold the firmware's checkpoint codes */
void spapr_panel_vty_write(DeviceState *dev, SpaprVioDevice *sdev,
                           uint8_t *buf, int len);

#endif
