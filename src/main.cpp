#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <FS.h>

// async WebServer
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>

// persistency
#include <ESP_EEPROM.h>

// display imports
#include <LiquidCrystal_I2C.h>
#include "config.h"

// current sensor
#include <Adafruit_INA219.h>

// Servers
#define DNS_PORT 53
DNSServer dnsServer;
AsyncWebServer server(80);
#define SDAa SDA

// Actions to be triggered by the http server
bool new_quote = false;
bool reset = false;

// LiquidCrystal_I2C lcd(0x27, SDA, SCL);
#define LCD_COLS 16
#define LCD_ROWS 2
LiquidCrystal_I2C lcd(0x3f, LCD_COLS, LCD_ROWS);
char lcd_upper[LCD_COLS] = "  -- Pablo --  ";
char lcd_lower[LCD_COLS] = "               ";

// current sensor
Adafruit_INA219 ina219;

// Data Storage
#define QUOTES_FILE_PATH "/quotes.txt"
String quotes[QUOTE_COUNT];
int latest_quote = -1;

void quote_add(String quote){
    latest_quote = (latest_quote + 1) % QUOTE_COUNT;
    quotes[latest_quote] = quote;
}

bool quotes_read() {
    File quotes_file = SPIFFS.open(QUOTES_FILE_PATH, "r");
    if (!quotes_file) {
        Serial.println("Quotes file couldn't be opened.");
        return false;
    }

    while (quotes_file.position() < quotes_file.size()) {
        String quote = quotes_file.readStringUntil('\n');
        if (quote.length() > 0) {
            quote_add(quote);
            Serial.printf("Imported: %s\n", quote.c_str());
        }
    }

    quotes_file.close();
    return true;
}

void quotes_write() {
    File quotes_file = SPIFFS.open(QUOTES_FILE_PATH, "w");
    if (!quotes_file) {
        Serial.println("Quotes file couldn't be opened.");
        SPIFFS.format();
        File quotes_file = SPIFFS.open(QUOTES_FILE_PATH, "w");
    }

    if (!quotes_file) {
        Serial.println("Quotes file couldn't be opened after formatting, aborting.");
        return;
    }

    for (int i = (latest_quote+1) % QUOTE_COUNT; i != latest_quote; i = (i+1) % QUOTE_COUNT) {
        quotes_file.printf("%s\n", quotes[i].c_str());
        Serial.printf("Exported: %s\n", quotes[i].c_str());
    }
    quotes_file.printf("%s\n", quotes[latest_quote].c_str());
    Serial.printf("Exported latest: %s\n", quotes[latest_quote].c_str());

    quotes_file.close();
    Serial.println("All quotes exported to SPIFFS.");
}


String quote_displayed; 
unsigned int quote_displayed_i;
unsigned int quote_remaining_spaces = 0;

char quote_iter_next() {
    // if there are more chars to push
    if (quote_displayed_i < quote_displayed.length()) {
        char next_char = quote_displayed.charAt(quote_displayed_i);
        quote_displayed_i++;
        return next_char;
    }

    if (quote_remaining_spaces > 0) {
        quote_remaining_spaces--;
        return ' ';
    }

    // if a new quote was added
    String quote;
    if (new_quote) {
        quote = quotes[latest_quote];
        new_quote = false;
        quotes_write();

        Serial.printf("Showing new quote: %s\n", quote.c_str());
    } else {
        do {
            quote = quotes[random(QUOTE_COUNT)];
        } while ((quote.length() == 0) || quote_displayed.equals(quote));

        Serial.printf("Showing random quote: %s\n", quote.c_str());
    }

    quote_displayed = quote;
    quote_displayed_i = 0;
    quote_remaining_spaces = QUOTE_SPACE;

    return ' ';
}

unsigned int lcd_timeout;
float power_mW;

