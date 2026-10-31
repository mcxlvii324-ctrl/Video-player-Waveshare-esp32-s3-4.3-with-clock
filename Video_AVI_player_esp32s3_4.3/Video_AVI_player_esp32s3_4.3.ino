#include <Arduino.h>
#include <vector> 
#include <WiFi.h>  
#include "time.h"  

typedef struct {
    uint16_t bitmapOffset;
    uint8_t  width;
    uint8_t  height;
    uint8_t  xAdvance;
    int8_t   xOffset;
    int8_t   yOffset;
} GFXglyph;

typedef struct {
    uint8_t  *bitmap;
    GFXglyph *glyph;
    uint16_t  first;
    uint16_t  last;
    uint8_t   yAdvance;
} GFXfont;

#include <esp_display_panel.hpp>
#include "esp_lcd_panel_rgb.h"
#include <FS.h>
#include <SD_MMC.h>
#include "FreeSansBold12pt7b.h"

const char* ssid = "your SSID name here";
const char* password = "your wifi password here";
const char* ntpServer = "pool.ntp.org";
const char* time_zone = "EET-2EEST,M3.5.0/3,M10.5.0/4"; //here put your timezome for your city,country from here :"https://gist.github.com/tivaliy/a5ef9c7ccb4d57bdc248f0855c5eb1ff"

size_t output_buf_size;
uint16_t *output_buf = nullptr;

// Ορίζουμε το ψεύτικο GFX στα 480x272 για να έχει πάντα αρκετό estimate buffer η AviFunc
class MockArduinoGFX {
public:
    int width() { return 480; }
    int height() { return 272; }
    void draw16bitBeRGBBitmap(int x, int y, uint16_t *buf, int w, int h) {}
};
MockArduinoGFX static_gfx;
MockArduinoGFX *gfx = &static_gfx; 

#include "AviFunc.h"

const char *AVI_FOLDER = "/avi";
#define MAX_FILES 10
String aviFileList[MAX_FILES];
int fileCount = 0;
int selectedIndex = 0;
unsigned long lastClockUpdateMs = 0; 

#define RGB565_BLACK       0x0000
#define RGB565_WHITE       0xFFFF
#define RGB565_DARKGREEN   0x03E0
#define COLOR_NAVY_BLUE    0x0110 
#define COLOR_GRAY         0x31A6 
#define COLOR_YELLOW       0xFFE0 
#define COLOR_GREEN        0x07E0 

#define SD_MMC_CLK  12
#define SD_MMC_MOSI 11
#define SD_MMC_MISO 13

esp_panel::board::Board *board = nullptr;
uint16_t *gfx_frame_buffer = nullptr;

const char* days[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};



extern const uint8_t FreeSansBold12pt7bBitmaps[];
extern const GFXglyph FreeSansBold12pt7bGlyphs[];

// ΔΙΟΡΘΩΣΗ: Σχεδίαση χαρακτήρα γράφοντας ταυτόχρονα και το Background (bg) για να μην φλασάρει!
void drawChar_Internal(int x, int y, char c, uint16_t color, uint16_t bg, int scale) {
    if (c < 0x20 || c > 0x7E) return; 

    GFXglyph glyph = FreeSansBold12pt7bGlyphs[c - 0x20];
    uint8_t *bitmap = (uint8_t *)FreeSansBold12pt7bBitmaps;

    uint16_t bo = glyph.bitmapOffset;
    uint8_t  w  = glyph.width;
    uint8_t  h  = glyph.height;
    int8_t   xo = glyph.xOffset;
    int8_t   yo = glyph.yOffset;
    uint8_t  xx, yy, bits = 0, bit = 0;

    int baseline = 24; 

    // Γεμίζουμε πρώτα το block του χαρακτήρα με το background χρώμα για καθαρό UI
    int total_w = glyph.xAdvance * scale;
    int total_h = 32 * scale; // Standard ύψος για τη γραμματοσειρά 12pt
    for (int py = y; py < y + total_h; py++) {
        for (int px = x; px < x + total_w; px++) {
            if (px >= 0 && px < 800 && py >= 0 && py < 480) {
                gfx_frame_buffer[py * 800 + px] = bg;
            }
        }
    }

    // Σχεδίαση των ενεργών pixels του γράμματος από πάνω
    for (yy = 0; yy < h; yy++) {
        for (xx = 0; xx < w; xx++) {
            if (!(bit & 7)) {
                bits = bitmap[bo++];
            }
            bit++;
            if (bits & 0x80) {
                for (int sx = 0; sx < scale; sx++) {
                    for (int sy = 0; sy < scale; sy++) {
                        int px = x + (xo + xx) * scale + sx;
                        int py = y + (yo + yy + baseline) * scale + sy;
                        if (px >= 0 && px < 800 && py >= 0 && py < 480) {
                            gfx_frame_buffer[py * 800 + px] = color;
                        }
                    }
                }
            }
            bits <<= 1;
        }
    }
}

