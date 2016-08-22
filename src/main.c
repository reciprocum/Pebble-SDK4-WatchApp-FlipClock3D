/*
   Project: FlipClock3D (watchapp)
   File   : main.c
   Author : Afonso Santos, Portugal

   Last revision: 09h45 August 22 2016
*/

#include <pebble.h>
#include <fastmath/FastMath.h>
#include <r3/R3.h>
#include <interpolator/Interpolator.h>
#include <cam3d/Cam3D.h>
#include <transformr3/TransformR3.h>
#include <sampler/Sampler.h>
#include <clock3d/Clock3D.h>

#include "Config.h"


// UI related
static Window         *window ;
static Layer          *world_layer ;
static ActionBarLayer *action_bar;


// World related
#define ACCEL_SAMPLER_CAPACITY    8
#define WORLD_UPDATE_INTERVAL_MS  35

typedef enum { WORLD_MODE_UNDEFINED
             , WORLD_MODE_DYNAMIC
             , WORLD_MODE_STEADY
             }
WorldMode ;

// Animation related
#define ANIMATION_INTERVAL_MS     40
#define ANIMATION_FLIP_STEPS      50
#define ANIMATION_SPIN_STEPS      75

static int        world_updateCount   = 0 ;
static WorldMode  world_mode          = WORLD_MODE_UNDEFINED ;
static AppTimer  *world_updateTimer   = NULL ;

Sampler   *sampler_accelX = NULL ;            // To be allocated at world_initialize( ).
Sampler   *sampler_accelY = NULL ;            // To be allocated at world_initialize( ).
Sampler   *sampler_accelZ = NULL ;            // To be allocated at world_initialize( ).

float     *spinRotationFraction    = NULL ;   // To be allocated at world_initialize( ).
float     *animRotationFraction    = NULL ;   // To be allocated at world_initialize( ).
float     *animTranslationFraction = NULL ;   // To be allocated at world_initialize( ).


// Persistence related
#define PKEY_WORLD_MODE            1
#define PKEY_TRANSPARENCY_MODE     2

#define WORLD_MODE_DEFAULT           WORLD_MODE_DYNAMIC
#define MESH3D_TRANSPARENCY_DEFAULT  MESH3D_TRANSPARENCY_SOLID


// APP run mode related.
Blinker   configMode_inkBlinker ;
Blinker   clock_minutes_inkBlinker ;

// User related
#define USER_SECONDSINACTIVE_MAX       90

static uint8_t user_secondsInactive  = 0 ;


// Spin(Z) CONSTANTS & variables
#define        SPIN_ROTATION_QUANTA   0.0001
#define        SPIN_ROTATION_STEADY  -DEG_045
//DEBUG #define        SPIN_ROTATION_STEADY     DEG_135
#define        SPIN_SPEED_BUTTON_STEP 20
#define        SPIN_SPEED_PUNCH_STEP  1000

static int     spin_speed     = 0 ;                      // Initial spin speed.
static float   spin_rotation  = SPIN_ROTATION_STEADY ;   // Initial spin rotation angle allows to view hours/minutes/seconds faces.


// Camera related
#define  CAM3D_DISTANCEFROMORIGIN    (2.2 * CUBE_SIZE)

static Cam3D                     cam ;
static float                     cam_zoom           = PBL_IF_RECT_ELSE(1.25, 1.15) ;
static Mesh3D_TransparencyMode   transparencyMode   = MESH3D_TRANSPARENCY_SOLID ;   // To be loaded/initialized from persistent storage.


// Button click handlers
void
spinSpeed_increment_click_handler
( ClickRecognizerRef recognizer
, void              *context
)
{
  user_secondsInactive = 0 ;
  spin_speed += SPIN_SPEED_BUTTON_STEP ;
}


void
spinSpeed_decrement_click_handler
( ClickRecognizerRef recognizer
, void              *context
)
{
  user_secondsInactive = 0 ;
  spin_speed -= SPIN_SPEED_BUTTON_STEP ;
}


void
transparencyMode_change_click_handler
( ClickRecognizerRef recognizer
, void              *context
)
{
  user_secondsInactive = 0 ;

  // Cycle trough the transparency modes.
  switch (transparencyMode)
  {
    case MESH3D_TRANSPARENCY_SOLID:
     transparencyMode = MESH3D_TRANSPARENCY_XRAY ;
     break ;

   case MESH3D_TRANSPARENCY_XRAY:
     transparencyMode = MESH3D_TRANSPARENCY_WIREFRAME ;
     break ;

   case MESH3D_TRANSPARENCY_WIREFRAME:
   default:
     transparencyMode = MESH3D_TRANSPARENCY_SOLID ;
     break ;
  } ;
}


