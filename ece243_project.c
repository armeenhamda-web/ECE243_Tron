/*
 *
 * ECE243 - FINAL PROJECT
 * Tron Game on DE1-SoC
 * Written by Hamda Armeen and Malak Khalifa
 *
 * FUNCTIONALITY:
 * A real-time Tron-style game where two players control continuously moving
 * squares on a VGA display using a PS/2 keyboard. Each player leaves a trail
 * that acts as a wall The game updates at a fixed time interval, and players
 * lose if they collide with trails, the opponent, or screen boundaries.Audio
 * feedback is generated on collisions. An optional single-player mode includes
 * AI-controlled movement which will be implemented based on real-time user
 * input.
 *
 */

#include <stdint.h>

// Hardware addresses
#define VGA_PIXEL_CTRL_BASE 0xFF203020
#define VGA_STATUS_OFFSET 0x0C
#define PS2_BASE_PRIMARY 0xFF200100
#define PS2_BASE_MOUSE 0xFF200108
#define AUDIO_BASE 0xFF203040u

// Screen constants
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define VGA_X_SHIFT 10

// Colour palette
#define COLOR_BLACK 0x0000
#define COLOR_WHITE 0xFFFF

#define COLOR_BLUE 0x05BF
#define COLOR_GREEN 0x07F0
#define COLOR_BLUE_GLOW 0x039F
#define COLOR_GREEN_GLOW 0x0400

#define COLOR_GRAY 0x632C
#define COLOR_VIVID_BLUE 0x18E3
#define COLOR_DARK_BLUE 0x0208
#define COLOR_TEAL 0x3528
#define COLOR_CYAN 0x07FF
#define COLOR_MAGENTA 0xF81F
#define COLOR_MAGENTA_GLOW 0xA01F
#define COLOR_MIDNIGHT 0x0108

// Game constants
#define BORDER_THICK 4
#define TRAIL 2
#define PLAYER_HEAD (TRAIL + 1)
#define MAX_SOUND_SEGMENTS 8

// Memory mapped reader and writer helpers
#define MMIO32(addr) (*(volatile uint32_t*)(uintptr_t)(addr))
#define MMIO16(addr) (*(volatile uint16_t*)(uintptr_t)(addr))

// Globals
volatile int pixel_buffer_start = 0;
int MODE = 0; // 1 = single-player, 2 = multiplayer
int DIFFICULTY = 1; // 1 = easy, 2 = medium, 3 = hard
int PLAYER_COLOR = 1; // 1 = player is blue (AI is green), 2 = player is green (AI is blue)

volatile int pause_toggle_request = 0;
volatile int PAUSED = 0;
int pause_banner_drawn = 0;

volatile int quit_request = 0;

// Structs
typedef struct {
  int x, y;
  int dir;
  int alive;
} Player;

typedef struct {
  int x, y; // current cursor position
  int click; // detects if the mouse is clicked
  int click_held; // tracks if the mouse is held in click position
} Mouse;

typedef struct {
  uint32_t hz;
  unsigned total;
} SendSegments;

struct {
  SendSegments segs[MAX_SOUND_SEGMENTS];  // segment array
  int number_of_segs;      // number of segments in the selected audio
  int current_segment;     // curreng audio segment
  unsigned completed_seg;  // number of samples that have already been played
  int sample;              // output value of sound (+amp or -amp)
  int counter;             // half-period counter
  int amp;                 // volume
} audio = {{{0, 0}}, 0, 0, 0, 0, 0, 0};

// Array Definitions
uint8_t occupied[SCREEN_HEIGHT][SCREEN_WIDTH];
uint16_t trail_color[SCREEN_HEIGHT][SCREEN_WIDTH];
uint16_t obstacle_color[SCREEN_HEIGHT][SCREEN_WIDTH];
uint16_t cursor_shape[5] = {0, 0, 0, 0, 0};  // created to hold the old colours beneath cursor for later restoration
Mouse start_mouse = {SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, 0, 0};

// Character Decoder
const uint8_t FONT[128][5] = {
    ['A'] = {0x7C, 0x12, 0x11, 0x12, 0x7C},
    ['B'] = {0x7F, 0x49, 0x49, 0x49, 0x36},
    ['C'] = {0x3E, 0x41, 0x41, 0x41, 0x22},
    ['D'] = {0x7F, 0x41, 0x41, 0x22, 0x1C},
    ['E'] = {0x7F, 0x49, 0x49, 0x49, 0x41},
    ['F'] = {0x7F, 0x09, 0x09, 0x09, 0x01},
    ['G'] = {0x3E, 0x41, 0x49, 0x49, 0x7A},
    ['H'] = {0x7F, 0x08, 0x08, 0x08, 0x7F},
    ['I'] = {0x00, 0x41, 0x7F, 0x41, 0x00},
    ['K'] = {0x7F, 0x08, 0x14, 0x22, 0x41},
    ['L'] = {0x7F, 0x40, 0x40, 0x40, 0x40},
    ['M'] = {0x7F, 0x02, 0x04, 0x02, 0x7F},
    ['N'] = {0x7F, 0x04, 0x08, 0x10, 0x7F},
    ['O'] = {0x3E, 0x41, 0x41, 0x41, 0x3E},
    ['P'] = {0x7F, 0x09, 0x09, 0x09, 0x06},
    ['R'] = {0x7F, 0x09, 0x19, 0x29, 0x46},
    ['S'] = {0x46, 0x49, 0x49, 0x49, 0x31},
    ['T'] = {0x01, 0x01, 0x7F, 0x01, 0x01},
    ['U'] = {0x3F, 0x40, 0x40, 0x40, 0x3F},
    ['V'] = {0x1F, 0x20, 0x40, 0x20, 0x1F},
    ['W'] = {0x3F, 0x40, 0x38, 0x40, 0x3F},
    ['Y'] = {0x07, 0x08, 0x70, 0x08, 0x07},
    ['Z'] = {0x61, 0x51, 0x49, 0x45, 0x43},

    ['0'] = {0x3E, 0x51, 0x49, 0x45, 0x3E},
    ['1'] = {0x00, 0x42, 0x7F, 0x40, 0x00},
    ['2'] = {0x42, 0x61, 0x51, 0x49, 0x46},
    ['3'] = {0x21, 0x41, 0x45, 0x4B, 0x31},

    [' '] = {0x00, 0x00, 0x00, 0x00, 0x00},
    ['-'] = {0x08, 0x08, 0x08, 0x08, 0x08},
    ['|'] = {0x00, 0x00, 0x7F, 0x00, 0x00},
    ['!'] = {0x00, 0x00, 0x5F, 0x00, 0x00},
};

// ═══════════════════════════════════════════════════════════════════════════
//  AUDIO FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

int audio_push(int32_t L, int32_t R) {
  volatile int* data = (volatile int*)AUDIO_BASE;
  // Check write space for left AND right channel before writing either
  if (((data[1] >> 16) & 0xFF) == 0) return 0;  // Left  FIFO full
  if (((data[1] >> 24) & 0xFF) == 0) return 0;  // Right FIFO full
  data[2] = (int)(L << 8);
  data[3] = (int)(R << 8);
  return 1;
}

void play_square_tone(uint32_t hz, unsigned n_samples, int amp) {
 int sample = amp;
 int counter = 0;
 int half_period = 8000 / (2 * (int)hz);
 unsigned n;
 if (half_period < 1) half_period = 1;
 for (n = 0; n < n_samples; n++) {
   while (!audio_push(sample, sample)) {
   }
   if (++counter >= half_period) {
     sample = -sample;
     counter = 0;
   }
 }
}

void play_silence(unsigned n_samples) {
 unsigned n;
 for (n = 0; n < n_samples; n++) {
   while (!audio_push(0, 0)) {
   }
 }
}

void play_start_screen_beep(void) {
  const int amp = 420000;
  play_square_tone(392u, 320u, amp);
  play_silence(70u);
  play_square_tone(523u, 320u, amp);
  play_silence(70u);
  play_square_tone(659u, 320u, amp);
  play_silence(70u);
  play_square_tone(784u, 380u, amp);
  play_silence(90u);
  play_square_tone(1047u, 720u, amp);
  play_silence(120u);
  play_square_tone(1568u, 180u, amp);
}

void sound_initialize(const SendSegments* segs, int n, int amp) {
  // sets the tones
  for (int i = 0; i < n && i < MAX_SOUND_SEGMENTS; i++) audio.segs[i] = segs[i];
  audio.number_of_segs = n;
  audio.current_segment = 0;
  audio.completed_seg = 0;
  audio.amp = amp;
  audio.sample = amp;
  audio.counter = 0;
}

void sound_play(void) {
  if (audio.current_segment >= audio.number_of_segs) return;
  while (audio.current_segment < audio.number_of_segs) {
    SendSegments* seg =
        &audio.segs[audio.current_segment];  // extract the current segment
    if (audio.completed_seg >=
        seg->total) {  // Advance to next segment if current one is finished
      audio.current_segment++;
      audio.completed_seg = 0;
      audio.sample = audio.amp;
      audio.counter = 0;
      continue;
    }

    int32_t s =
        (seg->hz == 0)
            ? 0
            : audio.sample;  // check what the sound is (silence or some sample)
    if (!audio_push(s, s)) return;  // Push one sample, stop if FIFO is full

    audio.completed_seg++;  // then that segment is done, move on

    if (seg->hz != 0) {  // then as long as it's not 0,  play it
      int half =
          (int)(8000 /
                (2 * (int)seg->hz));  // We output 8000 samples per second
      if (half < 1) half = 1;
      if (++audio.counter >= half) {
        audio.sample = -audio.sample;
        audio.counter = 0;
      }
    }
  }
}

void play_collision_sound(void) {  // Collision sound (descending sounds)
  const SendSegments segs[] = {{784u, 160u}, {0u, 35u}, {523u, 180u}, {0u, 35u}, {330u, 200u}, {0u, 40u}, {196u, 260u}};
  sound_initialize(segs, 7, 380000);
}

void play_game_over_sound(void) {  // Game-over sound (dramatic falling tones)
  const SendSegments segs[] = {{600u, 300u}, {0u, 60u}, {400u, 350u}, {0u, 60u}, {200u, 500u}, {0u, 80u}, {120u, 700u}};
  sound_initialize(segs, 7, 420000);
}

void play_pause_sound(void) {
  const SendSegments segs[] = {{880u, 300u}, {0u, 80u}, {523u, 400u}, {0u, 120u}, {659u, 350u}};
  sound_initialize(segs, 5, 340000);
}

void play_unpause_sound(void) { 
  const SendSegments segs[] = {{523u, 300u}, {0u, 80u}, {880u, 400u}, {0u, 120u}, {784u, 350u}};
  sound_initialize(segs, 5, 340000);
}

// ═══════════════════════════════════════════════════════════════════════════
//  SCREEN / DRAW FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

void draw_pixel(int x, int y, uint16_t color) {
  if ((unsigned)x >= SCREEN_WIDTH || (unsigned)y >= SCREEN_HEIGHT) return;
  uint32_t addr = (uint32_t)(pixel_buffer_start + (y << VGA_X_SHIFT) + (x << 1));
  MMIO16(addr) = color;  // paint it that colour at that address
}

