#include <stdio.h>
#include <stdlib.h>

#include <SDL2/SDL.h>               // libsdl2-dev

/* ffmpeg */
#include <libavformat/avformat.h>   // libavformat-dev  : Audio-Video Foramt - 用于音视频文件封装、解封装
#include <libavcodec/avcodec.h>     // libavcodec-dev   : Audio-Video Codec - 用于音视频数据编解码
#include <libavutil/imgutils.h>     // libavutil-dev    : Audio-Video Utilities - 一些实用函数
#include <libswscale/swscale.h>     // libswscale-dev   : Software Scale - 软件缩放算法

#include "decoder.h"

/* 视频通常使用 16:9 的分辨率 */
static const int WIDTH = 640;
static const int HEIGHT = 360; 

int main(int argc, char* argv[])
{
    /* 参数检查 */
    if (argc != 2)
    {
        printf("Usage: %s <file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* 初始化 */
    SDL_Init(SDL_INIT_EVERYTHING);

    /* 创建窗口 */
    SDL_Window* window = NULL;
    SDL_Renderer* renderer = NULL;
    if (SDL_CreateWindowAndRenderer(WIDTH, HEIGHT, SDL_WINDOW_SHOWN, &window, &renderer) < 0)
    {
        fprintf(stderr, "SDL_CreateWindowAndRenderer failed\n");
        return EXIT_FAILURE;
    }
    

    // 创建纹理和互斥量等
    DecoderData data;
    data.file = argv[1];
    data.width = WIDTH;
    data.height = HEIGHT;
    data.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_TARGET|SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);
    data.renderMutex = SDL_CreateMutex();
    data.exitMutex = 0;
    data.exitMutex = SDL_CreateMutex();

    SDL_Thread* thread = SDL_CreateThread(decoder, "decoder", &data);

    SDL_Event event;
    while (1)
    {
        // 收到退出事件，退出
        if (SDL_PollEvent(&event) > 0 && event.type == SDL_QUIT)
        {
            SDL_LockMutex(data.exitMutex);
            data.exit = 1;
            SDL_UnlockMutex(data.exitMutex);
            break;
        }

        // 播放完毕，退出
        SDL_LockMutex(data.exitMutex);
        int exit = data.exit;
        SDL_UnlockMutex(data.exitMutex);
        if (exit)
        {
            break;
        }

        SDL_LockMutex(data.renderMutex);
        SDL_RenderCopy(renderer, data.texture, NULL, NULL);
        SDL_RenderPresent(renderer);
        SDL_UnlockMutex(data.renderMutex);
    }

    SDL_WaitThread(thread, NULL);
    
    SDL_DestroyTexture(data.texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}