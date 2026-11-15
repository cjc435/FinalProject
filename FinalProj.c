
/**
 * Hunter Adams (vha3@cornell.edu)
 * 
 * This demonstration animates two balls bouncing about the screen.
 * Through a serial interface, the user can change the ball color.
 *
 * HARDWARE CONNECTIONS
  - GPIO 16 ---> VGA Hsync
  - GPIO 17 ---> VGA Vsync
  - GPIO 18 ---> VGA Green lo-bit --> 470 ohm resistor --> VGA_Green
  - GPIO 19 ---> VGA Green hi_bit --> 330 ohm resistor --> VGA_Green
  - GPIO 20 ---> 330 ohm resistor ---> VGA-Blue
  - GPIO 21 ---> 330 ohm resistor ---> VGA-Red
  - RP2040 GND ---> VGA-GND
 *
 * RESOURCES USED
 *  - PIO state machines 0, 1, and 2 on PIO instance 0
 *  - DMA channels (2, by claim mechanism)
 *  - 153.6 kBytes of RAM (for pixel color data)
 *
 */

// Include the VGA grahics library
#include "VGA/vga16_graphics_v3.h"
// Include standard libraries
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
// Include Pico libraries
#include "pico/stdlib.h"
#include "pico/divider.h"
#include "pico/multicore.h"
#include "pico/sync.h"
// Include hardware libraries
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
// Include protothreads
#include "pt_cornell_rp2040_v1_4.h"

// === the fixed point macros ========================================
typedef signed int fix15 ;
#define multfix15(a,b) ((fix15)((((signed long long)(a))*((signed long long)(b)))>>15))
#define float2fix15(a) ((fix15)((a)*32768.0)) // 2^15
#define fix2float15(a) ((float)(a)/32768.0)
#define absfix15(a) abs(a) 
#define int2fix15(a) ((fix15)(a << 15))
#define fix2int15(a) ((int)(a >> 15))
#define char2fix15(a) (fix15)(((fix15)(a)) << 15)
#define divfix(a,b) (fix15)(div_s64s64( (((signed long long)(a)) << 15), ((signed long long)(b))))


typedef struct {int16_t x; int16_t y;} vector2d_t;
typedef struct {int16_t x[2]; int16_t y[2];} wall_t;
typedef struct {wall_t wall[4]; int16_t NumWalls; int16_t floor; int16_t celing; } sector_t;
typedef struct {int16_t x; int16_t y; int16_t height; uint8_t angle;} player_t;
player_t player;


// typedef struct {int NumWalls; int floor; int ceil; wall_t wall;} sector_t;


// LED
#define LED_PIN 25

// Keypad pin configurations
#define BASE_KEYPAD_PIN 9
#define KEYROWS         4
#define NUMKEYS         12


// Global Variables //

// Keypad
unsigned int keycodes[NUMKEYS] = {      0x57, 0x6E, 0x5E, 0x3E, 0x6D,
                                        0x5D, 0x3D, 0x6B, 0x5B, 0x3B,
                                        0x67, 0x37} ;
unsigned int scancodes[KEYROWS] = {   0xE, 0xD, 0xB, 0x7} ;
unsigned int button = 0x70 ;

int CurrentKeyPressed = -1;

enum DebouncerState {
  NotPressed,
  MaybePressed,
  Pressed,
  MaybeNotPressed
};

// VGA
char color = WHITE ;

// Screen width/height
#define VGA_WIDTH 640
#define VGA_HEIGHT 480


// Renderer



// Binary Angular Measurement (BAM)
#define BAM_SIZE 256
#define DEG_SIZE 360
#define PI 3.14159265358979323846f

uint8_t degree_to_bam[DEG_SIZE];

float sin_lut[BAM_SIZE];
float cos_lut[BAM_SIZE];

