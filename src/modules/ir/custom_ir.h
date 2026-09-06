
#pragma once

#include <Arduino.h>
#include <FS.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <SD.h>
#include <globals.h>

struct IRCode {
    IRCode(
        String protocol = "", String address = "", String command = "", String data = "", uint8_t bits = 32
    )
        : protocol(protocol), address(address), command(command), data(data), bits(bits) {}

    IRCode(IRCode *code) {
        name = String(code->name);
        type = String(code->type);
        protocol = String(code->protocol);
        address = String(code->address);
        command = String(code->command);
        frequency = code->frequency;
        bits = code->bits;
        // duty_cycle = code->duty_cycle;
        data = String(code->data);
        filepath = String(code->filepath);
    }

    String protocol = "";
    String address = "";
    String command = "";
    String data = "";
    uint8_t bits = 32;
    String name = "";
    String type = "";
    uint16_t frequency = 0;
    // float duty_cycle;
    String filepath = "";
};

// Custom IR
void sendIRCommand(IRCode *code, bool hideDefaultUI = false);
void sendRawCommand(uint16_t frequency, String rawData, bool hideDefaultUI = false);
void sendNECCommand(String address, String command, bool hideDefaultUI = false);
void sendNECextCommand(String address, String command, bool hideDefaultUI = false);
void sendRC5Command(String address, String command, bool hideDefaultUI = false);
void sendRC6Command(String address, String command, bool hideDefaultUI = false);
void sendSamsungCommand(String address, String command, bool hideDefaultUI = false);
void sendSonyCommand(String address, String command, uint8_t nbits, bool hideDefaultUI = false);
void sendKaseikyoCommand(String address, String command, bool hideDefaultUI = false);
bool sendDecodedCommand(String protocol, String value, uint8_t bits = 32, bool hideDefaultUI = false);
void otherIRcodes();
bool txIrFile(FS *fs, const String &filepath, bool hideDefaultUI = false);
bool chooseCmdIrFile(FS *fs, const String &filepath);
// Same file-parsing/picker UI as chooseCmdIrFile(), but returns the chosen
// command via outCode instead of transmitting it immediately -- used by IR
// Timed Transmit, which needs to hold onto the selected code until a delay
// countdown finishes. Return values:
//   0 = a command was picked, outCode is filled in
//   1 = user backed out to the file browser (short ESC / no selection)
//   2 = user backed out to the main menu (long-press ESC / "Main Menu")
int pickCmdIrFile(FS *fs, const String &filepath, IRCode &outCode);
// Entry point for the new "Timed Transmit" IR submenu: pick a source
// (Custom IR file / TV-B-Gone / Record New Signal), pick a delay, then
// wait and fire automatically. Registered from IRMenu.cpp.
void timedIrTransmitMenu();