void clear_screen(void) {
  int y, x;
  for (y = 0; y < SCREEN_HEIGHT; y++)
    for (x = 0; x < SCREEN_WIDTH; x++)
      draw_pixel(x, y, 0);  // fill every pixel with black to clear screen
}

void draw_trail(int cx, int cy, uint16_t inner, uint16_t glow) {
  int dx, dy, ax, ay;
  for (dy = -TRAIL; dy <= TRAIL; dy++)
    for (dx = -TRAIL; dx <= TRAIL; dx++) draw_pixel(cx + dx, cy + dy, inner);
  for (dy = -PLAYER_HEAD; dy <= PLAYER_HEAD; dy++)
    for (dx = -PLAYER_HEAD; dx <= PLAYER_HEAD; dx++) {
      ax = cx + dx;
      ay = cy + dy;
      if ((unsigned)ax >= SCREEN_WIDTH || (unsigned)ay >= SCREEN_HEIGHT)
        continue;
      if (dx < -TRAIL || dx > TRAIL || dy < -TRAIL || dy > TRAIL)
        draw_pixel(ax, ay, glow);
    }
}

void draw_player_face(int cx, int cy, uint16_t base_color) {
  // White eyes
  draw_pixel(cx - 1, cy - 1, COLOR_WHITE);
  draw_pixel(cx + 1, cy - 1, COLOR_WHITE);
  
  // Black pupils
  draw_pixel(cx - 1, cy - 2, COLOR_BLACK);
  draw_pixel(cx + 1, cy - 2, COLOR_BLACK);
  
  // HAPPY smile (curved up like a U shape, but upside down)
  // The smile goes UP, not down
  if (base_color == COLOR_BLUE) {
    // Blue player gets cyan smile
    draw_pixel(cx - 2, cy + 1, COLOR_CYAN);
    draw_pixel(cx - 1, cy + 2, COLOR_CYAN);
    draw_pixel(cx, cy + 2, COLOR_CYAN);
    draw_pixel(cx + 1, cy + 2, COLOR_CYAN);
    draw_pixel(cx + 2, cy + 1, COLOR_CYAN);
  } else {
    // Green player gets magenta smile
    draw_pixel(cx - 2, cy + 1, COLOR_MAGENTA);
    draw_pixel(cx - 1, cy + 2, COLOR_MAGENTA);
    draw_pixel(cx, cy + 2, COLOR_MAGENTA);
    draw_pixel(cx + 1, cy + 2, COLOR_MAGENTA);
    draw_pixel(cx + 2, cy + 1, COLOR_MAGENTA);
  }
}

void fill_rectangle(int x, int y, int w, int h, uint16_t c) {
  int j, i;
  for (j = 0; j < h; j++)
    for (i = 0; i < w; i++) draw_pixel(x + i, y + j, c);
}

void fill_obstacle_rectangle(int x, int y, int w, int h, uint16_t color) {
  int xx, yy;
  for (yy = y; yy < y + h; yy++)
    for (xx = x; xx < x + w; xx++) {
      if ((unsigned)xx < SCREEN_WIDTH && (unsigned)yy < SCREEN_HEIGHT) {
        occupied[yy][xx] = 1;
        obstacle_color[yy][xx] = color;
        draw_pixel(xx, yy, color);
      }
    }
}

void draw_char(char c, int x, int y, uint16_t color) {
  int cx, cy;  // Draws a single 5×7 character from FONT
  if ((unsigned char)c >= 128) return;
  for (cx = 0; cx < 5; cx++) {
    uint8_t col_bits = FONT[(unsigned char)c][cx];
    for (cy = 0; cy < 7; cy++)
      if (col_bits & (1 << cy)) draw_pixel(x + cx, y + cy, color);
  }
}

void draw_string(const char* s, int x, int y, uint16_t color) {
  for (; *s; s++, x += 6) draw_char(*s, x, y, color);
}

void draw_string_scaled(const char* s, int x, int y, uint16_t color, int scale) {
  int cx, cy, sx, sy;
  for (; *s; s++, x += 6 * scale) {
    for (cx = 0; cx < 5; cx++) {
      uint8_t col_bits = FONT[(unsigned char)*s][cx];
      for (cy = 0; cy < 7; cy++) {
        if (col_bits & (1 << cy)) {
          for (sx = 0; sx < scale; sx++)
            for (sy = 0; sy < scale; sy++)
              draw_pixel(x + cx * scale + sx, y + cy * scale + sy, color);
        }
      }
    }
  }
}

int str_len(const char* s) {
  int n = 0;
  while (*s++) n++;
  return n;
}

static uint16_t read_pixel(int x, int y) {
  // Reads one pixel colour from the VGA buffer
  if ((unsigned)x >= SCREEN_WIDTH || (unsigned)y >= SCREEN_HEIGHT) return 0;
  uint32_t addr =
      (uint32_t)(pixel_buffer_start + (y << VGA_X_SHIFT) + (x << 1));
  return MMIO16(addr);
}

void wait_for_video_frame(void) {  // read the VGA controller's status register
  while (MMIO32(VGA_PIXEL_CTRL_BASE + VGA_STATUS_OFFSET) & 0x1) {
  }  // wait until VGA is free, will become false (0)
  MMIO32(VGA_PIXEL_CTRL_BASE) =
      1;  // then when available, swap the frame buffers
  while (MMIO32(VGA_PIXEL_CTRL_BASE + VGA_STATUS_OFFSET) & 0x1) {
  }  // wait until VGA controller has finished processing the buffer swap
}

// ═══════════════════════════════════════════════════════════════════════════
//  MOUSE FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

void flush_ps2(volatile uint32_t *ps2) {
    while (*ps2 & 0x8000) {
        volatile int dump = *ps2;
    }
}

int wait_for_byte_timeout(volatile uint32_t *ps2, unsigned char expected, int timeout) {
    while (timeout--) {
        int data = *ps2;
        if (data & 0x8000) {
            if ((data & 0xFF) == expected)
                return 1;
        }
    }
    return 0;
}

void mouse_init(void) {
    volatile uint32_t* ps2 = (volatile uint32_t*)PS2_BASE_MOUSE;
    
    flush_ps2(ps2);
    
    // Send reset
    ps2[1] = 0xFF;
    if (!wait_for_byte_timeout(ps2, 0xFA, 5000000)) return;  // Wait for ACK
    if (!wait_for_byte_timeout(ps2, 0xAA, 5000000)) return;  // Wait for BAT
    
    // Send enable
    ps2[1] = 0xF4;
    wait_for_byte_timeout(ps2, 0xFA, 5000000);  // Wait for ACK
}

void mouse_ignore(void) {
  while (MMIO32(PS2_BASE_MOUSE) &
         0x8000) {  // Reads the lowest 8 bits of the register
    (void)(MMIO32(PS2_BASE_MOUSE) & 0xFF);  // void ignores those reads
  }
}

void draw_cursor(int x, int y) {  // Draws a white cross
  int cx[5] = {x, x - 1, x + 1, x, x};
  int cy[5] = {y, y, y, y - 1, y + 1};
  for (int i = 0; i < 5; i++) {
    cursor_shape[i] = read_pixel(cx[i], cy[i]);
    draw_pixel(cx[i], cy[i], COLOR_WHITE);
  }
}

void erase_cursor(int x, int y) {  /// Restores the 5 pixels that were under the white cross
  int cx[5] = {x, x - 1, x + 1, x, x};
  int cy[5] = {y, y, y, y - 1, y + 1};
  for (int i = 0; i < 5; i++){
    draw_pixel(cx[i], cy[i], cursor_shape[i]);
  }
}

void mouse_poll(void) {
  static int state = 0; // which byte is arriving (0,1,2)
  static uint8_t packet[3] = {0, 0, 0};  // stores byte info before next arrival

  start_mouse.click = 0;  // set the mouse click (press and release) to zero initially

  while (1) {
    uint32_t mouse_register = MMIO32(PS2_BASE_MOUSE);
    if (!(mouse_register & 0x8000))
      break;  // check bit 15 to see if a byte has arrived, if not, break
    uint8_t byte = (uint8_t)(mouse_register & 0xFF);  // store that byte

    if (state == 0 && !(byte & 0x08))
      continue;  // check if that byte was the last one

    packet[state] = byte;  // preserve that byte in the array before next one arrives
    state++; // increment incoming byte

    if (state == 3) {  // if this was the last byte
      state = 0; // change the state back to the first byte

      // Decode signed X/Y from packet
      // How?
      // From byte 0, bit 4 and 5 are for sign of X and Y
      // then the acc X and Y is inside byte 1 and 2
      int x_negative = (packet[0] >> 4) & 1;
      int y_negative = (packet[0] >> 5) & 1;

      // Sign extension
      // if x_negative is 1 (meaning signed) then subtract 256 to get the
      // negative number that it acc is otherwise subtract nothing (0)
      int dx = (int)packet[1] - (x_negative ? 256 : 0);
      int dy = (int)packet[2] - (y_negative ? 256 : 0);

      start_mouse.x += dx;  // move it x direction
      start_mouse.y -= dy;  // move it x direction (y direciton is -ve because of VGA standard)

      // Don't pass boundaries
      if (start_mouse.x < 0) start_mouse.x = 0;
      if (start_mouse.x >= SCREEN_WIDTH) start_mouse.x = SCREEN_WIDTH - 1;
      if (start_mouse.y < 0) start_mouse.y = 0;
      if (start_mouse.y >= SCREEN_HEIGHT) start_mouse.y = SCREEN_HEIGHT - 1;

      // Check button release
      int button = packet[0] & 0x01;  // bit 0 is the left button press status
      // If button WAS pressed last time AND is released now (click has occured)
      if (start_mouse.click_held && !button)
        start_mouse.click = 1; // Register the click
      start_mouse.click_held = button;  // Remember current state for next time
    }
  }
}

int mouse_within_button(int rx, int ry, int rw, int rh) {
  return (start_mouse.x >= rx && start_mouse.x < rx + rw && start_mouse.y >= ry && start_mouse.y < ry + rh);
}

// ═══════════════════════════════════════════════════════════════════════════
//  KEYBOARD FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

// Interrupt handler called automatically when PS/2 interrupt occurs
void ps2_interrupt_handler(void) __attribute__((interrupt));
//The __attribute__((interrupt)) tells the compiler this is an interrupt handler,
//and it will automatically save/restore registers for you.

void ps2_interrupt_handler(void) {
    volatile uint32_t* ps2 = (volatile uint32_t*)PS2_BASE_PRIMARY;
    uint32_t data = *ps2;
    uint8_t byte = data & 0xFF;
    
    // If spacebar pressed (0x29 is make code)
    if (byte == 0x29) { pause_toggle_request = 1; }
}

// Called in main() to set up the interrupt
void setup_ps2_interrupt(void) {
    volatile uint32_t* ps2 = (volatile uint32_t*)PS2_BASE_PRIMARY;
    ps2[1] = 1; // Enable PS/2 interrupt

    //__asm__ is used to embed assembly language instructions directly into your C code
    //this is to know where to go to when an interrupt occurs
    __asm__ volatile("csrw mtvec, %0" : : "r"(ps2_interrupt_handler)); // Set interrupt handler address
    __asm__ volatile("csrs mie, %0" : : "r"(1 << 19));// Enable PS/2 interrupt in mie (bit 19)
    __asm__ volatile("csrs mstatus, %0" : : "r"(8)); // Enable global interrupts
}