void init_lookup_tables(void) {

    // Float degrees -> uint8_t BAM
    for (int i = 0; i < DEG_SIZE; i++) {
        degree_to_bam[i] = (uint8_t)(i * 256.0f / 360.0f);
    }

    // BAM -> float sine/cosine
    for (int i = 0; i < BAM_SIZE; i++) {

        float radians = i * 2.0f * PI / BAM_SIZE;

        sin_lut[i] = sinf(radians);
        cos_lut[i] = cosf(radians);
    }
}
uint8_t float_to_bam(float angle) {
    // Normalize angle to [0, 360)
    while (angle < 0.0f)
        angle += 360.0f;

    while (angle >= 360.0f)
        angle -= 360.0f;

    int index = (int)angle;

    return degree_to_bam[index];
}


// Functions // 

wall_t WorldTranslation(player_t player, wall_t wall) {

  wall_t TranslatedWall;
  TranslatedWall.x[0] = wall.x[0] - player.x;
  TranslatedWall.y[0] = wall.y[0] - player.y;  
  TranslatedWall.x[1] = wall.x[1] - player.x;
  TranslatedWall.y[1] = wall.y[1] - player.y;
  return TranslatedWall;

  // angle manipulations here

  // clipping here
  
}

int16_t ConvertHorizontalFovToScreen(int16_t x) {
  int16_t screen_point;
  screen_point = (uint16_t)((float)x + ((VGA_WIDTH-1)/2));
  return screen_point;
}

int16_t ConvertVerticalFovToScreen(int16_t y) {
  int16_t screen_point;
  screen_point = (uint16_t)((float)y*-1 + ((VGA_HEIGHT-1)/2));
  return screen_point;
}


void RenderWall(wall_t wall, uint16_t FloorHeight, uint16_t CeilHeight) {
  uint16_t x[2];
  uint16_t yFloor[2];
  uint16_t yCeil[2];

  const int d = VGA_WIDTH/2; // See https://sunshine2k.de/articles/algorithm/camperspscreentransform/simplisticcameraperspectivescreentransform.html

  // Convert 2D coordinates to player's projection view
  x[0] = wall.x[0] * d / wall.y[0];
  x[1] = wall.x[1] * d / wall.y[1];

  yFloor[0] = (FloorHeight * d / wall.y[0]);
  yFloor[1] = (FloorHeight * d / wall.y[1]);

  yCeil[0] = (CeilHeight * d / wall.y[1]);
  yCeil[1] = (CeilHeight * d / wall.y[1]);

  // Convert new 3D coordinates to 2D screen coordinates
  // Screen goes from 0 -> 640 in x and 0 -> 480 in y

  x[0]      = ConvertHorizontalFovToScreen  (x[0]);
  x[1]      = ConvertHorizontalFovToScreen  (x[1]);
  yFloor[0] = ConvertVerticalFovToScreen    (yFloor[0]);
  yFloor[1] = ConvertVerticalFovToScreen    (yFloor[1]);
  yCeil[0]  = ConvertVerticalFovToScreen    (yCeil[0]);
  yCeil[1]  = ConvertVerticalFovToScreen    (yCeil[1]);

  drawPixel((short)x[0], (short)yFloor[0], WHITE);
  drawPixel((short)x[0], (short)yCeil[0], WHITE);
  drawPixel((short)x[1], (short)yFloor[1], WHITE);
  drawPixel((short)x[1], (short)yCeil[1], WHITE);
}

void init_player(player_t *player) {
  player->x = 0;
  player->y = 0;
  player->angle = 0;
  player->height = 0;
}

void init_wall(wall_t *wall, int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
  wall->x[0] = x0;
  wall->y[0] = y0;
  wall->x[1] = x1;
  wall->y[1] = y1;
}

// void init_sector(sector_t sector, wall_t &wall, int16_t NumWalls, int16_t floor, int16_t ceiling) {
//   wall->x[0] = x0;
//   wall->y[0] = y0;
//   wall->x[1] = x1;
//   wall->y[1] = y1;
// }

// Protothreads //

