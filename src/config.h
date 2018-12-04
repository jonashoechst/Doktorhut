// Quote configuration
#define NAME "Doktorhut"
#define RESET_PW "reset"
#define QUOTE_DURATION_MS 3000
#define QUOTE_COUNT 20

// WiFi AP Parameters
#define WIFI_AP_SSID NAME
#define WIFI_AP_PSK ""
#define WIFI_AP_IP IPAddress(10, 0, 23, 42)
#define WIFI_AP_MASK IPAddress(255, 255, 255, 0)

typedef struct WiFiNetwork {
    char* ssid;
    char* psk;
} WiFiNetwork;

// WiFi Client Networks
WiFiNetwork networks[3] = { 
    { "SSID", "PSK"}, 
};

// Minigame
#define BRIGHTNESS_THRES 100
#define COLOR_DURATION_MS 1000
#define COLOR_COUNT 8

int game_colors[COLOR_COUNT][3] = {
    {   0,    0,    0},     
    {   0,    0, 1024},     // blue
    {1024,    0,    0},     // red
    {   0, 1024,    0},     // green
    {1024,    0, 1024},     // magenta
    {1024, 1024,    0},     // yellow
    {   0, 1024, 1024},     // cyan
    {1024, 1024, 1024},     // white
};

#define GAME_SOLUTION "brgmycw"