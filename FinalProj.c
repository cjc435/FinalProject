
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

// Screen width/height
#define VGA_WIDTH 640
#define VGA_HEIGHT 480

// Binary Angular Measurement (BAM)
#define BAM_SIZE 256
#define DEG_SIZE 360
#define PI 3.14159265358979323846f

// LED
#define LED_PIN 25

// Keypad pin configurations
#define BASE_KEYPAD_PIN 9
#define KEYROWS         4
#define NUMKEYS         12

// Mini map
#define MAP_X 10
#define MAP_Y 10
#define MAP_WIDTH  150
#define MAP_HEIGHT 100

#define MAP_CENTER_X (MAP_X + MAP_WIDTH/2)
#define MAP_CENTER_Y (MAP_Y + MAP_HEIGHT/2)

#define MAP_SCALE 10

// Minimap Line Clip
#define LEFT   1
#define RIGHT  2
#define BOTTOM 4
#define TOP    8


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


// Typedefs
typedef struct {float x; float y;} vector2d_t;
typedef struct {vector2d_t v1; vector2d_t v2; bool isPortal;} wall_t;
typedef struct {wall_t wall[8]; int NumWalls; float floor; float celing; } sector_t;
// typedef struct {float x; float y; float height; uint8_t angle;} player_t;
typedef struct {float x; float y; float height; float angle;} player_t;


// typedef struct {int NumWalls; int floor; int ceil; wall_t wall;} sector_t;


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
semaphore_t draw_semaphore ;

// Rendering
player_t player;



// BAM
uint8_t degree_to_bam[DEG_SIZE];

float sin_lut[BAM_SIZE];
float cos_lut[BAM_SIZE];


// Functions // 

// BAM
// void init_lookup_tables(void) {

//     // Float degrees -> uint8_t BAM
//     for (int i = 0; i < DEG_SIZE; i++) {
//         degree_to_bam[i] = (uint8_t)(i * 256.0f / 360.0f);
//     }

//     // BAM -> float sine/cosine
//     for (int i = 0; i < BAM_SIZE; i++) {

//         float radians = i * 2.0f * PI / BAM_SIZE;

//         sin_lut[i] = sinf(radians);
//         cos_lut[i] = cosf(radians);
//     }
// }
// uint8_t angle_to_bam(float angle) {
//     // Normalize angle to [0, 360)
//     while (angle < 0.0f)
//         angle += 360.0f;

//     while (angle >= 360.0f)
//         angle -= 360.0f;

//     int index = (int)angle;

//     return degree_to_bam[index];
// }

// float bam_to_angle(uint8_t angle) {
//   return (float)angle * (360.0 / 256.0); 
// }



// Initialize Functions
void init_player(player_t *player) {
  player->x = 0;
  player->y = 0;
  player->angle = 0;
  player->height = 1;
}

void init_wall(wall_t *wall, float x0, float y0, float x1, float y1) {
  wall->v1.x = x0;
  wall->v1.y = y0;
  wall->v2.x = x1;
  wall->v2.y = y1;
}

// void init_sector(sector_t sector, wall_t &wall, int NumWalls, int floor, int ceiling) {
//   wall->v1.x = x0;
//   wall->v1.y = y0;
//   wall->v2.x = x1;
//   wall->v2.y = y1;
// }


// Rendering Functions
wall_t WorldTranslation(player_t player, wall_t wall) {

  wall_t TranslatedWall;
  
  // Move the wall around the player
  TranslatedWall.v1.x = wall.v1.x - player.x;
  TranslatedWall.v1.y = wall.v1.y - player.y;

  TranslatedWall.v2.x = wall.v2.x - player.x;
  TranslatedWall.v2.y = wall.v2.y - player.y;

  // Rotate the wall around the player
  
  // float cos = cos_lut[player.angle];
  // float sin = sin_lut[player.angle];
  float cos = cosf(player.angle);
  float sin = sinf(player.angle);

  // NOTE: The equasion with the + sign determines where 0 degrees is located, i.e. positive x or y axis
  TranslatedWall.v1.x = TranslatedWall.v1.x * cos + TranslatedWall.v1.y * sin;
  TranslatedWall.v1.y = TranslatedWall.v1.x * sin - TranslatedWall.v1.y * cos;

  TranslatedWall.v2.x = TranslatedWall.v2.x * cos + TranslatedWall.v2.y * sin;
  TranslatedWall.v2.y = TranslatedWall.v2.x * sin - TranslatedWall.v2.y * cos;

  return TranslatedWall;  
}

