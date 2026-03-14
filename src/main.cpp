#include "app.h"
#include "gui/window.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <backends/imgui_impl_sdl3.h>

#include <chrono>
#include <thread>
#include <signal.h>

#include "cli.h"

// signal handler forwards termination requests to the App singleton
static void signalHandler(int /*sig*/) {
    requestAppExit();
}

int main(int argc, char** argv) {
    // parse CLI options early so we know whether we need video support
    CliOptions opts = parseCli(argc, argv);

    // install a simple signal handler to catch termination requests from
    // external controllers (e.g. `timeout` or container orchestrators).
    // The handler will flip a flag that the App observes and cleanly exit.
    ::signal(SIGTERM, signalHandler);
    ::signal(SIGINT, signalHandler);

    // initialise SDL with video only if GUI is requested; otherwise we
    // skip SDL completely (we only use std::chrono for timing in headless
    // mode).  This avoids pulling in video subsystems on CI.
    if (opts.useGui) {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            SDL_Log("SDL_Init failed: %s", SDL_GetError());
            return 1;
        }
    }

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;

    if (opts.useGui) {
        // create a maximized (borderless) window by default, or a normal
        // resizable window if explicitly requested.  In SDL3 the
        // SDL_WINDOW_MAXIMIZED flag is used for this behaviour – it mirrors the
        // old SDL_WINDOW_FULLSCREEN_DESKTOP from SDL2 but keeps the window in
        // windowed mode so Alt+Tab works and the display mode isn't changed.
        uint32_t winFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
        if (opts.fullscreen)
            winFlags |= SDL_WINDOW_MAXIMIZED;

        int w = opts.fullscreen ? 0 : 1400;
        int h = opts.fullscreen ? 0 : 900;
        window = SDL_CreateWindow("WASM Quine Bootloader", w, h, winFlags);
        if (!window) {
            SDL_Log("SDL_CreateWindow: %s", SDL_GetError());
            SDL_Quit();
            return 1;
        }

        renderer = SDL_CreateRenderer(window, nullptr);
        if (!renderer) {
            SDL_Log("SDL_CreateRenderer: %s", SDL_GetError());
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
        SDL_SetRenderVSync(renderer, 1);

        Gui gui;
        gui.init(window, renderer);

        App  app(opts);
        bool running = true;
        while (running) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) {
                ImGui_ImplSDL3_ProcessEvent(&ev);
                if (ev.type == SDL_EVENT_QUIT) running = false;
                if (ev.type == SDL_EVENT_KEY_DOWN) {
                    if (ev.key.key == SDLK_SPACE)  app.togglePause();
                    if (ev.key.key == SDLK_E)        app.exportNow();
                    if (ev.key.key == SDLK_F) {      // toggle fullscreen/windowed
                        Uint32 flags = SDL_GetWindowFlags(window);
                        if (flags & SDL_WINDOW_MAXIMIZED) {
                            SDL_RestoreWindow(window);
                        } else {
                            SDL_MaximizeWindow(window);
                        }
                    }
                    if (ev.key.key == SDLK_H) {      // show a simple help text in logs
                        app.log("Shortcut: Space=pause, E=export, F=toggle fullscreen, H=help, Q/Esc=quit", "info");
                    }
                    if (ev.key.key == SDLK_Q ||
                        ev.key.key == SDLK_ESCAPE)  running = false;
                }
            }

            // honor return value; `update()` will return false when the
            // max-gen or max-run-ms limit has been reached, or when the app
            // requests shutdown via other means.
            if (!app.update())
                running = false;
            gui.renderFrame(app);
        }

        gui.shutdown();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
    } else {
        // fallback headless mode (very simple for now).  We still create an
        // App instance and call update() on a timer so that the core logic
        // exercises the boot sequence; logs are not rendered.
        App app(opts);
        bool running = true;
        using Clock = std::chrono::high_resolution_clock;
        auto last = Clock::now();
        while (running) {
            running = app.update();
            auto now = Clock::now();
            auto diff = now - last;
            if (diff < std::chrono::milliseconds(16))
                std::this_thread::sleep_for(std::chrono::milliseconds(16) - diff);
            last = now;
        }
    }

    SDL_Quit();
    return 0;
}
