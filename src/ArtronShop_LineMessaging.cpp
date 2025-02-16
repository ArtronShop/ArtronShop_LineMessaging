#include "ArtronShop_LineMessaging.h"
#include "LINE_Messaging_CA.h"
#ifdef ARDUINO_UNOWIFIR4
#include <WiFiSSLClient.h>
#else
#include <WiFiClientSecure.h>
#endif
#include "ArduinoJson-v7.3.0.h"

#ifdef ARDUINO_UNOWIFIR4
#define ESP_LOGI(...) ;
#define ESP_LOGE(...) ;
#endif

static const char * TAG = "LINE-Messaging";

static String urlEncode(const char *msg) ;
static String urlEncode(String msg) ;

ArtronShop_LineMessaging::ArtronShop_LineMessaging() {

}

void ArtronShop_LineMessaging::begin(String token, Client *client) {
    this->token = token;
    this->client = client;
}

void ArtronShop_LineMessaging::setToken(String token) {
    this->token = token;
}

void ArtronShop_LineMessaging::setClient(Client *client) {
    this->client = client;
}

bool ArtronShop_LineMessaging::send(String to, String massage, LINE_Messaging_Massage_Option_t *option) {
    if (massage.length() <= 0) {
        ESP_LOGE(TAG, "massage can't empty");
        return false;
    }

    bool imageUpload = false;

    // TODO: use user client for Ethernet support
    if (!this->client) {
#ifdef ARDUINO_UNOWIFIR4
        this->client = new WiFiSSLClient();
#else
        this->client = new WiFiClientSecure();
        // ((WiFiClientSecure *) this->client)->setInsecure(); 
        ((WiFiClientSecure *) this->client)->setCACert(LINE_Messaging_CA);
#endif
    }

    int ret = this->client->connect("api.line.me", 443);
    if (ret <= 0) {
        ESP_LOGE(TAG, "connect to LINE server fail code : %d", ret);
        return false;
    }

    // TODO: support multipart/form-data for upload image
    JsonDocument doc;
    doc["to"] = to;
    doc["messages"][0]["type"] = "text";
    doc["messages"][0]["text"] = massage;

    uint8_t messages_index = 1;
    String payload = "message=" + urlEncode(massage);
    if (option) {
        if (option->sticker.package_id && option->sticker.id) {
            doc["messages"][messages_index]["type"] = "sticker";
            doc["messages"][messages_index]["packageId"] = String(option->sticker.package_id);
            doc["messages"][messages_index]["stickerId"] = String(option->sticker.id);
            messages_index++;
        }
        if (option->image.url.length() > 0) {
            doc["messages"][messages_index]["type"] = "image";
            doc["messages"][messages_index]["originalContentUrl"] = String(option->image.url);
            doc["messages"][messages_index]["previewImageUrl"] = String(option->image.url);
            messages_index++;
        }
        if (option->map.lat && option->map.lng) {
            if (option->map.service == LONGDO_MAP) {
                String map_url = "https://mmmap15.longdo.com/mmmap/snippet/index.php?width=1000&height=1000";
                map_url += "&lat=" + String(option->map.lat, 9);
                map_url += "&long=" + String(option->map.lng, 9);
                map_url += "&zoom=" + String(option->map.zoom);
                map_url += "&pinmark=" + String(option->map.noMaker ? '0' : '1');
                if (option->map.option.length() > 0) {
                    map_url += "&" + option->map.option;
                }
                map_url += "&HD=1";

                ESP_LOGI(TAG, "Map image URL: %s", map_url.c_str());

                doc["messages"][messages_index]["type"] = "image";
                doc["messages"][messages_index]["originalContentUrl"] = String(map_url);
                doc["messages"][messages_index]["previewImageUrl"] = String(map_url);
                messages_index++;
            } else if (option->map.service == GOOGLE_MAP) {
                String map_url = "https://maps.googleapis.com/maps/api/staticmap";
                map_url += "?center=" + String(option->map.lat, 9) + "," + String(option->map.lng, 9);
                map_url += "&markers=color:red%7Clabel:U%7C" + String(option->map.lat, 9) + "," + String(option->map.lng, 9);
                map_url += "&zoom=" + String(option->map.zoom);
                map_url += "&size=1000x1000";
                map_url += "&format=jpg";
                map_url += "&key=" + option->map.api_key;
                if (option->map.option.length() > 0) {
                    map_url += "&" + option->map.option;
                }

                ESP_LOGI(TAG, "Map image URL: %s", map_url.c_str());

                doc["messages"][messages_index]["type"] = "image";
                doc["messages"][messages_index]["originalContentUrl"] = String(map_url);
                doc["messages"][messages_index]["previewImageUrl"] = String(map_url);
                messages_index++;
            }
        }
        imageUpload = (option->image.data.buffer) && (option->image.data.size > 0);
    }

    this->client->print("POST /v2/bot/message/push HTTP/1.1\r\n");
    this->client->print("Host: api.line.me\r\n");
    this->client->print("Authorization: Bearer " + this->token + "\r\n");
    this->client->print("User-Agent: ESP32\r\n");
    this->client->print("Content-Type: application/json\r\n");
    this->client->print("Content-Length: " + String(measureJson(doc)) + "\r\n");
    this->client->print("\r\n");
    serializeJson(doc, *this->client);
    serializeJson(doc, Serial);

    delay(20); // wait server respond

    long timeout = millis() + 30000;
    bool first_line = true;
    int state = 0;
    while(this->client->connected() && (timeout > millis())) {
        if (this->client->available()) {
            if (state == 0) { // Header
                String line = this->client->readStringUntil('\n');
                if (line.endsWith("\r")) {
                    line = line.substring(0, line.length() - 1);
                }
                ESP_LOGI(TAG, "Header: %s", line.c_str());
                if (first_line) {
                    if (sscanf(line.c_str(), "HTTP/%*f %d", &this->status_code) >= 1) {
                        first_line = false;
                    } else {
                        ESP_LOGE(TAG, "invalid first line");
                    }
                } else {
                    // Header
                    if (line.length() == 0) {
                        state = 2;
                    }
                }
            } else if (state == 2) { // Data
                String line = this->client->readStringUntil('\n');
                if (line.endsWith("\r")) {
                    line = line.substring(0, line.length() - 1);
                }
                ESP_LOGI(TAG, "Data: %s", line.c_str());
                if (line.length() == 0) {
                    break;
                }
            }
        }
        delay(10);
    }
    ESP_LOGI(TAG, "END");
    
    this->client->stop();
    delete this->client;
    this->client = NULL;

    return this->status_code == 200;
}

ArtronShop_LineMessaging LINE;

// Code from https://github.com/plageoj/urlencode
static String urlEncode(const char *msg) {
  const char *hex = "0123456789ABCDEF";
  String encodedMsg = "";

  while (*msg != '\0') {
    if (
        ('a' <= *msg && *msg <= 'z') || ('A' <= *msg && *msg <= 'Z') || ('0' <= *msg && *msg <= '9') || *msg == '-' || *msg == '_' || *msg == '.' || *msg == '~') {
      encodedMsg += *msg;
    } else {
      encodedMsg += '%';
      encodedMsg += hex[(unsigned char)*msg >> 4];
      encodedMsg += hex[*msg & 0xf];
    }
    msg++;
  }
  return encodedMsg;
}

static String urlEncode(String msg) {
  return urlEncode(msg.c_str());
}