void keyboard_init(void) {
    volatile uint32_t* ps2 = (volatile uint32_t*)PS2_BASE_PRIMARY;
    ps2[1] = 0xFF;  // Reset keyboard
    for(int i = 0; i < 100000; i++); // Wait a bit
    ps2[1] = 0xF4;  // Enable keyboard
}

int keyboard_input(uint32_t ps2_base) {
  uint32_t read = MMIO32(ps2_base);
  if (read & 0x8000) return (int)(read & 0xFF);
  return -1;
}

void compute_direction(Player* p, int newdir);  // forward declaration needed by read_input

void read_input(Player* p1, Player* p2, uint8_t b) {
 static uint8_t release = 0;  // read the first 8 bits of keyboard
 if (b == 0xF0) {  // 0xF0 is a signal key that informs that the next key is the release key
   // so we are dealing with the release key before moving on
   release = 1;
   return;
 }
 if (b == 0x15) {  // Q key scancode
    quit_request = 1;
    return;
}
 if (release) {  // now here we can read the release key and ignore it
   release = 0;
   return;
 }
 //Catch spacebar here so it isn't missed
 if (b == 0x29) {
   pause_toggle_request = 1;
   return;
 }
 switch (b) {
   case 0x1D:
     compute_direction(p1, 0);
     break;  // W
   case 0x1C:
     compute_direction(p1, 3);
     break;  // A
   case 0x1B:
     compute_direction(p1, 2);
     break;  // S
   case 0x23:
     compute_direction(p1, 1);
     break;  // D
   case 0x43:
     compute_direction(p2, 0);
     break;  // I
   case 0x3B:
     compute_direction(p2, 3);
     break;  // J
   case 0x42:
     compute_direction(p2, 2);
     break;  // K
   case 0x4B:
     compute_direction(p2, 1);
     break;  // L
 }
}

void poll_ps2_keyboard(Player* p1, Player* p2) {
  int poll;  // Drains the keyboard FIFO and dispatches all pending bytes for
             // both players.
  while ((poll = keyboard_input(PS2_BASE_PRIMARY)) >= 0)
    read_input(p1, p2, (uint8_t)poll);
}

int check_space_bar(void) {
  int sc;  // Returns 1 if the space bar make-code (0x29) was in the keyboard FIFO.
  uint8_t release = 0;
  while ((sc = keyboard_input(PS2_BASE_PRIMARY)) >= 0) {
    if (sc == 0xF0) {
      release = 1;
      continue;
    }
    if (release) {
      release = 0;
      continue;
    }
    if (sc == 0x29) return 1;  // space bar scancode is 0x29
  }
  return 0;
}

// Function Purpose: Ignore IJKL so P2 is controlled by AI
void read_input_p1_only(Player* p1, uint8_t b) {
 static uint8_t release = 0;
 if (b == 0xF0) {
   release = 1;
   return;
 }
 if (b == 0x15) {  // Q key scancode
    quit_request = 1;
    return;
}
 if (release) {
   release = 0;
   return;
 }
 //Catch spacebar here so it isn't missed in single-player mode
 if (b == 0x29) {
   pause_toggle_request = 1;
   return;
 }
 switch (b) {
   case 0x1D:
     compute_direction(p1, 0);
     break;
   case 0x1C:
     compute_direction(p1, 3);
     break;
   case 0x1B:
     compute_direction(p1, 2);
     break;
   case 0x23:
     compute_direction(p1, 1);
     break;
 }
}

int poll_mode_selection(void) {
  static uint8_t rel = 0;
  int c;
  while ((c = keyboard_input(PS2_BASE_PRIMARY)) >= 0) {
    if (c == 0xF0) {
      rel = 1;
      continue;
    }
    if (rel) {
      rel = 0;
      continue;
    }
    if (c == 0x1B) return 1;  // S key (scancode 0x1B)
    if (c == 0x3A) return 2;  // M key (scancode 0x3A)
  }
  return 0;
}

int poll_difficulty_selection(void) {
  static uint8_t rel = 0;
  int c;
  while ((c = keyboard_input(PS2_BASE_PRIMARY)) >= 0) {
    if (c == 0xF0) {
      rel = 1;
      continue;
    }
    if (rel) {
      rel = 0;
      continue;
    }
    if (c == 0x16) return 1;  // key 1
    if (c == 0x1E) return 2;  // key 2
    if (c == 0x26) return 3;  // key 3
  }
  return 0;
}

// Polls keyboard for color selection: B = blue (1), G = green (2)
int poll_color_selection(void) {
  static uint8_t rel = 0;
  int c;
  while ((c = keyboard_input(PS2_BASE_PRIMARY)) >= 0) {
    if (c == 0xF0) {
      rel = 1;
      continue;
    }
    if (rel) {
      rel = 0;
      continue;
    }
    if (c == 0x32) return 1;  // B key (scancode 0x32) → blue
    if (c == 0x34) return 2;  // G key (scancode 0x34) → green
  }
  return 0;
}

// ═══════════════════════════════════════════════════════════════════════════
//  GAME LOGIC FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

void direction_converter(int dir, int* dx, int* dy) {
  *dx = 0;
  *dy = 0;
  switch (dir) {
    case 0:
      *dy = -1;
      break;
    case 1:
      *dx = 1;
      break;
    case 2:
      *dy = 1;
      break;
    case 3:
      *dx = -1;
      break;
  }
}

int in_bounds(int x, int y) {
  return ((unsigned)x < SCREEN_WIDTH) && ((unsigned)y < SCREEN_HEIGHT);
}

int dirs_are_opposite(int a, int b) {
  // avoid turning 180 degrees, illegal move basically
  if ((a == 0 && b == 2) || (a == 2 && b == 0)) return 1;
  if ((a == 3 && b == 1) || (a == 1 && b == 3)) return 1;
  return 0;
}

void compute_direction(Player* p, int newdir) {
  if (!p->alive) return;                          // dead player cannot input
  if (dirs_are_opposite(p->dir, newdir)) return;  // check if illegal move
  p->dir = newdir;                                // then compute new direction
}

void compute_next_location(const Player* p, int* nx, int* ny) {  // calculates the next position
  int dx, dy;
  direction_converter(p->dir, &dx, &dy);
  *nx = p->x + dx;
  *ny = p->y + dy;
}

int check_collision(int alive, int nx, int ny, int ox, int oy, int other_alive) {
  int dy, dx, tx, ty;
  if (!alive) return 0;
  // Check every cell in the 5x5 footprint against walls and trails
  for (dy = -PLAYER_HEAD; dy <= PLAYER_HEAD; dy++)
    for (dx = -PLAYER_HEAD; dx <= PLAYER_HEAD; dx++) {
      tx = nx + dx;
      ty = ny + dy;
      if (!in_bounds(tx, ty)) return 1;
      if (occupied[ty][tx]) return 1;
    }
  // Head-on check: two 5×5 squares overlapping
  if (other_alive) {
    int ddx = nx - ox;
    if (ddx < 0) ddx = -ddx;
    int ddy = ny - oy;
    if (ddy < 0) ddy = -ddy;
    if (ddx <= 2 * PLAYER_HEAD && ddy <= 2 * PLAYER_HEAD) return 1;
    if (ddx < 5 && ddy < 5) return 1;
  }
  return 0;
}

int trail_footprint_clear(int cx, int cy, const uint8_t occ[SCREEN_HEIGHT][SCREEN_WIDTH]) {
  // Returns 1 if the 5×5 footprint centred on (cx,cy) is fully clear
  // in the occ[][] grid (used by the AI to check safe moves)
  int dy, dx, tx, ty;
  for (dy = -PLAYER_HEAD; dy <= PLAYER_HEAD; dy++)
    for (dx = -PLAYER_HEAD; dx <= PLAYER_HEAD; dx++) {
      tx = cx + dx;
      ty = cy + dy;
      if (!in_bounds(tx, ty)) return 0;
      if (occ[ty][tx]) return 0;
    }
  return 1;
}

void avoid_self_collision(int ox, int oy, int nx, int ny, uint16_t color) {
  // avoids marking head as occupied to avoid self collision
  int dy, dx, tx, ty;
  for (dy = -PLAYER_HEAD; dy <= PLAYER_HEAD; dy++)
    for (dx = -PLAYER_HEAD; dx <= PLAYER_HEAD; dx++) {
      tx = ox + dx;
      ty = oy + dy;
      if (!in_bounds(tx, ty)) continue;
      if (tx >= nx - PLAYER_HEAD && tx <= nx + PLAYER_HEAD && ty >= ny - PLAYER_HEAD &&
          ty <= ny + PLAYER_HEAD)
        continue;
      occupied[ty][tx] = 1;
      trail_color[ty][tx] = color;  // Store the trail color
    }
}

void restore_game_screen(Player *p1, Player *p2, int p1_score, int p2_score) {
    // Redraw everything from scratch using current game state
    clear_screen();
    
    // Redraw the border
    int x, y;
    for (y = 0; y < SCREEN_HEIGHT; y++) {
        for (x = 0; x < SCREEN_WIDTH; x++) {
            int border = (x < BORDER_THICK) || (x >= SCREEN_WIDTH - BORDER_THICK) ||
                        (y < BORDER_THICK) || (y >= SCREEN_HEIGHT - BORDER_THICK);
            if (border) {
                draw_pixel(x, y, COLOR_CYAN);
            }
        }
    }
    // Redraw obstacles as is
    for (y = 0; y < SCREEN_HEIGHT; y++) {
        for (x = 0; x < SCREEN_WIDTH; x++) {
            if (occupied[y][x] && obstacle_color[y][x] != 0) {
                draw_pixel(x, y, obstacle_color[y][x]);
            }
        }
    }
    
    // Redraw trails as single pixels (1x1)
    for (y = 0; y < SCREEN_HEIGHT; y++) {
        for (x = 0; x < SCREEN_WIDTH; x++) {
            if (occupied[y][x] && trail_color[y][x] != 0) {
                draw_pixel(x, y, trail_color[y][x]);
            }
        }
    }
    
    // Redraw player positions with correct colors based on PLAYER_COLOR
    if (MODE == 1 && PLAYER_COLOR == 2) {
        // Player chose green: p1 (human) gets green, p2 (AI) gets blue
        draw_trail(p1->x, p1->y, COLOR_GREEN, COLOR_GREEN_GLOW);
        draw_trail(p2->x, p2->y, COLOR_BLUE, COLOR_BLUE_GLOW);
        draw_player_face(p1->x, p1->y, COLOR_GREEN);
        draw_player_face(p2->x, p2->y, COLOR_BLUE);
    } else {
        // Default: p1 blue, p2 green
        draw_trail(p1->x, p1->y, COLOR_BLUE, COLOR_BLUE_GLOW);
        draw_trail(p2->x, p2->y, COLOR_GREEN, COLOR_GREEN_GLOW);
        draw_player_face(p1->x, p1->y, COLOR_BLUE);
        draw_player_face(p2->x, p2->y, COLOR_GREEN);
    }
    
    // Redraw score
    draw_score(p1_score, p2_score);
}

