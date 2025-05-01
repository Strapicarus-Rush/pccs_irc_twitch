#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>
#include <cmath>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <iostream>
// Constants
constexpr int SCREEN_WIDTH = 800;
constexpr int SCREEN_HEIGHT = 600;
constexpr int DISPLAY_DURATION_MS = 90000;

// Paths
const char* IMAGE_PATH = "~/Imágenes/patreon/nuevofollow.jpg";
const char* AUDIO_PATH = "~/Música/LASERS_EP-11879/LASERS_-_01_-_Amsterdam.flac";
const char* FONT_PATH = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf";
std::atomic<bool> running {true};

std::string expand_path(const char* path) {
    if (path[0] == '~') {
        const char* home = getenv("HOME");
        return std::string(home) + (path + 1);
    }
    return std::string(path);
}

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

void render_alert(SDL_Renderer* renderer, SDL_Texture* imgTexture, SDL_Texture* textTexture, SDL_Rect textRect) {
    Uint32 start = SDL_GetTicks();

    while (running) {
        Uint32 now = SDL_GetTicks();
        Uint32 elapsed = now - start;
        if (elapsed > DISPLAY_DURATION_MS) break;

        float t = elapsed / static_cast<float>(DISPLAY_DURATION_MS);
        Uint8 alpha = 255;
        if (t < 0.2f) {
            alpha = static_cast<Uint8>(lerp(0, 255, t / 0.2f));
        } else if (t > 0.8f) {
            alpha = static_cast<Uint8>(lerp(255, 0, (t - 0.8f) / 0.2f));
        }

        SDL_SetTextureAlphaMod(imgTexture, alpha);
        SDL_SetTextureAlphaMod(textTexture, alpha);

        float wobble = std::sinf(now * 0.01f) * 4.0f;
        float scale = 0.5f + 0.05f * std::sinf(now * 0.001f);

        int iw = static_cast<int>(SCREEN_WIDTH * scale);
        int ih = static_cast<int>(SCREEN_HEIGHT * scale);
        int ix = static_cast<int>((SCREEN_WIDTH - iw) / 2 + wobble);
        int iy = static_cast<int>((SCREEN_HEIGHT - ih) / 2 + wobble);
        SDL_Rect imgRect = { ix, iy, iw, ih };

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, imgTexture, nullptr, &imgRect);
        SDL_RenderCopy(renderer, textTexture, nullptr, &textRect);
        SDL_RenderPresent(renderer);

        // Actual OS-level sleep (~60fps, ultra low CPU)
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    /*if (std::this_thread.joinable())
    {
        std::this_thread.join();
    }*/
}

int main() {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
    IMG_Init(IMG_INIT_JPG);
    TTF_Init();
    Mix_Init(MIX_INIT_MP3);

    SDL_Window* window = SDL_CreateWindow("Follow Alert", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          SCREEN_WIDTH, SCREEN_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    // Load image
    std::string imgPath = expand_path(IMAGE_PATH);
    SDL_Surface* imgSurf = IMG_Load(imgPath.c_str());
    SDL_Texture* imgTexture = SDL_CreateTextureFromSurface(renderer, imgSurf);
    SDL_FreeSurface(imgSurf);

    // Load font + text
    TTF_Font* font = TTF_OpenFont(FONT_PATH, 48);
    SDL_Color white = {255, 255, 255, 255};
    SDL_Surface* textSurf = TTF_RenderText_Blended(font, "Thank you follower", white);
    SDL_Texture* textTexture = SDL_CreateTextureFromSurface(renderer, textSurf);
    SDL_Rect textRect = {
        SCREEN_WIDTH / 2 - textSurf->w / 2,
        SCREEN_HEIGHT - 120,
        textSurf->w,
        textSurf->h
    };
    SDL_FreeSurface(textSurf);

    // Load audio
    Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048);
    std::string audioPath = expand_path(AUDIO_PATH);
    Mix_Music* music = Mix_LoadMUS(audioPath.c_str());

    SDL_Event e;

    while (running) {
        if (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                std::cout << "running false..." << std::endl;
                running = false;
                break;
            } else if (e.type == SDL_KEYDOWN) {
                Mix_PlayMusic(music, 1);
                render_alert(renderer, imgTexture, textTexture, textRect);
                Mix_HaltMusic();

                // After alert, clear screen
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                SDL_RenderClear(renderer);
                SDL_RenderPresent(renderer);
            }
        }
    }

    // Cleanup
    SDL_DestroyTexture(imgTexture);
    SDL_DestroyTexture(textTexture);
    Mix_FreeMusic(music);
    TTF_CloseFont(font);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    Mix_CloseAudio();
    Mix_Quit();
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();

    return 0;
}