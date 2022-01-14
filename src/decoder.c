#include <stdio.h>
#include <stdlib.h>

#include <SDL2/SDL.h>               // libsdl2-dev

/* ffmpeg */
#include <libavformat/avformat.h>       // libavformat-dev   : Audio-Video Foramt - 用于音视频文件封装、解封装
#include <libavcodec/avcodec.h>         // libavcodec-dev    : Audio-Video Codec - 用于音视频数据编解码
#include <libavutil/imgutils.h>         // libavutil-dev     : Audio-Video Utilities - 一些实用函数
#include <libswscale/swscale.h>         // libswscale-dev    : Software Scale - 软件缩放算法
#include <libswresample/swresample.h>   // libswresample-dev : Software Resample - 软件重采样算法

#include "decoder.h"

typedef struct DecoderData
{
    const char* file;
    
    int width;
    int height;
    
    SDL_mutex* videoMutex;
    Queue* videoQueue;

    SDL_mutex* exitMutex;
    int exit;

    SDL_mutex* audioMutex;
    Queue* audioQueue;

    AVFormatContext* formatContext;
    int videoIndex;                 // 视频流的索引
    int audioIndex;                 // 音频流的索引
    
    AVStream* videoStream;          // 视频流
    AVCodecParameters* videoParams; // 视频流参数
    AVCodec* videoCodec;            // 视频解码器
    AVCodecContext* videoContext;   // 视频解码器上下文
    AVFrame* decodedVideoFrame;     // 解码后的视频帧
    int videoBufferSize;            // 缩放后的视频缓冲区大小
    void* displayVideoBuffer;       // 缩放后的视频缓冲区
    AVFrame* displayVideoFrame;     // 缩放后的视频帧
    struct SwsContext* swsContext;  // 缩放算法上下文

    AVStream* audioStream;          // 音频流
    AVCodecParameters* audioParams; // 音频流参数
    AVCodec* audioCodec;            // 音频解码器
    AVCodecContext* audioContext;   // 音频解码器上下文
    AVFrame* decodedAudioFrame;     // 解码后的音频帧
    int audioBufferSize;            // 重采样后音频缓冲区大小
    uint8_t* displayAudioBuffer;    // 重采样后音频缓冲区
    SwrContext* swrContext;         // 重采样上下文
}DecoderData;

// 初始化
DecoderData* createDecoderData(const char* file, int width, int height)
{
    DecoderData* data = malloc(sizeof(DecoderData));
    if (data == NULL)
    {
        fprintf(stderr,  "%s:%d bad alloc\n", __FILE__, __LINE__);
        return NULL;
    }

    data->file = file;
    data->width = width;
    data->height = height;

    data->videoMutex = SDL_CreateMutex();
    data->videoQueue = NULL;

    data->exitMutex = SDL_CreateMutex();
    data->exit = 0;

    data->audioMutex = SDL_CreateMutex();
    data->audioQueue = NULL;

    return data;
}

// 删除
void deleteDecoderData(DecoderData* data)
{
    deleteQueue(data->videoQueue);
    deleteQueue(data->audioQueue);
    SDL_DestroyMutex(data->videoMutex);
    SDL_DestroyMutex(data->audioMutex);
    free(data);
}

// 退出
void setExit(DecoderData* data, int n)
{
    SDL_LockMutex(data->exitMutex);
    data->exit = n;
    SDL_UnlockMutex(data->exitMutex);
}

// 读取是否退出
int isExit(const DecoderData* data)
{
    SDL_LockMutex(data->exitMutex);
    int n = data->exit;
    SDL_UnlockMutex(data->exitMutex);
    return n;
}

// 压入一帧视频数据
void pushVideo(DecoderData* data, void* videoBuffer)
{
    SDL_LockMutex(data->videoMutex);
    pushQueue(data->videoQueue, videoBuffer);
    SDL_UnlockMutex(data->videoMutex);
}

// 弹出一帧视频数据
void* popVideo(DecoderData* data)
{
    SDL_LockMutex(data->videoMutex);
    void* buffer = popQueue(data->videoQueue);
    SDL_UnlockMutex(data->videoMutex);

    return buffer;
}

// 压入一帧音频数据
void pushAudio(DecoderData* data, void* audioBuffer)
{
    SDL_LockMutex(data->videoMutex);
    pushQueue(data->audioQueue, audioBuffer);
    SDL_UnlockMutex(data->videoMutex);
}

