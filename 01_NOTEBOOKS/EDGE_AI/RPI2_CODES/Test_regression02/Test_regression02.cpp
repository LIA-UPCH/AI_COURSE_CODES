#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

#include "pico/stdlib.h"
#include "hardware/i2c.h"

#include "model.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

// ======================== Pin Definitions ========================
constexpr uint I2C_SDA_PIN     = 4;
constexpr uint I2C_SCL_PIN     = 5;
constexpr uint ENC_A_PIN       = 6;
constexpr uint ENC_B_PIN       = 7;
constexpr uint ENC_PUSH_PIN    = 8;
constexpr uint BTN_CONFIRM_PIN = 9;
constexpr uint BTN_BACK_PIN    = 10;

// ======================== TFLite Micro Setup =====================
namespace {
constexpr int kTensorArenaSize = 8 * 1024;
alignas(16) uint8_t tensor_arena[kTensorArenaSize];

const tflite::Model* tfl_model = nullptr;
tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* input_tensor = nullptr;
TfLiteTensor* output_tensor = nullptr;
} // namespace

bool init_tflite_model() {
    tfl_model = tflite::GetModel(g_model);
    if (tfl_model->version() != TFLITE_SCHEMA_VERSION) {
        printf("Model schema %lu != supported %d\n",
               tfl_model->version(), TFLITE_SCHEMA_VERSION);
        return false;
    }

    static tflite::MicroMutableOpResolver<6> resolver;
    resolver.AddFullyConnected();
    resolver.AddAdd();
    resolver.AddMul();
    resolver.AddReshape();
    resolver.AddQuantize();
    resolver.AddDequantize();

    static tflite::MicroInterpreter static_interpreter(
        tfl_model, resolver, tensor_arena, kTensorArenaSize);
    interpreter = &static_interpreter;

    if (interpreter->AllocateTensors() != kTfLiteOk) {
        printf("AllocateTensors() failed\n");
        return false;
    }

    input_tensor = interpreter->input(0);
    output_tensor = interpreter->output(0);
    return true;
}

float predict_regression(float x) {
    if (!interpreter || !input_tensor || !output_tensor) {
        return NAN;
    }

    // Write input (supports both float32 and int8 quantized models)
    if (input_tensor->type == kTfLiteFloat32) {
        input_tensor->data.f[0] = x;
    } else if (input_tensor->type == kTfLiteInt8) {
        int32_t q = static_cast<int32_t>(std::round(x / input_tensor->params.scale)) +
                    input_tensor->params.zero_point;
        if (q < -128) q = -128;
        if (q > 127)  q = 127;
        input_tensor->data.int8[0] = static_cast<int8_t>(q);
    }

    if (interpreter->Invoke() != kTfLiteOk) {
        printf("Invoke() failed\n");
        return NAN;
    }

    // Read output
    if (output_tensor->type == kTfLiteFloat32) {
        return output_tensor->data.f[0];
    } else if (output_tensor->type == kTfLiteInt8) {
        return (output_tensor->data.int8[0] - output_tensor->params.zero_point) *
               output_tensor->params.scale;
    }

    return NAN;
}

// ======================== Minimal SSD1306 Driver =================
class SSD1306 {
public:
    static constexpr uint8_t ADDR = 0x3C;
    static constexpr int WIDTH = 128;
    static constexpr int HEIGHT = 64;
    static constexpr int PAGES = HEIGHT / 8;

    SSD1306(i2c_inst_t* i2c_port) : i2c(i2c_port) {
        clear();
    }

    void init() {
        const uint8_t cmds[] = {
            0xAE,       // Display OFF
            0x20, 0x00, // Horizontal addressing mode
            0xB0,       // Page start address
            0xC8,       // COM scan direction remapped
            0x00, 0x10, // Low/High column start
            0x40,       // Start line 0
            0x81, 0x7F, // Contrast
            0xA1,       // Segment remap
            0xA6,       // Normal display
            0xA8, 0x3F, // Multiplex ratio 64
            0xA4,       // Display follows RAM
            0xD3, 0x00, // Display offset 0
            0xD5, 0x80, // Clock divide
            0xD9, 0xF1, // Pre-charge
            0xDA, 0x12, // COM pins
            0xDB, 0x40, // VCOMH deselect
            0x8D, 0x14, // Charge pump ON
            0xAF        // Display ON
        };
        for (uint8_t c : cmds) send_cmd(c);
        clear();
        show();
    }