// Rendering on core 0
static PT_THREAD (protothread_render(struct pt *pt))
{
    // Mark beginning of thread
    PT_BEGIN(pt);

    // Render setup
    wall_t wall[4];
    wall_t TranslatedWall;
    init_wall(&wall[0], -1, 5, 1, 5); // Initialize wall with coordinates
    init_wall(&wall[1], -1, 5, -1, 10); // Initialize wall with coordinates
    init_wall(&wall[2], -1, 10, 1, 10); // Initialize wall with coordinates
    init_wall(&wall[3], 1, 10, 1, 5); // Initialize wall with coordinates

    sector_t sector;
    sector.NumWalls = 4;
    sector.floor = 0;
    sector.celing = 1;
    sector.wall[0] = wall[0];
    sector.wall[1] = wall[1];
    sector.wall[2] = wall[2];
    sector.wall[3] = wall[3];


    while(1) {
      // Wait for the signal that the buffer's changed
      PT_YIELD_UNTIL(pt, draw_start_signal()) ;
      // Clear the buffer
      clearLowFrame(0, BLACK);
      // for (every wall) {
      // Translate the wall around the player. Returns wall transformations
      TranslatedWall = WorldTranslation(player, wall);
      
      RenderWall(TranslatedWall, 0, 1); // Render the wall with floor and ceiling heights


     // NEVER exit while
    } // END WHILE(1)
  PT_END(pt);
} // animation thread




// ==================================================
// === Keypad thread 
// ==================================================
// This thread runs on core 0
static PT_THREAD (protothread_keypad(struct pt *pt))
{
    // Indicate thread beginning
    PT_BEGIN(pt) ;

    // Some variables
    static int i ;
    static uint32_t keypad ;

    // State Machine Variables
    static enum DebouncerState DebounceState = NotPressed;
    static int PotentialKeypress = -1;

    while(1) {

      // Scan the keypad!
      for (i=0; i<KEYROWS; i++) {
          // Set a row low
          gpio_put_masked((0xF << BASE_KEYPAD_PIN),
                          (scancodes[i] << BASE_KEYPAD_PIN)) ;
          // Small delay required
          sleep_us(1) ;
          // Read the keycode
          keypad = ((gpio_get_all() >> BASE_KEYPAD_PIN) & 0x7F) ;
          // Break if button(s) are pressed
          if ((~keypad) & button) break ;
      }
      // If we found a button . . .
      if ((~keypad) & button) {
          // Look for a valid keycode.
          for (i=0; i<NUMKEYS; i++) {
              if (keypad == keycodes[i]) break ;
          }
          // If we don't find one, report invalid keycode
          if (i==NUMKEYS) (i = -1) ;
      }
      // Otherwise, indicate invalid/non-pressed buttons
      else (i=-1) ;

      // Print key to terminal
      // printf("\n%d", i) ;

      // Debouncing State Machine //
      switch (DebounceState) {
        case NotPressed:
          if(i != -1) {
            DebounceState = MaybePressed;
            PotentialKeypress = i;
          }
          break;
        case MaybePressed:
          DebounceState = (i == PotentialKeypress) ? Pressed : NotPressed;
          break;
        case Pressed:
          DebounceState = (i == PotentialKeypress) ? Pressed : MaybeNotPressed;
          break;
        case MaybeNotPressed:
          DebounceState = (i == PotentialKeypress) ? Pressed : NotPressed;
          break;
        default:
          DebounceState = NotPressed;
          PotentialKeypress = -1;
      }

      // Post the current keypressed to a global
      CurrentKeyPressed = (DebounceState != NotPressed) ? PotentialKeypress : -1;


      PT_YIELD_usec(30000) ;
    }
    // Indicate thread end
    PT_END(pt) ;
}


