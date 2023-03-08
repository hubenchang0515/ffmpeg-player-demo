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
static const int WIDTH = 1920;
static const int HEIGHT = 1080;

/* 音频线程数据 */
typedef struct AudioUserData
{
    DecoderData* decoder;
    int64_t startTicks;
    bool end;
}AudioUserData;

int threadDecode(void* userdata);
void getAudioData(void *userdata, Uint8* stream, int len);
bool delayTo(AudioUserData* audio, uint32_t ms);

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
    decoderInitSwScale(data, WIDTH, HEIGHT, AV_PIX_FMT_YUV420P);
    decoderInitAudioCodec(data);
    decoderInitSwResample(data, AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_FLT, 44100);

    AudioUserData audio;
    audio.decoder = data;
    audio.end = false;
    audio.startTicks = 0;

    /* 打开音频设备 */
    SDL_AudioSpec audioSpec;
    audioSpec.channels = 2;             // stereo
    audioSpec.format = AUDIO_F32;       // 32bit 小端浮点数
    audioSpec.freq = 44100;             // 44100Hz
    audioSpec.silence = 0;
    audioSpec.samples = decoderSamples(data);

    audioSpec.userdata = &audio;
    audioSpec.callback = getAudioData;
    SDL_AudioDeviceID audioDeviceId = SDL_OpenAudioDevice(NULL, 0, &audioSpec, NULL, 0);
    if (audioDeviceId <= 0)
    {
        printf("cannot open audio device\n");
        audio.end = true;
    }

    /* 创建线程进行解码 */
    SDL_Thread* thread = SDL_CreateThread(threadDecode, "threadDecode", data);

    /* 开始播放音频 */
    SDL_PauseAudioDevice(audioDeviceId, 0);

    SDL_Event event;
    bool running = true;
    while (running)
    {
        // 收到退出事件，退出
        while (SDL_PollEvent(&event) > 0)
        {
            if (event.type == SDL_QUIT)
            {
                decoderNotifyBuffer(data);
                decoderSetEnd(data, true);
                audio.end = true;
                running = false;
                break;
            }
        }

        int64_t pts = 0;
        void* videoBuffer = decoderPopVideo(data, &pts); // TODO: 这里没有消息同步，一直读，导致CPU占用高
        if (videoBuffer != NULL)
        {
            decoderNotifyBuffer(data);
            
            // 如果进度落后就跳过当前
            if (delayTo(&audio, pts))
            {
                SDL_UpdateTexture(texture, NULL, videoBuffer, WIDTH);
                SDL_RenderCopy(renderer, texture, NULL, NULL);
                SDL_RenderPresent(renderer);
            }
            
            free(videoBuffer);
        }
        else if(decoderIsEnd(data) && audio.end)
        {
            break;
        }
    }

    SDL_WaitThread(thread, NULL);       // 等待解码线程退出
    SDL_PauseAudioDevice(audioDeviceId, 1);
    SDL_CloseAudioDevice(audioDeviceId);
    
    deleteDecoder(data);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return EXIT_SUCCESS;
}

bool delayTo(AudioUserData* audio, uint32_t ms)
{
    if (audio->startTicks == 0)
    {
        audio->startTicks = SDL_GetTicks();
    }

    if (audio->startTicks + ms > SDL_GetTicks())
    {
        SDL_Delay(audio->startTicks + ms - SDL_GetTicks());
        return true;
    }

    return false;
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
    int64_t pts;
    void* audioBuffer = decoderPopAudio(decoder, &pts);
    if (audioBuffer != NULL)
    {
        data->startTicks = SDL_GetTicks() - pts;
        SDL_memcpy(stream, audioBuffer, len);
        free(audioBuffer);
        decoderNotifyBuffer(decoder);
    }
    else if (decoderIsEnd(decoder))
    {
        data->end = true;
    }
    else
    {
        // 清除开始时的噪音
        memset(stream, 0, len);
    }
}