void lcd_loop() {
    if (lcd_timeout >= millis())
        return;

    float shuntvoltage = ina219.getShuntVoltage_mV();
    float busvoltage = ina219.getBusVoltage_V();
    float loadvoltage = busvoltage + (shuntvoltage / 1000);
    float current_mA = -ina219.getCurrent_mA();
    power_mW = ina219.getPower_mW();

    // "4.62V 30.2mA 123.2mW"
    snprintf(lcd_upper, LCD_COLS+1, "%3.2fV %2.0fmA %3.0fmW", loadvoltage, current_mA, power_mW);

    // get next word
    char word[QUOTE_SCROLL_STEP_MAX];
    int word_pos = -1;
    do {
        word_pos++;
        word[word_pos] = quote_iter_next();
    } while (word_pos < QUOTE_SCROLL_STEP_MAX && word[word_pos] != ' ');
    word_pos++;

    for (int i = 0; i < LCD_COLS; i++) {
        // shift already existing chars
        if (i < LCD_COLS-word_pos) {
            lcd_lower[i] = lcd_lower[i+word_pos];
        } 
        // pick new chars
        else {
            lcd_lower[i] = word[i - (LCD_COLS - word_pos)];
        }
    }

    lcd.setCursor(0, 0);
    lcd.print(lcd_upper);
    lcd.setCursor(0, 1);
    lcd.print(lcd_lower);

    lcd_timeout = millis() + QUOTE_SCROLL_DURATION_MS;
}

void lcd_loop_init() {
    lcd_timeout = millis();
}

// *** HTML Building Blocks
String html_header = String("<!DOCTYPE html>\n\
<html> \n\
<head> \n\
    <title>Pablos Doktorhut</title> \n\
        <meta http-equiv='Content-Type' content='text/html; charset=utf-8'/> \n\
        <style>html *{ font-family: Arial; }</style> \n\
        <meta name='viewport' content='width=device-width, initial-scale=1'>\n\
    </head> \n\
<body> \n\
    <a href='/'><h1>Pablos Doktorhut</h1></a>\n");

String html_form = String("\n\
    <form action='.' method='post' > \n\
        <input type='text' name='quote' placeholder='Komm, schreib etwas...'/> \n\
        <input type='submit' value='Quote hinzufügen' /> \n\
    </form>\n");

String html_footer = String("   <footer><br />\n\
        <a href='/game'>Game</a>\n\
        <a href='/reset'>Reset</a>\n\
    </footer>\n    </body>\n</html>");

String html_quotes() {
    String response = String();
    response += "   <h2>Quotes: </h2>\n   <ul>\n";
    for(int i = 0; i < QUOTE_COUNT; i++) {
        response += "       <li><i>";
        response += quotes[i];
        response += "</i></li>\n";
    }
    response += "   </ul>\n";

    return response;
}

// *** HTTP Endpoints
void http_notFound(AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
}

void http_index(AsyncWebServerRequest *request) {
    AsyncWebParameter *quoteParameter = request->getParam("quote", true, false);
    String html_added = String();
    if (quoteParameter) {
        String quote = quoteParameter->value();
        if (quote.length() > 0) {
            html_added += "    <p>Quote hinzugefügt: '<i>";
            html_added += quote;
            html_added += "</i>'</p>\n";

            quote_add(quote);
            Serial.printf("Added quote: %s\n", quote.c_str());
            new_quote = true;
        } else {
            html_added += "    <p>Leerer Quote nicht hinzugefügt... </p>\n";
        }
    }

    request->send(200, "text/html", html_header + html_added + html_form + html_quotes() + html_footer);
}

String html_reset = String("\n\
    <form action='/reset' method='post' > \n\
        <input type='password' name='password' placeholder='Password'/> \n\
        <input type='submit' value='Quotes zurücksetzen' /> \n\
    </form>\n");
String html_reset_successful = String("    <p>Reset erfolgreich.</p>");
String html_reset_failed = String("    <p>Reset fehlgeschlagen, falsches Passwort.</p>");

void http_reset(AsyncWebServerRequest *request) {
    AsyncWebParameter *passwordParameter = request->getParam("password", true, false);

    if (passwordParameter) {
        String password = passwordParameter->value();
        Serial.printf("Reset requested, password: '%s', submitted: '%s'", RESET_PW, password.c_str());
        if (password.equals(RESET_PW)){
            request->send(200, "text/html", html_header + html_reset_successful + html_footer);
            reset = true;
        } else {
            request->send(200, "text/html", html_header + html_reset_failed + html_reset + html_footer);
        }
    } else {
        request->send(200, "text/html", html_header + html_reset + html_footer);
    }
}

String html_game = String("\n\
    <p>Autsch, hier wir ganz schön viel Strom verbraucht... Du hast doch daran geforscht. Kannst du helfen?</p>\n");
