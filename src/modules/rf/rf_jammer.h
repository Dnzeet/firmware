#ifndef __RF_JAMMER_H__
#define __RF_JAMMER_H__

class RFJammer {
public:
    RFJammer(bool full = false);
    ~RFJammer();

    void setup();
    bool returnToMenu = false;  // FIX #5: exposed so caller can check exit state

private:
    int nTransmitterPin;
    bool sendRF    = true;
    bool fullJammer = false;

    void display_banner();
    void run_full_jammer();
    void run_itmt_jammer();
    void send_optimized_pulse(uint32_t width);   // FIX #2: corrected param type
    void send_random_pattern(int numPulses);
    void ensure_tx_pin_output();                 // FIX #5: helper to guarantee pin direction
};

#endif