void
displayType_cycle_click_handler
( ClickRecognizerRef recognizer
, void              *context
)
{
  user_secondsInactive = 0 ;
  Clock3D_cycleDisplayType( ) ;
}


// Forward declare all click_config_providers( ).
void  normalMode_click_config_provider( void *context ) ;
void  configMode_click_config_provider( void *context ) ;

void
configMode_enter_click_handler
( ClickRecognizerRef recognizer
, void              *context
)
{
  user_secondsInactive = 0 ;

  clock_days_leftDigitA          ->mesh->inkBlinker
  = clock_days_leftDigitB        ->mesh->inkBlinker
  = clock_days_rightDigitA       ->mesh->inkBlinker
  = clock_days_rightDigitB       ->mesh->inkBlinker
  = clock_hours_leftDigitA       ->mesh->inkBlinker
  = clock_hours_leftDigitB       ->mesh->inkBlinker
  = clock_hours_rightDigitA      ->mesh->inkBlinker
  = clock_hours_rightDigitB      ->mesh->inkBlinker
  = clock_minutes_leftDigitA     ->mesh->inkBlinker
  = clock_minutes_leftDigitB     ->mesh->inkBlinker
  = clock_minutes_rightDigitA    ->mesh->inkBlinker
  = clock_minutes_rightDigitB    ->mesh->inkBlinker
  = clock_seconds_leftDigit      ->mesh->inkBlinker
  = clock_seconds_rightDigit     ->mesh->inkBlinker
  = clock_second100ths_leftDigit ->mesh->inkBlinker
  = clock_second100ths_rightDigit->mesh->inkBlinker
  = Blinker_start( &configMode_inkBlinker
                 , 250      // lengthOn (ms)
                 , 250      // lengthOff (ms)
                 , INK100   // inkOn (100%)
                 , INK0     // inkOff  (0%)
                 )
  ;

  action_bar_layer_set_click_config_provider( action_bar, configMode_click_config_provider ) ;
}


void
configMode_exit_click_handler
( ClickRecognizerRef recognizer
, void              *context
)
{
  user_secondsInactive = 0 ;

  clock_days_leftDigitA          ->mesh->inkBlinker
  = clock_days_leftDigitB        ->mesh->inkBlinker
  = clock_days_rightDigitA       ->mesh->inkBlinker
  = clock_days_rightDigitB       ->mesh->inkBlinker
  = clock_hours_leftDigitA       ->mesh->inkBlinker
  = clock_hours_leftDigitB       ->mesh->inkBlinker
  = clock_hours_rightDigitA      ->mesh->inkBlinker
  = clock_hours_rightDigitB      ->mesh->inkBlinker
  = clock_seconds_leftDigit      ->mesh->inkBlinker
  = clock_seconds_rightDigit     ->mesh->inkBlinker
  = clock_second100ths_leftDigit ->mesh->inkBlinker
  = clock_second100ths_rightDigit->mesh->inkBlinker
  = NULL
  ;

  clock_minutes_leftDigitA       ->mesh->inkBlinker
  = clock_minutes_leftDigitB     ->mesh->inkBlinker
  = clock_minutes_rightDigitA    ->mesh->inkBlinker
  = clock_minutes_rightDigitB    ->mesh->inkBlinker
  = &clock_minutes_inkBlinker
  ;

  Blinker_stop( &configMode_inkBlinker ) ;
  action_bar_layer_set_click_config_provider( action_bar, normalMode_click_config_provider ) ;
}


void
configMode_click_config_provider
( void *context )
{
  window_single_repeating_click_subscribe( BUTTON_ID_UP
                                         , 100
                                         , (ClickHandler) displayType_cycle_click_handler
                                         ) ;

  window_single_repeating_click_subscribe( BUTTON_ID_DOWN
                                         , 100
                                         , (ClickHandler) displayType_cycle_click_handler
                                         ) ;

  window_long_click_subscribe( BUTTON_ID_SELECT
                             , 0                                                // Use default 500ms
                             , (ClickHandler) configMode_exit_click_handler     // Down handler.
                             , NULL                                             // Up handler.
                             ) ;
}


