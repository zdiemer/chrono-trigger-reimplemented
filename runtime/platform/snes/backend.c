#include "platform/snes/backend.h"

PfSnesCall pf_snes_call;
uint16_t pf_snes_odd_frame_word;
bool pf_snes_drain_budgets;
uint8_t pf_snes_entropy_bytes[3];