    void clear() {
        std::memset(buffer, 0, sizeof(buffer));
    }

    void show() {
        send_cmd(0x21); send_cmd(0); send_cmd(WIDTH - 1);
        send_cmd(0x22); send_cmd(0); send_cmd(PAGES - 1);

        uint8_t tx[WIDTH * PAGES + 1];
        tx[0] = 0x40;
        std::memcpy(&tx[1], buffer, sizeof(buffer));
        i2c_write_blocking(i2c, ADDR, tx, sizeof(tx), false);
    }

    void draw_text(int x, int page, const char* text) {
        if (page < 0 || page >= PAGES) return;
        while (*text && x <= WIDTH - 6) {
            draw_char(x, page, *text++);
            x += 6;
        }
    }

private:
    i2c_inst_t* i2c;
    uint8_t buffer[WIDTH * PAGES];

    void send_cmd(uint8_t cmd) {
        uint8_t buf[2] = {0x00, cmd};
        i2c_write_blocking(i2c, ADDR, buf, 2, false);
    }

    void get_glyph(char c, uint8_t out[5]) {
        static const uint8_t blank[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
        std::memcpy(out, blank, 5);
        if (c >= 'a' && c <= 'z') c -= 32;

        switch (c) {
            case '0': { const uint8_t g[5]={0x3E,0x51,0x49,0x45,0x3E}; std::memcpy(out,g,5); break; }
            case '1': { const uint8_t g[5]={0x00,0x42,0x7F,0x40,0x00}; std::memcpy(out,g,5); break; }
            case '2': { const uint8_t g[5]={0x42,0x61,0x51,0x49,0x46}; std::memcpy(out,g,5); break; }
            case '3': { const uint8_t g[5]={0x21,0x41,0x45,0x4B,0x31}; std::memcpy(out,g,5); break; }
            case '4': { const uint8_t g[5]={0x18,0x14,0x12,0x7F,0x10}; std::memcpy(out,g,5); break; }
            case '5': { const uint8_t g[5]={0x27,0x45,0x45,0x45,0x39}; std::memcpy(out,g,5); break; }
            case '6': { const uint8_t g[5]={0x3C,0x4A,0x49,0x49,0x30}; std::memcpy(out,g,5); break; }
            case '7': { const uint8_t g[5]={0x01,0x71,0x09,0x05,0x03}; std::memcpy(out,g,5); break; }
            case '8': { const uint8_t g[5]={0x36,0x49,0x49,0x49,0x36}; std::memcpy(out,g,5); break; }
            case '9': { const uint8_t g[5]={0x06,0x49,0x49,0x29,0x1E}; std::memcpy(out,g,5); break; }
            case 'A': { const uint8_t g[5]={0x7E,0x11,0x11,0x11,0x7E}; std::memcpy(out,g,5); break; }
            case 'B': { const uint8_t g[5]={0x7F,0x49,0x49,0x49,0x36}; std::memcpy(out,g,5); break; }
            case 'C': { const uint8_t g[5]={0x3E,0x41,0x41,0x41,0x22}; std::memcpy(out,g,5); break; }
            case 'D': { const uint8_t g[5]={0x7F,0x41,0x41,0x22,0x1C}; std::memcpy(out,g,5); break; }
            case 'E': { const uint8_t g[5]={0x7F,0x49,0x49,0x49,0x41}; std::memcpy(out,g,5); break; }
            case 'F': { const uint8_t g[5]={0x7F,0x09,0x09,0x09,0x01}; std::memcpy(out,g,5); break; }
            case 'G': { const uint8_t g[5]={0x3E,0x41,0x49,0x49,0x7A}; std::memcpy(out,g,5); break; }
            case 'H': { const uint8_t g[5]={0x7F,0x08,0x08,0x08,0x7F}; std::memcpy(out,g,5); break; }
            case 'I': { const uint8_t g[5]={0x00,0x41,0x7F,0x41,0x00}; std::memcpy(out,g,5); break; }
            case 'K': { const uint8_t g[5]={0x7F,0x08,0x14,0x22,0x41}; std::memcpy(out,g,5); break; }
            case 'L': { const uint8_t g[5]={0x7F,0x40,0x40,0x40,0x40}; std::memcpy(out,g,5); break; }
            case 'M': { const uint8_t g[5]={0x7F,0x02,0x0C,0x02,0x7F}; std::memcpy(out,g,5); break; }
            case 'N': { const uint8_t g[5]={0x7F,0x04,0x08,0x10,0x7F}; std::memcpy(out,g,5); break; }
            case 'O': { const uint8_t g[5]={0x3E,0x41,0x41,0x41,0x3E}; std::memcpy(out,g,5); break; }
            case 'P': { const uint8_t g[5]={0x7F,0x09,0x09,0x09,0x06}; std::memcpy(out,g,5); break; }
            case 'R': { const uint8_t g[5]={0x7F,0x09,0x19,0x29,0x46}; std::memcpy(out,g,5); break; }
            case 'S': { const uint8_t g[5]={0x46,0x49,0x49,0x49,0x31}; std::memcpy(out,g,5); break; }
            case 'T': { const uint8_t g[5]={0x01,0x01,0x7F,0x01,0x01}; std::memcpy(out,g,5); break; }
            case 'U': { const uint8_t g[5]={0x3F,0x40,0x40,0x40,0x3F}; std::memcpy(out,g,5); break; }
            case 'V': { const uint8_t g[5]={0x1F,0x20,0x40,0x20,0x1F}; std::memcpy(out,g,5); break; }
            case 'W': { const uint8_t g[5]={0x3F,0x40,0x38,0x40,0x3F}; std::memcpy(out,g,5); break; }
            case 'X': { const uint8_t g[5]={0x63,0x14,0x08,0x14,0x63}; std::memcpy(out,g,5); break; }
            case 'Y': { const uint8_t g[5]={0x07,0x08,0x70,0x08,0x07}; std::memcpy(out,g,5); break; }
            case ':': { const uint8_t g[5]={0x00,0x36,0x36,0x00,0x00}; std::memcpy(out,g,5); break; }
            case '.': { const uint8_t g[5]={0x00,0x60,0x60,0x00,0x00}; std::memcpy(out,g,5); break; }
            case '-': { const uint8_t g[5]={0x08,0x08,0x08,0x08,0x08}; std::memcpy(out,g,5); break; }
            case '>': { const uint8_t g[5]={0x41,0x22,0x14,0x08,0x00}; std::memcpy(out,g,5); break; }
            default: break;
        }
    }

    void draw_char(int x, int page, char c) {
        uint8_t glyph[5];
        get_glyph(c, glyph);
        for (int i = 0; i < 5; ++i) {
            buffer[page * WIDTH + x + i] = glyph[i];
        }
    }
};

// ======================== Input Helpers ==========================
struct Button {
    uint pin;
    bool last_state = true;
    uint32_t last_change_ms = 0;

