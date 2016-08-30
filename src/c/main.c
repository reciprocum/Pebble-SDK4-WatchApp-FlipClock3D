/*
   Project: FlipClock3D (watchapp)
   File   : main.c
   Author : Afonso Santos, Portugal

   Last revision: 19h10 August 29 2016
*/

#include <pebble.h>
#include <karambola/FastMath.h>
#include <karambola/R3.h>
#include <karambola/Interpolator.h>
#include <karambola/CamR3.h>
#include <karambola/TransformR3.h>
#include <karambola/Sampler.h>
#include <karambola/Clock3D.h>

#include "Config.h"

// Obstruction related.
GSize available_screen ;


// UI related
static Window         *s_window ;
static Layer          *s_window_layer ;
static Layer          *s_world_layer ;
static ActionBarLayer *s_action_bar;


// World related
#define ACCEL_SAMPLER_CAPACITY    8
#define WORLD_UPDATE_INTERVAL_MS  35

static Clock3D s_clock ;  // The main/only world object.

typedef enum { WORLD_MODE_UNDEFINED
             , WORLD_MODE_DYNAMIC
             , WORLD_MODE_STEADY
             }
WorldMode ;

// Animation related
#define ANIMATION_INTERVAL_MS     40
#define ANIMATION_FLIP_STEPS      50
#define ANIMATION_SPIN_STEPS      75

static int        s_world_updateCount       = 0 ;
static WorldMode  s_world_mode              = WORLD_MODE_UNDEFINED ;
static AppTimer  *s_world_updateTimer_ptr   = NULL ;

Sampler   *sampler_accelX = NULL ;            // To be allocated at world_initialize( ).
Sampler   *sampler_accelY = NULL ;            // To be allocated at world_initialize( ).
Sampler   *sampler_accelZ = NULL ;            // To be allocated at world_initialize( ).

float     *spinRotationFraction    = NULL ;   // To be allocated at world_initialize( ).
float     *animRotationFraction    = NULL ;   // To be allocated at world_initialize( ).
float     *animTranslationFraction = NULL ;   // To be allocated at world_initialize( ).


// Persistence related
#define PKEY_WORLD_MODE            1
#define PKEY_TRANSPARENCY_MODE     2

#define WORLD_MODE_DEFAULT         WORLD_MODE_DYNAMIC
#define MESH_TRANSPARENCY_DEFAULT  MESH_TRANSPARENCY_SOLID


// APP run mode related.
Blinker   configMode_inkBlinker ;
Blinker   clock_minutes_inkBlinker ;

// User related
#define USER_SECONDSINACTIVE_MAX       90

static uint8_t s_user_secondsInactive  = 0 ;


// Spin(Z) CONSTANTS & variables
#define        SPIN_ROTATION_QUANTA   0.0001
#define        SPIN_ROTATION_STEADY  -DEG_045
#define        SPIN_SPEED_BUTTON_STEP 20
#define        SPIN_SPEED_PUNCH_STEP  1000

static int     s_spin_speed     = 0 ;                      // Initial spin speed.
static float   s_spin_rotation  = SPIN_ROTATION_STEADY ;   // Initial spin rotation angle allows to view hours/minutes/seconds faces.


// Camera related
#define  CAM3D_DISTANCEFROMORIGIN    (2.2 * CUBE_SIZE)

static CamR3             s_cam ;
static float             s_cam_zoom           = PBL_IF_RECT_ELSE(1.25f, 1.15f) ;
static MeshTransparency  s_transparencyMode   = MESH_TRANSPARENCY_SOLID ;   // To be loaded/initialized from persistent storage.


// Button click handlers
void
spinSpeed_increment_click_handler
( ClickRecognizerRef recognizer
, void              *context
)
{
  s_user_secondsInactive = 0 ;
  s_spin_speed += SPIN_SPEED_BUTTON_STEP ;
}


void
spinSpeed_decrement_click_handler
( ClickRecognizerRef recognizer
, void              *context
)
{
  s_user_secondsInactive = 0 ;
  s_spin_speed -= SPIN_SPEED_BUTTON_STEP ;
}


