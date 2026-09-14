#include "natives.h"

#define M8 CPU_FLAG_M, CPU_FLAG_M                       /* entry needs 8-bit A */
#define M8_X16 CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M      /* 8-bit A, 16-bit X */

/* Cost is the original's time per native bus-access clock, x1000, from the
 * original/native-bus ratio shadow mode reports. */
const NativeEntry game_natives[] = {
    { 0xC00B4E, "c0_disable_display_and_interrupts", c0_disable_display_and_interrupts, M8, 4107 },
    { 0xC00B64, "c0_install_field_nmi", c0_install_field_nmi, M8_X16, 3500 },
    { 0xC00B75, "c0_install_field_irq", c0_install_field_irq, M8_X16, 3500 },
    { 0xC30002, "c3_decompress", c3_decompress, 0, 0, 3953 },
};

const int game_native_count = (int)(sizeof game_natives / sizeof game_natives[0]);