void
normalMode_click_config_provider
( void *context )
{
  window_single_repeating_click_subscribe( BUTTON_ID_UP
                                         , 100
                                         , (ClickHandler) spinSpeed_decrement_click_handler
                                         ) ;

  window_single_repeating_click_subscribe( BUTTON_ID_DOWN
                                         , 100
                                         , (ClickHandler) spinSpeed_increment_click_handler
                                         ) ;

  window_single_click_subscribe( BUTTON_ID_SELECT
                               , (ClickHandler) transparencyMode_change_click_handler
                               ) ;

  window_long_click_subscribe( BUTTON_ID_SELECT
                             , 0                                                // Use default 500ms
                             , (ClickHandler) configMode_enter_click_handler    // Down handler.
                             , NULL                                             // Up handler.
                             ) ;
}


// Acellerometer handlers.
void
accel_data_service_handler
( AccelData *data
, uint32_t   num_samples
)
{ }


void
accel_tap_service_handler
( AccelAxisType  axis        // Process tap on ACCEL_AXIS_X, ACCEL_AXIS_Y or ACCEL_AXIS_Z
, int32_t        direction   // Direction is 1 or -1
)
{
  user_secondsInactive = 0 ;      // Tap event qualifies as active user interaction.

  // Forward declaration
  void set_world_mode( uint8_t worldMode ) ;

  switch ( axis )
  {
    case ACCEL_AXIS_X:    // Punch: stop/launch spinning motion.
      spin_speed += SPIN_SPEED_PUNCH_STEP ;       // Spin faster.
      break ;

    case ACCEL_AXIS_Y:    // Twist: change the world mode.
      switch( world_mode )
      { 
         case WORLD_MODE_STEADY:
          set_world_mode( WORLD_MODE_DYNAMIC ) ;
          break ;

        case WORLD_MODE_DYNAMIC:
          set_world_mode( WORLD_MODE_STEADY ) ;
          break ;

        case WORLD_MODE_UNDEFINED:
        default:
          break ;
      }

      break ;

    case ACCEL_AXIS_Z:    // Ykes: stop spinning, bring to default spin rotation angle.
    default:
      spin_speed    = 0 ;                         // Stop spinning motion.
      spin_rotation = SPIN_ROTATION_STEADY ;      // Spin rotation angle that allows to view days/hours/minutes faces.
      break ;
  }
}


// Forward declare.
void  world_stop( ) ;
void  world_finalize( ) ;


static
void
tick_timer_service_handler
( struct tm *tick_time
, TimeUnits  units_changed
)
{
  if (spin_speed == 0)
    ++user_secondsInactive ;

  // Auto-exit application on lack of user interaction.
  if (user_secondsInactive > USER_SECONDSINACTIVE_MAX)
  {
    world_stop( ) ;
    world_finalize( ) ;
    window_stack_pop_all( true )	;    // Exit app.
  }

  Clock3D_setTime_DDHHMMSS( tick_time->tm_mday   // days
                          , tick_time->tm_hour   // hours
                          , tick_time->tm_min    // minutes
                          , tick_time->tm_sec    // seconds
                          ) ;
}


void
cam_config
( R3         *viewPoint
, const float rotationZ
)
{
  // setup 3D camera
  Cam3D_lookAtOriginUpwards( &cam
                           , TransformR3_rotateZ( R3_scale( CAM3D_DISTANCEFROMORIGIN    // View point.
                                                          , viewPoint
                                                          )
                                                , rotationZ
                                                )
                           , cam_zoom                                                   // Zoom
                           , CAM3D_PROJECTION_PERSPECTIVE
                           ) ;
}


void
set_world_mode
( const WorldMode pWorldMode )
{ // Clean-up exiting mode. Unsubscribe from no longer needed services.
  switch ( world_mode )
  {
    case WORLD_MODE_DYNAMIC:
    	accel_data_service_unsubscribe( ) ;
      break ;

    case WORLD_MODE_STEADY:                                   // Nothing to unsubscribe from.
    case WORLD_MODE_UNDEFINED:
    default:
      break ;
  }

  // Start-up entering mode. Subscribe to newly needed services. Apply relevant configurations.
  switch ( world_mode = pWorldMode )
  {
    case WORLD_MODE_STEADY:
      spin_speed    = 0 ;                                  // Stop spinning motion.
      spin_rotation = SPIN_ROTATION_STEADY ;               // Spin rotation angle that allows to view days/hours/minutes faces.
      break ;

    case WORLD_MODE_DYNAMIC:
    	accel_data_service_subscribe( 0, accel_data_service_handler ) ;
      break ;

    case WORLD_MODE_UNDEFINED:
    default:
      break ;
  }
}