// ═══════════════════════════════════════════════════════════════════════════
//  AI FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

int ai_move(int nx, int ny, int facing, const uint8_t occ[SCREEN_HEIGHT][SCREEN_WIDTH]) {
  // AI checks for a position it still HASN'T moved to yet
  // but will depending on how much it can move AFTER it mvoes there
  int n = 0;
  int td[3], dx, dy, fx, fy;
  td[0] = facing;
  td[1] = (facing + 3) & 3;
  td[2] = (facing + 1) & 3;
  for (int k = 0; k < 3; k++) {
    direction_converter(td[k], &dx, &dy);
    fx = nx + dx;
    fy = ny + dy;
    if (trail_footprint_clear(fx, fy, occ)) n++;
  }
  return n;
}

void ai_advanced_movement(Player* ai, const Player* human, const uint8_t occ[SCREEN_HEIGHT][SCREEN_WIDTH]) {
  int try_direction[3];
  int best_dir, best_score, best_d1, best_d0, found;
  int pdx, pdy;
  int pred1x, pred1y, pred2x, pred2y, pred3x, pred3y;

  if (!ai->alive) return;  // if you're dead then well you're dead

  // Predict the human's next 3 positions in the current direction they're
  // heading in
  direction_converter(human->dir, &pdx, &pdy);
  pred1x = human->x + pdx;
  pred1y = human->y + pdy;
  if (!in_bounds(pred1x, pred1y)) {  // clamp if out of bounds
    pred1x = human->x;
    pred1y = human->y;
  }
  pred2x = pred1x + pdx;
  pred2y = pred1y + pdy;
  if (!in_bounds(pred2x, pred2y)) {  // clamp if out of bounds
    pred2x = pred1x;
    pred2y = pred1y;
  }
  pred3x = pred2x + pdx;
  pred3y = pred2y + pdy;
  if (!in_bounds(pred3x, pred3y)) {  // clamp if out of bounds
    pred3x = pred2x;
    pred3y = pred2y;
  }

  // Directions to try for AI (ignoring 180 degree turn)
  try_direction[0] = ai->dir;
  try_direction[1] = (ai->dir + 3) & 3;
  try_direction[2] = (ai->dir + 1) & 3;

  // initializing to very large numbers (not possible to hit in this game)
  best_score = 0x7FFFFFFF;
  best_d1 = 0x7FFF;
  best_d0 = 0x7FFF;
  best_dir = ai->dir;
  found = 0;

  for (int i = 0; i < 3; i++) {
    int dir = try_direction[i];
    int dx, dy, nx, ny, ax, ay, d0, d1, d2, d3, score;

    direction_converter(dir, &dx, &dy);
    nx = ai->x + dx;
    ny = ai->y + dy;
    if (!trail_footprint_clear(nx, ny, occ))
      continue;  // ignore this path if its occupied

    // calculating Manhattan distance algorithm

    // current distance to human
    ax = human->x - nx;
    if (ax < 0) ax = -ax;
    ay = human->y - ny;
    if (ay < 0) ay = -ay;
    d0 = ax + ay;

    // distance to human after first movement
    ax = pred1x - nx;
    if (ax < 0) ax = -ax;
    ay = pred1y - ny;
    if (ay < 0) ay = -ay;
    d1 = ax + ay;

    // distance to human after second movement
    ax = pred2x - nx;
    if (ax < 0) ax = -ax;
    ay = pred2y - ny;
    if (ay < 0) ay = -ay;
    d2 = ax + ay;

    // distance to human after third movement
    ax = pred3x - nx;
    if (ax < 0) ax = -ax;
    ay = pred3y - ny;
    if (ay < 0) ay = -ay;
    d3 = ax + ay;

    // calcualte score based on where human will be in near future
    //(not too far in future, not current either)
    score = 2 * d0 + 10 * d1 + 6 * d2 + 3 * d3;

    // Reducing human's escape options
    int human_exits = ai_move(pred1x, pred1y, human->dir, occ);
    score -= 8 * (3 - human_exits);

    if (nx == pred1x && ny == pred1y) score -= 24;
    if (nx == pred2x && ny == pred2y) score -= 12;

    {
      int exits = ai_move(
          nx, ny, dir,
          occ);  // check the number of available locations after that move
      score += 14 * (3 - exits);  // dead end moves suck
    }

    found = 1;  // we have a valid move
    // Select move with lowest score and breaks ties with closer distance
    if (score < best_score ||
        (score == best_score &&
         (d1 < best_d1 || (d1 == best_d1 && d0 < best_d0)))) {
      best_score = score;
      best_d1 = d1;
      best_d0 = d0;
      best_dir = dir;
    }
  }
  if (found) ai->dir = best_dir;
}

// ═══════════════════════════════════════════════════════════════════════════
//  ARENA SETUP
// ═══════════════════════════════════════════════════════════════════════════

void arena_place_medium_obstacles(void) {
  fill_obstacle_rectangle(144, 107, 32, 12, COLOR_CYAN);
  fill_obstacle_rectangle(89, 69, 14, 47, COLOR_CYAN);
  fill_obstacle_rectangle(217, 69, 14, 47, COLOR_CYAN);
  fill_obstacle_rectangle(124, 41, 72, 7, COLOR_CYAN);
  fill_obstacle_rectangle(124, 187, 72, 7, COLOR_CYAN);
  
  fill_obstacle_rectangle(145, 108, 30, 10, COLOR_MAGENTA);
  fill_obstacle_rectangle(90, 70, 12, 45, COLOR_MAGENTA);
  fill_obstacle_rectangle(218, 70, 12, 45, COLOR_MAGENTA);
  fill_obstacle_rectangle(125, 42, 70, 5, COLOR_MAGENTA);
  fill_obstacle_rectangle(125, 188, 70, 5, COLOR_MAGENTA);
}

void arena_place_hard_maze(void) {
  fill_obstacle_rectangle(84, 51, 57, 6, COLOR_CYAN);
  fill_obstacle_rectangle(179, 51, 57, 6, COLOR_CYAN);
  fill_obstacle_rectangle(84, 111, 57, 6, COLOR_CYAN);
  fill_obstacle_rectangle(179, 111, 57, 6, COLOR_CYAN);
  fill_obstacle_rectangle(84, 171, 57, 6, COLOR_CYAN);
  fill_obstacle_rectangle(179, 171, 57, 6, COLOR_CYAN);
  
  fill_obstacle_rectangle(157, 61, 6, 44, COLOR_CYAN);
  fill_obstacle_rectangle(97, 81, 6, 57, COLOR_CYAN);
  fill_obstacle_rectangle(217, 81, 6, 57, COLOR_CYAN);
  fill_obstacle_rectangle(127, 131, 66, 6, COLOR_CYAN);
  fill_obstacle_rectangle(129, 151, 6, 37, COLOR_CYAN);
  fill_obstacle_rectangle(185, 151, 6, 37, COLOR_CYAN);
  
  fill_obstacle_rectangle(85, 52, 55, 4, COLOR_MAGENTA);
  fill_obstacle_rectangle(180, 52, 55, 4, COLOR_MAGENTA);
  fill_obstacle_rectangle(85, 112, 55, 4, COLOR_MAGENTA);
  fill_obstacle_rectangle(180, 112, 55, 4, COLOR_MAGENTA);
  fill_obstacle_rectangle(85, 172, 55, 4, COLOR_MAGENTA);
  fill_obstacle_rectangle(180, 172, 55, 4, COLOR_MAGENTA);
  
  fill_obstacle_rectangle(158, 62, 4, 42, COLOR_MAGENTA);
  fill_obstacle_rectangle(98, 82, 4, 55, COLOR_MAGENTA);
  fill_obstacle_rectangle(218, 82, 4, 55, COLOR_MAGENTA);
  fill_obstacle_rectangle(128, 132, 64, 4, COLOR_MAGENTA);
  fill_obstacle_rectangle(130, 152, 4, 35, COLOR_MAGENTA);
  fill_obstacle_rectangle(186, 152, 4, 35, COLOR_MAGENTA);
}


void setup_round(Player* p1, Player* p2) {
  int x, y;

  // Set everything as unoccupied initially
  for (y = 0; y < SCREEN_HEIGHT; y++){
    for (x = 0; x < SCREEN_WIDTH; x++){
      occupied[y][x] = 0;
      trail_color[y][x] = 0;  // Initialize trail colors
    }
  }

  // Border occupation
  for (y = 0; y < SCREEN_HEIGHT; y++) {
    for (x = 0; x < SCREEN_WIDTH; x++) {
      int border = (x < BORDER_THICK) || (x >= SCREEN_WIDTH - BORDER_THICK) ||
                   (y < BORDER_THICK) || (y >= SCREEN_HEIGHT - BORDER_THICK);
      if (border) {
        occupied[y][x] = 1;
        trail_color[y][x] = COLOR_CYAN;
        draw_pixel(x, y, COLOR_CYAN);
      }
    }
  }

  // Mode selection occupation
  if (DIFFICULTY == 2)
    arena_place_medium_obstacles();
  else if (DIFFICULTY == 3)
    arena_place_hard_maze();

  // Player spawning
  p1->x = BORDER_THICK + 30;
  p1->y = BORDER_THICK + 40;
  p1->dir = 1;
  p1->alive = 1;

  p2->x = SCREEN_WIDTH - BORDER_THICK - 30;
  p2->y = SCREEN_HEIGHT - BORDER_THICK - 40;
  p2->dir = 3;
  p2->alive = 1;

  // Draw initial positions with proper colors based on PLAYER_COLOR for single-player mode
  if (MODE == 1 && PLAYER_COLOR == 2) {
    // Player chose green: p1 (human) gets green, p2 (AI) gets blue
    draw_trail(p1->x, p1->y, COLOR_GREEN, COLOR_GREEN_GLOW);
    draw_trail(p2->x, p2->y, COLOR_BLUE, COLOR_BLUE_GLOW);
    draw_player_face(p1->x, p1->y, COLOR_GREEN);
    draw_player_face(p2->x, p2->y, COLOR_BLUE);
  } else {
    // Default: p1 (human) blue, p2 (AI or human) green
    draw_trail(p1->x, p1->y, COLOR_BLUE, COLOR_BLUE_GLOW);
    draw_trail(p2->x, p2->y, COLOR_GREEN, COLOR_GREEN_GLOW);
    draw_player_face(p1->x, p1->y, COLOR_BLUE);
    draw_player_face(p2->x, p2->y, COLOR_GREEN);
  }
}

// ═══════════════════════════════════════════════════════════════════════════
//  SCORE FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

void draw_digit(int digit, int x, int y, uint16_t color) {
  const uint8_t digits[4][7] = {
      {0x1F, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1F},  // 0
      {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},  // 1
      {0x1F, 0x01, 0x01, 0x1F, 0x10, 0x10, 0x1F},  // 2
      {0x1F, 0x01, 0x01, 0x1F, 0x01, 0x01, 0x1F},  // 3
  };
  int row, col;
  for (row = 0; row < 7; row++) {
    uint8_t row_data = digits[digit][row];
    for (col = 0; col < 5; col++)
      if (row_data & (1 << (4 - col))) draw_pixel(x + col, y + row, color);
  }
}