// 弹出一帧音频数据
void* popAudio(DecoderData* data)
{
    SDL_LockMutex(data->videoMutex);
    void* buffer = popQueue(data->audioQueue);
    SDL_UnlockMutex(data->videoMutex);

    return buffer;
}



// 解码线程
int decoder(void* userdata)
{
    DecoderData* data = userdata;
    /* 打开文件 */
    AVFormatContext* formatContext = avformat_alloc_context();
    if (avformat_open_input(&formatContext, data->file, NULL, NULL) != 0)
    {
        fprintf(stderr, "avformat_open_input failed: %s\n", data->file);
        avformat_free_context(formatContext);
        return EXIT_FAILURE;
    }

    /* 解封装: 从 MP4、AVI 等封装格式中提取出 H.264、pcm 等音视频编码数据 */
    if (avformat_find_stream_info(formatContext, NULL) != 0)
    {
        fprintf(stderr, "avformat_find_stream_info failed\n");
        avformat_close_input(&formatContext);
        avformat_free_context(formatContext);
        return EXIT_FAILURE;
    }

    // 遍历 stream 查找音视频流的数据
    int videoIndex = -1;
    int audioIndex = -1;
    for (unsigned int i = 0; i < formatContext->nb_streams; i++)
    {
        if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            videoIndex = i;
        }

        if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            audioIndex = i;
        }
    }

    if (audioIndex == -1 && videoIndex == -1)
    {
        fprintf(stderr, "cannot find stream\n");
        avformat_free_context(formatContext);
        return EXIT_FAILURE;
    }

    /* 解码: H.264、pcm 等音视频编码数据恢复为原始数据 */

    // 获取流
    AVStream* videoStream = formatContext->streams[videoIndex];
    AVStream* audioStream = formatContext->streams[audioIndex];

    // 获取解码器参数
    AVCodecParameters* videoParams = videoStream->codecpar;
    AVCodecParameters* audioParams = audioStream->codecpar;

    // 获取解码器
    AVCodec* videoCodec = avcodec_find_decoder(videoParams->codec_id);
    AVCodec* audioCodec = avcodec_find_decoder(audioParams->codec_id);

    // 创建解码器上下文
    AVCodecContext* videoContext = avcodec_alloc_context3(videoCodec);
    AVCodecContext* audioContext = avcodec_alloc_context3(audioCodec);

    // 打开解码器
    avcodec_parameters_to_context(videoContext, videoParams);
    avcodec_parameters_to_context(audioContext, audioParams);
    avcodec_open2(videoContext, videoCodec, NULL);
    avcodec_open2(audioContext, audioCodec, NULL);

    // 为解码出来的帧分配缓存
    AVFrame* decodedVideoFrame = av_frame_alloc();
    AVFrame* decodedAudioFrame = av_frame_alloc();

    // 为显示的视频帧分配内存
    AVFrame* displayVideoFrame = av_frame_alloc();
    int videoBufferSize = av_image_get_buffer_size(
        AV_PIX_FMT_YUV420P, 
        data->width, 
        data->height, 
        1
    );

    // 为缩放后的视频数据分配内存
    void* displayVideoBuffer = av_malloc(videoBufferSize);
    av_image_fill_arrays(
        displayVideoFrame->data, 
        displayVideoFrame->linesize, 
        displayVideoBuffer, 
        AV_PIX_FMT_YUV420P,
        data->width, 
        data->height,
        1
    );

    // 创建视频数据队列
    data->videoQueue = createQueue(videoBufferSize);

    // 为播放的音频帧分配内存
    AVFrame* displayAudioFrame = av_frame_alloc();

    // 软件缩放上下文
    struct SwsContext* swsContext = sws_getContext(
        videoParams->width,     // 缩放之前的尺寸
        videoParams->height,
        videoParams->format,    // 缩放之前像素格式
        data->width,            // 缩放后的尺寸
        data->height,
        AV_PIX_FMT_YUV420P,     // 缩放后的像素格式
        SWS_BICUBIC,            // 缩放算法:双三次方插值
        NULL,
        NULL,
        NULL
    );

    /* 初始化音频重采样 */
    SwrContext* swrContext = swr_alloc();
    swr_alloc_set_opts(
        swrContext,
        AV_CH_LAYOUT_STEREO,            // 输出声道布局: 双声道 stereo
        AV_SAMPLE_FMT_FLT,              // 输出音频数据格式: 浮点数
        44100,                          // 输出采样率
        audioParams->channel_layout,    // 输入声道布局
        audioParams->format,            // 输入格式
        audioParams->sample_rate,       // 输入采样频率
        0,
        NULL
    );

    swr_init(swrContext);

    // 重采样输出缓存空间大小
    int audioBufferSize = av_samples_get_buffer_size(
        NULL, 
        2,                              // 输出双声道
        audioParams->frame_size,        // 一个声道的采样个数，受限于输入
        AV_SAMPLE_FMT_FLT,              // 数据格式
        1
    );

    // 创建音频数据队列
    data->audioQueue = createQueue(audioBufferSize);

    // 创建音频缓存
    uint8_t* displayAudioBuffer = av_malloc(audioBufferSize);

    AVPacket packet;
    while (1)
    {
        if (isExit(data))
            break;

        if (av_read_frame(formatContext, &packet) < 0)
            break;

        // 解码视频
        do
        {
            if (packet.stream_index != videoIndex)
                break;
            
            // 将 packet 发送给视频解码器解码
            int ret = avcodec_send_packet(videoContext, &packet);
            if (ret < 0) 
            {
                // EAGAIN 是当前数据不完整，需要后续的 packet 补充数据
                if (ret != AVERROR(EAGAIN))
                    fprintf(stderr, "avcodec_send_packet failed: %d\n", ret);

                break;
            }

            // 从解码器接收解码后的视频数据
            ret = avcodec_receive_frame(videoContext, decodedVideoFrame);
            if (ret < 0)
            {
                if (ret != AVERROR(EAGAIN))
                    fprintf(stderr, "avcodec_receive_frame failed\n");
                    
                break;
            }

            // 将解码后的数据进行缩放
            ret = sws_scale(
                swsContext, 
                (const unsigned char * const*)decodedVideoFrame->data, 
                decodedVideoFrame->linesize, 
                0, 
                videoParams->height, 
                displayVideoFrame->data, 
                displayVideoFrame->linesize
            );

            // 释放 frame
            av_frame_unref(decodedVideoFrame);

            if (ret <= 0)
            {
                fprintf(stderr, "sws_scale failed\n");
                break;
            }

            // 将最终显示的视频数据压入队列
            pushVideo(data, displayVideoBuffer);
        } while (0);
        
        // 解码音频
        do
        {
            if (packet.stream_index != audioIndex)
                break;

            // 将 packet 发送给音频解码器解码
            int ret = avcodec_send_packet(audioContext, &packet);
            if (ret < 0) 
            {
                // EAGAIN 是当前数据不完整，需要后续的 packet 补充数据
                if (ret != AVERROR(EAGAIN))
                    fprintf(stderr, "avcodec_send_packet failed: %d\n", ret);

                break;
            }

            // 从解码器接收解码后的音频数据
            ret = avcodec_receive_frame(audioContext, decodedAudioFrame);
            if (ret < 0)
            {
                if (ret != AVERROR(EAGAIN))
                    fprintf(stderr, "avcodec_receive_frame failed\n");
                    
                break;
            }

            // 进行重采样
            ret = swr_convert(
                swrContext, 
                &displayAudioBuffer, 
                audioParams->frame_size, 
                (const uint8_t**)decodedAudioFrame->data, 
                decodedAudioFrame->nb_samples
            );
            if (ret < 0)
            {
                if (ret != AVERROR(EAGAIN))
                    fprintf(stderr, "swr_convert failed\n");
                    
                break;
            } 
            pushAudio(data, displayAudioBuffer);

        } while (0);

        // 释放 packet
        av_packet_unref(&packet);

        
    }
    

    /* 释放资源 */
    av_free(displayVideoBuffer);
    // swr_free(swrContext);
    sws_freeContext(swsContext);
    av_frame_free(&displayVideoFrame);
    av_frame_free(&decodedVideoFrame);
    av_frame_free(&displayAudioFrame);
    av_frame_free(&decodedAudioFrame);
    avcodec_free_context(&audioContext);
    avcodec_free_context(&videoContext);
    avformat_close_input(&formatContext);
    avformat_free_context(formatContext);
    
    setExit(data, 1);

    return EXIT_SUCCESS;
}