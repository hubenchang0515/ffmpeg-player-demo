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

/* 音频线程数据 */
typedef struct AudioUserData
{
    DecoderData* decoder;
    SDL_mutex* audioMutex;
    bool end;
}AudioUserData;

int threadDecode(void* userdata);
void getAudioData(void *userdata, Uint8* stream, int len);
void delayFps(double fps);

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
    DecoderData* data = createDecoder();
    decoderUnpack(data, argv[1]);
    decoderInitVideoCodec(data);
    decoderInitSwScale(data, 640, 360, AV_PIX_FMT_YUV420P);
    decoderInitAudioCodec(data);
    decoderInitSwResample(data, AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_FLT, 44100);

    AudioUserData audio;
    audio.decoder = data;
    audio.end = false;
    audio.audioMutex = SDL_CreateMutex();

    /* 打开音频设备 */
    SDL_AudioSpec audioSpec;
    audioSpec.channels = 2;             // stereo
    audioSpec.format = AUDIO_F32;       // 32bit 小端浮点数
    audioSpec.freq = 44100;             // 44100Hz
    audioSpec.silence = 0;
    audioSpec.samples = decoderSamples(data);

    audioSpec.userdata = &audio;
    audioSpec.callback = getAudioData;
    SDL_OpenAudio(&audioSpec, NULL);

    /* 创建线程进行解码 */
    SDL_Thread* thread = SDL_CreateThread(threadDecode, "threadDecode", data);

    /* 开始播放音频 */
    SDL_PauseAudio(0);

    SDL_Event event;
    double fps = decoderFps(data);
    while (1)
    {
        // 收到退出事件，退出
        if (SDL_PollEvent(&event) > 0 && event.type == SDL_QUIT)
        {
            decoderNotifyBuffer(data);
            decoderSetEnd(data, true);
            audio.end = true;
            break;
        }

        void* videoBuffer = decoderPopVideo(data);
        if (videoBuffer != NULL)
        {
            decoderNotifyBuffer(data);
            SDL_UpdateTexture(texture, NULL, videoBuffer, WIDTH);
            SDL_RenderCopy(renderer, texture, NULL, NULL);
            SDL_RenderPresent(renderer);
            free(videoBuffer);
            delayFps(fps);
        }
        else if(decoderIsEnd(data) && audio.end)
        {
            break;
        }
    }

    SDL_WaitThread(thread, NULL);       // 等待解码线程退出
    SDL_PauseAudio(1);
    
    deleteDecoder(data);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return EXIT_SUCCESS;
}

void delayFps(double fps)
{
    static int prevTicks = 0;

    if (prevTicks == 0)
    {
        SDL_Delay(1000 / fps);
        prevTicks = SDL_GetTicks();
        return;
    }

    int ticks = SDL_GetTicks();

    int delay = 1000 / fps - (ticks - prevTicks);
    if (delay > 0)
        SDL_Delay(delay);

    prevTicks = SDL_GetTicks();
}

int threadDecode(void* userdata)
{
    DecoderData* data = (DecoderData*)(userdata);
    return decoderRun(data);
}

void getAudioData(void *userdata, Uint8* stream, int len)
{
    AudioUserData* data = (AudioUserData*)(userdata);
    DecoderData* decoder = data->decoder;
    void* audioBuffer = decoderPopAudio(decoder);
    if (audioBuffer != NULL)
    {
        SDL_memcpy(stream, audioBuffer, len);
        free(audioBuffer);
        decoderNotifyBuffer(decoder);
    }
    else if (decoderIsEnd(decoder))
    {
        data->end = true;
    }
}