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

typedef struct {float x; float y;} vector2d_t;
typedef struct {float x[2]; float y[2];} wall_t;
typedef struct {wall_t wall[4]; int NumWalls; float floor; float celing; } sector_t;
typedef struct {float x; float y; float height; uint8_t angle;} player_t;
player_t player;


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
bool KeystrokeTriggered = 0; // $$$$$$$$$

enum DebouncerState {
  NotPressed,
  MaybePressed,
  Pressed,
  MaybeNotPressed
};
enum DebouncerState DebounceState = NotPressed;  // $$$$$$$$$

// VGA
char color = WHITE ;

// Screen width/height
#define VGA_WIDTH 640
#define VGA_HEIGHT 480

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

// Initialize Functions
void init_player(player_t *player) {
  player->x = 0;
  player->y = 0;
  player->angle = 0;
  player->height = 1;
}

void init_wall(wall_t *wall, float x0, float y0, float x1, float y1) {
  wall->x[0] = x0;
  wall->y[0] = y0;
  wall->x[1] = x1;
  wall->y[1] = y1;
}

// void init_sector(sector_t sector, wall_t &wall, int NumWalls, int floor, int ceiling) {
//   wall->x[0] = x0;
//   wall->y[0] = y0;
//   wall->x[1] = x1;
//   wall->y[1] = y1;
// }



wall_t WorldTranslation(player_t player, wall_t wall) {

  wall_t TranslatedWall;
  
  for(int i=0; i<2; i++) {
    // Move the wall around the player
    TranslatedWall.x[i] = wall.x[i] - player.x;
    TranslatedWall.y[i] = wall.y[i] - player.y;

    // Rotate the wall around the player
    TranslatedWall.x[i] = TranslatedWall.x[i] * cos_lut[player.angle] - TranslatedWall.y[i] * sin_lut[player.angle];
    TranslatedWall.y[i] = TranslatedWall.x[i] * sin_lut[player.angle] + TranslatedWall.y[i] * cos_lut[player.angle];

    // Add clipping here
  }
  return TranslatedWall;  
}

int ConvertHorizontalFovToScreen(int x) {
  int screen_point = (int)(x + (((float)VGA_WIDTH-1.0)/2.0));
  return screen_point;
}

int ConvertVerticalFovToScreen(int y) {
  int screen_point = (int)(-y + (((float)VGA_HEIGHT-1.0)/2.0));
  return screen_point;
}


void RenderWall(wall_t wall, float FloorHeight, float CeilHeight) {
  int x[2];
  int yFloor[2];
  int yCeil[2];

  const float d = VGA_WIDTH/2; // See https://sunshine2k.de/articles/algorithm/camperspscreentransform/simplisticcameraperspectivescreentransform.html

  // Convert 2D coordinates to player's projection view
  x[0] = wall.x[0] * d / wall.y[0];
  x[1] = wall.x[1] * d / wall.y[1];

  yFloor[0] = FloorHeight * d / wall.y[0];
  yFloor[1] = FloorHeight * d / wall.y[1];

  yCeil[0] = CeilHeight * d / wall.y[1];
  yCeil[1] = CeilHeight * d / wall.y[1];

  // Convert new 3D coordinates to 2D screen coordinates
  // Screen goes from 0 -> 640 in x and 0 -> 480 in y

  x[0]      = ConvertHorizontalFovToScreen  (x[0]);
  x[1]      = ConvertHorizontalFovToScreen  (x[1]);
  yFloor[0] = ConvertVerticalFovToScreen    (yFloor[0]);
  yFloor[1] = ConvertVerticalFovToScreen    (yFloor[1]);
  yCeil[0]  = ConvertVerticalFovToScreen    (yCeil[0]);
  yCeil[1]  = ConvertVerticalFovToScreen    (yCeil[1]);

  // drawPixel((short)x[0], (short)yFloor[0], WHITE);
  // drawPixel((short)x[0], (short)yCeil[0], WHITE);
  // drawPixel((short)x[1], (short)yFloor[1], WHITE);
  // drawPixel((short)x[1], (short)yCeil[1], WHITE);

  drawLine((short)x[0], (short)yFloor[0], (short)x[1], (short)yFloor[1], WHITE);
  drawLine((short)x[0], (short)yCeil[0], (short)x[1], (short)yCeil[1], WHITE);
  drawLine((short)x[0], (short)yFloor[0], (short)x[0], (short)yCeil[0], WHITE);
  drawLine((short)x[1], (short)yFloor[1], (short)x[1], (short)yCeil[1], WHITE);
}


