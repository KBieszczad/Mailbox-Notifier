#include "Arduino.h"
#include <WiFi.h>
#include <Wire.h>
#include <Preferences.h>
#include <WebServer.h>
#include "Adafruit_VL53L0X.h"
#include "ESP_Mail_Client.h"
#include "driver/gpio.h" 
#include "esp_sleep.h"
#include "esp_log.h"
#include "time.h"

extern "C" {
    int __wrap_esp_log_writev(int level, const char *tag, const char *format, va_list args) {
        return vprintf(format, args);
    }
    int __wrap_esp_log_write(int level, const char *tag, const char *format, ...) {
        va_list args;
        va_start(args, format);
        int ret = __wrap_esp_log_writev(level, tag, format, args);
        va_end(args);
        return ret;
    }
}

String config_wifi_ssid = "";
String config_wifi_pass = "";
String config_smtp_host = "smtp.gmail.com";
int    config_smtp_port = 465;
String config_auth_email = "";
String config_auth_pass = "";
String config_recipient = "";
int    config_trigger_dist = 45; 
int    config_alarm_hour = 9;
int    config_boot_delay = 10;
String config_service_ssid = "SERWIS";

#define PIN_SDA_VL   6   
#define PIN_SCL_VL   7   
#define PIN_SDA_RTC  21  
#define PIN_SCL_RTC  20  
#define PIN_HOLD     3   
#define PIN_BAT      2   

static const char* TAG = "Mailbox";
Adafruit_VL53L0X lox = Adafruit_VL53L0X();
String currentTimeStr = "0000-00-00 00:00:00";
bool rtcValid = false;
WebServer server(80); 

const char* www_username = "KB";
const char* www_password = "trudnehaslo";

void killPower(); 

uint8_t decToBcd(int val) { return ((val / 10 * 16) + (val % 10)); }
int bcdToDec(uint8_t val) { return ((val / 16 * 10) + (val % 16)); }

void readRTCTime() {
    Wire.end(); delay(10);
    Wire.setPins(PIN_SDA_RTC, PIN_SCL_RTC);
    if(!Wire.begin(PIN_SDA_RTC, PIN_SCL_RTC)) return;
    Wire.beginTransmission(0x51); Wire.write(0x02);
    if (Wire.endTransmission() == 0) {
        Wire.requestFrom(0x51, 7);
        if (Wire.available() == 7) {
            uint8_t s = bcdToDec(Wire.read() & 0x7F);
            uint8_t m = bcdToDec(Wire.read() & 0x7F);
            uint8_t h = bcdToDec(Wire.read() & 0x3F);
            uint8_t d = bcdToDec(Wire.read() & 0x3F);
            Wire.read(); 
            uint8_t mon = bcdToDec(Wire.read() & 0x1F);
            uint8_t y = bcdToDec(Wire.read() & 0xFF);
            char buf[32];
            snprintf(buf, sizeof(buf), "20%02d-%02d-%02d %02d:%02d:%02d", y, mon, d, h, m, s);
            currentTimeStr = String(buf);
            if (y > 20) rtcValid = true; else rtcValid = false;
        }
    }
    Wire.end(); 
}

void setDailyAlarm(int hour, int minute) {
    Wire.end(); delay(10);
    Wire.setPins(PIN_SDA_RTC, PIN_SCL_RTC);
    if (!Wire.begin(PIN_SDA_RTC, PIN_SCL_RTC)) return;
    Wire.beginTransmission(0x51); Wire.write(0x01); Wire.endTransmission();
    Wire.requestFrom(0x51, 1);
    uint8_t status = Wire.read();
    status &= ~0x08; status |= 0x02;  
    Wire.beginTransmission(0x51); Wire.write(0x01); Wire.write(status); Wire.endTransmission();
    Wire.beginTransmission(0x51); Wire.write(0x09);         
    Wire.write(decToBcd(minute) & 0x7F); Wire.write(decToBcd(hour) & 0x7F);    
    Wire.write(0x80); Wire.write(0x80); Wire.endTransmission();
    Wire.end();
}