void draw_score(int p1_score, int p2_score) {
  int start_x =
      SCREEN_WIDTH -
      70;  // Position score in top right corner (70 pixels from right edge)
  int start_y = 8;      // 8 pixels from top
  int digit_width = 6;  // 5 pixels for digit + 1 pixel space
  int separator_x;

  // Isolating digits to draw P1 score (blue)
  int p1_digit1 = p1_score / 10;
  int p1_digit2 = p1_score % 10;

  draw_digit(p1_digit1, start_x, start_y, COLOR_BLUE);
  draw_digit(p1_digit2, start_x + digit_width, start_y, COLOR_BLUE);

  // Draw separator "|" in white
  separator_x = start_x + (digit_width * 2) + 3;
  for (int y = 0; y < 7; y++) {
    draw_pixel(separator_x, start_y + y, COLOR_WHITE);
  }

  // Isolating digits to draw P2 score (green)
  int p2_digit1 = p2_score / 10;
  int p2_digit2 = p2_score % 10;

  draw_digit(p2_digit1, separator_x + 4, start_y, COLOR_GREEN);
  draw_digit(p2_digit2, separator_x + 4 + digit_width, start_y, COLOR_GREEN);
}

// ═══════════════════════════════════════════════════════════════════════════
//  UI / SCREEN DRAWING FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════════

void draw_background(void) {
  int x, y, t;

  // grid background — deep blue with dim grid lines and bright nodes at
  // intersections
  for (y = 0; y < SCREEN_HEIGHT; y++) {
    for (x = 0; x < SCREEN_WIDTH; x++) {
      uint16_t c = COLOR_DARK_BLUE;
      if ((x % 10 == 0) || (y % 10 == 0)) c = COLOR_VIVID_BLUE;
      if ((x % 10 == 0) && (y % 10 == 0)) c = COLOR_TEAL;
      draw_pixel(x, y, c);
    }
  }

  // screen border (cyan, triple thickness)
  for (t = 0; t < 3; t++) {
    for (x = t; x < SCREEN_WIDTH - t; x++) {
      draw_pixel(x, t, COLOR_CYAN);
      draw_pixel(x, SCREEN_HEIGHT - 1 - t, COLOR_CYAN);
    }
    for (y = t; y < SCREEN_HEIGHT - t; y++) {
      draw_pixel(t, y, COLOR_CYAN);
      draw_pixel(SCREEN_WIDTH - 1 - t, y, COLOR_CYAN);
    }
  }

  // inner border (design)
  for (x = 6; x < SCREEN_WIDTH - 6; x++) {
    draw_pixel(x, 6, COLOR_CYAN);
    draw_pixel(x, SCREEN_HEIGHT - 7, COLOR_CYAN);
  }
  for (y = 6; y < SCREEN_HEIGHT - 6; y++) {
    draw_pixel(6, y, COLOR_CYAN);
    draw_pixel(SCREEN_WIDTH - 7, y, COLOR_CYAN);
  }

  // corner L shaped design in magenta
  for (x = 0; x < 14; x++) {
    draw_pixel(10 + x, 10, COLOR_MAGENTA);
    draw_pixel(10, 10 + x, COLOR_MAGENTA);
    draw_pixel(SCREEN_WIDTH - 11 - x, 10, COLOR_MAGENTA);
    draw_pixel(SCREEN_WIDTH - 11, 10 + x, COLOR_MAGENTA);
    draw_pixel(10 + x, SCREEN_HEIGHT - 11, COLOR_MAGENTA);
    draw_pixel(10, SCREEN_HEIGHT - 11 - x, COLOR_MAGENTA);
    draw_pixel(SCREEN_WIDTH - 11 - x, SCREEN_HEIGHT - 11, COLOR_MAGENTA);
    draw_pixel(SCREEN_WIDTH - 11, SCREEN_HEIGHT - 11 - x, COLOR_MAGENTA);
  }
}

void draw_title_screen(void) {
  int center_x = SCREEN_WIDTH / 2;
  int center_y = SCREEN_HEIGHT / 2;
  int x;
  int text_y = center_y - 38;

  draw_background();

  // Player coloured lines on sides and the tiny shadow beneath them
  for (x = 12; x < 52; x++) {
    draw_pixel(x, text_y + 3, COLOR_BLUE);
    draw_pixel(x, text_y + 4, COLOR_MIDNIGHT);
  }
  for (x = SCREEN_WIDTH - 52; x < SCREEN_WIDTH - 12; x++) {
    draw_pixel(x, text_y + 3, COLOR_GREEN);
    draw_pixel(x, text_y + 4, COLOR_MIDNIGHT);
  }

  // TRON title 3× scaled, one colour per letter with a shadow
  {
    int title_w = 4 * 6 * 3 - 3;
    int title_x = center_x - title_w / 2;
    int title_y = center_y - 46;

    draw_string_scaled("TRON", title_x + 2, title_y + 2, COLOR_MIDNIGHT, 3);
    draw_string_scaled("T", title_x, title_y, COLOR_CYAN, 3);
    draw_string_scaled("R", title_x + 18, title_y, COLOR_MAGENTA, 3);
    draw_string_scaled("O", title_x + 36, title_y, COLOR_GREEN, 3);
    draw_string_scaled("N", title_x + 54, title_y, COLOR_BLUE, 3);

    // title underline: cyan + magenta double rule
    for (x = title_x - 11; x <= title_x + title_w + 11; x++) {
      draw_pixel(x, title_y + 23, COLOR_CYAN);
      draw_pixel(x, title_y + 24, COLOR_MAGENTA);
    }
  }

  //"PRESS SPACE" in white
  {
    const char* line1 = "PRESS SPACE";
    draw_string(line1, center_x - (str_len(line1) * 6 - 1) / 2, center_y + 20,
                COLOR_WHITE);
  }

  //"TO START" in cyan
  {
    const char* sub = "TO START";
    draw_string(sub, center_x - (str_len(sub) * 6 - 1) / 2, center_y + 30,
                COLOR_CYAN);
  }
}

void draw_mode_selection_screen(void) {
  int x, y, t;
  int center_x = SCREEN_WIDTH / 2;

  // Background
  for (y = 0; y < SCREEN_HEIGHT; y++) {
    for (x = 0; x < SCREEN_WIDTH; x++) {
      uint16_t c = COLOR_DARK_BLUE;
      if ((x % 10 == 0) || (y % 10 == 0)) c = COLOR_VIVID_BLUE;
      if ((x % 10 == 0) && (y % 10 == 0)) c = COLOR_TEAL;
      draw_pixel(x, y, c);
    }
  }

  // Screen border
  for (t = 0; t < 3; t++) {
    for (x = t; x < SCREEN_WIDTH - t; x++) {
      draw_pixel(x, t, COLOR_CYAN);
      draw_pixel(x, SCREEN_HEIGHT - 1 - t, COLOR_CYAN);
    }
    for (y = t; y < SCREEN_HEIGHT - t; y++) {
      draw_pixel(t, y, COLOR_CYAN);
      draw_pixel(SCREEN_WIDTH - 1 - t, y, COLOR_CYAN);
    }
  }

  // Title "SELECT MODE" with original colors (with letter shadows)
  {
    int title_y = 30;
    // Draw each letter with its own shadow (shadow offset by 2px)
    // S
    draw_string_scaled("S", center_x - 100 + 2, title_y + 2, COLOR_MIDNIGHT, 2);
    draw_string_scaled("S", center_x - 100, title_y, COLOR_CYAN, 2);
    // E
    draw_string_scaled("E", center_x - 80 + 2, title_y + 2, COLOR_MIDNIGHT, 2);
    draw_string_scaled("E", center_x - 80, title_y, COLOR_MAGENTA, 2);
    // L
    draw_string_scaled("L", center_x - 60 + 2, title_y + 2, COLOR_MIDNIGHT, 2);
    draw_string_scaled("L", center_x - 60, title_y, COLOR_GREEN, 2);
    // E
    draw_string_scaled("E", center_x - 40 + 2, title_y + 2, COLOR_MIDNIGHT, 2);
    draw_string_scaled("E", center_x - 40, title_y, COLOR_BLUE, 2);
    // C
    draw_string_scaled("C", center_x - 20 + 2, title_y + 2, COLOR_MIDNIGHT, 2);
    draw_string_scaled("C", center_x - 20, title_y, COLOR_CYAN, 2);
    // T
    draw_string_scaled("T", center_x + 2, title_y + 2, COLOR_MIDNIGHT, 2);
    draw_string_scaled("T", center_x, title_y, COLOR_MAGENTA, 2);
    // M
    draw_string_scaled("M", center_x + 30 + 2, title_y + 2, COLOR_MIDNIGHT, 2);
    draw_string_scaled("M", center_x + 30, title_y, COLOR_GREEN, 2);
    // O
    draw_string_scaled("O", center_x + 50 + 2, title_y + 2, COLOR_MIDNIGHT, 2);
    draw_string_scaled("O", center_x + 50, title_y, COLOR_BLUE, 2);
    // D
    draw_string_scaled("D", center_x + 70 + 2, title_y + 2, COLOR_MIDNIGHT, 2);
    draw_string_scaled("D", center_x + 70, title_y, COLOR_CYAN, 2);
    // E
    draw_string_scaled("E", center_x + 90 + 2, title_y + 2, COLOR_MIDNIGHT, 2);
    draw_string_scaled("E", center_x + 90, title_y, COLOR_MAGENTA, 2);
  }

  // Single Player Rectangle (Blue)
  int rect1_x = 40;
  int rect1_y = 100;
  int rect1_w = 110;
  int rect1_h = 90;

  fill_rectangle(rect1_x - 2, rect1_y - 2, rect1_w + 4, rect1_h + 4,
                 COLOR_MIDNIGHT);
  fill_rectangle(rect1_x, rect1_y, rect1_w, rect1_h, COLOR_BLUE);

  // Border
  for (t = 0; t < 2; t++) {
    for (x = 0; x < rect1_w; x++) {
      draw_pixel(rect1_x + x, rect1_y + t, COLOR_WHITE);
      draw_pixel(rect1_x + x, rect1_y + rect1_h - 1 - t, COLOR_WHITE);
    }
    for (y = 0; y < rect1_h; y++) {
      draw_pixel(rect1_x + t, rect1_y + y, COLOR_WHITE);
      draw_pixel(rect1_x + rect1_w - 1 - t, rect1_y + y, COLOR_WHITE);
    }
  }

  // "SINGLE" text using font table scaled
  draw_string_scaled("SINGLE", rect1_x + 20, rect1_y + 20, COLOR_WHITE, 2);
  // "PLAYER" text using font table scaled
  draw_string_scaled("PLAYER", rect1_x + 20, rect1_y + 45, COLOR_WHITE, 2);
  // "PRESS S" text using font table
  draw_string("PRESS S", rect1_x + 20, rect1_y + 70, COLOR_BLUE_GLOW);

  // Multiplayer Rectangle (Green)
  int rect2_x = SCREEN_WIDTH - 150;
  int rect2_y = 100;
  int rect2_w = 110;
  int rect2_h = 90;

  fill_rectangle(rect2_x - 2, rect2_y - 2, rect2_w + 4, rect2_h + 4, COLOR_MIDNIGHT);
  fill_rectangle(rect2_x, rect2_y, rect2_w, rect2_h, COLOR_GREEN);

  // Border
  for (t = 0; t < 2; t++) {
    for (x = 0; x < rect2_w; x++) {
      draw_pixel(rect2_x + x, rect2_y + t, COLOR_WHITE);
      draw_pixel(rect2_x + x, rect2_y + rect2_h - 1 - t, COLOR_WHITE);
    }
    for (y = 0; y < rect2_h; y++) {
      draw_pixel(rect2_x + t, rect2_y + y, COLOR_WHITE);
      draw_pixel(rect2_x + rect2_w - 1 - t, rect2_y + y, COLOR_WHITE);
    }
  }

  // "MULTI" text using font table scaled
  draw_string_scaled("MULTI", rect2_x + 20, rect2_y + 20, COLOR_WHITE, 2);
  // "PLAYER" text using font table scaled
  draw_string_scaled("PLAYER", rect2_x + 20, rect2_y + 45, COLOR_WHITE, 2);
  // "PRESS M" text using font table — dark so it reads against green
  draw_string("PRESS M", rect2_x + 25, rect2_y + 70, COLOR_GREEN_GLOW);
}