int getStringWidth(const char* str, int scale) {
    int w = 0;
    while (*str) {
        char c = *str++;
        if (c >= 0x20 && c <= 0x7E) {
            w += FreeSansBold12pt7bGlyphs[c - 0x20].xAdvance * scale;
        }
    }
    return w;
}

void drawString_Internal(int x, int y, const char* str, uint16_t color, uint16_t bg, int scale) {
    while (*str) {
        char c = *str;
        if (c >= 0x20 && c <= 0x7E) {
            drawChar_Internal(x, y, c, color, bg, scale);
            x += FreeSansBold12pt7bGlyphs[c - 0x20].xAdvance * scale;
        }
        str++;
    }
}

void fillRect_Internal(int x, int y, int w, int h, uint16_t color) {
    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            if (px >= 0 && px < 800 && py >= 0 && py < 480) {
                gfx_frame_buffer[py * 800 + px] = color;
            }
        }
    }
}

void fillTriangle_Internal(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color) {
    int build_y_min = min(y0, min(y1, y2));
    int build_y_max = max(y0, max(y1, y2));
    for (int y = build_y_min; y <= build_y_max; y++) {
        int x_start = 800, x_end = 0;
        if ((y0 <= y && y1 >= y) || (y1 <= y && y0 >= y)) {
            if (y1 != y0) x_start = min(x_start, x0 + (y - y0) * (x1 - x0) / (y1 - y0));
        }
        if ((y1 <= y && y2 >= y) || (y2 <= y && y1 >= y)) {
            if (y2 != y1) x_start = min(x_start, x1 + (y - y1) * (x2 - x1) / (y2 - y1));
        }
        if ((y2 <= y && y0 >= y) || (y0 <= y && y2 >= y)) {
            if (y0 != y2) x_start = min(x_start, x2 + (y - y2) * (x0 - x2) / (y0 - y2));
        }
        if ((y0 <= y && y1 >= y) || (y1 <= y && y0 >= y)) {
            if (y1 != y0) x_end = max(x_end, x0 + (y - y0) * (x1 - x0) / (y1 - y0));
        }
        if ((y1 <= y && y2 >= y) || (y2 <= y && y1 >= y)) {
            if (y2 != y1) x_end = max(x_end, x1 + (y - y1) * (x2 - x1) / (y2 - y1));
        }
        if ((y2 <= y && y0 >= y) || (y0 <= y && y2 >= y)) {
            if (y0 != y2) x_end = max(x_end, x2 + (y - y2) * (x0 - x2) / (y0 - y2));
        }
        for (int x = x_start; x <= x_end; x++) {
            if (x >= 0 && x < 800 && y >= 0 && y < 480) gfx_frame_buffer[y * 800 + x] = color;
        }
    }
}

void fillCircle_Internal(int xm, int ym, int r, uint16_t color) {
    int x = -r, y = 0, err = 2 - 2 * r;
    do {
        for (int i = xm + x; i <= xm - x; i++) {
            if (i >= 0 && i < 800 && (ym + y) >= 0 && (ym + y) < 480) gfx_frame_buffer[(ym + y) * 800 + i] = color;
            if (i >= 0 && i < 800 && (ym - y) >= 0 && (ym - y) < 480) gfx_frame_buffer[(ym - y) * 800 + i] = color;
        }
        r = err;
        if (r <= y) err += ++y * 2 + 1;
        if (r > x || err > y) err += ++x * 2 + 1;
    } while (x < 0);
}
void updateTitle() {
    fillRect_Internal(0, 40, 800, 50, RGB565_BLACK);
    String title = aviFileList[selectedIndex];
    int textW = getStringWidth(title.c_str(), 1); 
    int titleX = (800 - textW) / 2;
    drawString_Internal(titleX, 50, title.c_str(), COLOR_YELLOW, RGB565_BLACK, 1);
}