void syncRTCWithNTP() {
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)) return;
    Wire.end(); delay(10);
    Wire.setPins(PIN_SDA_RTC, PIN_SCL_RTC);
    if (!Wire.begin(PIN_SDA_RTC, PIN_SCL_RTC)) return;
    Wire.beginTransmission(0x51); Wire.write(0x02); 
    Wire.write(decToBcd(timeinfo.tm_sec) & 0x7F);
    Wire.write(decToBcd(timeinfo.tm_min));
    Wire.write(decToBcd(timeinfo.tm_hour));
    Wire.write(decToBcd(timeinfo.tm_mday));
    Wire.write(decToBcd(timeinfo.tm_wday));
    Wire.write(decToBcd(timeinfo.tm_mon + 1));
    Wire.write(decToBcd((timeinfo.tm_year + 1900) % 100));
    Wire.endTransmission();
    Wire.end(); 
    rtcValid = true;
}

float readBattery() {
    pinMode(PIN_BAT, INPUT);
    uint32_t Vbatt = 0;
    for(int i = 0; i < 16; i++) Vbatt += analogReadMilliVolts(PIN_BAT); 
    return (2 * Vbatt / 16.0 / 1000.0);
}

bool loadSettings() {
    Preferences prefs;
    prefs.begin("config", true); 
    config_wifi_ssid = prefs.getString("ssid", "");
    config_wifi_pass = prefs.getString("pass", "");
    config_auth_email = prefs.getString("email", "");
    config_auth_pass = prefs.getString("epass", ""); 
    config_recipient = prefs.getString("recip", "");
    config_trigger_dist = prefs.getInt("trig", 45);
    config_alarm_hour = prefs.getInt("ah", 9);
    config_boot_delay = prefs.getInt("bdel", 10);
    config_service_ssid = prefs.getString("sssid", "SERWIS");
    prefs.end();
    if (config_wifi_ssid == "") return false; 
    return true;
}

bool connectToWiFi() {
    if (WiFi.status() == WL_CONNECTED) return true;
    WiFi.begin(config_wifi_ssid.c_str(), config_wifi_pass.c_str());
    int t = 0;
    while (WiFi.status() != WL_CONNECTED && t < 30) { 
        delay(500); t++; 
    }
    if (WiFi.status() == WL_CONNECTED) {
        configTime(3600, 0, "pool.ntp.org", "time.nist.gov");
        return true;
    }
    return false;
}

void handleSync() {
    if (!server.authenticate(www_username, www_password)) return server.requestAuthentication();
    
    Preferences prefs;
    prefs.begin("config", false);
    prefs.putBool("fsync", true);
    prefs.end();

    server.send(200, "text/html", "<h1>Restarting</h1>");
    delay(1000); killPower();
}