void ConvertHorizontalFovToScreen(float *x) {
  *x = (*x + ((VGA_WIDTH-1.0)/2.0));
  return;
}

void ConvertVerticalFovToScreen(float *y) {
  *y = ((-(*y)) + ((VGA_HEIGHT-1.0)/2.0));
  return;
}


void Render(wall_t wall, float FloorHeight, float CeilHeight) {
  float x[2];
  float yFloor[2];
  float yCeil[2];

  // The following is in 2D coordinates: x,y

  // Translate the wall around the player. Returns wall transformations
  wall = WorldTranslation(player, wall);

  // If the wall is behind the player, ignore it completely
  if(wall.v1.x < 0 && wall.v2.x < 0) {
    return;
  // If half the wall is behind the player, clip to the respective edge
  //  i.e. cut the wall "short" by clipping anything past the player's frustum to that point
  } else if (wall.v1.x < 0) {

  } else if (wall.v2.x < 0) {

  }


  // Convert 2D coordinates to player's projection view
  //    i.e. following converts 2D coordinates to 3D coordinates
  //      x -> z
  //      y -> x
  //        -> y (introduced at height)
  //    via the projection equasion: (farther things become smaller)
  //       x' = x / z
  //       y' = y / z
  //
  //    Note that d represents the distance from the player to the "view plane", or screen.
  //    See https://sunshine2k.de/articles/algorithm/camperspscreentransform/simplisticcameraperspectivescreentransform.html
  //    d is calculated assuming a 90 degree FOV (field of view), which simplifies d to 1. 
  //    The calculation then reduces to jsut being half the screen width

  const float d = VGA_WIDTH/2; 

  x[0] = wall.v1.y * d / wall.v1.x;
  x[1] = wall.v2.y * d / wall.v2.x;

  yFloor[0] = FloorHeight * d / wall.v1.x;
  yFloor[1] = FloorHeight * d / wall.v2.x;

  yCeil[0] = CeilHeight * d / wall.v2.x;
  yCeil[1] = CeilHeight * d / wall.v2.x;

  // Convert new 3D coordinates to 2D screen coordinates
  // Screen goes from 0 -> 640 in x and 0 -> 480 in y

  ConvertHorizontalFovToScreen  (&x[0]);
  ConvertHorizontalFovToScreen  (&x[1]);
  ConvertVerticalFovToScreen    (&yFloor[0]);
  ConvertVerticalFovToScreen    (&yFloor[1]);
  ConvertVerticalFovToScreen    (&yCeil[0]);
  ConvertVerticalFovToScreen    (&yCeil[1]);

  // drawPixel((short)x[0], (short)yFloor[0], WHITE);
  // drawPixel((short)x[0], (short)yCeil[0], WHITE);
  // drawPixel((short)x[1], (short)yFloor[1], WHITE);
  // drawPixel((short)x[1], (short)yCeil[1], WHITE);

  drawLine((short)x[0], (short)yFloor[0], (short)x[1], (short)yFloor[1], WHITE);
  drawLine((short)x[0], (short)yCeil[0], (short)x[1], (short)yCeil[1], WHITE);
  drawLine((short)x[0], (short)yFloor[0], (short)x[0], (short)yCeil[0], WHITE);
  drawLine((short)x[1], (short)yFloor[1], (short)x[1], (short)yCeil[1], WHITE);
}

// Minimap Functions
int MiniMapClip(float x, float y) {

    int code = 0;

    if (x < MAP_X) code |= LEFT;
    else if (x > MAP_X + MAP_WIDTH) code |= RIGHT;

    if (y < MAP_Y) code |= TOP;
    else if (y > MAP_Y + MAP_HEIGHT) code |= BOTTOM;

    return code;
}

