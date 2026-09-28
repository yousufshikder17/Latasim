// A representative media-center application (media_center.h): CMSIS-RTOS v1 C
// code in style, compiled as C++ because the USB speaker it uses is.
#include "media_center.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
#include "Board_ADC.h"
#include "Board_GLCD.h"
#include "Board_Joystick.h"
#include "Board_LED.h"
#include "cmsis_os.h"
#include "usb_speaker.h"
extern GLCD_FONT GLCD_Font_16x24;
extern GLCD_FONT GLCD_Font_6x8;
}

volatile media_status_t media;

namespace {

constexpr std::uint32_t kWhite = 0xFFFF, kBlue = 0x001F, kNavy = 0x000F, kYellow = 0xFFE0;
constexpr std::uint32_t kWidth = 320;
constexpr const char* kItems[MEDIA_ITEMS] = {"Photo gallery", "Audio player", "Paddle game"};

// Pictures: generated RGB565 patterns, 96 x 72.
constexpr std::uint32_t kPhotoW = 96, kPhotoH = 72;
std::uint16_t photos[MEDIA_PHOTOS_COUNT][kPhotoW * kPhotoH];

void make_photos() {
    for (std::uint32_t y = 0; y < kPhotoH; ++y)
        for (std::uint32_t x = 0; x < kPhotoW; ++x) {
            const std::uint32_t i = y * kPhotoW + x;
            photos[0][i] = static_cast<std::uint16_t>(((x * 31 / kPhotoW) << 11) | ((y * 63 / kPhotoH) << 5) | 8);
            const std::uint32_t dx = x > kPhotoW / 2 ? x - kPhotoW / 2 : kPhotoW / 2 - x;
            const std::uint32_t dy = y > kPhotoH / 2 ? y - kPhotoH / 2 : kPhotoH / 2 - y;
            photos[1][i] = ((dx * dx + dy * dy) / 64) % 2 ? 0xF800 : 0x07E0;  // rings
            photos[2][i] = ((x / 12) + (y / 12)) % 2 ? 0xFFFF : 0x001F;       // checks
        }
}

std::uint32_t joystick_last;

std::uint32_t joystick_edges() {  // positions newly pressed since the last poll
    const std::uint32_t now = Joystick_GetState();
    const std::uint32_t edges = now & ~joystick_last;
    joystick_last = now;
    return edges;
}

void title(const char* text) {
    GLCD_SetBackgroundColor(kNavy);
    GLCD_SetForegroundColor(kWhite);
    GLCD_ClearScreen();
    GLCD_SetFont(&GLCD_Font_16x24);
    GLCD_DrawString(8, 8, text);
    media.redraws++;
}

void draw_menu() {
    title("Latasim media");
    for (std::uint32_t i = 0; i < MEDIA_ITEMS; ++i) {
        const bool selected = i == media.selection;
        GLCD_SetBackgroundColor(selected ? kWhite : kNavy);
        GLCD_SetForegroundColor(selected ? kBlue : kWhite);
        GLCD_DrawString(32, 60 + 40 * i, kItems[i]);
    }
    for (std::uint32_t n = 0; n < 8; ++n) (n == media.selection) ? LED_On(n) : LED_Off(n);
}

void draw_photo() {
    title("Photos");
    GLCD_DrawBitmap((kWidth - kPhotoW) / 2, 70, kPhotoW, kPhotoH, reinterpret_cast<const std::uint8_t*>(photos[media.photo]));
    char text[24];
    std::snprintf(text, sizeof text, "%u / %u", static_cast<unsigned>(media.photo + 1), MEDIA_PHOTOS_COUNT);
    GLCD_SetFont(&GLCD_Font_6x8);
    GLCD_SetBackgroundColor(kNavy);
    GLCD_DrawString(148, 160, text);
}

// ---- audio player ----

void audio_screen_update() {
    char text[40];
    GLCD_SetFont(&GLCD_Font_6x8);
    GLCD_SetBackgroundColor(kNavy);
    GLCD_SetForegroundColor(kWhite);
    std::snprintf(text, sizeof text, "USB: %-12s", usb_speaker.streaming ? "streaming" : usb_speaker.configured ? "configured" : "waiting");
    GLCD_DrawString(16, 60, text);
    std::snprintf(text, sizeof text, "Volume %3u", static_cast<unsigned>(media.volume));
    GLCD_DrawString(16, 80, text);
    GLCD_SetForegroundColor(kYellow);
    GLCD_DrawBargraph(16, 100, 288, 16, media.volume * 100 / 255);
}

void audio_poll_volume() {
    const int32_t value = ADC_GetValue();  // the conversion started on the last poll
    if (value >= 0) {
        media.volume = static_cast<std::uint32_t>(value) >> 4;  // 12 bits to 0-255
        usb_speaker_set_volume(media.volume);
    }
    ADC_StartConversion();
}

// ---- game ----

constexpr std::uint32_t kPaddleW = 48, kPaddleY = 220, kBall = 6;
int ball_dx = 4, ball_dy = 3;

void fill(std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h, std::uint32_t color) {
    GLCD_SetForegroundColor(color);
    GLCD_DrawBargraph(x, y, w, h, 100);
}

void game_reset_ball() {
    media.ball_x = 150;
    media.ball_y = 50;
    ball_dx = 4;
    ball_dy = 3;
}

void game_step(std::uint32_t edges, std::uint32_t held) {
    (void)edges;
    fill(media.paddle_x, kPaddleY, kPaddleW, 6, kNavy);  // erase
    fill(media.ball_x, media.ball_y, kBall, kBall, kNavy);
    if ((held & JOYSTICK_LEFT) && media.paddle_x >= 8) media.paddle_x -= 8;
    if ((held & JOYSTICK_RIGHT) && media.paddle_x + kPaddleW + 8 <= kWidth) media.paddle_x += 8;
    int x = static_cast<int>(media.ball_x) + ball_dx, y = static_cast<int>(media.ball_y) + ball_dy;
    if (x <= 0 || x + static_cast<int>(kBall) >= static_cast<int>(kWidth)) ball_dx = -ball_dx, x = static_cast<int>(media.ball_x);
    if (y <= 40) ball_dy = -ball_dy, y = static_cast<int>(media.ball_y);
    if (y + static_cast<int>(kBall) >= static_cast<int>(kPaddleY)) {
        if (x + static_cast<int>(kBall) >= static_cast<int>(media.paddle_x) && x <= static_cast<int>(media.paddle_x + kPaddleW)) {
            media.score++;
            ball_dy = -ball_dy;
            y = static_cast<int>(kPaddleY - kBall - 1);
        } else {
            media.misses++;
            game_reset_ball();
            x = static_cast<int>(media.ball_x);
            y = static_cast<int>(media.ball_y);
        }
    }
    media.ball_x = static_cast<std::uint32_t>(x);
    media.ball_y = static_cast<std::uint32_t>(y);
    fill(media.paddle_x, kPaddleY, kPaddleW, 6, kWhite);
    fill(media.ball_x, media.ball_y, kBall, kBall, kYellow);
    char text[24];
    GLCD_SetFont(&GLCD_Font_6x8);
    GLCD_SetBackgroundColor(kNavy);
    GLCD_SetForegroundColor(kWhite);
    std::snprintf(text, sizeof text, "Score %u  Miss %u", static_cast<unsigned>(media.score), static_cast<unsigned>(media.misses));
    GLCD_DrawString(200, 16, text);
}

// ---- screens ----

void enter(media_screen screen) {
    media.screen = screen;
    switch (screen) {
    case MEDIA_MENU: draw_menu(); break;
    case MEDIA_PHOTOS: draw_photo(); break;
    case MEDIA_AUDIO:
        title("Audio player");
        usb_speaker_start();
        ADC_StartConversion();
        audio_screen_update();
        break;
    case MEDIA_GAME:
        title("Paddle");
        media.paddle_x = (kWidth - kPaddleW) / 2;
        game_reset_ball();
        break;
    }
}

void ui_thread(void const* argument) {
    (void)argument;
    enter(MEDIA_MENU);
    for (;;) {
        osDelay(MEDIA_POLL_MS);
        const std::uint32_t edges = joystick_edges();
        switch (media.screen) {
        case MEDIA_MENU:
            if (edges & JOYSTICK_UP && media.selection > 0) media.selection--, draw_menu();
            if (edges & JOYSTICK_DOWN && media.selection + 1 < MEDIA_ITEMS) media.selection++, draw_menu();
            if (edges & JOYSTICK_CENTER) enter(static_cast<media_screen>(MEDIA_PHOTOS + media.selection));
            break;
        case MEDIA_PHOTOS:
            if (edges & JOYSTICK_CENTER) { enter(MEDIA_MENU); break; }
            if (edges & JOYSTICK_RIGHT) media.photo = (media.photo + 1) % MEDIA_PHOTOS_COUNT, draw_photo();
            if (edges & JOYSTICK_LEFT) media.photo = (media.photo + MEDIA_PHOTOS_COUNT - 1) % MEDIA_PHOTOS_COUNT, draw_photo();
            break;
        case MEDIA_AUDIO:
            if (edges & JOYSTICK_CENTER) {
                usb_speaker_stop();
                enter(MEDIA_MENU);
                break;
            }
            audio_poll_volume();
            audio_screen_update();
            break;
        case MEDIA_GAME:
            if (edges & JOYSTICK_CENTER) { enter(MEDIA_MENU); break; }
            game_step(edges, Joystick_GetState());
            break;
        }
    }
}
osThreadDef(ui_thread, osPriorityNormal, 1, 0);

}  // namespace

extern "C" {

void media_main(void) {
    osKernelInitialize();
    LED_Initialize();
    Joystick_Initialize();
    ADC_Initialize();
    GLCD_Initialize();
    make_photos();
    osThreadCreate(osThread(ui_thread), nullptr);
    osKernelStart();
}

void media_reset(void) {
    std::memset(const_cast<media_status_t*>(&media), 0, sizeof media);
    joystick_last = 0;
    ball_dx = 4;
    ball_dy = 3;
}

}  // extern "C"
