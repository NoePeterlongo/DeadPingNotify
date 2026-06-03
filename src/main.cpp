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
    String html = "<html><body>";
    html += "<h1>Configuration</h1>";
    html += "<form action='/save' method='post'>";
    html += "Server: <input type='text' name='server' value='" + config.server + "'><br>";
    html += "ID: <input type='text' name='id' value='" + config.id + "'><br>";
    html += "API Key: <input type='text' name='api_key' value='" + config.api_key + "'><br>";
    html += "Interval (seconds): <input type='number' name='interval_seconds' value='"
            + String(config.intervalSeconds) + "'><br>";
    html += "<input type='submit' value='Save'>";
    html += "</form></body></html>";
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

    // 1. Préparation du payload JSON
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
    bool res = wm.autoConnect();
    if (!res) {
        Serial.println("Failed to connect");
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