String html_game_stage1 = String("    <p>Das ist aber immer noch zu viel... Da musst du wohl kreativ werden!<p>\n");

String html_game_solved = String("    <p>Wow, du hast die Energieprobleme der Welt gelöst!</p>\n\
    <div>Dieser Doktorhut wurde erstellt von: \n\
        <ul><li>Markus Sommer</li><li>Lars Baumgärtner</li><li>Roland Schwarzkopf</li><li>Patrick Lampe</li><Artur Sterz</li><li>Jonas Höchst</li></ul>\n\
    </div><div> \n\
        Vielen Dank für die gemeinsame Zeit mit dir in der Arbeitsgruppe.\n\
        Danke für die zahlreichen Vorlesungen und alles was wir von dir lernen konnten. \n\
        Danke auch für die vielen Ideen und neuen Eindrücke auf unseren Wegen in der Forschung. \n\
        <br />Alles Gute für deinen weiten Weg.</div>");

void http_game(AsyncWebServerRequest *request) {
    String html_game_measurement = String("    <p>Aktuell werden ") + power_mW + String(" mW verbraucht. </p>\n");

    if (power_mW > 350) {
        request->send(200, "text/html", html_header + html_game + html_game_measurement + html_footer); 
    } else if (power_mW > 250) {
        request->send(200, "text/html", html_header + html_game + html_game_measurement + html_game_stage1 + html_footer); 
    } else if (power_mW < 100) {
        request->send(200, "text/html", html_header + html_game_measurement + html_game_solved + html_footer); 
    }
}

void wifi_connect_sta() {
    WiFi.hostname(NAME);
    WiFi.mode(WIFI_AP_STA);
    for (WiFiNetwork &network : networks) {
        WiFi.begin(network.ssid.c_str(), network.psk.c_str());
        Serial.printf("Connecting to WiFi '%s'.", network.ssid.c_str());

        for (int i = 0; i < 10; i++) {
            if (WiFi.status() == WL_CONNECTED) break;
            delay(1000);
            Serial.print(".");
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.print(" successful!\n");
            Serial.print("IP Address: ");
            Serial.println(WiFi.localIP());
            break;
        } else {
            Serial.println(" failed.");
        }
    }
}

void wifi_setup_ap() {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(WIFI_AP_IP, WIFI_AP_IP, WIFI_AP_MASK);
    if (WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PSK)) {
        Serial.printf("WiFi AP configured: '%s'\n", WIFI_AP_SSID);
        dnsServer.start(DNS_PORT, "*", WIFI_AP_IP);
    } else {
        Serial.println("WiFi AP Configuration failed...");
    }
}

void setup() {
    // Init serial
    Serial.begin(115200);
    Serial.println();

    // Init lcd
    lcd.init();
    lcd.setBacklight(1);
    lcd_loop_init();

    // Init current sensor
    ina219.begin();
    ina219.setCalibration_16V_400mA();

    // Initialise the builtin led 
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, 1);

    server.on("/", HTTP_GET, http_index);
    server.on("/", HTTP_POST, http_index);
    server.on("/reset", HTTP_GET, http_reset);
    server.on("/reset", HTTP_POST, http_reset);
    server.on("/game", HTTP_GET, http_game);
    server.on("/game", HTTP_POST, http_game);
    server.onNotFound(http_notFound);
    server.begin();

    // Init quotes
    SPIFFS.begin();

    if (quotes_read()) {
        Serial.println("Imported all quotes from the storage.");
    } else {
        quote_add("Essen um 12?");
        quote_add("Wie kann man dir helfen? - Och, mir kann man nicht helfen.");
        quote_add("*klick* *klack* (im Vorbeigehen)");
        quote_add("Naaaaaaa?");
        quote_add("Energy-efficient Transitional Near-* Computing");
        quote_add("Thea und die Kids");
        quote_add("Meins, Deins, das sind doch buergerliche Kategorien.");

        Serial.println("Set initial quotes.");
    }

    wifi_connect_sta();
    wifi_setup_ap();
}

void loop() {
    if (reset) {
        SPIFFS.format();
        ESP.restart();
    }

    dnsServer.processNextRequest();
    if (WiFi.status() != WL_CONNECTED){
        wifi_connect_sta();
    }

    lcd_loop();

    // toggle LED
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    delay(100);
}
