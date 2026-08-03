#if !defined(LITE_VERSION)
#include "chat_sharing.h"
#include "core/display.h"
#include "core/mykeyboard.h"
#include <WiFi.h>
#include <esp_wifi.h>

// setupPeer() (in EspConnection) registers peers with channel=0, meaning
// "whatever channel WiFi is on right now". Without ever connecting to an
// AP, that channel is NOT deterministic - it depends on each device's WiFi
// history/NVS state, so two devices can easily end up on different
// channels and never see each other. Pinning both devices to the same
// fixed channel before starting ESP-NOW makes delivery reliable.
static constexpr uint8_t CHAT_WIFI_CHANNEL = 1;

ChatSharing::ChatSharing() {}

bool ChatSharing::lockWifiChannel() {
    // Disconnect any leftover STA connection first: esp_wifi_set_channel()
    // is rejected while actually associated to an AP, which would leave us
    // silently stuck on whatever channel that AP was using.
    WiFi.disconnect(true, true);
    delay(100);
    WiFi.mode(WIFI_STA);
    delay(100);

    esp_err_t result = esp_wifi_set_channel(CHAT_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (result != ESP_OK) {
        Serial.printf("esp_wifi_set_channel failed: %s\n", esp_err_to_name(result));
        displayError("Gagal set WiFi channel");
        delay(1500);
        return false;
    }
    return true;
}

bool ChatSharing::chooseTarget() {
    drawMainBorderWithTitle("ESP-NOW Chat");
    padprintln("");
    padprintln("MAC kamu:");
    padprintln(WiFi.macAddress());
    padprintln("");
    padprintln("(kasih tau MAC ini ke");
    padprintln(" device tujuan kalau mau");
    padprintln(" chat langsung)");
    padprintln("");
    padprintln("SEL: Broadcast (semua)");
    padprintln("NEXT: Kirim ke MAC tertentu");
    padprintln("ESC: Batal");

    while (true) {
        InputHandler();
        wakeUpScreen();

        if (check(EscPress)) return false;

        if (check(SelPress)) {
            setDstAddress(broadcastAddress);
            chatTitle = "Chat: Broadcast";
            return true;
        }

        if (check(NextPress)) {
            String macInput = keyboard("", 12, "MAC tujuan (12 hex):");
            if (macInput == "\x1B") return false; // user pressed ESC in keyboard()

            macInput.trim();
            macInput.toUpperCase();
            if (macInput.length() != 12) {
                displayError("MAC harus 12 karakter hex");
                delay(1500);
                return false;
            }

            uint8_t targetMac[6];
            for (int i = 0; i < 6; i++) {
                String byteStr = macInput.substring(i * 2, i * 2 + 2);
                char *endPtr;
                long value = strtol(byteStr.c_str(), &endPtr, 16);
                if (*endPtr != '\0') {
                    displayError("MAC tidak valid");
                    delay(1500);
                    return false;
                }
                targetMac[i] = (uint8_t)value;
            }

            if (!setupPeer(targetMac)) {
                displayError("Gagal tambah peer");
                delay(1500);
                return false;
            }

            setDstAddress(targetMac);
            chatTitle = "Chat: " + macInput;
            return true;
        }

        delay(20);
    }
}

// ---- ring buffer -----------------------------------------------------
// Fixed HISTORY_SIZE slots, oldest entry silently overwritten once full.
// Keeps memory bounded no matter how long the chat session runs, which
// matters on no-PSRAM boards (Cardputer/TTGO) where String churn is the
// usual cause of heap fragmentation crashes.
void ChatSharing::pushLine(const String &text, bool self) {
    ChatLine &line = history[historyHead];
    line.text        = text;
    line.timestampMs = millis();
    line.self        = self;

    historyHead = (historyHead + 1) % HISTORY_SIZE;
    if (historyCount < HISTORY_SIZE) historyCount++;
    screenDirty = true;
}

// Drains whatever EspConnection::onDataRecv queued up. Ping/pong control
// messages never reach recvQueue (the base class intercepts those), so
// everything here is a real chat line from another device.
void ChatSharing::processIncoming() {
    while (!recvQueue.empty()) {
        Message msg = recvQueue.front();
        recvQueue.erase(recvQueue.begin());

        if (msg.dataSize == 0) continue;

        String text;
        size_t len = min(msg.dataSize, (size_t)ESP_DATA_SIZE);
        text.reserve(len);
        for (size_t i = 0; i < len; i++) text += msg.data[i];

        pushLine(text, false);
    }
}

void ChatSharing::sendChatMessage(const String &text) {
    if (text.isEmpty()) return;

    Message message   = createMessage(text);
    // dstAddress is either broadcastAddress or the manually-entered peer's
    // MAC, whichever the user picked in chooseTarget().
    esp_err_t response = esp_now_send(dstAddress, (uint8_t *)&message, sizeof(message));

    if (response != ESP_OK) {
        Serial.printf("Chat send response: %s\n", esp_err_to_name(response));
        displayError("Gagal kirim pesan");
        delay(500);
        screenDirty = true; // repaint chat screen after the error banner
        return;
    }

    pushLine(text, true);
}

// Only redraws when something actually changed (new message in/out), so
// the screen doesn't flicker while idling in the input loop.
void ChatSharing::render() {
    if (!screenDirty) return;
    screenDirty = false;

    drawMainBorderWithTitle(chatTitle);

    uint8_t start = historyCount > MAX_LINES ? historyCount - MAX_LINES : 0;
    for (uint8_t i = start; i < historyCount; i++) {
        uint8_t idx = (historyHead + HISTORY_SIZE - historyCount + i) % HISTORY_SIZE;
        ChatLine &line = history[idx];

        uint32_t secAgo = (millis() - line.timestampMs) / 1000;
        String prefix   = line.self ? "> " : "< ";

        padprintln(prefix + "[" + String(secAgo) + "s] " + line.text);
    }

    padprintln("");
    padprintln("SEL: type msg | ESC: exit");
}

void ChatSharing::run() {
    drawMainBorderWithTitle("ESP-NOW Chat");
    padprintln("");
    padprintln("Setting up radio...");

    if (!lockWifiChannel()) return;

    // beginEspnow() is inherited (protected) from EspConnection: starts
    // esp_now, registers the broadcast peer, installs send/recv callbacks.
    // Its destructor (~EspConnection) tears the radio down again
    // automatically when this object goes out of scope, so cleanup is
    // guaranteed even on early return/ESC.
    if (!beginEspnow()) return;

    if (!chooseTarget()) return;

    historyHead  = 0;
    historyCount = 0;
    screenDirty  = true;

    bool exitChat = false;
    while (!exitChat) {
        InputHandler();
        wakeUpScreen();

        processIncoming();
        render();

        if (check(EscPress)) {
            exitChat = true;
            continue;
        }

        if (check(SelPress)) {
            String msg = keyboard("", ESP_DATA_SIZE - 1, "Type your message:");
            if (msg != "\x1B" && msg.length() > 0) { // ESC from keyboard() returns "\x1B"
                sendChatMessage(msg);
            }
            screenDirty = true; // keyboard drew over the chat screen, force repaint
        }

        delay(20); // keep the loop responsive without pegging the CPU
    }
}
#endif
