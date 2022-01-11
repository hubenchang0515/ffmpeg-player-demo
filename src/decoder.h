#ifndef FFMPEG_PLAYER_DEMO_DECODER
#define FFMPEG_PLAYER_DEMO_DECODER

typedef struct SDL_Texture SDL_Texture;
typedef struct SDL_mutex SDL_mutex;

typedef struct DecoderData
{
    const char* file;
    int width;
    int height;
    SDL_Texture* texture;
    SDL_mutex* renderMutex;
    int exit;
    SDL_mutex* exitMutex;
}DecoderData;

int decoder(void* userdata);

#endif // FFMPEG_PLAYER_DEMO_DECODER