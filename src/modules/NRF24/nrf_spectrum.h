#ifndef __NRF_SPECTRUM_H
#define __NRF_SPECTRUM_H
#include "modules/NRF24/nrf_common.h"
#include <RF24.h>

// nRF24 covers channels 0-125 (2.400-2.525 GHz), 126 channels total
#define NRF_SPECTRUM_CHANNELS 126

void nrf_spectrum();

String scanChannels(bool web = false);

#endif