// ==================================================
// === Player Movement Thread
// ==================================================
static PT_THREAD (protothread_player(struct pt *pt))
{
    // Mark beginning of thread
    PT_BEGIN(pt);

    // Player movement speed
    float move_speed = 1.0f;

    while(1) {
      // Wait until the keypressed in not invalid
      PT_YIELD_UNTIL(pt, (CurrentKeyPressed != -1)) ;

      // =================================
      // Keypad Player Movement
      // =================================

      // 2 Move Forward
      if (CurrentKeyPressed == 1) {
          player.x += cosf(player.angle) * move_speed;
          player.y += sinf(player.angle) * move_speed;
      }
      // 4 Turn Left
      else if (CurrentKeyPressed == 3) {
          player.x += cosf(player.angle) * move_speed;
          player.y -= sinf(player.angle) * move_speed;
      }
      // 5 move backward
      else if (CurrentKeyPressed == 4) {
          player.x -= cosf(player.angle) * move_speed;
          player.y -= sinf(player.angle) * move_speed;
      }
      // 6 Turn Right
      else if (CurrentKeyPressed == 5) {
          player.x -= cosf(player.angle) * move_speed;
          player.y += sinf(player.angle) * move_speed;

      }

    } // END WHILE(1)
  PT_END(pt);
} 


// User input thread. User can change draw speed
static PT_THREAD (protothread_serial(struct pt *pt))
{
    PT_BEGIN(pt) ;
    static char classifier ;
    int16_t movement = 1;

    while(1) {
        sprintf(pt_serial_out_buffer, "input a command: (w,a,s,d)\n");
        serial_write ;
        // spawn a thread to do the non-blocking serial read
        serial_read ;
        // convert input string to number
        sscanf(pt_serial_in_buffer,"%c", &classifier) ;

        switch (classifier) {
            case 'w':
              player.y += movement;
              break;

            case 'a':
              player.x -= movement;
              break;

            case 's':
              player.y -= movement;
              break;
            
            case 'd':
              player.x += movement;
              break;

            case 'q':
              player.x += movement;
              break;

            case 'e':
              player.x += movement;
              break;
        }
    }
    PT_END(pt) ;
}

// ========================================
// === core 1 main -- started in main below
// ========================================
void core1_main(){
  // Add animation thread
  // pt_add_thread(protothread_keypad) ;
  // pt_add_thread(protothread_player) ;

  // DEBUG PROTOTHREAD
  pt_add_thread(protothread_serial) ;
  // Start the scheduler
  pt_schedule_start ;

}

// ========================================
// === main
// ========================================
// USE ONLY C-sdk library
int main(){
  set_sys_clock_khz(150000, true) ;
  // initialize stio
  stdio_init_all() ;

  // set up LED gpio 25
  gpio_init(LED_PIN) ;  
  gpio_set_dir(LED_PIN, GPIO_OUT) ;
  gpio_put(LED_PIN, true);

  // initialize VGA
  initVGA() ;
  
  ////////////////// KEYPAD INITS ///////////////////////
  // Initialize the keypad GPIO's
  gpio_init_mask((0x7F << BASE_KEYPAD_PIN)) ;
  gpio_set_dir((BASE_KEYPAD_PIN+4), GPIO_IN);
  gpio_set_dir((BASE_KEYPAD_PIN+5), GPIO_IN);
  gpio_set_dir((BASE_KEYPAD_PIN+6), GPIO_IN);
  // Set row-pins to output
  gpio_set_dir_out_masked((0xF << BASE_KEYPAD_PIN)) ;
  // Set all output pins to low
  gpio_put_masked((0xF << BASE_KEYPAD_PIN), (0xF << BASE_KEYPAD_PIN)) ;
  // Turn on pulldown resistors for column pins (on by default)
  gpio_pull_up((BASE_KEYPAD_PIN+4)) ;
  gpio_pull_up((BASE_KEYPAD_PIN+5)) ;
  gpio_pull_up((BASE_KEYPAD_PIN+6)) ;

  init_lookup_tables();

  init_player(&player);


  // start core 1 
  multicore_reset_core1();
  multicore_launch_core1(&core1_main);

  // add threads
  pt_add_thread(protothread_render);

  // start scheduler
  pt_schedule_start ;
} 
