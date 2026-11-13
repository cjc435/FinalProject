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
typedef struct {vector2d_t x0; vector2d_t x1;} wall_t;
typedef struct {vector2d_t location; uint8_t angle;} player_t;
// typedef struct {int NumWalls; int floor; int ceil; wall_t wall;} sector_t;


// LED
#define LED_PIN 25

// Keypad pin configurations
#define BASE_KEYPAD_PIN 9
#define KEYROWS         4
#define NUMKEYS         12
player_t player;
// Global Variables //

// Keypad
unsigned int keycodes[NUMKEYS] = {      0x57, 0x6E, 0x5E, 0x3E, 0x6D,
                                        0x5D, 0x3D, 0x6B, 0x5B, 0x3B,
                                        0x67, 0x37} ;
unsigned int scancodes[KEYROWS] = {   0xE, 0xD, 0xB, 0x7} ;
unsigned int button = 0x70 ;

int CurrentKeyPressed = -1;
bool KeystrokeTriggered = 0;

enum DebouncerState {
  NotPressed,
  MaybePressed,
  Pressed,
  MaybeNotPressed
};
enum DebouncerState DebounceState = NotPressed;

// VGA
char color = WHITE ;

// Screen width/height
#define VGA_WIDTH 640
#define VGA_HEIGHT 480

// Renderer


// Functions // 

void WorldTranslation(player_t *player, wall_t *wall) {

  wall->x0.x = wall->x0.x - player->location.x;
  wall->x0.y = wall->x0.y - player->location.y;  
  wall->x1.x = wall->x1.x - player->location.x;
  wall->x1.y = wall->x1.y - player->location.y;

  // angle manipulations here

  // clipping here

}

void RenderWall(wall_t *wall) {
  int16_t x0 = wall->x0.x / wall->x0.y;
  int16_t x1 = wall->x1.x / wall->x1.y;

  const uint16_t ScreenMidWidth = VGA_WIDTH/2;
  const uint16_t ScreenMidHeight = VGA_HEIGHT/2;

  // drawPixel();

}

// ==================================================
// === Keypad thread 
// ==================================================
// This thread runs on core 0
static PT_THREAD (protothread_keypad(struct pt *pt))
{
    static int i;
    static uint32_t keypad;
    static int PotentialKeypress = -1;

    PT_BEGIN(pt);

    while (1) {

        // Scan keypad
        for (i = 0; i < KEYROWS; i++) {
            gpio_put_masked(
                (0xF << BASE_KEYPAD_PIN),
                (scancodes[i] << BASE_KEYPAD_PIN)
            );

            sleep_us(1);

            keypad = ((gpio_get_all() >> BASE_KEYPAD_PIN) & 0x7F);

            if ((~keypad) & button) break;
        }

        // Find pressed key
        if ((~keypad) & button) {
            for (i = 0; i < NUMKEYS; i++) {
                if (keypad == keycodes[i]) break;
            }

            if (i == NUMKEYS) i = -1;
        }
        else {
            i = -1;
        }

        // Debouncing State Machine
        switch (DebounceState) {

            case NotPressed:
                if (i != -1) {
                    DebounceState = MaybePressed;
                    PotentialKeypress = i;
                }
                break;

            case MaybePressed:
                DebounceState =
                    (i == PotentialKeypress) ? Pressed : NotPressed;
                break;

            case Pressed:
                DebounceState =
                    (i == PotentialKeypress) ? Pressed : MaybeNotPressed;
                break;

            case MaybeNotPressed:
                DebounceState =
                    (i == PotentialKeypress) ? Pressed : NotPressed;
                break;

            default:
                DebounceState = NotPressed;
                PotentialKeypress = -1;
        }

        // Current pressed key
        CurrentKeyPressed =
            (DebounceState != NotPressed) ? PotentialKeypress : -1;

        // Trigger once when key is confirmed pressed
        KeystrokeTriggered =
            (DebounceState == Pressed &&
             i == PotentialKeypress) ? 1 : 0;

        PT_YIELD_usec(30000);
    }

    PT_END(pt);
}

// Animation on core 0
static PT_THREAD (protothread_render(struct pt *pt))
{
    // Mark beginning of thread
    PT_BEGIN(pt);

    // Render setup

    
    wall_t wall;
    wall.x0.x = -10;
    wall.x0.y = 20;
    wall.x1.x = 21;
    wall.x1.y = 26;

    while(1) {
      // Wait for the signal that the buffer's changed
      PT_YIELD_UNTIL(pt, draw_start_signal()) ;
      // Clear the buffer
      clearLowFrame(0, BLACK);
      // Translate the wall around the player. Returns wall transformations
      WorldTranslation(&player, &wall);
      
      RenderWall(&wall);


     // NEVER exit while
    } // END WHILE(1)
  PT_END(pt);
}
// animation thread


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
    // Wait for the signal that the buffer's changed
        PT_YIELD_UNTIL(pt, KeystrokeTriggered) ;

        // press
        // check key is pressed
        // reset the value
        // at the end do a wait

        // =================================
        // Keypad Player Movement
        // =================================

        // 2 Move Forward
        if (CurrentKeyPressed == 1) {
            player.location.x += cosf(player.angle) * move_speed;
            player.location.y += sinf(player.angle) * move_speed;
        }
        // 4 Turn Left
        else if (CurrentKeyPressed == 3) {
            player.location.x += cosf(player.angle) * move_speed;
            player.location.y -= sinf(player.angle) * move_speed;
        }
        // 5 move backward
       else if (CurrentKeyPressed == 4) {
            player.location.x -= cosf(player.angle) * move_speed;
            player.location.y -= sinf(player.angle) * move_speed;
        }
        // 6 Turn Right
        else if (CurrentKeyPressed == 5) {
            player.location.x -= cosf(player.angle) * move_speed;
            player.location.y += sinf(player.angle) * move_speed;

      }
      CurrentKeyPressed = -1; // Reset the value
      PT_YIELD_usec(30000) ;
      // Reset the value
      KeystrokeTriggered = 0;

    } // END WHILE(1)
  PT_END(pt);
} 

// ========================================
// === core 1 main -- started in main below
// ========================================
void core1_main(){
  // Add animation thread
  pt_add_thread(protothread_keypad) ;
  pt_add_thread(protothread_player) ;
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
  gpio_set_dir((BASE_KEYPAD_PIN), GPIO_IN);
  gpio_set_dir((BASE_KEYPAD_PIN+1), GPIO_IN);
  gpio_set_dir((BASE_KEYPAD_PIN+2), GPIO_IN);
  // Set row-pins to output
  gpio_set_dir_out_masked((0xF << BASE_KEYPAD_PIN)) ;
  // Set all output pins to low
  gpio_put_masked((0xF << BASE_KEYPAD_PIN), (0xF << BASE_KEYPAD_PIN)) ;
  // Turn on pulldown resistors for column pins (on by default)
  gpio_pull_up((BASE_KEYPAD_PIN)) ;
  gpio_pull_up((BASE_KEYPAD_PIN+1)) ;
  gpio_pull_up((BASE_KEYPAD_PIN+2)) ;


  // start core 1 
  multicore_reset_core1();
  multicore_launch_core1(&core1_main);

  // add threads
  pt_add_thread(protothread_render);

  // start scheduler
  pt_schedule_start ;
} 