void drawClock_Internal() {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        char time_str[32];
        char date_str[64];
        
        sprintf(time_str, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        sprintf(date_str, "%s  %02d/%02d/%04d", days[timeinfo.tm_wday], timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);

        int timeW = getStringWidth(time_str, 1);
        int timeX = (800 - timeW) / 2;
        drawString_Internal(timeX, 180, time_str, COLOR_YELLOW, RGB565_BLACK, 1);

        int dateW = getStringWidth(date_str, 1);
        int dateX = (800 - dateW) / 2;
        drawString_Internal(dateX, 230, date_str, COLOR_YELLOW, RGB565_BLACK, 1);
    }
}

void displaySelectedFile() {
    fillRect_Internal(0, 0, 800, 480, RGB565_BLACK);
    int centerY = 480 / 2;
    int arrowSize = 40;
    int margin = 20;

    fillTriangle_Internal(margin, centerY, margin + arrowSize, centerY - arrowSize / 2, margin + arrowSize, centerY + arrowSize / 2, RGB565_WHITE);
    fillTriangle_Internal(800 - margin, centerY, 800 - margin - arrowSize, centerY - arrowSize / 2, 800 - margin - arrowSize, centerY + arrowSize / 2, RGB565_WHITE);

    int playX = 800 / 2;
    int playY = 380;
    fillCircle_Internal(playX, playY, 30, RGB565_DARKGREEN);
    fillTriangle_Internal(playX - 10, playY - 15, playX - 10, playY + 15, playX + 15, playY, RGB565_WHITE);

    updateTitle();
    drawClock_Internal(); 
}

void waitForTouchRelease() {
    auto touch = board->getTouch();
    if (touch != nullptr) {
        std::vector<esp_panel::drivers::TouchPoint> points;
        touch->readRawData(-1, -1, 30);
        touch->getPoints(points);
        while (!points.empty()) {
            delay(50);
            points.clear();
            touch->readRawData(-1, -1, 30);
            touch->getPoints(points);
        }
    }
    delay(50);
}

void loadAviFiles() {
    File aviDir = SD_MMC.open(AVI_FOLDER);
    if (!aviDir) {
        Serial.println("Failed to open AVI folder");
        return;
    }
    fileCount = 0;
    while (true) {
        File file = aviDir.openNextFile();
        if (!file) break;
        if (!file.isDirectory()) {
            String name = file.name();
            if (name.endsWith(".avi") || name.endsWith(".AVI")) {
                aviFileList[fileCount++] = name;
                if (fileCount >= MAX_FILES) break;
            }
        }
        file.close();
    }
    aviDir.close();
}

void custom_avi_draw(uint16_t *frame_buffer, int video_width, int video_height) {
    if (gfx_frame_buffer == nullptr || frame_buffer == nullptr) return;

    int out_x_offset = (800 - video_width) / 2;  
    int out_y_offset = (480 - video_height) / 2; 

    for (int y = 0; y < video_height; y++) {
        int target_y = out_y_offset + y;
        for (int x = 0; x < video_width; x++) {
            uint16_t raw_pixel = frame_buffer[y * video_width + x];
            gfx_frame_buffer[target_y * 800 + (out_x_offset + x)] = __builtin_bswap16(raw_pixel);
        }
    }
}

bool playAviFile(char *avifile) {
    bool user_stopped = false;
    
    if (avi_open(avifile)) {
        Serial.printf("AVI start %s\n", avifile);
        fillRect_Internal(0, 0, 800, 480, RGB565_BLACK); 
        
        waitForTouchRelease();
        delay(300);
        
        avi_start_ms = millis();
        Serial.println("Start play loop");
        
        int touch_counter = 0;
        
        while (avi_curr_frame < avi_total_frames && !user_stopped) {
            if (avi_decode()) {
                custom_avi_draw(output_buf, avi_w, avi_h); 
            } else {
                avi_curr_frame++; 
            }

            touch_counter++;
            if (touch_counter >= 12) { 
                touch_counter = 0;
                
                auto touch = board->getTouch();
                if (touch != nullptr) {
                    touch->readRawData(-1, -1, 10); 
                    std::vector<esp_panel::drivers::TouchPoint> p_pts;
                    touch->getPoints(p_pts);
                    if (!p_pts.empty()) {
                        user_stopped = true; 
                        Serial.println("Video stopped by user touch!");
                    }
                }
            }
        }
        avi_close();
        Serial.println("AVI end");
        avi_show_stat();
    } else {
        Serial.println(AVI_strerror());
    }
    
    return user_stopped; 
}

