#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <FS.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>

AsyncWebServer server(80);
unsigned long lastPing = 0;

struct {
    String server = "";
    String id = "";
    String api_key = "";
    int intervalSeconds = 60;
} config;

String buildHTML() {
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, "
            "Arial, sans-serif; background-color: #f4f7f9; color: #333; display: flex; "
            "justify-content: center; align-items: center; min-height: 100vh; margin: 0; }";
    html += ".container { background: white; padding: 2rem; border-radius: 12px; box-shadow: 0 4px "
            "15px rgba(0,0,0,0.1); width: 100%; max-width: 400px; }";
    html += "h1 { color: #2c3e50; font-size: 1.5rem; margin-bottom: 1.5rem; text-align: center; }";
    html += ".form-group { margin-bottom: 1rem; }";
    html += "label { display: block; font-weight: 600; margin-bottom: 0.5rem; font-size: 0.9rem; "
            "color: #555; }";
    html += "input[type='text'], input[type='number'] { width: 100%; padding: 0.75rem; border: 1px "
            "solid #ddd; border-radius: 6px; box-sizing: border-box; font-size: 1rem; }";
    html += "input:focus { outline: none; border-color: #3498db; box-shadow: 0 0 0 2px "
            "rgba(52,152,219,0.2); }";
    html += "input[type='submit'] { width: 100%; background-color: #3498db; color: white; border: "
            "none; padding: 0.8rem; border-radius: 6px; font-size: 1rem; font-weight: 600; cursor: "
            "pointer; margin-top: 1rem; transition: background-color 0.2s; }";
    html += "input[type='submit']:hover { background-color: #2980b9; }";
    html += ".footer { text-align: center; margin-top: 1.5rem; font-size: 0.8rem; color: #888; }";
    html += "</style>";
    html += "<title>DeadPingNotify Config</title></head><body>";
    html += "<div class='container'>";
    html += "<h1>Configuration</h1>";
    html += "<form action='/save' method='post'>";
    html += "<div class='form-group'><label>Server URL</label><input type='text' name='server' "
            "value='"
            + config.server + "' placeholder='https://script.google.com/...'></div>";
    html += "<div class='form-group'><label>Device ID</label><input type='text' name='id' value='"
            + config.id + "' placeholder='ex: Salon'></div>";
    html
        += "<div class='form-group'><label>API Key</label><input type='text' name='api_key' value='"
           + config.api_key + "'></div>";
    html += "<div class='form-group'><label>Interval (seconds)</label><input type='number' "
            "name='interval_seconds' value='"
            + String(config.intervalSeconds) + "'></div>";
    html += "<input type='submit' value='Enregistrer la configuration'>";
    html += "</form>";
    html += "<div class='footer'>NoPingAlert &bull; ESP32 Monitor</div>";
    html += "</div></body></html>";
    return html;
}

void loadConfig() {
    if (!LittleFS.begin()) {
        Serial.println("Failed to mount LittleFS");
        return;
    }
    File file = LittleFS.open("/config.json", "r");
    if (!file) {
        Serial.println("Failed to open config file");
        return;
    }
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    if (!error) {
        config.server = doc["server"].as<String>();
        config.id = doc["id"].as<String>();
        config.api_key = doc["api_key"].as<String>();
        config.intervalSeconds = doc["interval_seconds"].as<int>();
    }
    else {
        Serial.println("Failed to parse config file");
    }
    file.close();
}

void saveConfig() {
    if (!LittleFS.begin()) {
        Serial.println("Failed to mount LittleFS");
        return;
    }
    File file = LittleFS.open("/config.json", "w");
    if (!file) {
        Serial.println("Failed to open config file");
        return;
    }
    JsonDocument doc;
    doc["server"] = config.server;
    doc["id"] = config.id;
    doc["api_key"] = config.api_key;
    doc["interval_seconds"] = config.intervalSeconds;
    serializeJson(doc, file);
    file.close();
}

void sendPing() {
    if (config.server.isEmpty())
        return;

    JsonDocument doc;
    doc["api_key"] = config.api_key;
    doc["id"] = config.id;

    String payload;
    serializeJson(doc, payload);

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;

    if (http.begin(client, config.server)) {
        http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
        http.addHeader("Content-Type", "application/json");

        const char *headerKeys[] = {"Location"};
        http.collectHeaders(headerKeys, 1);

        int httpCode = http.POST(payload);

        if (httpCode == 302) {
            String redirectUrl = http.header("Location");

            http.end();

            if (!redirectUrl.isEmpty() && http.begin(client, redirectUrl)) {
                httpCode = http.GET();
            }
        }

        if (httpCode > 0) {
            Serial.printf("Ping sent: %d\n", httpCode);
            String response = http.getString();
            Serial.println("Response: " + response);
        }
        else {
            Serial.printf("Ping failed: %s\n", http.errorToString(httpCode).c_str());
        }
        http.end();
    }
    else {
        Serial.println("Unable to connect to server");
    }
}

void setup() {
    Serial.begin(115200);

    // Wifi setup
    WiFiManager wm;
    wm.setConfigPortalTimeout(180);
    bool res = wm.autoConnect();
    if (!res) {
        Serial.println("Restarting...");
        delay(1000);
        ESP.restart();
    }

    Serial.println("Connected");

    loadConfig();
    server.on("/", AsyncWebRequestMethod::HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", buildHTML());
    });

    server.on("/save", AsyncWebRequestMethod::HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("server", true)) {
            config.server = request->getParam("server", true)->value();
        }
        if (request->hasParam("id", true)) {
            config.id = request->getParam("id", true)->value();
        }
        if (request->hasParam("api_key", true)) {
            config.api_key = request->getParam("api_key", true)->value();
        }
        if (request->hasParam("interval_seconds", true)) {
            config.intervalSeconds = request->getParam("interval_seconds", true)->value().toInt();
        }
        saveConfig();
        request->redirect("/");
    });
    server.begin();
}

void loop() {
    unsigned long now = millis();
    if (config.intervalSeconds > 0 && now - lastPing >= config.intervalSeconds * 1000UL) {
        sendPing();
        lastPing = now;
    }
}