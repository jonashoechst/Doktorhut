#include <ESP8266WiFi.h>
#include <FS.h>

// async WebServer
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>

// persistency
#include <ESP_EEPROM.h>

// display imports
#include <Wire.h>
#include <SSD1306Wire.h>

#include "config.h"

#define LDR A0
#define LED_R D8
#define LED_G D6
#define LED_B D7

// Servers
#define DNS_PORT 53
DNSServer dnsServer;
AsyncWebServer server(80);

// Display: address, sda, scl
SSD1306Wire display(0x3c, SDA, SCL);

// Data Storage
#define QUOTES_FILE_PATH "/quotes.txt"
String quotes[QUOTE_COUNT];
int latest_quote = -1;

// Timout of the current Quote
long quote_timeout;

void add_quote(String quote){
    latest_quote = (latest_quote + 1) % QUOTE_COUNT;
    quotes[latest_quote] = quote;
}

String displayed_quote; 
void display_quote(String quote) {
    displayed_quote = quote;
    display.clear();

    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.setFont(ArialMT_Plain_16);
    display.drawStringMaxWidth(0, 0, 128, quote);

    display.display();

    Serial.printf("Quote: %s\n", quote.c_str());
}

void display_quote_random() {
    String quote;
    do {
        quote = quotes[random(QUOTE_COUNT)];
    } while ((quote.length() == 0) || displayed_quote.equals(quote));
    
    display_quote(quote);
}

bool read_quotes() {
    File quotes_file = SPIFFS.open(QUOTES_FILE_PATH, "r");
    if (!quotes_file) {
        Serial.println("Quotes file couldn't be opened.");
        return false;
    }

    while (quotes_file.position() < quotes_file.size()) {
        String quote = quotes_file.readStringUntil('\n');
        if (quote.length() > 0) {
            add_quote(quote);
            Serial.printf("Imported: %s\n", quote.c_str());
        }
    }

    quotes_file.close();
    return true;
}

void write_quotes() {
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

// *** HTML Building Blocks
String html_header = String("<!DOCTYPE html>\n\
<html> \n\
<head> \n\
    <title>Lars' Doktorhut</title> \n\
        <meta http-equiv='Content-Type' content='text/html; charset=utf-8'/> \n\
        <style>html *{ font-family: Arial; }</style> \n\
        <meta name='viewport' content='width=device-width, initial-scale=1'>\n\
    </head> \n\
<body> \n\
    <a href='/'><h1>Lars' Doktorhut</h1></a>\n");

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

// Actions to be triggered by the http server
bool new_quote = false;
bool reset = false;

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

            add_quote(quote);
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
    <form action='/game' method='post' > \n\
        <p>Siehst du Farben? Nein? Wenn du sie siehst, sind sie deine Lösung...</p>\n\
        <input type='text' name='solution' placeholder='Lösung'/> \n\
        <input type='submit' value='Lösung senden' /> \n\
    </form>\n");

String html_game_failed = String("    <p>Das war leider nicht richtig, probier's doch noch einmal...</p>");
String html_game_solved = String("    <p>Wow, richtig!</p>\n\
    <div>Dieser Doktorhut wurde erstellt von: \n\
        <ul><li>Markus Sommer</li><li>Alvar Penning</li><li>Pablo Graubner</li><li>Patrick Lampe</li><Artur Sterz</li><li>Jonas Höchst</li></ul>\n\
    </div><div> \n\
        Vielen Dank für die Jahre mit dir, deine meistens hilfreichen Anmerkungen und Tipps. \
        Dafür, dass du unsere Begeisterung für die Wissenschaft wecken konntest und uns auf unserem Weg in diese Arbeitsgruppe begleitet hast.\
        <br \>Alles gute für deinen weiten Weg.</div>");

void http_game(AsyncWebServerRequest *request) {
    AsyncWebParameter *solutionParameter = request->getParam("solution", true, false);

    if (solutionParameter) {
        String solution = solutionParameter->value();
        Serial.printf("Game solution submitted: '%s', solution: '%s'", solution.c_str(), GAME_SOLUTION);
        if (solution.equals(GAME_SOLUTION)){
            request->send(200, "text/html", html_header + html_game_solved + html_footer);
            reset = true;
        } else {
            request->send(200, "text/html", html_header + html_game_failed + html_game + html_footer);
        }
    } else {
        request->send(200, "text/html", html_header + html_game + html_footer);
    }
}

void wifi_connect_sta() {
    WiFi.hostname(NAME);
    WiFi.mode(WIFI_AP_STA);
    for (WiFiNetwork &network : networks) {
        WiFi.begin(network.ssid, network.psk);
        Serial.printf("Connecting to WiFi '%s'.", network.ssid);

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

    // Init Display
    display.init();
    display.flipScreenVertically();

    // Initialise the builtin led 
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, 1);

    // Initialize LDR and RGB LED
    pinMode(LDR, INPUT);
    pinMode(LED_R, OUTPUT);
    pinMode(LED_G, OUTPUT);
    pinMode(LED_B, OUTPUT);

    server.on("/", HTTP_GET, http_index);
    server.on("/", HTTP_POST, http_index);
    server.on("/reset", HTTP_GET, http_reset);
    server.on("/reset", HTTP_POST, http_reset);
    server.on("/game", HTTP_GET, http_game);
    server.on("/game", HTTP_POST, http_game);
    server.onNotFound(http_notFound);
    server.begin();

    // Init messages
    SPIFFS.begin();

    if (read_quotes()) {
        Serial.println("Imported all quotes from the storage.");
    } else {
        add_quote("Mam Pizza? Labbricher Teig, immer zu spät...");
        add_quote("Kannste schon so machen, ist dann halt scheiße...");
        add_quote("Alte, weiße Männer!");
        add_quote("Ach was, ist doch schon fast Freitag!");
        add_quote("lars@-ibreakstuff.com");
        add_quote("Da musste ich zeigen, wie man Google bedient.");
        add_quote("Wortwitze: NieVieDia und Sellerie");
        add_quote("Secure Communi-cation in Disaster Scenarios");

        Serial.println("Set initial quotes.");
    }

    wifi_connect_sta();
    wifi_setup_ap();

    quote_timeout = millis();
}

void set_rgb_color(int col[3]) {
    analogWrite(LED_R, col[0]);
    analogWrite(LED_G, col[1]);
    analogWrite(LED_B, col[2]);
}

int game_state = -1;
long color_timeout = 0;

void loop() {
    if (reset) {
        SPIFFS.format();
        ESP.restart();
    }

    dnsServer.processNextRequest();
    if (WiFi.status() != WL_CONNECTED)
        wifi_connect_sta();

    if (new_quote) {
        display_quote(quotes[latest_quote]);
        new_quote = false;
        quote_timeout = millis() + QUOTE_DURATION_MS;

        write_quotes();
    } else if (quote_timeout <= millis()) {
        display_quote_random();
        quote_timeout = millis() + QUOTE_DURATION_MS;
    }

    int brightness = analogRead(LDR);
    if ((game_state == -1) && (brightness < BRIGHTNESS_THRES)) {
        game_state = 0;
        Serial.println("Hidden game started...");
    }

    if ((game_state > -1) && (color_timeout <= millis())) {
        if (game_state >= COLOR_COUNT) {
            game_state = -1;
            set_rgb_color(game_colors[0]);
            Serial.println("Color sequence finished.");
        } else {
            set_rgb_color(game_colors[game_state]);
            game_state++;
        }
        
        color_timeout = millis() + COLOR_DURATION_MS;
    }

    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    delay(100);
}
