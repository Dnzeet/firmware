#ifndef __ESP_CHAT_SHARING_H__
#define __ESP_CHAT_SHARING_H__
#if !defined(LITE_VERSION)
#include "esp_connection.h"

// Chat over ESP-NOW, built on top of the shared EspConnection infrastructure
// (same base class used by FileSharing). Target selection is manual, not
// the ping/pong auto-discovery FileSharing uses: the user either picks
// "Broadcast" (everyone in range gets every message) or types in the
// destination device's MAC address by hand (shown on that device's own
// chat screen). Manual entry sidesteps the ping/pong discovery window,
// which depends on both devices opening chat within the same ~500ms and
// is unreliable in practice.
class ChatSharing : public EspConnection {
public:
    ChatSharing();

    // Entry point, call as ChatSharing().run();
    void run();

private:
    static constexpr uint8_t HISTORY_SIZE = 20; // fixed-size ring buffer, no heap growth over time
    static constexpr uint8_t MAX_LINES    = 8;  // visible lines, trimmed to screen height at render time

    struct ChatLine {
        String   text;
        uint32_t timestampMs = 0;
        bool     self        = false;
    };

    ChatLine history[HISTORY_SIZE];
    uint8_t  historyHead  = 0; // next write slot
    uint8_t  historyCount = 0;
    bool     screenDirty  = true;
    String   chatTitle    = "ESP-NOW Chat"; // set once the target (broadcast/MAC) is picked

    void pushLine(const String &text, bool self);
    void processIncoming();
    void sendChatMessage(const String &text);
    void render();

    // Locks WiFi to a single fixed channel so both sides are guaranteed to
    // match, disconnecting any leftover STA connection first (ESP-IDF
    // refuses a manual channel change while actually associated to an AP).
    // Returns false (and shows an error) if the channel could not be set.
    bool lockWifiChannel();

    // Shows own MAC + a Broadcast/Direct picker, then either sets dstAddress
    // to broadcastAddress or asks for a 12-hex-digit MAC via keyboard(),
    // parses it, and registers it as an ESP-NOW peer. Returns false if the
    // user cancels or types an invalid MAC.
    bool chooseTarget();
};

#endif
#endif
