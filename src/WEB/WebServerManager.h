#pragma once

#include <Arduino.h>
#include <WebServer.h>

class LedController;

class WebServerManager
{
public:
    WebServerManager();

    void begin(
        LedController& ledController,
        void (*onAction)(),
        void (*offAction)()
    );

    void handleClient();

private:
    WebServer server;
    LedController* led;
    void (*onAction)();
    void (*offAction)();

    String makeWebPage();
    void handleRoot();
    void handleStatus();
    void handleOn();
    void handleOff();
};