void handleRoot() { 
    if (!server.authenticate(www_username, www_password)) {
        return server.requestAuthentication();
    }
    
    float bat = readBattery();
    
    String html = R"rawliteral(
    <!DOCTYPE html><html><head><title>Konfiguracja Skrzynki</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
      body{font-family:Arial;padding:20px;background:#f2f2f2;max-width:600px;margin:auto;}
      h2{text-align:center;}
      .card{background:white;padding:20px;border-radius:10px;box-shadow:0 2px 5px rgba(0,0,0,0.1);}
      input{display:block;margin-bottom:15px;width:100%;padding:10px;box-sizing:border-box;border:1px solid #ccc;border-radius:5px;}
      input[type=submit], .btn{background:#4CAF50;color:white;border:none;cursor:pointer;font-weight:bold;padding:10px;width:100%;display:block;text-align:center;text-decoration:none;box-sizing:border-box;}
      input[type=submit]:hover, .btn:hover{background:#45a049;}
      .bat{background:#e7f3fe;border-left:6px solid #2196F3;padding:10px;margin-bottom:20px;}
      .sync{background:#2196F3; margin-bottom:15px;}
    </style>
    </head><body>
    <h2>Mailbox Settings</h2>
    <div class="card">
      <div class="bat">
        <strong>Battery status: </strong> )rawliteral";
        
    html += String(bat, 2) + " V";
    
    html += R"rawliteral(
      </div>
      
      <a href="/sync" class="btn sync">SYNC RTC NOW (Restart)</a>
      
      <form action="/save" method="POST">
        <label>WiFi SSID:</label><input type="text" name="ssid" placeholder="Wifi name" value=")rawliteral" + config_wifi_ssid + R"rawliteral(">
        <label>WiFi Wifi Password:</label><input type="text" name="pass" placeholder="Password" value=")rawliteral" + config_wifi_pass + R"rawliteral(">
        
        <hr>
        <label>Recipient Mail:</label><input type="text" name="recip" placeholder="recipient@gmail.com" value=")rawliteral" + config_recipient + R"rawliteral(">
        <label>Sender Mail:</label><input type="text" name="email" placeholder="sender@gmail.com" value=")rawliteral" + config_auth_email + R"rawliteral(">
        <label>Gmail verification code:</label><input type="password" name="epass" placeholder="xxxx xxxx xxxx xxxx" value=")rawliteral" + config_auth_pass + R"rawliteral(">
        
        <hr>
        <label>Mailbox size [mm]:</label><input type="number" name="trig" value=")rawliteral" + String(config_trigger_dist) + R"rawliteral(">
        <label>Daily wake up time:</label><input type="number" name="ah" value=")rawliteral" + String(config_alarm_hour) + R"rawliteral(">
        <label>Measurement delay [s]:</label><input type="number" name="bdel" value=")rawliteral" + String(config_boot_delay) + R"rawliteral(">
        <label>Service Hotspot Name:</label><input type="text" name="sssid" value=")rawliteral" + config_service_ssid + R"rawliteral(">
        
        <input type="submit" value="SAVE AND RESET">
      </form>
    </div>
    </body></html>
    )rawliteral";

    server.send(200, "text/html", html); 
}

void handleSave() {
    if (!server.authenticate(www_username, www_password)) {
        return server.requestAuthentication();
    }
    Preferences prefs;
    prefs.begin("config", false); 
    prefs.putString("ssid", server.arg("ssid"));
    prefs.putString("pass", server.arg("pass"));
    prefs.putString("email", server.arg("email"));
    prefs.putString("epass", server.arg("epass"));
    prefs.putString("recip", server.arg("recip"));
    prefs.putInt("trig", server.arg("trig").toInt());
    prefs.putInt("ah", server.arg("ah").toInt());
    prefs.putInt("bdel", server.arg("bdel").toInt());
    prefs.putString("sssid", server.arg("sssid"));
    prefs.end();
    server.send(200, "text/html", "<h1>Zapisano!</h1><p>Restart...</p>");
    delay(2000); killPower(); 
}

void startConfigAP() {
    ESP_LOGI(TAG, "ap start");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("MailboxSetup", "12345678"); 
    server.on("/", handleRoot);
    server.on("/save", handleSave);
    server.on("/sync", handleSync);
    server.begin();
    
    unsigned long startTime = millis();
    while(millis() - startTime < 300000) { 
        server.handleClient();
        delay(10); 
        gpio_set_level(GPIO_NUM_3, 1); 
    }
    killPower();
}

bool checkForServiceNetwork() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    int n = WiFi.scanNetworks();
    if (n == 0) return false;

    bool serviceFound = false;
    for (int i = 0; i < n; ++i) {
        if (WiFi.SSID(i) == config_service_ssid) { 
            serviceFound = true;
            break;
        }
        if(WiFi.SSID(i) == "SERWIS"){
            serviceFound = true;
            break;
        }
    }
    return serviceFound;
}

bool sendEmail(int dist, float bat, String timeStr, String extraMsg) {
    if (!connectToWiFi()) return false; 

    if (!rtcValid) {
        struct tm timeinfo;
        if(getLocalTime(&timeinfo, 3000)) syncRTCWithNTP(); 
    }

    SMTPSession smtp;
    Session_Config config;
    config.server.host_name = config_smtp_host;
    config.server.port = config_smtp_port;
    config.login.email = config_auth_email;
    config.login.password = config_auth_pass;
    config.time.ntp_server = "pool.ntp.org";
    
    SMTP_Message message;
    message.sender.name = "Skrzynka";
    message.sender.email = config_auth_email;
    message.subject = "Poczta! " + extraMsg;
    message.addRecipient("Ja", config_recipient);
    
    String body = "Status: " + extraMsg + "\nCzas: " + timeStr + "\nDystans: " + String(dist) + " mm\nBateria: " + String(bat, 2) + " V";
    message.text.content = body.c_str();

    if (!smtp.connect(&config)) return false;
    if (!MailClient.sendMail(&smtp, &message)) return false;
    return true;
}

void killPower() {
    Serial.flush(); 
    gpio_set_level(GPIO_NUM_3, 0); 
    delay(2000);
	esp_deep_sleep_start();
}

void run_mailbox_logic() {
    Preferences prefs_conf;
    prefs_conf.begin("config", false);
    bool forceSync = prefs_conf.getBool("fsync", false);
    
    if (forceSync) {
        if (connectToWiFi()) {
            syncRTCWithNTP();
            prefs_conf.putBool("fsync", false);
        }
    }
    prefs_conf.end();

    delay(config_boot_delay * 1000); 

    Preferences prefs_mail;
    prefs_mail.begin("mail", false);

    readRTCTime();
    float currentBat = readBattery();
    int currentDist = 8888;
    
    Wire.end(); delay(10);
    Wire.setPins(PIN_SDA_VL, PIN_SCL_VL);
    if (Wire.begin(PIN_SDA_VL, PIN_SCL_VL)) {
        if (lox.begin()) {
            VL53L0X_RangingMeasurementData_t measure;
            lox.rangingTest(&measure, false);
            if (measure.RangeStatus != 4) currentDist = measure.RangeMilliMeter;
        }
    }
    Wire.end(); 

    ESP_LOGI(TAG, "Dist: %d mm, Bat: %.2f V", currentDist, currentBat);

    if (prefs_mail.getBool("pend", false)) {
        if (connectToWiFi()) {
            int pDist = prefs_mail.getInt("d", 0);
            String pTime = prefs_mail.getString("t", "");
            if (sendEmail(pDist, currentBat, pTime, "ZALEGLY LIST")) {
                prefs_mail.putBool("pend", false);
            }
        }
    }

    if (currentBat < 3.50 && currentBat > 2.0) { 
        sendEmail(0, currentBat, currentTimeStr, "NISKI POZIOM BATERII!");
    }

    int prevDist = prefs_mail.getInt("prevDist", 8888);
    
    String subject = "";
    bool shouldSend = false;
    bool saveAsPendingIfFail = false;

    if (currentDist > config_trigger_dist) {
        if (prevDist != 8888) {
             prefs_mail.putInt("prevDist", 8888);
        }
    } else {
        int margin = 15; 
        
        if (prevDist - currentDist > margin) {
             subject = "NOWY LIST";
             shouldSend = true;
             saveAsPendingIfFail = true;
             prefs_mail.putInt("prevDist", currentDist);
        }
    }

    if (shouldSend) {
        bool sent = sendEmail(currentDist, currentBat, currentTimeStr, subject);

        if (!sent && saveAsPendingIfFail) {
            prefs_mail.putInt("d", currentDist);
            prefs_mail.putString("t", currentTimeStr);
            prefs_mail.putBool("pend", true);
        }
    }
    
    setDailyAlarm(config_alarm_hour, 0);
    killPower();
}

extern "C" void app_main() {
    initArduino();
    gpio_reset_pin(GPIO_NUM_3);
    gpio_set_direction(GPIO_NUM_3, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_3, 1);
    
    bool settingsFound = loadSettings();

    if (!settingsFound) {
        startConfigAP();
        return;
    }

    if (checkForServiceNetwork()) {
        startConfigAP();
        return;
    }

    run_mailbox_logic();
}