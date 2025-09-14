/*
 * Compile: gcc -shared -fPIC -o libsdl2_scaler.so libsdl2_scaler.c -ldl -lSDL2
 */

 #define _GNU_SOURCE
 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include <dlfcn.h>
 #include <SDL2/SDL.h>
 
 /* Use variables, not macros, for forced resolution */
 static int FORCED_WIDTH = 640;
 static int FORCED_HEIGHT = 480;
 
 /* Debug logging */
 #define DEBUG_LOG 1
 
 #if DEBUG_LOG
 #define LOG(fmt, ...) fprintf(stderr, "[SDL2 PATCH LIBRARY]" fmt "\n", ##__VA_ARGS__)
 #else
 #define LOG(fmt, ...)
 #endif
 
 /* Function pointers to real SDL functions */
 static SDL_Window *(*real_SDL_CreateWindow)(const char *, int, int, int, int, Uint32) = NULL;
 static int (*real_SDL_SetWindowSize)(SDL_Window *, int, int) = NULL;
 static void (*real_SDL_GetWindowSize)(SDL_Window *, int *, int *) = NULL;
 static Uint32 (*real_SDL_GetMouseState)(int *, int *) = NULL;
 static int (*real_SDL_PollEvent)(SDL_Event *) = NULL;
 static int (*real_SDL_WaitEvent)(SDL_Event *) = NULL;
 static SDL_Surface *(*real_SDL_CreateRGBSurface)(Uint32, int, int, int, Uint32, Uint32, Uint32, Uint32) = NULL;
 static SDL_Surface *(*real_SDL_SetVideoMode)(int, int, int, Uint32) = NULL;
 
 /* Global state */
 static int g_requested_width = 640;
 static int g_requested_height = 480;
 static int g_initialized = 0;
 
 /* Initialize real function pointers */
 static void init_real_functions(void)
 {
     if (g_initialized)
         return;
 
     LOG("Initializing real SDL function pointers...");
 
     real_SDL_CreateWindow = dlsym(RTLD_NEXT, "SDL_CreateWindow");
     real_SDL_SetWindowSize = dlsym(RTLD_NEXT, "SDL_SetWindowSize");
     real_SDL_GetWindowSize = dlsym(RTLD_NEXT, "SDL_GetWindowSize");
     real_SDL_GetMouseState = dlsym(RTLD_NEXT, "SDL_GetMouseState");
     real_SDL_PollEvent = dlsym(RTLD_NEXT, "SDL_PollEvent");
     real_SDL_WaitEvent = dlsym(RTLD_NEXT, "SDL_WaitEvent");
     real_SDL_CreateRGBSurface = dlsym(RTLD_NEXT, "SDL_CreateRGBSurface");
     real_SDL_SetVideoMode = dlsym(RTLD_NEXT, "SDL_SetVideoMode");
 
     if (!real_SDL_CreateWindow)
     {
         LOG("WARNING: Could not find real SDL_CreateWindow");
     }
 
     g_initialized = 1;
     LOG("SDL function pointers initialized");
 }
 
 /* Helper function to transform coordinates */
 static void transform_coordinates_to_requested(int real_x, int real_y, int *req_x, int *req_y)
 {
     if (req_x)
         *req_x = (real_x * g_requested_width) / FORCED_WIDTH;
     if (req_y)
         *req_y = (real_y * g_requested_height) / FORCED_HEIGHT;
 }
 
 static void transform_coordinates_to_real(int req_x, int req_y, int *real_x, int *real_y)
 {
     if (real_x)
         *real_x = (req_x * FORCED_WIDTH) / g_requested_width;
     if (real_y)
         *real_y = (req_y * FORCED_HEIGHT) / g_requested_height;
 }
 
 /* Intercepted SDL functions */
 
 SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags)
 {
     init_real_functions();
 
     LOG("Intercepted SDL_CreateWindow: %dx%d -> %dx%d", w, h, FORCED_WIDTH, FORCED_HEIGHT);
 
     /* Store requested dimensions */
     g_requested_width = w;
     g_requested_height = h;
 
     /* Always create at forced resolution */
     SDL_Window *result = real_SDL_CreateWindow(title, x, y, FORCED_WIDTH, FORCED_HEIGHT, flags);
 
     if (result)
     {
         LOG("Successfully created %dx%d window", FORCED_WIDTH, FORCED_HEIGHT);
     }
     else
     {
         LOG("Failed to create window: %s", SDL_GetError());
     }
 
     return result;
 }
 
 void SDL_SetWindowSize(SDL_Window *window, int w, int h)
 {
     init_real_functions();
 
     LOG("Intercepted SDL_SetWindowSize: %dx%d -> %dx%d", w, h, FORCED_WIDTH, FORCED_HEIGHT);
 
     /* Update requested size but don't actually resize */
     g_requested_width = w;
     g_requested_height = h;
 }
 
 void SDL_GetWindowSize(SDL_Window *window, int *w, int *h)
 {
     init_real_functions();
 
     /* Always report the forced size as the actual window size */
     if (w)
         *w = FORCED_WIDTH;
     if (h)
         *h = FORCED_HEIGHT;
 
     LOG("SDL_GetWindowSize returning: %dx%d", FORCED_WIDTH, FORCED_HEIGHT);
 }
 /*
  * I need mouse things apperantly or it doesnt compile, idfk why, maybe i added something that i shouldnt have added */
 Uint32 SDL_GetMouseState(int *x, int *y)
 {
     init_real_functions();
 
     Uint32 result = real_SDL_GetMouseState(x, y);
 
     /* Transform coordinates to requested resolution */
     if (x && y)
     {
         int old_x = *x, old_y = *y;
         transform_coordinates_to_requested(*x, *y, x, y);
         LOG("Mouse coordinates transformed: (%d,%d) -> (%d,%d)", old_x, old_y, *x, *y);
     }
 
     return result;
 }
 
 static void transform_event_coordinates(SDL_Event *event)
 {
     if (!event)
         return;
 
     switch (event->type)
     {
     case SDL_MOUSEBUTTONDOWN:
     case SDL_MOUSEBUTTONUP:
         transform_coordinates_to_requested(event->button.x, event->button.y,
                                            &event->button.x, &event->button.y);
         LOG("Mouse button event coordinates transformed to (%d,%d)",
             event->button.x, event->button.y);
         break;
 
     case SDL_MOUSEMOTION:
         transform_coordinates_to_requested(event->motion.x, event->motion.y,
                                            &event->motion.x, &event->motion.y);
         /* Transform relative motion too */
         event->motion.xrel = (event->motion.xrel * g_requested_width) / FORCED_WIDTH;
         event->motion.yrel = (event->motion.yrel * g_requested_height) / FORCED_HEIGHT;
         LOG("Mouse motion event coordinates transformed to (%d,%d) rel(%d,%d)",
             event->motion.x, event->motion.y, event->motion.xrel, event->motion.yrel);
         break;
 
     case SDL_MOUSEWHEEL:
         break;
     }
 }
 
 int SDL_PollEvent(SDL_Event *event)
 {
     init_real_functions();
 
     int result = real_SDL_PollEvent(event);
 
     if (result && event)
     {
         transform_event_coordinates(event);
     }
 
     return result;
 }
 
 int SDL_WaitEvent(SDL_Event *event)
 {
     init_real_functions();
 
     int result = real_SDL_WaitEvent(event);
 
     if (result && event)
     {
         transform_event_coordinates(event);
     }
 
     return result;
 }
 
 SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth,
                                   Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask)
 {
     init_real_functions();
 
     /* Check if this surface matches the requested screen size */
     if (width == g_requested_width && height == g_requested_height)
     {
         LOG("Redirecting surface creation: %dx%d -> %dx%d", width, height, FORCED_WIDTH, FORCED_HEIGHT);
         return real_SDL_CreateRGBSurface(flags, FORCED_WIDTH, FORCED_HEIGHT, depth, Rmask, Gmask, Bmask, Amask);
     }
 
     /* Let other surfaces pass through unchanged */
     return real_SDL_CreateRGBSurface(flags, width, height, depth, Rmask, Gmask, Bmask, Amask);
 }
 
 /* SDL 1.2 compatibility */
 SDL_Surface *SDL_SetVideoMode(int width, int height, int bpp, Uint32 flags)
 {
     init_real_functions();
 
     LOG("Intercepted SDL_SetVideoMode: %dx%d -> %dx%d", width, height, FORCED_WIDTH, FORCED_HEIGHT);
 
     /* Store requested dimensions */
     g_requested_width = width;
     g_requested_height = height;
 
     /* Always set to forced resolution */
     SDL_Surface *result = NULL;
     if (real_SDL_SetVideoMode)
     {
         result = real_SDL_SetVideoMode(FORCED_WIDTH, FORCED_HEIGHT, bpp, flags);
     }
 
     if (result)
     {
         LOG("Successfully set %dx%d video mode", FORCED_WIDTH, FORCED_HEIGHT);
     }
     else
     {
         LOG("Failed to set video mode: %s", SDL_GetError());
     }
 
     return result;
 }
 
 /* Constructor - called when library is loaded */
 __attribute__((constructor)) static void library_init(void)
 {
     LOG("Patch library loaded");
 
     /* Check environment variables for configuration */
     const char *env_width = getenv("SDL_DEVICE_WIDTH");
     const char *env_height = getenv("SDL_DEVICE_HEIGHT");
 
     if (env_width && env_height)
     {
         int w = atoi(env_width);
         int h = atoi(env_height);
         if (w > 0 && h > 0)
         {
             FORCED_WIDTH = w;
             FORCED_HEIGHT = h;
             LOG("Using custom resolution from environment: %dx%d", w, h);
         }
     }
 
     init_real_functions();
 }
 
 /* Destructor - called when library is unloaded */
 __attribute__((destructor)) static void library_cleanup(void)
 {
     LOG("SDL2 320x240 LD_PRELOAD library unloaded");
 }
 
 /* Get the actual forced resolution */
 void SDL_GetForcedResolution(int *width, int *height)
 {
     if (width)
         *width = FORCED_WIDTH;
     if (height)
         *height = FORCED_HEIGHT;
 }
 
 /* Get the requested resolution */
 void SDL_GetRequestedResolution(int *width, int *height)
 {
     if (width)
         *width = g_requested_width;
     if (height)
         *height = g_requested_height;
 }
 
 /* Get scaling factors */
 void SDL_GetScalingFactors(float *scale_x, float *scale_y)
 {
     if (scale_x)
         *scale_x = (float)FORCED_WIDTH / (float)g_requested_width;
     if (scale_y)
         *scale_y = (float)FORCED_HEIGHT / (float)g_requested_height;
 }
 
 /* Manual coordinate transformation functions */
 void SDL_TransformToReal(int req_x, int req_y, int *real_x, int *real_y)
 {
     transform_coordinates_to_real(req_x, req_y, real_x, real_y);
 }
 
 void SDL_TransformToRequested(int real_x, int real_y, int *req_x, int *req_y)
 {
     transform_coordinates_to_requested(real_x, real_y, req_x, req_y);
 }