void
transparencyMode_change_click_handler
( ClickRecognizerRef recognizer
, void              *context
)
{
  s_user_secondsInactive = 0 ;

  // Cycle trough the transparency modes.
  switch (s_transparencyMode)
  {
    case MESH_TRANSPARENCY_SOLID:
     s_transparencyMode = MESH_TRANSPARENCY_XRAY ;
     break ;

   case MESH_TRANSPARENCY_XRAY:
     s_transparencyMode = MESH_TRANSPARENCY_WIREFRAME ;
     break ;

   case MESH_TRANSPARENCY_WIREFRAME:
   default:
     s_transparencyMode = MESH_TRANSPARENCY_SOLID ;
     break ;
  } ;
}


void
displayType_cycle_click_handler
( ClickRecognizerRef recognizer
, void              *context
)
{
  s_user_secondsInactive = 0 ;
  Clock3D_cycleDigitType( &s_clock ) ;
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
  s_user_secondsInactive = 0 ;

  s_clock.days_leftDigitA          ->mesh->inkBlinker
  = s_clock.days_leftDigitB        ->mesh->inkBlinker
  = s_clock.days_rightDigitA       ->mesh->inkBlinker
  = s_clock.days_rightDigitB       ->mesh->inkBlinker
  = s_clock.hours_leftDigitA       ->mesh->inkBlinker
  = s_clock.hours_leftDigitB       ->mesh->inkBlinker
  = s_clock.hours_rightDigitA      ->mesh->inkBlinker
  = s_clock.hours_rightDigitB      ->mesh->inkBlinker
  = s_clock.minutes_leftDigitA     ->mesh->inkBlinker
  = s_clock.minutes_leftDigitB     ->mesh->inkBlinker
  = s_clock.minutes_rightDigitA    ->mesh->inkBlinker
  = s_clock.minutes_rightDigitB    ->mesh->inkBlinker
  = s_clock.seconds_leftDigit      ->mesh->inkBlinker
  = s_clock.seconds_rightDigit     ->mesh->inkBlinker
  = s_clock.second100ths_leftDigit ->mesh->inkBlinker
  = s_clock.second100ths_rightDigit->mesh->inkBlinker
  = Blinker_start( &configMode_inkBlinker
                 , 250      // lengthOn (ms)
                 , 250      // lengthOff (ms)
                 , INK100   // inkOn (100%)
                 , INK0     // inkOff  (0%)
                 )
  ;

  action_bar_layer_set_click_config_provider( s_action_bar, configMode_click_config_provider ) ;
}