    void init(uint gpio) {
        pin = gpio;
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        gpio_pull_up(pin);
    }

    bool pressed() {
        bool current = gpio_get(pin);
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (current != last_state && (now - last_change_ms) > 40) {
            last_change_ms = now;
            last_state = current;
            if (!current) return true;
        }
        return false;
    }
};

class RotaryEncoder {
public:
    void init(uint pin_a, uint pin_b) {
        a = pin_a;
        b = pin_b;
        gpio_init(a); gpio_set_dir(a, GPIO_IN); gpio_pull_up(a);
        gpio_init(b); gpio_set_dir(b, GPIO_IN); gpio_pull_up(b);
        prev_state = (gpio_get(a) << 1) | gpio_get(b);
    }

    int read_step() {
        static const int8_t transition_table[16] = {
             0, -1,  1,  0,
             1,  0,  0, -1,
            -1,  0,  0,  1,
             0,  1, -1,  0
        };
        uint8_t curr = (gpio_get(a) << 1) | gpio_get(b);
        uint8_t idx = (prev_state << 2) | curr;
        prev_state = curr;
        accum += transition_table[idx];

        if (accum >= 4)  { accum = 0; return  1; }
        if (accum <= -4) { accum = 0; return -1; }
        return 0;
    }

private:
    uint a = 0, b = 0;
    uint8_t prev_state = 0;
    int accum = 0;
};

// ======================== Application FSM ========================
enum class AppState {
    MENU,
    ADD_INPUT,
    RANDOM_INPUT,
    OUTPUT
};

int main() {
    stdio_init_all();

    // Initialize I2C0 at 400 kHz
    i2c_init(i2c0, 400 * 1000);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);