// Protothreads //

// Rendering on core 0
static PT_THREAD (protothread_render(struct pt *pt))
{
    // Mark beginning of thread
    PT_BEGIN(pt);

    // Render setup
    wall_t wall[4];
    wall_t TranslatedWall;
    // See: https://coordinategrid.com/
    // init_wall(&wall[0], 4, 2, 8, 2); // Initialize wall with coordinates
    // init_wall(&wall[1], 8, 2, 8, -2); // Initialize wall with coordinates
    // init_wall(&wall[2], 8, -2, 4, -2); // Initialize wall with coordinates
    // init_wall(&wall[3], 4, -2, 4, 2); // Initialize wall with coordinates

    init_wall(&wall[0], -1, 5, 1, 5); // Initialize wall with coordinates
    init_wall(&wall[1], -1, 5, -1, 10); // Initialize wall with coordinates
    init_wall(&wall[2], -1, 10, 1, 10); // Initialize wall with coordinates
    init_wall(&wall[3], 1, 10, 1, 5); // Initialize wall with coordinates

    sector_t sector;
    sector.NumWalls = 1;
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

      for (int i=0; i<sector.NumWalls; i++) {
        // Translate the wall around the player. Returns wall transformations
        TranslatedWall = WorldTranslation(player, wall[i]);

        float floor =   sector.floor - player.height;
        float ceiling = sector.celing - player.height;
        
        RenderWall(TranslatedWall, floor, ceiling); // Render the wall with floor and ceiling heights
      }

     // NEVER exit while
    } // END WHILE(1)
  PT_END(pt);
} // animation thread

// ==================================================
// === Keypad Scan Child Thread
// ==================================================

static PT_THREAD (protothread_keypad_scan(struct pt *pt))
{
    static int i;
    static uint32_t keypad;

    PT_BEGIN(pt);

    // Default: no key pressed
    ScannedKey = -1;

    // Scan keypad rows
    for (i = 0; i < KEYROWS; i++) {

        gpio_put_masked(
            (0xF << BASE_KEYPAD_PIN),
            (scancodes[i] << BASE_KEYPAD_PIN)
        );

        sleep_us(1);

        keypad = ((gpio_get_all() >> BASE_KEYPAD_PIN) & 0x7F);

        if ((~keypad) & button) {
            break;
        }
    }

    // Identify pressed key
    if ((~keypad) & button) {

        for (i = 0; i < NUMKEYS; i++) {
            if (keypad == keycodes[i]) {
                ScannedKey = i;
                break;
            }
        }
    }

    PT_END(pt);
}