int ClipLine(int *x0, int *y0, int *x1, int *y1) {

    int code0 = MiniMapClip(*x0, *y0);
    int code1 = MiniMapClip(*x1, *y1);

    while (1) {

        if (!(code0 | code1)) return 1;

        if (code0 & code1) return 0;

        int codeOut = code0 ? code0 : code1;

        float x, y;

        if (codeOut & TOP) {
            x = *x0 + (*x1 - *x0) *
                (MAP_Y - *y0) / (*y1 - *y0);
            y = MAP_Y;
        }
        else if (codeOut & BOTTOM) {
            x = *x0 + (*x1 - *x0) *
                (MAP_Y + MAP_HEIGHT - *y0) / (*y1 - *y0);
            y = MAP_Y + MAP_HEIGHT;
        }
        else if (codeOut & RIGHT) {
            y = *y0 + (*y1 - *y0) *
                (MAP_X + MAP_WIDTH - *x0) / (*x1 - *x0);
            x = MAP_X + MAP_WIDTH;
        }
        else {
            y = *y0 + (*y1 - *y0) *
                (MAP_X - *x0) / (*x1 - *x0);
            x = MAP_X;
        }

        if (codeOut == code0) {
            *x0 = (int)x;
            *y0 = (int)y;
            code0 = MiniMapClip(*x0, *y0);
        }
        else {
            *x1 = (int)x;
            *y1 = (int)y;
            code1 = MiniMapClip(*x1, *y1);
        }
    }
}

void MiniMap(wall_t wall[], int numWalls) {

  int sx[2], sy[2];
  wall_t TranslatedWall;

  // Draw walls
  for (int i = 0; i < numWalls; i++) {

    TranslatedWall = WorldTranslation(player, wall[i]);

    sx[0] = MAP_CENTER_X + TranslatedWall.v1.x * MAP_SCALE;
    sy[0] = MAP_CENTER_Y - TranslatedWall.v1.y * MAP_SCALE;

    sx[1] = MAP_CENTER_X + TranslatedWall.v2.x * MAP_SCALE;
    sy[1] = MAP_CENTER_Y - TranslatedWall.v2.y * MAP_SCALE;

    // Clip wall to MiniMap boundary
    if (ClipLine(&sx[0], &sy[0], &sx[1], &sy[1])) {
        drawLine(sx[0], sy[0], sx[1], sy[1], WHITE);
    }
  }

  // Draw MiniMap boundary
  drawRect(MAP_X, MAP_Y,
          MAP_WIDTH, MAP_HEIGHT, WHITE);

  // Draw Player
  fillCircle(MAP_CENTER_X,
            MAP_CENTER_Y, 1, RED);
}

// Protothreads //

wall_t wall[4];
// Rendering on core 0
static PT_THREAD (protothread_render(struct pt *pt))
{
    // Mark beginning of thread
    PT_BEGIN(pt);

    // Render setup
    wall_t wall[4];
    // See: https://coordinategrid.com/
    // init_wall(&wall[0], 4, 2, 8, 2); // Initialize wall with coordinates
    // init_wall(&wall[1], 8, 2, 8, -2); // Initialize wall with coordinates
    // init_wall(&wall[2], 8, -2, 4, -2); // Initialize wall with coordinates
    // init_wall(&wall[3], 4, -2, 4, 2); // Initialize wall with coordinates

    init_wall(&wall[0], 6, 4, 6, -4); // Initialize wall with coordinates
    init_wall(&wall[1], -1, 5, -1, 10); // Initialize wall with coordinates
    init_wall(&wall[2], -1, 10, 1, 10); // Initialize wall with coordinates
    init_wall(&wall[3], 1, 10, 1, 5); // Initialize wall with coordinates

    // init_wall(&wall[0], 10, 0, 10, 1); // Initialize wall with coordinates

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
      // Tell core 1 to draw as well
      PT_SEM_SDK_SIGNAL(pt, &draw_semaphore);

      for (int i=0; i<sector.NumWalls; i++) {
        float floor =   sector.floor - player.height;
        float ceiling = sector.celing - player.height;
        Render(wall[i], floor, ceiling); // Render the wall with floor and ceiling heights

        // Keep track of vertical lines drawn and don't redraw over them
      }

      static char AngleStr[32];
      // snprintf(AngleStr, sizeof(int), "%d", (int)bam_to_angle(player.angle));        
      snprintf(AngleStr, sizeof(int), "%d", (int) (player.angle * 180.0/M_PI));        
      drawTextGLCD(10, 10, "Angle:", WHITE, BLACK);
      drawTextGLCD(10, 20, AngleStr, WHITE, BLACK);

     // NEVER exit while
    } // END WHILE(1)
  PT_END(pt);
} // animation thread