static
void
interpolations_initialize
( )
{
  Interpolator_AccelerateDecelerate( spinRotationFraction = malloc((ANIMATION_SPIN_STEPS+1)*sizeof(float))
                                   , ANIMATION_SPIN_STEPS
                                   ) ;

  Interpolator_AccelerateDecelerate( animRotationFraction = malloc((ANIMATION_FLIP_STEPS+1)*sizeof(float))
                                   , ANIMATION_FLIP_STEPS
                                   ) ;

  Interpolator_TrigonometricYoYo( animTranslationFraction = malloc((ANIMATION_FLIP_STEPS+1)*sizeof(float))
                                , ANIMATION_FLIP_STEPS
                                ) ;
}


static
void
sampler_initialize
( )
{
  sampler_accelX = Sampler_new( ACCEL_SAMPLER_CAPACITY ) ;
  sampler_accelY = Sampler_new( ACCEL_SAMPLER_CAPACITY ) ;
  sampler_accelZ = Sampler_new( ACCEL_SAMPLER_CAPACITY ) ;

  for ( int i = 0  ;  i < ACCEL_SAMPLER_CAPACITY  ;  ++i )
  {
    Sampler_push( sampler_accelX,  -81 ) ;   // STEADY viewPoint attractor.
    Sampler_push( sampler_accelY, -816 ) ;   // STEADY viewPoint attractor.
    Sampler_push( sampler_accelZ, -571 ) ;   // STEADY viewPoint attractor.
  }
}


void
world_initialize
( )
{ // Get previous configuration from persistent storage if it exists, otherwise use the defaults.
  world_mode       = persist_exists(PKEY_WORLD_MODE)        ? persist_read_int(PKEY_WORLD_MODE)        : WORLD_MODE_DEFAULT ;
  transparencyMode = persist_exists(PKEY_TRANSPARENCY_MODE) ? persist_read_int(PKEY_TRANSPARENCY_MODE) : MESH3D_TRANSPARENCY_DEFAULT ;

  Clock3D_initialize( ) ;

  clock_minutes_leftDigitA    ->mesh->inkBlinker
  = clock_minutes_leftDigitB  ->mesh->inkBlinker
  = clock_minutes_rightDigitA ->mesh->inkBlinker
  = clock_minutes_rightDigitB ->mesh->inkBlinker
  = &clock_minutes_inkBlinker
  ;

  sampler_initialize( ) ;
  interpolations_initialize( ) ;
  Clock3D_config( DIGIT2D_CURVYSKIN ) ;
}


// UPDATE CAMERA & WORLD OBJECTS PROPERTIES

static
void
world_update
( )
{
  ++world_updateCount ;

  Clock3D_updateAnimation( ANIMATION_FLIP_STEPS ) ;

  if (world_mode != WORLD_MODE_STEADY)
  {
    Clock3D_second100ths_update( ) ;

    AccelData ad ;

    if (accel_service_peek( &ad ) < 0)         // Accel service not available.
    {
      Sampler_push( sampler_accelX,  -81 ) ;   // STEADY viewPoint attractor.
      Sampler_push( sampler_accelY, -816 ) ;   // STEADY viewPoint attractor.
      Sampler_push( sampler_accelZ, -571 ) ;   // STEADY viewPoint attractor.
    }
    else
    {
#ifdef QEMU
      if (ad.x == 0  &&  ad.y == 0  &&  ad.z == -1000)   // Under QEMU with SENSORS off this is the default output.
      {
        Sampler_push( sampler_accelX,  -81 ) ;
        Sampler_push( sampler_accelY, -816 ) ;
        Sampler_push( sampler_accelZ, -571 ) ;
      }
      else                                               // If running under QEMU the SENSOR feed must be ON.
      {
        Sampler_push( sampler_accelX, ad.x ) ;
        Sampler_push( sampler_accelY, ad.y ) ;
        Sampler_push( sampler_accelZ, ad.z ) ;
      }
#else
      Sampler_push( sampler_accelX, ad.x ) ;
      Sampler_push( sampler_accelY, ad.y ) ;
      Sampler_push( sampler_accelZ, ad.z ) ;
#endif
    }
   
    float cam_rotation ;

    switch (world_mode)
    {
      case WORLD_MODE_DYNAMIC:
        // Friction: gradualy decrease spin speed until it stops.
        if (spin_speed > 0)
          --spin_speed ;

        if (spin_speed < 0)
          ++spin_speed ;

        if (spin_speed != 0)
          spin_rotation = FastMath_normalizeAngle( spin_rotation + (float)spin_speed * SPIN_ROTATION_QUANTA ) ;

        cam_rotation = spin_rotation ;
        break ;

      case WORLD_MODE_STEADY:
      default:
        cam_rotation = SPIN_ROTATION_STEADY ;
        break ;
    }

    cam_config( &(R3){ .x = (float)(sampler_accelX->samplesAcum / sampler_accelX->samplesNum)
                     , .y =-(float)(sampler_accelY->samplesAcum / sampler_accelY->samplesNum)
                     , .z =-(float)(sampler_accelZ->samplesAcum / sampler_accelZ->samplesNum)
                     }
              , cam_rotation
              ) ;
  }

  // this will queue a defered call to the world_draw( ) method.
  layer_mark_dirty( world_layer ) ;
}