void draw_difficulty_selection_screen(void) {
  int x, y, t;
  int center_x = SCREEN_WIDTH / 2;

  // Background
  for (y = 0; y < SCREEN_HEIGHT; y++) {
    for (x = 0; x < SCREEN_WIDTH; x++) {
      uint16_t c = COLOR_DARK_BLUE;
      if ((x % 10 == 0) || (y % 10 == 0)) c = COLOR_VIVID_BLUE;
      if ((x % 10 == 0) && (y % 10 == 0)) c = COLOR_TEAL;
      draw_pixel(x, y, c);
    }
  }

  // Border
  for (t = 0; t < 3; t++) {
    for (x = t; x < SCREEN_WIDTH - t; x++) {
      draw_pixel(x, t, COLOR_CYAN);
      draw_pixel(x, SCREEN_HEIGHT - 1 - t, COLOR_CYAN);
    }
    for (y = t; y < SCREEN_HEIGHT - t; y++) {
      draw_pixel(t, y, COLOR_CYAN);
      draw_pixel(SCREEN_WIDTH - 1 - t, y, COLOR_CYAN);
    }
  }

  // Title "DIFFICULTY"
  {
    int title_y = 18;
    const char* letters = "DIFFICULTY";
    uint16_t cols[10] = {COLOR_CYAN, COLOR_MAGENTA, COLOR_GREEN, COLOR_BLUE,
                         COLOR_CYAN, COLOR_MAGENTA, COLOR_GREEN, COLOR_BLUE,
                         COLOR_CYAN, COLOR_MAGENTA};
    int lx = center_x - (10 * 12) / 2;
    int i;
    char tmp[2] = {0, 0};
    for (i = 0; letters[i]; i++, lx += 20) {
      tmp[0] = letters[i];
      draw_string_scaled(tmp, lx + 2, title_y + 2, COLOR_MIDNIGHT, 2);
      draw_string_scaled(tmp, lx, title_y, cols[i], 2);
    }
  }

  /*
   * Three buttons, evenly spaced vertically.
   * Layout per button:
   *   - shadow rect (2px offset)
   *   - filled rect in difficulty colour
   *   - 2px white border
   *   - label text (difficulty name left, press hint right)
   *
   * Button dimensions and positions:
   *   Each: 240px wide × 44px tall, centred horizontally, 8px gap.
   */
  int btn_w = 240;
  int btn_h = 44;
  int btn_x = (SCREEN_WIDTH - btn_w) / 2;
  int btn_y1 = 60;
  int btn_y2 = btn_y1 + btn_h + 10;
  int btn_y3 = btn_y2 + btn_h + 10;

  int back_btn_x = 15;
  int back_btn_y = 10;
  int back_btn_w = 55;
  int back_btn_h = 25;

  uint16_t btn_colors[3] = {COLOR_GREEN, COLOR_BLUE, COLOR_MAGENTA};
  uint16_t btn_glows[3] = {COLOR_GREEN_GLOW, COLOR_BLUE_GLOW, COLOR_MAGENTA_GLOW};
  const char* btn_labels[3] = {"EASY", "MEDIUM", "HARD"};
  const char* btn_subtitles[3] = {"OPEN ARENA", "OBSTACLES", "MAZE"};
  const char* btn_hints[3] = {"PRESS 1", "PRESS 2", "PRESS 3"};
  int btn_ys[3] = {btn_y1, btn_y2, btn_y3};

  int b;
  for (b = 0; b < 3; b++) {
    int bx = btn_x, by = btn_ys[b];

    fill_rectangle(bx + 3, by + 3, btn_w, btn_h, COLOR_MIDNIGHT); //Shadow
    fill_rectangle(bx, by, btn_w, btn_h, btn_colors[b]); //Fill
    
    //White border 2px
    for (int t = 0; t < 2; t++) {
      for (int x = 0; x < btn_w; x++) {
        draw_pixel(bx + x, by + t, COLOR_WHITE);
        draw_pixel(bx + x, by + btn_h - 1 - t, COLOR_WHITE);
      }
      for (int y = 0; y < btn_h; y++) {
        draw_pixel(bx + t, by + y, COLOR_WHITE);
        draw_pixel(bx + btn_w - 1 - t, by + y, COLOR_WHITE);
      }
    }
    //Label (2× scale), subtitle (1×), hint (1×)
    draw_string_scaled(btn_labels[b], bx + 10, by + 6, btn_glows[b], 2);
    draw_string(btn_subtitles[b], bx + 10, by + 28, btn_glows[b]);
    draw_string(btn_hints[b], bx + btn_w - str_len(btn_hints[b]) * 6 - 10, by + 18, COLOR_WHITE);

    fill_rectangle(back_btn_x + 2, back_btn_y + 2, back_btn_w, back_btn_h, COLOR_MIDNIGHT);
    fill_rectangle(back_btn_x, back_btn_y, back_btn_w, back_btn_h, COLOR_GRAY);
    draw_string("BACK", back_btn_x + 10, back_btn_y + 7, COLOR_WHITE);
  }
}

void draw_block(int cx, int cy, int offset_x, int offset_y, int scale, uint16_t color) {
  int x, y;
  for (y = 0; y < scale; y++) {
    for (x = 0; x < scale; x++) {
      draw_pixel(cx + offset_x * scale + x, cy + offset_y * scale + y, color);
    }
  }
}
void draw_color_selection_screen(void) {
  int x, y, t, dx, dy;
  int center_x = SCREEN_WIDTH / 2;

  // Background
  for (y = 0; y < SCREEN_HEIGHT; y++) {
    for (x = 0; x < SCREEN_WIDTH; x++) {
      uint16_t c = COLOR_DARK_BLUE;
      if ((x % 10 == 0) || (y % 10 == 0)) c = COLOR_VIVID_BLUE;
      if ((x % 10 == 0) && (y % 10 == 0)) c = COLOR_TEAL;
      draw_pixel(x, y, c);
    }
  }

  // Border
  for (t = 0; t < 3; t++) {
    for (x = t; x < SCREEN_WIDTH - t; x++) {
      draw_pixel(x, t, COLOR_CYAN);
      draw_pixel(x, SCREEN_HEIGHT - 1 - t, COLOR_CYAN);
    }
    for (y = t; y < SCREEN_HEIGHT - t; y++) {
      draw_pixel(t, y, COLOR_CYAN);
      draw_pixel(SCREEN_WIDTH - 1 - t, y, COLOR_CYAN);
    }
  }

  // Title
  {
    int title_y = 15;
    const char* title = "PICK YOUR COLOR";
    int title_width = str_len(title) * 6;
    int title_x = center_x - (title_width / 2);
    draw_string(title, title_x, title_y, COLOR_WHITE);

    for (x = title_x - 5; x <= title_x + title_width + 5; x++) {
      draw_pixel(x, title_y + 10, COLOR_CYAN);
      draw_pixel(x, title_y + 11, COLOR_MAGENTA);
    }
  }

  int panel_w = 140, panel_h = 180, spacing = 20;
  int panel1_x = center_x - ((panel_w * 2 + spacing) / 2);
  int panel2_x = panel1_x + panel_w + spacing;
  int panel_y = 50;

  //BLUE PANEL
  {
    fill_rectangle(panel1_x + 3, panel_y + 3, panel_w, panel_h, COLOR_MIDNIGHT);
    fill_rectangle(panel1_x, panel_y, panel_w, panel_h, COLOR_BLUE);

    for (t = 0; t < 2; t++) {
      for (x = 0; x < panel_w; x++) {
        draw_pixel(panel1_x + x, panel_y + t, COLOR_WHITE);
        draw_pixel(panel1_x + x, panel_y + panel_h - 1 - t, COLOR_WHITE);
      }
      for (y = 0; y < panel_h; y++) {
        draw_pixel(panel1_x + t, panel_y + y, COLOR_WHITE);
        draw_pixel(panel1_x + panel_w - 1 - t, panel_y + y, COLOR_WHITE);
      }
    }

    int sw_cx = panel1_x + panel_w / 2;
    int sw_cy = panel_y + 50;
    int s = 10;

    // Trail (scaled)
    for (dy = -10; dy <= 10; dy++)
      for (dx = -10; dx <= 10; dx++)
        draw_pixel(sw_cx + dx, sw_cy + dy, COLOR_BLUE);

    // Glow
    for (dy = -60; dy <= 60; dy++) {
      for (dx = -60; dx <= 60; dx++) {
        if ((dx < -30 || dx > 30 || dy < -30 || dy > 30) &&
            (dx >= -40 && dx <= 40 && dy >= -40 && dy <= 40)) {
          draw_pixel(sw_cx + dx, sw_cy + dy, COLOR_BLUE_GLOW);
        }
      }
    }

    int face_cx = sw_cx - 5;

    // Face
    // Eyes
    draw_block(face_cx, sw_cy, -1, -1, s, COLOR_WHITE);
    draw_block(face_cx, sw_cy,  1, -1, s, COLOR_WHITE);

    // Pupils
    draw_block(face_cx, sw_cy, -1, -2, s, COLOR_BLACK);
    draw_block(face_cx, sw_cy,  1, -2, s, COLOR_BLACK);

    // Smile
    draw_block(face_cx, sw_cy, -1,  1, s, COLOR_CYAN);
    draw_block(face_cx, sw_cy,  0,  2, s, COLOR_CYAN);
    draw_block(face_cx, sw_cy,  1,  1, s, COLOR_CYAN);

    int tc = panel1_x + panel_w / 2;
    draw_string_scaled("BLUE", tc - 20, panel_y + 105, COLOR_WHITE, 2);
    draw_string("YOU PLAY AS", tc - 40, panel_y + 135, COLOR_WHITE);
    draw_string("BLUE TRAIL", tc - 35, panel_y + 147, COLOR_BLUE_GLOW);
    draw_string("AI IS GREEN", tc - 38, panel_y + 159, COLOR_GREEN_GLOW);
  }

  //GREEN
  {
    fill_rectangle(panel2_x + 3, panel_y + 3, panel_w, panel_h, COLOR_MIDNIGHT);
    fill_rectangle(panel2_x, panel_y, panel_w, panel_h, COLOR_GREEN);

    for (t = 0; t < 2; t++) {
      for (x = 0; x < panel_w; x++) {
        draw_pixel(panel2_x + x, panel_y + t, COLOR_WHITE);
        draw_pixel(panel2_x + x, panel_y + panel_h - 1 - t, COLOR_WHITE);
      }
      for (y = 0; y < panel_h; y++) {
        draw_pixel(panel2_x + t, panel_y + y, COLOR_WHITE);
        draw_pixel(panel2_x + panel_w - 1 - t, panel_y + y, COLOR_WHITE);
      }
    }

    int sw_cx = panel2_x + panel_w / 2;
    int sw_cy = panel_y + 50;
    int s = 10;

    // Trail
    for (dy = -10; dy <= 10; dy++)
      for (dx = -10; dx <= 10; dx++)
        draw_pixel(sw_cx + dx, sw_cy + dy, COLOR_GREEN);

    // Glow
    for (dy = -60; dy <= 60; dy++) {
      for (dx = -60; dx <= 60; dx++) {
        if ((dx < -30 || dx > 30 || dy < -30 || dy > 30) &&
            (dx >= -40 && dx <= 40 && dy >= -40 && dy <= 40)) {
          draw_pixel(sw_cx + dx, sw_cy + dy, COLOR_GREEN_GLOW);
        }
      }
    }

    int face_cx = sw_cx - 5;

    // Face
    // Eyes
    draw_block(face_cx, sw_cy, -1, -1, s, COLOR_WHITE);
    draw_block(face_cx, sw_cy,  1, -1, s, COLOR_WHITE);

    // Pupils
    draw_block(face_cx, sw_cy, -1, -2, s, COLOR_BLACK);
    draw_block(face_cx, sw_cy,  1, -2, s, COLOR_BLACK);

    // Smile
    draw_block(face_cx, sw_cy, -1,  1, s, COLOR_MAGENTA);
    draw_block(face_cx, sw_cy,  0,  2, s, COLOR_MAGENTA);
    draw_block(face_cx, sw_cy,  1,  1, s, COLOR_MAGENTA);

    int tc = panel2_x + panel_w / 2;
    draw_string_scaled("GREEN", tc - 25, panel_y + 105, COLOR_WHITE, 2);
    draw_string("YOU PLAY AS", tc - 40, panel_y + 135, COLOR_WHITE);
    draw_string("GREEN TRAIL", tc - 38, panel_y + 147, COLOR_GREEN_GLOW);
    draw_string("AI IS BLUE", tc - 35, panel_y + 159, COLOR_BLUE_GLOW);
  }

  // BACK button
  fill_rectangle(17, 12, 55, 25, COLOR_MIDNIGHT);
  fill_rectangle(15, 10, 55, 25, COLOR_GRAY);
  draw_string("BACK", 25, 17, COLOR_WHITE);
}

