#ifndef __ESP_CHAT_SHARING_H__
#define __ESP_CHAT_SHARING_H__
#if !defined(LITE_VERSION)
#include "esp_connection.h"

// Chat over ESP-NOW, built on top of the shared EspConnection infrastructure
// (same base class used by FileSharing). On start it reuses the existing
// ping/pong peer picker (EspConnection::beginSend) so the user can choose
// either "Broadcast" (everyone in range gets every message) or a specific
// discovered device (messages go only to that one peer). No new pairing
// code needed - it's the exact same discovery flow FileSharing uses.
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
    String   chatTitle    = "ESP-NOW Chat"; // set once the target (broadcast/peer) is picked

    void pushLine(const String &text, bool self);
    void processIncoming();
    void sendChatMessage(const String &text);
    void render();
};

#endif
#endif
