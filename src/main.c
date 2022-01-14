#include <stdio.h>
#include <stdlib.h>

#include <SDL2/SDL.h>               // libsdl2-dev

/* ffmpeg */
#include <libavformat/avformat.h>   // libavformat-dev  : Audio-Video Foramt - 用于音视频文件封装、解封装
#include <libavcodec/avcodec.h>     // libavcodec-dev   : Audio-Video Codec - 用于音视频数据编解码
#include <libavutil/imgutils.h>     // libavutil-dev    : Audio-Video Utilities - 一些实用函数
#include <libswscale/swscale.h>     // libswscale-dev   : Software Scale - 软件缩放算法

#include "queue.h"
#include "decoder.h"

/* 视频通常使用 16:9 的分辨率 */
static const int WIDTH = 640;
static const int HEIGHT = 360;

void getAudioData(void *userdata, Uint8* stream, int len);

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

    // 创建纹理
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_TARGET|SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);
    
    // 创建跨线程交互数据
    DecoderData* data = createDecoderData(argv[1]);
    unpack(data);
    initVideoCodec(data);
    initSwScale(data, 640, 360, AV_PIX_FMT_YUV420P);
    initAudioCodec(data);
    initSwResample(data, AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_FLT, 44100);

    /* 打开音频设备 */
    SDL_AudioSpec audioSpec;
    audioSpec.channels = 2;             // stereo
    audioSpec.format = AUDIO_F32LSB;    // 32bit 小端浮点数
    audioSpec.freq = 44100;             // 44100Hz
    audioSpec.silence = 0;
    audioSpec.samples = 1024;

    audioSpec.userdata = data;
    audioSpec.callback = getAudioData;
    SDL_OpenAudio(&audioSpec, NULL);

    /* 创建线程进行解码 */
    SDL_Thread* thread = SDL_CreateThread(decoder, "decoder", data);

    /* 开始播放音频 */
    SDL_PauseAudio(0);

    SDL_Event event;
    while (1)
    {
        // 收到退出事件，退出
        if (SDL_PollEvent(&event) > 0 && event.type == SDL_QUIT)
        {
            setExit(data, 1);
            break;
        }

        // 播放完毕，退出
        if (isExit(data))
            break;

        void* videoBuffer = popVideo(data);
        if (videoBuffer != NULL)
        {
            SDL_UpdateTexture(texture, NULL, videoBuffer, WIDTH);
            SDL_RenderCopy(renderer, texture, NULL, NULL);
            SDL_RenderPresent(renderer);
            free(videoBuffer);
        }
    }

    SDL_PauseAudio(1);
    SDL_WaitThread(thread, NULL);
    
    deleteDecoderData(data);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return EXIT_SUCCESS;
}

void getAudioData(void *userdata, Uint8* stream, int len)
{
    DecoderData* data = (DecoderData*)(userdata);
    void* audioBuffer = popAudio(data);
    if (audioBuffer != NULL)
    {
        SDL_memcpy(stream, audioBuffer, len);
    }
}