void draw_game_over_screen(int p1_score, int p2_score) {
  int center_x = SCREEN_WIDTH / 2;
  int center_y = SCREEN_HEIGHT / 2;
  int x;
  char p1_str[3], p2_str[3];

  draw_background();

  // Dark center rectangular panel
  {
    int panel_x = 28, panel_y = center_y - 60;
    int panel_w = SCREEN_WIDTH - 56, panel_h = 130;
    int bx, by;

    fill_rectangle(panel_x + 3, panel_y + 3, panel_w, panel_h, COLOR_MIDNIGHT);
    fill_rectangle(panel_x, panel_y, panel_w, panel_h, COLOR_DARK_BLUE);

    for (bx = 0; bx < panel_w; bx++) {
      draw_pixel(panel_x + bx, panel_y, COLOR_WHITE);
      draw_pixel(panel_x + bx, panel_y + 1, COLOR_WHITE);
      draw_pixel(panel_x + bx, panel_y + panel_h - 1, COLOR_WHITE);
      draw_pixel(panel_x + bx, panel_y + panel_h - 2, COLOR_WHITE);
    }
    for (by = 0; by < panel_h; by++) {
      draw_pixel(panel_x, panel_y + by, COLOR_WHITE);
      draw_pixel(panel_x + 1, panel_y + by, COLOR_WHITE);
      draw_pixel(panel_x + panel_w - 1, panel_y + by, COLOR_WHITE);
      draw_pixel(panel_x + panel_w - 2, panel_y + by, COLOR_WHITE);
    }
  }

  // GAME OVER — 3× scaled
  {
    int title_y = center_y - 52;
    int lx = center_x - 4 * 18;

    draw_string_scaled("GAME OVER", lx + 2, title_y + 2, COLOR_MIDNIGHT, 3);
    draw_string_scaled("G", lx, title_y, COLOR_GREEN, 3);
    draw_string_scaled("A", lx + 18, title_y, COLOR_MAGENTA, 3);
    draw_string_scaled("M", lx + 36, title_y, COLOR_BLUE, 3);
    draw_string_scaled("E", lx + 54, title_y, COLOR_CYAN, 3);
    draw_string_scaled("O", lx + 90, title_y, COLOR_CYAN, 3);
    draw_string_scaled("V", lx + 108, title_y, COLOR_MAGENTA, 3);
    draw_string_scaled("E", lx + 126, title_y, COLOR_GREEN, 3);
    draw_string_scaled("R", lx + 144, title_y, COLOR_BLUE, 3);

    for (x = lx - 8; x <= lx + 162; x++) {
      draw_pixel(x, title_y + 23, COLOR_CYAN);
      draw_pixel(x, title_y + 24, COLOR_MAGENTA);
    }
  }

  // Score display
  {
    int sy = center_y - 12;
    p1_str[0] = '0' + (p1_score / 10);
    p1_str[1] = '0' + (p1_score % 10);
    p1_str[2] = '\0';
    p2_str[0] = '0' + (p2_score / 10);
    p2_str[1] = '0' + (p2_score % 10);
    p2_str[2] = '\0';
    draw_string("P1", center_x - 40, sy, COLOR_BLUE);
    draw_string(p1_str, center_x - 26, sy, COLOR_BLUE);
    draw_string("-", center_x - 6, sy, COLOR_WHITE);
    draw_string("P2", center_x + 6, sy, COLOR_GREEN);
    draw_string(p2_str, center_x + 20, sy, COLOR_GREEN);
  }

  // Winner announcement
  {
    const char* msg;
    uint16_t col;
    int ay = center_y + 8;
    if (p1_score > p2_score) {
      msg = "BLUE WINS!";
      col = COLOR_BLUE;
    } else if (p2_score > p1_score) {
      msg = "GREEN WINS!";
      col = COLOR_GREEN;
    } else {
      msg = "DRAW!";
      col = COLOR_WHITE;
    }
    draw_string(msg, center_x - (str_len(msg) * 6 - 1) / 2 + 1, ay + 1,
                COLOR_MIDNIGHT);
    draw_string(msg, center_x - (str_len(msg) * 6 - 1) / 2, ay, col);
  }

  // Restart prompt (clickable)
  {
    const char* restart = "PRESS SPACE TO RESTART";
    draw_string(restart, center_x - (str_len(restart) * 6 - 1) / 2,
                center_y + 34, COLOR_CYAN);
  }
}

