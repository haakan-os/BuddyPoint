#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "config.h"

class BookSyncServer {
public:
    BookSyncServer();
    
    bool start();
    void stop();
    void handleClient();
    bool isRunning() const { return _running; }

private:
    WebServer _server;
    bool _running;
    
    void handleRoot();
    void handleStatus();
    void handleUpload();
    void handleSync();
    void handleNotFound();
};

extern BookSyncServer SyncServer;
