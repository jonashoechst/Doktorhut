// Quote configuration
#define NAME "Doktorhut"
#define RESET_PW "reset"
#define QUOTE_SCROLL_DURATION_MS 1000
#define QUOTE_SCROLL_STEP_MAX 10
#define QUOTE_SPACE 3
#define QUOTE_COUNT 20

// WiFi AP Parameters
#define WIFI_AP_SSID NAME
#define WIFI_AP_PSK ""
#define WIFI_AP_IP IPAddress(10, 0, 23, 42)
#define WIFI_AP_MASK IPAddress(255, 255, 255, 0)

typedef struct WiFiNetwork {
    String ssid;
    String psk;
} WiFiNetwork;

// WiFi Client Networks
WiFiNetwork networks[3] = { 
    { "SSID", "PSK"},
};