// ==================================================
// === Keypad thread 
// ==================================================
// This thread runs on core 0
static PT_THREAD (protothread_keypad(struct pt *pt))
{
    static struct pt pt_scan;
    static int i;
    static int PotentialKeypress = -1;

    PT_BEGIN(pt);

    while (1) {

        // Run keypad scan child thread
        PT_SPAWN(pt, &pt_scan, protothread_keypad_scan(&pt_scan));

        // Get scanning result
        i = ScannedKey;

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
             i == PotentialKeypress) ? 1 : 0;  // $$$$$$$$$

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

        // =================================
        // Keypad Player Movement
        // =================================

        // 2 Move Forward
        if (CurrentKeyPressed == 1) {
            player.x += cos_lut[player.angle] * move_speed;
            player.y += sin_lut[player.angle] * move_speed;
        }
        // 4 Strafe Left
        else if (CurrentKeyPressed == 3) {
            player.x += cos_lut[player.angle] * move_speed;
            player.y -= sin_lut[player.angle] * move_speed;
        }
        // 5 move backward
       else if (CurrentKeyPressed == 4) {
            player.x -= cos_lut[player.angle] * move_speed;
            player.y -= sin_lut[player.angle] * move_speed;
        }
        // 6 Strafe Right
        else if (CurrentKeyPressed == 5) {
            player.x -= cos_lut[player.angle] * move_speed;
            player.y += sin_lut[player.angle] * move_speed;
        }
        // 1 Turn Left
        else if (CurrentKeyPressed == 0) {
            player.angle--;
        }

        // 3 Turn Right
        else if (CurrentKeyPressed == 2) {
            player.angle++;
        }

        // 7 Fly
        else if (CurrentKeyPressed == 6) {
            player.height += move_speed;
        }

        // 8 Descend
        else if (CurrentKeyPressed == 7) {
            player.height -= move_speed;
        }

        PT_YIELD_usec(30000) ;
    }

    PT_END(pt);
} 
 


// User input thread. User can change draw speed
static PT_THREAD (protothread_serial(struct pt *pt))
{
    PT_BEGIN(pt) ;
    static char classifier ;
    float movement = 1;

    while(1) {
        sprintf(pt_serial_out_buffer, "input a command: (w,a,s,d)\n");
        serial_write ;
        // spawn a thread to do the non-blocking serial read
        serial_read ;
        // convert input string to number
        sscanf(pt_serial_in_buffer,"%c", &classifier) ;

        switch (classifier) {
            // Forward
            case 'w':
              player.x += sin_lut[player.angle] * movement;
              player.y += cos_lut[player.angle] * movement;
              break;

            // Backward
            case 's':
              player.x -= sin_lut[player.angle] * movement;
              player.y -= cos_lut[player.angle] * movement;
              break;

            // Strafe Left
            case 'a':
              player.x -= cos_lut[player.angle] * movement;
              player.y += sin_lut[player.angle] * movement;
              break;

            // Strafe Right
            case 'd':
              player.x += cos_lut[player.angle] * movement;
              player.y -= sin_lut[player.angle] * movement;
              break;

            // Fly Up
            case 'z':
              player.height += movement;
              break;

            // Fly Down
            case 'x':
              player.height -= movement;
              break;

            // Rotate Left  
            case 'q':
              player.angle -= 1;
              break;

            // Rotate Right
            case 'e':
              player.angle += 1;
              break;

            default:
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
  gpio_set_dir((BASE_KEYPAD_PIN), GPIO_IN);  // $$$$$$$$$$$$$$$$
  gpio_set_dir((BASE_KEYPAD_PIN+1), GPIO_IN);  // $$$$$$$$$$$$$$$$
  gpio_set_dir((BASE_KEYPAD_PIN+2), GPIO_IN);  // $$$$$$$$$$$$$$$$
  // Set row-pins to output
  gpio_set_dir_out_masked((0xF << BASE_KEYPAD_PIN)) ;
  // Set all output pins to low
  gpio_put_masked((0xF << BASE_KEYPAD_PIN), (0xF << BASE_KEYPAD_PIN)) ;
  // Turn on pulldown resistors for column pins (on by default)
  gpio_pull_up((BASE_KEYPAD_PIN)) ;  // $$$$$$$$$$$$$$$$
  gpio_pull_up((BASE_KEYPAD_PIN+1)) ;  // $$$$$$$$$$$$$$$$
  gpio_pull_up((BASE_KEYPAD_PIN+2)) ;  // $$$$$$$$$$$$$$$$


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