    SSD1306 oled(i2c0);
    oled.init();

    if (!init_tflite_model()) {
        oled.clear();
        oled.draw_text(10, 3, "MODEL INIT ERR");
        oled.show();
        while (true) {
            sleep_ms(1000);
        }
    }

    RotaryEncoder encoder;
    encoder.init(ENC_A_PIN, ENC_B_PIN);

    Button enc_push, btn_confirm, btn_back;
    enc_push.init(ENC_PUSH_PIN);
    btn_confirm.init(BTN_CONFIRM_PIN);
    btn_back.init(BTN_BACK_PIN);

    std::srand(static_cast<unsigned int>(time_us_32()));

    AppState state = AppState::MENU;
    int menu_idx = 0;
    int input_val = 50;
    float output_val = 0.0f;
    bool needs_redraw = true;

    while (true) {
        int step = encoder.read_step();
        bool confirm = enc_push.pressed() || btn_confirm.pressed();
        bool back = btn_back.pressed();

        switch (state) {
            case AppState::MENU:
                if (step != 0) {
                    menu_idx = (menu_idx + 1) % 2;
                    needs_redraw = true;
                }
                if (confirm) {
                    if (menu_idx == 0) {
                        state = AppState::ADD_INPUT;
                    } else {
                        std::srand(static_cast<unsigned int>(time_us_32()));
                        input_val = std::rand() % 101;
                        state = AppState::RANDOM_INPUT;
                    }
                    needs_redraw = true;
                }
                break;

            case AppState::ADD_INPUT:
                if (step != 0) {
                    input_val += step;
                    if (input_val < 0) input_val = 0;
                    if (input_val > 100) input_val = 100;
                    needs_redraw = true;
                }
                if (confirm) {
                    output_val = predict_regression(static_cast<float>(input_val));
                    state = AppState::OUTPUT;
                    needs_redraw = true;
                } else if (back) {
                    state = AppState::MENU;
                    needs_redraw = true;
                }
                break;

            case AppState::RANDOM_INPUT:
                if (step != 0) {
                    input_val = std::rand() % 101;
                    needs_redraw = true;
                }
                if (confirm) {
                    output_val = predict_regression(static_cast<float>(input_val));
                    state = AppState::OUTPUT;
                    needs_redraw = true;
                } else if (back) {
                    state = AppState::MENU;
                    needs_redraw = true;
                }
                break;

            case AppState::OUTPUT:
                if (confirm || back) {
                    state = AppState::MENU;
                    needs_redraw = true;
                }
                break;
        }

        if (needs_redraw) {
            oled.clear();
            char line[32];

            switch (state) {
                case AppState::MENU:
                    oled.draw_text(10, 0, "REGRESSION MODEL");
                    oled.draw_text(6,  3, menu_idx == 0 ? "> ADD INPUT"    : "  ADD INPUT");
                    oled.draw_text(6,  5, menu_idx == 1 ? "> RANDOM INPUT" : "  RANDOM INPUT");
                    break;

                case AppState::ADD_INPUT:
                    oled.draw_text(10, 0, "ADD INPUT");
                    std::snprintf(line, sizeof(line), "VAL: %d", input_val);
                    oled.draw_text(20, 3, line);
                    oled.draw_text(4,  6, "PUSH TO PREDICT");
                    break;

                case AppState::RANDOM_INPUT:
                    oled.draw_text(10, 0, "RANDOM INPUT");
                    std::snprintf(line, sizeof(line), "VAL: %d", input_val);
                    oled.draw_text(20, 3, line);
                    oled.draw_text(4,  6, "PUSH TO PREDICT");
                    break;

                case AppState::OUTPUT:
                    oled.draw_text(10, 0, "MODEL OUTPUT");
                    std::snprintf(line, sizeof(line), "VAL: %d", input_val);
                    oled.draw_text(16, 2, line);
                    std::snprintf(line, sizeof(line), "OUT: %.2f", output_val);
                    oled.draw_text(16, 4, line);
                    oled.draw_text(4,  6, "PUSH FOR MENU");
                    break;
            }

            oled.show();
            needs_redraw = false;
        }

        sleep_ms(2);
    }
}