void setup() {
    Serial.begin(115200);

    board = new esp_panel::board::Board();
    board->init();
    assert(board->begin());

    if (board != NULL && board->getLCD() != NULL) {
        esp_lcd_panel_handle_t native_panel = (esp_lcd_panel_handle_t)board->getLCD()->getHandle();
        if (native_panel != NULL) {
            esp_lcd_rgb_panel_get_frame_buffer(native_panel, 1, (void**)&gfx_frame_buffer);
        }
    }

    if (gfx_frame_buffer != nullptr) {
        fillRect_Internal(0, 0, 800, 480, COLOR_NAVY_BLUE);
    }

    // Σύνδεση Wi-Fi και λήψη ώρας
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }
    configTime(0, 0, ntpServer);
    setenv("TZ", time_zone, 1);
    tzset();
    
    struct tm timeinfo;
    while (!getLocalTime(&timeinfo)) { delay(500); }

    // --- ΚΛΕΙΣΙΜΟ WI-FI (Απελευθερώνει την CPU και σταματάει τα sparkles) ---
    // Αφού πήραμε την ώρα, κλείνουμε τελείως το Wi-Fi για να ηρεμήσει το Hardware timing!
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    SD_MMC.setPins(SD_MMC_CLK, SD_MMC_MOSI, SD_MMC_MISO);
    if (!SD_MMC.begin("/sdcard", true)) {
        Serial.println("SD_MMC MOUNT FAILED!");
        while(true);
    }

    output_buf_size = 480 * 272 * 2;
    output_buf = (uint16_t *)aligned_alloc(16, output_buf_size);
    if (!output_buf) {
        while (true);
    }

    avi_init();
    loadAviFiles();
    displaySelectedFile();
}

void loop() {
    if (millis() - lastClockUpdateMs > 1000) {
        lastClockUpdateMs = millis();
        drawClock_Internal();
    }

    auto touch = board->getTouch();
    if (touch != nullptr) {
        touch->readRawData(-1, -1, 30); 
        std::vector<esp_panel::drivers::TouchPoint> points;
        touch->getPoints(points);

        if (!points.empty()) {
            int tx = points[0].x;
            int ty = points[0].y;

            int arrowSize = 40;
            int margin = 20;
            int centerY = 480 / 2;

            if (tx < margin + arrowSize + 20 && ty > centerY - arrowSize && ty < centerY + arrowSize) {
                selectedIndex--;
                if (selectedIndex < 0) selectedIndex = fileCount - 1;
                updateTitle();
                drawClock_Internal();
                waitForTouchRelease();
            }
            else if (tx > 800 - margin - arrowSize - 20 && ty > centerY - arrowSize && ty < centerY + arrowSize) {
                selectedIndex++;
                if (selectedIndex >= fileCount) selectedIndex = 0;
                updateTitle();
                drawClock_Internal();
                waitForTouchRelease();
            }
            else if (tx > 350 && tx < 450 && ty > 330 && ty < 430) {
                bool stop_playlist = false;
                
                while (!stop_playlist && fileCount > 0) {
                    String fullPath = String("/sdcard") + String(AVI_FOLDER) + "/" + aviFileList[selectedIndex];
                    char aviFilename[256];
                    fullPath.toCharArray(aviFilename, sizeof(aviFilename));
                    
                    bool pressed_stop = playAviFile(aviFilename);
                    
                    if (pressed_stop) {
                        stop_playlist = true; 
                    } else {
                        selectedIndex++;
                        if (selectedIndex >= fileCount) selectedIndex = 0; 
                    }
                }
                
                displaySelectedFile();
                waitForTouchRelease();
            }
        }
    }
    delay(10); 
}