void draw_paused_banner(void) {
    // Only draw the banner overlay if we haven't drawn it yet this pause session
    if (!pause_banner_drawn) {
        int cx = SCREEN_WIDTH / 2;
        int cy = SCREEN_HEIGHT / 2;
        
        const char* line1 = "PAUSED";
        const char* line2 = "PRESS SPACE TO RESUME";
        
        int panel_w = 220;
        int panel_h = 70;
        
        int x0 = cx - panel_w / 2;
        int y0 = cy - panel_h / 2;
        
        // Save the current screen content before drawing the banner
        // We'll store a small region around the banner area
        static uint16_t saved_screen[70][220];  // Save the banner area
        int x, y;
        
        // Save the area where the banner will be drawn
        for (y = 0; y < panel_h + 4; y++) {
            for (x = 0; x < panel_w + 4; x++) {
                if (x0 + x < SCREEN_WIDTH && y0 + y < SCREEN_HEIGHT) {
                    saved_screen[y][x] = read_pixel(x0 + x, y0 + y);
                }
            }
        }
        
        // Dim the entire screen
        for (y = 0; y < SCREEN_HEIGHT; y++) {
            for (x = 0; x < SCREEN_WIDTH; x++) {
                uint16_t orig_color = read_pixel(x, y);
                // Dim the original color
                uint16_t dimmed = ((orig_color & 0xF7DE) >> 1);
                draw_pixel(x, y, dimmed);
            }
        }
        
        // outer glow border
        fill_rectangle(x0 - 2, y0 - 2, panel_w + 4, panel_h + 4, COLOR_CYAN);
        
        // inner border
        fill_rectangle(x0 - 1, y0 - 1, panel_w + 2, panel_h + 2, COLOR_BLUE);
        
        // main panel
        fill_rectangle(x0, y0, panel_w, panel_h, COLOR_MIDNIGHT);
        
        // title
        draw_string(line1,
                    cx - (str_len(line1) * 6 - 1) / 2,
                    y0 + 14,
                    COLOR_CYAN);
        
        // subtitle
        draw_string(line2,
                    cx - (str_len(line2) * 6 - 1) / 2,
                    y0 + 36,
                    COLOR_WHITE);
        
        pause_banner_drawn = 1;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  MAIN
// ═══════════════════════════════════════════════════════════════════════════

int main(void) {
 pixel_buffer_start = (int)MMIO32(VGA_PIXEL_CTRL_BASE);

 mouse_init();
 keyboard_init();

setup_ps2_interrupt();

 Player p1, p2;
 int p1_score = 0;
 int p2_score = 0;
 int game_active = 0;
 int game_paused = 0;
 int waiting_for_start = 1;
 int choosing_mode = 0;
 int mode_redraw = 1;
 int choosing_diff = 0;
 int diff_redraw = 1;
 int choosing_color = 0;      // NEW: color selection step (single-player only)
 int color_redraw = 1;        // NEW: flag to (re)draw the color screen once

 int prev_cx = start_mouse.x, prev_cy = start_mouse.y;

 draw_title_screen();
 play_start_screen_beep();
 wait_for_video_frame();

 while (1) {
   // Title screen
   if (waiting_for_start) {
     mouse_ignore();

     if (check_space_bar()) {
       waiting_for_start = 0;
       choosing_mode = 1;
       mode_redraw = 1;
     }
     wait_for_video_frame();
     continue;
   }

   // Difficulty selection
   if (choosing_diff) {
     if (diff_redraw) {
       mouse_ignore();
       draw_difficulty_selection_screen();
       prev_cx = start_mouse.x;
       prev_cy = start_mouse.y;
       draw_cursor(prev_cx, prev_cy);
       diff_redraw = 0;
     }

     mouse_poll();

     if (start_mouse.x != prev_cx || start_mouse.y != prev_cy) {
       erase_cursor(prev_cx, prev_cy);
       prev_cx = start_mouse.x;
       prev_cy = start_mouse.y;
       draw_cursor(prev_cx, prev_cy);
     }

    int btn_w = 240, btn_h = 44;
    int btn_x = (SCREEN_WIDTH - btn_w) / 2;
    int btn_y1 = 60, btn_y2 = btn_y1 + btn_h + 10, btn_y3 = btn_y2 + btn_h + 10;
    int dpick = poll_difficulty_selection();

     if (start_mouse.click) {
      if (mouse_within_button(btn_x, btn_y1, btn_w, btn_h))
        dpick = 1;
      else if (mouse_within_button(btn_x, btn_y2, btn_w, btn_h))
        dpick = 2;
      else if (mouse_within_button(btn_x, btn_y3, btn_w, btn_h))
        dpick = 3;
    }

    int back_btn_x = 15;
    int back_btn_y = 10;
    int back_btn_w = 55;
    int back_btn_h = 25;

    if (start_mouse.click && mouse_within_button(back_btn_x, back_btn_y, back_btn_w, back_btn_h)) {
        choosing_diff = 0;
        // NEW: go back to color selection if single-player, mode selection if multiplayer
        if (MODE == 1) {
          choosing_color = 1;
          color_redraw = 1;
        } else {
          choosing_mode = 1;
          mode_redraw = 1;
        }
        diff_redraw = 1;
    }

    int back_key = 0;
    int c;
    while ((c = keyboard_input(PS2_BASE_PRIMARY)) >= 0) {
        if (c == 0x32) {  //'B' key scancode — reused as back only in multiplayer
            // In single-player, 0x32 means "pick blue" on color screen, not back.
            // Here on the diff screen there's no ambiguity; treat as back.
            back_key = 1;
        }
    }
    if (back_key) {
        choosing_diff = 0;
        // NEW: go back to color selection if single-player, mode selection if multiplayer
        if (MODE == 1) {
          choosing_color = 1;
          color_redraw = 1;
        } else {
          choosing_mode = 1;
          mode_redraw = 1;
        }
        diff_redraw = 1;
    }

     if (dpick >= 1 && dpick <= 3) {
      DIFFICULTY = dpick;
      choosing_diff = 0;
      game_active = 1;
      game_paused = 0;
      clear_screen();
      setup_round(&p1, &p2);
      draw_score(p1_score, p2_score);
     }

     wait_for_video_frame();
     continue;
   }

   // NEW: Color selection screen (single-player only, shown before difficulty)
   if (choosing_color) {
     if (color_redraw) {
       mouse_ignore();
       draw_color_selection_screen();
       prev_cx = start_mouse.x;
       prev_cy = start_mouse.y;
       draw_cursor(prev_cx, prev_cy);
       color_redraw = 0;
     }

     mouse_poll();

     if (start_mouse.x != prev_cx || start_mouse.y != prev_cy) {
       erase_cursor(prev_cx, prev_cy);
       prev_cx = start_mouse.x;
       prev_cy = start_mouse.y;
       draw_cursor(prev_cx, prev_cy);
     }

     // Panel hit areas — must match draw_color_selection_screen() layout
     int rect1_x = 30,  rect1_y = 50, rect1_w = 120, rect1_h = 150;
     int rect2_x = SCREEN_WIDTH - 30 - 120, rect2_y = 50, rect2_w = 120, rect2_h = 150;
     int back_btn_x = 15, back_btn_y = 10, back_btn_w = 55, back_btn_h = 25;

     int cpick = poll_color_selection();  // B key → 1, G key → 2

     // Mouse click on blue panel
     if (start_mouse.click && mouse_within_button(rect1_x, rect1_y, rect1_w, rect1_h))
       cpick = 1;
     // Mouse click on green panel
     if (start_mouse.click && mouse_within_button(rect2_x, rect2_y, rect2_w, rect2_h))
       cpick = 2;

     // BACK button → return to mode selection
     if (start_mouse.click && mouse_within_button(back_btn_x, back_btn_y, back_btn_w, back_btn_h)) {
       choosing_color = 0;
       choosing_mode = 1;
       mode_redraw = 1;
       color_redraw = 1;
     }

     if (cpick == 1 || cpick == 2) {
       PLAYER_COLOR = cpick;   // 1 = player blue / AI green, 2 = player green / AI blue
       choosing_color = 0;
       choosing_diff = 1;
       diff_redraw = 1;
     }

     wait_for_video_frame();
     continue;
   }

   // Mode selection
   if (choosing_mode) {
     if (mode_redraw) {
       mouse_ignore();
       draw_mode_selection_screen();
       prev_cx = start_mouse.x;
       prev_cy = start_mouse.y;
       draw_cursor(prev_cx, prev_cy);
       mode_redraw = 0;
     }

     mouse_poll();

     if (start_mouse.x != prev_cx || start_mouse.y != prev_cy) {
       erase_cursor(prev_cx, prev_cy);
       prev_cx = start_mouse.x;
       prev_cy = start_mouse.y;
       draw_cursor(prev_cx, prev_cy);
     }

     int rect1_x = 40, rect1_y = 100, rect1_w = 110, rect1_h = 90;
     int rect2_x = SCREEN_WIDTH - 150, rect2_y = 100, rect2_w = 110,
         rect2_h = 90;
     int pick = poll_mode_selection();

     if (start_mouse.click) {
       if (mouse_within_button(rect1_x, rect1_y, rect1_w, rect1_h))
         pick = 1;
       else if (mouse_within_button(rect2_x, rect2_y, rect2_w, rect2_h))
         pick = 2;
     }

     if (pick == 1) {
       MODE = 1;
       choosing_mode = 0;
       // NEW: single-player goes to color selection first, then difficulty
       choosing_color = 1;
       color_redraw = 1;
     } else if (pick == 2) {
       MODE = 2;
       choosing_mode = 0;
       choosing_diff = 1;
       diff_redraw = 1;
     }

     wait_for_video_frame();
     continue;
   }

   // Gameplay
   if (game_active) {
     mouse_ignore();
     pause_toggle_request = 0;

     if (quit_request) {
       quit_request = 0;
        game_active = 0;
        PAUSED = 0;
        waiting_for_start = 1;
        p1_score = 0;
        p2_score = 0;
        clear_screen();
        draw_title_screen();
        play_start_screen_beep();
        wait_for_video_frame();
        continue;
   }
   
     if (MODE == 1) {
       int poll;
       // NEW: route keyboard to the correct player struct based on chosen color.
       // p1 is always the human-controlled player; p2 is always the AI.
       // setup_round() always spawns p1 top-left (blue slot) and p2 bottom-right
       // (green slot).  When the player chose green (PLAYER_COLOR == 2) we still
       // pass &p1 to read_input_p1_only so WASD controls the human, and we pass
       // &p2 to ai_advanced_movement so the AI controls the other bike.
       // The visual color swap is handled in the game-loop draw calls below.
       while ((poll = keyboard_input(PS2_BASE_PRIMARY)) >= 0)
         read_input_p1_only(&p1, (uint8_t)poll);
       ai_advanced_movement(&p2, &p1, (const uint8_t(*)[SCREEN_WIDTH])occupied);
     } else {
       poll_ps2_keyboard(&p1, &p2);
     }

     if (pause_toggle_request) {
       pause_toggle_request = 0;
       
       if (!PAUSED) {
         // We are entering pause state
         PAUSED = 1;
         pause_banner_drawn = 0;  // Reset banner flag for this pause session
         draw_paused_banner();
         play_pause_sound();
       } else {
         // We are exiting pause state
         PAUSED = 0;
         pause_banner_drawn = 0;  // Reset for next time
         // Restore the game screen without resetting positions
         restore_game_screen(&p1, &p2, p1_score, p2_score);
         play_unpause_sound();
       }
     }

     if (PAUSED) {
       sound_play();
       wait_for_video_frame();
       continue;
     }

     // Game logic continues only when NOT paused
     int nx1 = p1.x, ny1 = p1.y;
     int nx2 = p2.x, ny2 = p2.y;

     if (p1.alive) compute_next_location(&p1, &nx1, &ny1);
     if (p2.alive) compute_next_location(&p2, &nx2, &ny2);

     int col1 = check_collision(p1.alive, nx1, ny1, p2.x, p2.y, p2.alive);
     int col2 = check_collision(p2.alive, nx2, ny2, p1.x, p1.y, p1.alive);

     if (col1 || col2) {
       play_collision_sound();
       if (col1 && !col2)
         p2_score++;
       else if (!col1 && col2)
         p1_score++;

       if (p1_score >= 3 || p2_score >= 3) {
         game_active = 0;
         PAUSED = 0;
         draw_game_over_screen(p1_score, p2_score);
         play_game_over_sound();
         wait_for_video_frame();
         continue;
       }

       clear_screen();
       PAUSED = 0;
       setup_round(&p1, &p2);
       draw_score(p1_score, p2_score);
       wait_for_video_frame();
       continue;
     }

     // NEW: draw each bike in the colour the player actually chose.
     // PLAYER_COLOR 1 → p1 = blue (human), p2 = green (AI)  [default, unchanged]
     // PLAYER_COLOR 2 → p1 = green (human), p2 = blue (AI)  [swapped visuals]
     if (MODE == 1 && PLAYER_COLOR == 2) {
       // Player chose green; p1 is human but rendered green, p2 is AI rendered blue
       if (p1.alive) {
         avoid_self_collision(p1.x, p1.y, nx1, ny1, COLOR_GREEN_GLOW);
         p1.x = nx1;
         p1.y = ny1;
         draw_trail(p1.x, p1.y, COLOR_GREEN, COLOR_GREEN_GLOW);
         draw_player_face(p1.x, p1.y, COLOR_GREEN);
       }
       if (p2.alive) {
         avoid_self_collision(p2.x, p2.y, nx2, ny2, COLOR_BLUE_GLOW);
         p2.x = nx2;
         p2.y = ny2;
         draw_trail(p2.x, p2.y, COLOR_BLUE, COLOR_BLUE_GLOW);
         draw_player_face(p2.x, p2.y, COLOR_BLUE);
       }
     } else {
       // Default rendering: p1 = blue, p2 = green
       if (p1.alive) {
         avoid_self_collision(p1.x, p1.y, nx1, ny1, COLOR_BLUE_GLOW);
         p1.x = nx1;
         p1.y = ny1;
         draw_trail(p1.x, p1.y, COLOR_BLUE, COLOR_BLUE_GLOW);
         draw_player_face(p1.x, p1.y, COLOR_BLUE);
       }
       if (p2.alive) {
         avoid_self_collision(p2.x, p2.y, nx2, ny2, COLOR_GREEN_GLOW);
         p2.x = nx2;
         p2.y = ny2;
         draw_trail(p2.x, p2.y, COLOR_GREEN, COLOR_GREEN_GLOW);
         draw_player_face(p2.x, p2.y, COLOR_GREEN);
       }
     }

     draw_score(p1_score, p2_score);
     sound_play();
     wait_for_video_frame();

   } else {
     // Game-over screen
     sound_play();
     mouse_ignore();

     if (check_space_bar()) {
       p1_score = 0;
       p2_score = 0;
       PAUSED = 0;
       waiting_for_start = 1;
       draw_title_screen();
       play_start_screen_beep();
     }

     wait_for_video_frame();
   }
 }
}