static void
world_draw
( Layer    *me
, GContext *gCtx
)
{
  const GRect layerBounds = layer_get_bounds( me ) ;
  const uint8_t w = layerBounds.size.w ;
  const uint8_t h = layerBounds.size.h ;

  Clock3D_draw( gCtx, &cam, w, h, transparencyMode ) ;
}


static
void
interpolations_finalize
( )
{
  free( animRotationFraction    ) ; animRotationFraction    = NULL ;
  free( animTranslationFraction ) ; animTranslationFraction = NULL ;
  free( spinRotationFraction    ) ; spinRotationFraction    = NULL ;
}


static
void
sampler_finalize
( )
{
  free( Sampler_free( sampler_accelX ) ) ; sampler_accelX = NULL ;
  free( Sampler_free( sampler_accelY ) ) ; sampler_accelY = NULL ;
  free( Sampler_free( sampler_accelZ ) ) ; sampler_accelZ = NULL ;
}


void
world_finalize
( )
{
  Clock3D_finalize( ) ;
  sampler_finalize( ) ;
  interpolations_finalize( ) ;

  // Save current configuration into persistent storage on app exit.
  persist_write_int( PKEY_WORLD_MODE       , world_mode       ) ;
  persist_write_int( PKEY_TRANSPARENCY_MODE, transparencyMode ) ;
}


void
world_update_timer_handler
( void *data )
{
  world_update( ) ;

  // Call me again.
  world_updateTimer = app_timer_register( WORLD_UPDATE_INTERVAL_MS, world_update_timer_handler, data ) ;
}


void
world_start
( )
{ // Position clock handles according to current time.
  // Initialize blinkers.
  Blinker_start( &clock_minutes_inkBlinker
               , 500      // lengthOn (ms)
               , 500      // lengthOff (ms)
               , INK100   // inkOn (100%)
               , INK50    // inkOff (50%)
               ) ;

  // Set initial world mode (and subscribe to related services).
  set_world_mode( world_mode ) ;                                               

  // Activate clock
  tick_timer_service_subscribe( SECOND_UNIT, tick_timer_service_handler ) ;    

  // Become tap aware.
  accel_tap_service_subscribe( accel_tap_service_handler ) ;                   

  // Trigger call to launch animation, will self repeat.
  world_update_timer_handler( NULL ) ;
}


void
world_stop
( )
{
  Blinker_stop( &clock_minutes_inkBlinker ) ;

  // Stop animation.
  app_timer_cancel( world_updateTimer ) ;

  // Stop clock.
  tick_timer_service_unsubscribe( ) ;

  // Tap unaware.
  accel_tap_service_unsubscribe( ) ;               

  // Gravity unaware.
  accel_data_service_unsubscribe( ) ;

  // Compass unaware.
  compass_service_unsubscribe( ) ;                 
}


void
window_load
( Window *window )
{
  Layer *window_root_layer = window_get_root_layer( window ) ;

  action_bar = action_bar_layer_create( ) ;
  action_bar_layer_add_to_window( action_bar, window ) ;
  action_bar_layer_set_click_config_provider( action_bar, normalMode_click_config_provider ) ;

  GRect bounds = layer_get_frame( window_root_layer ) ;
  world_layer = layer_create( bounds ) ;
  layer_set_update_proc( world_layer, world_draw ) ;
  layer_add_child( window_root_layer, world_layer ) ;

  // Position clock handles according to current time, launch blinkers, launch animation, start clock.
  world_start( ) ;
}


void
window_unload
( Window *window )
{
  world_stop( ) ;
  layer_destroy( world_layer ) ;
}


void
app_init
( void )
{
  world_initialize( ) ;

  window = window_create( ) ;
  window_set_background_color( window, GColorBlack ) ;
 
  window_set_window_handlers( window
                            , (WindowHandlers)
                              { .load   = window_load
                              , .unload = window_unload
                              }
                            ) ;

  window_stack_push( window, false ) ;
}


void
app_deinit
( void )
{
  window_stack_remove( window, false ) ;
  window_destroy( window ) ;
  world_finalize( ) ;
}


int
main
( void )
{
  app_init( ) ;
  app_event_loop( ) ;
  app_deinit( ) ;
}