// ==================================================
// === Keypad thread 
// ==================================================
// This thread runs on core 1. Controls player movements via keypad
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

      // =================================
      // Keypad Player Movement
      // =================================
      float movement = 1;

        switch (CurrentKeyPressed) {
            // Forward
            case 2:
            //   player.x += cos_lut[player.angle] * movement;
            //   player.y += sin_lut[player.angle] * movement;
              player.x += cosf(player.angle) * movement;
              player.y += sinf(player.angle) * movement;
              break;

            // Backward
            case 5:
            //   player.x -= cos_lut[player.angle] * movement;
            //   player.y -= sin_lut[player.angle] * movement;
              player.x -= cosf(player.angle) * movement;
              player.y -= sinf(player.angle) * movement;
              break;

            // Strafe Left
            case 4:
            //   player.x -= sin_lut[player.angle] * movement;
            //   player.y += cos_lut[player.angle] * movement;
              player.x -= sinf(player.angle) * movement;
              player.y += cosf(player.angle) * movement;
              break;

            // Strafe Right
            case 6:
            //   player.x += sin_lut[player.angle] * movement;
            //   player.y -= cos_lut[player.angle] * movement;
              player.x += sinf(player.angle) * movement;
              player.y -= cosf(player.angle) * movement;
              break;

            // Fly Up
            case 7:
              player.height += movement;
              break;

            // Fly Down
            case 8:
              player.height -= movement;
              break;

            // Rotate Left  
            case 1:
              // player.angle -= 5;
              player.angle += 0.0872665;
              break;

            // Rotate Right
            case 3:
              // player.angle += 5;
              player.angle -= 0.0872665;
              break;

            default:
              break;
        }

        
        // if(player.angle > 2*M_PI) {player.angle -= 2*M_PI;}
        // else if(player.angle < 2*M_PI) {player.angle += 2*M_PI;}

      PT_YIELD_usec(100000) ;    //30000
    }
    // Indicate thread end
    PT_END(pt) ;
}


// ==================================================
// === Serial thread 
// ==================================================
// This thread runs on core 1. Controls player movements via serial terminal for debug
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

        // switch (classifier) {
            // // Forward
            // case 'w':
            // //   player.x += cos_lut[player.angle] * movement;
            // //   player.y += sin_lut[player.angle] * movement;
            //   player.x += cosf(player.angle) * movement;
            //   player.y += sinf(player.angle) * movement;
            //   break;

            // // Backward
            // case 'a':
            // //   player.x -= cos_lut[player.angle] * movement;
            // //   player.y -= sin_lut[player.angle] * movement;
            //   player.x -= cosf(player.angle) * movement;
            //   player.y -= sinf(player.angle) * movement;
            //   break;

            // // Strafe Left
            // case 's':
            // //   player.x -= sin_lut[player.angle] * movement;
            // //   player.y += cos_lut[player.angle] * movement;
            //   player.x -= sinf(player.angle) * movement;
            //   player.y += cosf(player.angle) * movement;
            //   break;

            // // Strafe Right
            // case 'd':
            // //   player.x += sin_lut[player.angle] * movement;
            // //   player.y -= cos_lut[player.angle] * movement;
            //   player.x += sinf(player.angle) * movement;
            //   player.y -= cosf(player.angle) * movement;
            //   break;

        //     // Fly Up
        //     case 'z':
        //       player.height += movement;
        //       break;

        //     // Fly Down
        //     case 'x':
        //       player.height -= movement;
        //       break;

        //     // Rotate Left  
        //     case 'q':
        //       player.angle -= 1;
        //       break;

        //     // Rotate Right
        //     case 'e':
        //       player.angle += 1;
        //       break;

        //     default:
        //       break;
        // }
    }
    PT_END(pt) ;
}


// ==================================================
// === Minimap Thread
// ==================================================
static PT_THREAD (protothread_minimap(struct pt *pt))
{
    PT_BEGIN(pt);

    while (1) {
        PT_SEM_SDK_WAIT(pt, &draw_semaphore) ;

        MiniMap(wall, 1);
    }

    PT_END(pt);
}


// ========================================
// === core 1 main -- started in main below
// ========================================
void core1_main(){
  // Add keypad movement thread
  pt_add_thread(protothread_keypad) ;
  // DEBUG PROTOTHREAD
  pt_add_thread(protothread_serial) ;
  // Minimap protothread
  pt_add_thread(protothread_minimap);
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

  // init_lookup_tables();

  init_player(&player);

  sem_init(&draw_semaphore, 0, 1) ;

  // start core 1 
  multicore_reset_core1();
  multicore_launch_core1(&core1_main);

  // add threads
  pt_add_thread(protothread_render);

  // start scheduler
  pt_schedule_start ;
} 