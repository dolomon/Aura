// web_server.h - Web Server for Theme Configuration
// Version: 1.3.0
// Forked from Surrey-Homeware/Aura

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <WebServer.h>
#include <WiFi.h>
#include <LittleFS.h>
#include "theme_config.h"

WebServer server(80);
ThemeManager* theme_mgr = nullptr;
bool web_server_running = false;

void handleRoot() {
  File file = LittleFS.open("/index.html", "r");
  if (!file) {
    server.send(404, "text/plain", "File not found");
    return;
  }
  server.streamFile(file, "text/html");
  file.close();
}

void handleCurrent() {
  if (!theme_mgr) {
    server.send(500, "application/json", "{\"error\":\"Theme manager not initialized\"}");
    return;
  }
  
  ThemeColors theme = theme_mgr->get();
  char json[650];
  snprintf(json, sizeof(json),
    "{\"bg_top\":\"%06X\",\"bg_bottom\":\"%06X\",\"text_primary\":\"%06X\","
    "\"text_secondary\":\"%06X\",\"text_tertiary\":\"%06X\",\"text_low\":\"%06X\","
    "\"text_clock\":\"%06X\",\"box_bg\":\"%06X\"}",
    theme.bg_top, theme.bg_bottom, theme.text_primary,
    theme.text_secondary, theme.text_tertiary, theme.text_low,
    theme.text_clock, theme.box_bg
  );
  server.send(200, "application/json", json);
}

void handleSave() {
  if (!theme_mgr) {
    server.send(500, "application/json", "{\"success\":false}");
    return;
  }
  
  String body = server.arg("plain");
  
  // Parse JSON manually (lightweight)
  ThemeColors newTheme = theme_mgr->get();
  
  int idx = body.indexOf("\"bg_top\":\"");
  if (idx >= 0) newTheme.bg_top = strtoul(body.substring(idx + 10, idx + 16).c_str(), NULL, 16);
  
  idx = body.indexOf("\"bg_bottom\":\"");
  if (idx >= 0) newTheme.bg_bottom = strtoul(body.substring(idx + 13, idx + 19).c_str(), NULL, 16);
  
  idx = body.indexOf("\"text_primary\":\"");
  if (idx >= 0) newTheme.text_primary = strtoul(body.substring(idx + 16, idx + 22).c_str(), NULL, 16);
  
  idx = body.indexOf("\"text_secondary\":\"");
  if (idx >= 0) newTheme.text_secondary = strtoul(body.substring(idx + 18, idx + 24).c_str(), NULL, 16);
  
  idx = body.indexOf("\"text_low\":\"");
  if (idx >= 0) newTheme.text_low = strtoul(body.substring(idx + 12, idx + 18).c_str(), NULL, 16);
  
  idx = body.indexOf("\"text_clock\":\"");
  if (idx >= 0) newTheme.text_clock = strtoul(body.substring(idx + 14, idx + 20).c_str(), NULL, 16);
  
  idx = body.indexOf("\"box_bg\":\"");
  if (idx >= 0) newTheme.box_bg = strtoul(body.substring(idx + 10, idx + 16).c_str(), NULL, 16);
  
  theme_mgr->setTheme(newTheme);
  
  server.send(200, "application/json", "{\"success\":true}");
  
  // Trigger UI refresh
  delay(500);
  ESP.restart();
}

void setupWebServer(ThemeManager* tm) {
  theme_mgr = tm;
  
  // Initialize LittleFS
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
    return;
  }
  
  server.on("/", handleRoot);
  server.on("/current", handleCurrent);
  server.on("/save", HTTP_POST, handleSave);
  
  server.begin();
  web_server_running = true;
  
  Serial.println("Web server started on port 80");
  Serial.print("Access at: http://");
  Serial.println(WiFi.localIP());
}

void handleWebServer() {
  if (web_server_running) {
    server.handleClient();
  }
}

#endif