void
configMode_exit_click_handler
( ClickRecognizerRef recognizer
, void              *context
)
{
  s_user_secondsInactive = 0 ;

  s_clock.days_leftDigitA          ->mesh->inkBlinker
  = s_clock.days_leftDigitB        ->mesh->inkBlinker
  = s_clock.days_rightDigitA       ->mesh->inkBlinker
  = s_clock.days_rightDigitB       ->mesh->inkBlinker
  = s_clock.hours_leftDigitA       ->mesh->inkBlinker
  = s_clock.hours_leftDigitB       ->mesh->inkBlinker
  = s_clock.hours_rightDigitA      ->mesh->inkBlinker
  = s_clock.hours_rightDigitB      ->mesh->inkBlinker
  = s_clock.seconds_leftDigit      ->mesh->inkBlinker
  = s_clock.seconds_rightDigit     ->mesh->inkBlinker
  = s_clock.second100ths_leftDigit ->mesh->inkBlinker
  = s_clock.second100ths_rightDigit->mesh->inkBlinker
  = NULL
  ;

  s_clock.minutes_leftDigitA       ->mesh->inkBlinker
  = s_clock.minutes_leftDigitB     ->mesh->inkBlinker
  = s_clock.minutes_rightDigitA    ->mesh->inkBlinker
  = s_clock.minutes_rightDigitB    ->mesh->inkBlinker
  = &clock_minutes_inkBlinker
  ;

  Blinker_stop( &configMode_inkBlinker ) ;
  action_bar_layer_set_click_config_provider( s_action_bar, normalMode_click_config_provider ) ;
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
  s_user_secondsInactive = 0 ;      // Tap event qualifies as active user interaction.

  // Forward declaration
  void set_world_mode( uint8_t worldMode ) ;

  switch ( axis )
  {
    case ACCEL_AXIS_X:    // Punch: stop/launch spinning motion.
      s_spin_speed += SPIN_SPEED_PUNCH_STEP ;       // Spin faster.
      break ;

    case ACCEL_AXIS_Y:    // Twist: change the world mode.
      switch( s_world_mode )
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
      s_spin_speed    = 0 ;                         // Stop spinning motion.
      s_spin_rotation = SPIN_ROTATION_STEADY ;      // Spin rotation angle that allows to view days/hours/minutes faces.
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
  if (s_spin_speed == 0)
    ++s_user_secondsInactive ;

  // Auto-exit application on lack of user interaction.
  if (s_user_secondsInactive > USER_SECONDSINACTIVE_MAX)
  {
    world_stop( ) ;
    world_finalize( ) ;
    window_stack_pop_all( true )	;    // Exit app.
  }

  Clock3D_setTime_DDHHMMSS( &s_clock
                          , tick_time->tm_mday   // days
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
  CamR3_lookAtOriginUpwards( &s_cam
                           , TransformR3_rotateZ( R3_scale( CAM3D_DISTANCEFROMORIGIN    // View point.
                                                          , viewPoint
                                                          )
                                                , rotationZ
                                                )
                           , s_cam_zoom                                                   // Zoom
                           , CAM_PROJECTION_PERSPECTIVE
                           ) ;
}


void
set_world_mode
( const WorldMode pWorldMode )
{ // Clean-up exiting mode. Unsubscribe from no longer needed services.
  switch ( s_world_mode )
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
  switch ( s_world_mode = pWorldMode )
  {
    case WORLD_MODE_STEADY:
      s_spin_speed    = 0 ;                                  // Stop spinning motion.
      s_spin_rotation = SPIN_ROTATION_STEADY ;               // Spin rotation angle that allows to view days/hours/minutes faces.
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
  s_world_mode       = persist_exists(PKEY_WORLD_MODE)        ? persist_read_int(PKEY_WORLD_MODE)        : WORLD_MODE_DEFAULT ;
  s_transparencyMode = persist_exists(PKEY_TRANSPARENCY_MODE) ? persist_read_int(PKEY_TRANSPARENCY_MODE) : MESH_TRANSPARENCY_DEFAULT ;

  Clock3D_initialize( &s_clock ) ;

  s_clock.minutes_leftDigitA    ->mesh->inkBlinker
  = s_clock.minutes_leftDigitB  ->mesh->inkBlinker
  = s_clock.minutes_rightDigitA ->mesh->inkBlinker
  = s_clock.minutes_rightDigitB ->mesh->inkBlinker
  = &clock_minutes_inkBlinker
  ;

  sampler_initialize( ) ;
  interpolations_initialize( ) ;
  Clock3D_config( &s_clock, DIGIT2D_CURVYSKIN ) ;
}


// UPDATE CAMERA & WORLD OBJECTS PROPERTIES

static
void
world_update
( )
{
  ++s_world_updateCount ;

  Clock3D_updateAnimation( &s_clock, ANIMATION_FLIP_STEPS ) ;

  if (s_world_mode != WORLD_MODE_STEADY)
  {
    Clock3D_second100ths_update( &s_clock ) ;

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

    switch (s_world_mode)
    {
      case WORLD_MODE_DYNAMIC:
        // Friction: gradualy decrease spin speed until it stops.
        if (s_spin_speed > 0)
          --s_spin_speed ;

        if (s_spin_speed < 0)
          ++s_spin_speed ;

        if (s_spin_speed != 0)
          s_spin_rotation = FastMath_normalizeAngle( s_spin_rotation + (float)s_spin_speed * SPIN_ROTATION_QUANTA ) ;

        cam_rotation = s_spin_rotation ;
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
  layer_mark_dirty( s_world_layer ) ;
}


#ifdef LOG
static int s_world_draw_count = 0 ;
#endif

void
world_draw
( Layer    *me
, GContext *gCtx
)
{
  LOGD( "world_draw:: count = %d", ++s_world_draw_count ) ;

  // Disable antialiasing if running under QEMU (crashes after a few frames otherwise).
#ifdef QEMU
    graphics_context_set_antialiased( gCtx, false ) ;
#endif

  Clock3D_draw( gCtx, &s_clock, &s_cam, available_screen.w, available_screen.h, s_transparencyMode ) ;
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
  Clock3D_finalize( &s_clock ) ;
  sampler_finalize( ) ;
  interpolations_finalize( ) ;

  // Save current configuration into persistent storage on app exit.
  persist_write_int( PKEY_WORLD_MODE       , s_world_mode       ) ;
  persist_write_int( PKEY_TRANSPARENCY_MODE, s_transparencyMode ) ;
}


void
world_update_timer_handler
( void *data )
{
  world_update( ) ;

  // Call me again.
  s_world_updateTimer_ptr = app_timer_register( WORLD_UPDATE_INTERVAL_MS, world_update_timer_handler, data ) ;
}


void
world_start
( )
{ // Position s_clock handles according to current time.
  // Initialize blinkers.
  Blinker_start( &clock_minutes_inkBlinker
               , 500      // lengthOn (ms)
               , 500      // lengthOff (ms)
               , INK100   // inkOn (100%)
               , INK50    // inkOff (50%)
               ) ;

  // Set initial world mode (and subscribe to related services).
  set_world_mode( s_world_mode ) ;                                               

  // Activate s_clock
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
  app_timer_cancel( s_world_updateTimer_ptr ) ;

  // Stop s_clock.
  tick_timer_service_unsubscribe( ) ;

  // Tap unaware.
  accel_tap_service_unsubscribe( ) ;               

  // Gravity unaware.
  accel_data_service_unsubscribe( ) ;

  // Compass unaware.
  compass_service_unsubscribe( ) ;                 
}


void
unobstructed_area_change_handler
( AnimationProgress progress
, void             *context
)
{
  available_screen = layer_get_unobstructed_bounds( s_window_layer ).size ;
}


void
window_load
( Window *s_window )
{
  s_window_layer    = window_get_root_layer( s_window ) ;
  available_screen  = layer_get_unobstructed_bounds( s_window_layer ).size ;

  s_action_bar = action_bar_layer_create( ) ;
  action_bar_layer_add_to_window( s_action_bar, s_window ) ;
  action_bar_layer_set_click_config_provider( s_action_bar, normalMode_click_config_provider ) ;

  GRect bounds = layer_get_frame( s_window_layer ) ;
  s_world_layer = layer_create( bounds ) ;
  layer_set_update_proc( s_world_layer, world_draw ) ;
  layer_add_child( s_window_layer, s_world_layer ) ;

  // Obstrution handling.
  UnobstructedAreaHandlers unobstructed_area_handlers = { .change = unobstructed_area_change_handler } ;
  unobstructed_area_service_subscribe( unobstructed_area_handlers, NULL ) ;

  // Position s_clock handles according to current time, launch blinkers, launch animation, start s_clock.
  world_start( ) ;
}


void
window_unload
( Window *s_window )
{
  world_stop( ) ;
  unobstructed_area_service_unsubscribe( ) ;
  layer_destroy( s_world_layer ) ;
}


void
app_init
( void )
{
  world_initialize( ) ;

  s_window = window_create( ) ;
  window_set_background_color( s_window, GColorBlack ) ;
 
  window_set_window_handlers( s_window
                            , (WindowHandlers)
                              { .load   = window_load
                              , .unload = window_unload
                              }
                            ) ;

  window_stack_push( s_window, false ) ;
}


void
app_deinit
( void )
{
  window_stack_remove( s_window, false ) ;
  window_destroy( s_window ) ;
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