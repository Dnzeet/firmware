#ifndef __NRF_COMMON_H
#define __NRF_COMMON_H
#include <RF24.h>
#include <globals.h>

// Define the Macros case it hasn't been declared.
// FIX(Bug#5): These -1 defaults are intentional — pins are passed dynamically
// via begin(NRFSPI, ce_pin, cs_pin) in nrf_start() using bruceConfigPins at runtime.
// The RF24 object is constructed with -1 here to avoid static init order issues
// (bruceConfigPins not yet populated at global constructor time).
// DO NOT manually set CE pin state before begin() is called — the library
// initializes CE correctly during begin(). Pre-begin CE manipulation causes
// hardware/library CE state desync → no RF output (module stays cold).
#ifndef NRF24_CE_PIN
#define NRF24_CE_PIN -1
#endif
#ifndef NRF24_SS_PIN
#define NRF24_SS_PIN -1
#endif

enum NRF24_MODE {
    NRF_MODE_DISABLED, // 0b00
    NRF_MODE_SPI,      // 0b01
    NRF_MODE_UART,     // 0b10
    NRF_MODE_BOTH      // 0b11
};
#define CHECK_NRF_SPI(mode) (mode & NRF_MODE_SPI)
#define CHECK_NRF_UART(mode) (mode & NRF_MODE_UART)
#define CHECK_NRF_BOTH(mode) (mode == NRF_MODE_BOTH)

extern RF24 NRFradio;
extern HardwareSerial NRFSerial; // Uses UART2 for External NRF's

NRF24_MODE nrf_setMode();

bool nrf_start(NRF24_MODE mode);

void nrf_info();
#endif
