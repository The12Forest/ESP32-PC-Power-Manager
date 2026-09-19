#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <uri/UriBraces.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>

//======================
//  XIAO ESP32-C3 Pins
//======================
#define button_1 3
#define pc_pled_high 8
#define pc_ps_1 20

//======================
//  WiFi Credentials
//======================
const char* ssid = "WIFI_NAME";
const char* password = "WIFI_PASSWORD";

//======================
//  Home Assistant Webhooks
//======================
const char* ha_webhook_single = "http://10.10.3.2/api/webhook/monitoron";
const char* ha_webhook_double = "http://10.10.3.2/api/webhook/monitoron";
const char* ha_webhook_api    = "http://10.10.3.2/api/webhook/monitoron";

//======================
//  Click detection tuning
//======================
const unsigned long debounceDelay     = 30;
const unsigned long doubleClickWindow = 350;

WebServer server(80);

//======================
//  Click state
//======================
bool lastButtonState = HIGH;   // idle = HIGH (INPUT_PULLUP)
unsigned long lastEdgeTime = 0;
int clickCount = 0;
unsigned long lastClickTime = 0;

//======================
//  HA webhook helper
//======================
void callWebhook(const char* url) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected - skipping webhook call");
    return;
  }

  HTTPClient http;
  http.setTimeout(3000);
  http.begin(url);
  int httpCode = http.POST("");

  if (httpCode > 0) {
    Serial.printf("Webhook call -> %s [HTTP %d]\n", url, httpCode);
  } else {
    Serial.printf("Webhook call FAILED -> %s (%s)\n", url, http.errorToString(httpCode).c_str());
  }
  http.end();
}

//======================
//  API Handlers
//======================
void handleRoot() {
  String text = "PC Control API - XIAO ESP32-C3\n";
  text += "==================================\n\n";
  text += "Endpoints:\n\n";
  text += "GET /api/reboot\n";
  text += "  - Reboots the ESP32\n";
  text += "GET /api/status\n";
  text += "  - Returns the current state of the PC Power (JSON).\n";
  text += "GET /api/power\n";
  text += "  - Simulates pressing the PC power button for 200ms (JSON).\n";
  text += "GET /api/power/<ms>\n";
  text += "  - Simulates pressing the PC power button for <ms> milliseconds (JSON).\n";
  text += "GET /api/force_shutdown\n";
  text += "  - Simulates holding the power button for 5 seconds to hard-kill the PC (JSON).\n";
  text += "GET /api/button\n";
  text += "  - Triggers the 'API button press' Home Assistant webhook (JSON).\n";

  server.send(200, "text/plain", text);
}

void handleReboot() {
  server.send(200, "text/plain", "XIAO ESP32 C3 Reboots now...");
  ESP.restart();
}

void handlePower() {
  digitalWrite(pc_ps_1, HIGH);
  delay(200);
  digitalWrite(pc_ps_1, LOW);

  server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Power switch triggered (200ms press)\"}");
}

void handlePowerCustom() {
  String msArg = server.pathArg(0);
  long ms = msArg.toInt();

  if (ms <= 0) {
    server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid duration, must be a positive integer (ms)\"}");
    return;
  }

  digitalWrite(pc_ps_1, HIGH);
  delay(ms);
  digitalWrite(pc_ps_1, LOW);

  String json = "{\"status\":\"success\",\"message\":\"Power switch triggered (" + String(ms) + "ms press)\"}";
  server.send(200, "application/json", json);
}

void handleForceShutdown() {
  digitalWrite(pc_ps_1, HIGH);
  delay(5000);
  digitalWrite(pc_ps_1, LOW);

  server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Power switch triggered (5 second hard-kill)\"}");
}

void handleButtonApi() {
  Serial.println("API button trigger -> HA webhook (api)");
  callWebhook(ha_webhook_api);

  server.send(200, "application/json", "{\"status\":\"success\",\"message\":\"Button press simulated via API, HA webhook triggered\"}");
}

void handleStatus() {
  String json = "{\"power_state\":\"";
  if (digitalRead(pc_pled_high) == HIGH) {
    json += "ON";
  } else {
    json += "OFF";
  }
  json += "\"}";

  server.send(200, "application/json", json);
}

//======================
//  Main Setup
//======================
void setup() {
  Serial.begin(115200);

  pinMode(button_1, INPUT_PULLUP);

  pinMode(pc_ps_1, OUTPUT);
  digitalWrite(pc_ps_1, LOW);

  pinMode(pc_pled_high, INPUT);

  Serial.println();
  Serial.println("******************************************************");
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  int i = 0;
  while (WiFi.status() != WL_CONNECTED) {
    if (i == 60) {
      ESP.restart();
    }
    delay(500);
    Serial.print(".");
    i++;
  }

  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  ArduinoOTA.setHostname("ESP32-XIAO-C3-PC-Controller");

  ArduinoOTA.onStart([]() {
    Serial.println("OTA Update Starting...");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA Update Finished!");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
  });
  ArduinoOTA.begin();
  Serial.println("OTA Ready.");

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/reboot", HTTP_GET, handleReboot);
  server.on("/api/power", HTTP_GET, handlePower);
  server.on(UriBraces("/api/power/{}"), HTTP_GET, handlePowerCustom);
  server.on("/api/force_shutdown", HTTP_GET, handleForceShutdown);
  server.on("/api/button", HTTP_GET, handleButtonApi);
  server.on("/api/status", HTTP_GET, handleStatus);

  server.begin();
  Serial.println("HTTP server started");
}

//======================
//  Button handling
//======================
void handleButton() {
  bool reading = digitalRead(button_1);
  unsigned long now = millis();

  if (reading != lastButtonState) {
    lastEdgeTime = now;
  }

  //if ((now - lastEdgeTime) > debounceDelay && reading != lastButtonState) {
  //}

  static bool stableState = HIGH;
  if ((now - lastEdgeTime) > debounceDelay) {
    if (reading != stableState) {
      stableState = reading;

      if (stableState == LOW) {
        digitalWrite(pc_ps_1, HIGH);
        while (digitalRead(button_1) == LOW) {
      Serial.println("Button Pressed.");
          delay(1);
        }
        digitalWrite(pc_ps_1, LOW);

        clickCount++;
        lastClickTime = millis();
      }
    }
  }

  lastButtonState = reading;

  if (clickCount > 0 && (millis() - lastClickTime) > doubleClickWindow) {
    if (clickCount == 1) {
      Serial.println("Single click -> HA webhook (single)");
      callWebhook(ha_webhook_single);
    } else {
      Serial.println("Double click -> HA webhook (double)");
      callWebhook(ha_webhook_double);
    }
    clickCount = 0;
  }
}

//======================
//  Main Loop
//======================
void loop() {
  delay(10);

  ArduinoOTA.handle();
  server.handleClient();

  handleButton();
}