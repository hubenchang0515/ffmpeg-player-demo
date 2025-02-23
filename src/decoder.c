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

    SDL_mutex* endMutex;
    bool end;

    SDL_mutex* videoMutex;
    Queue* videoQueue;
    Queue* videoPtsQueue;

    SDL_mutex* audioMutex;
    Queue* audioQueue;
    Queue* audioPtsQueue;

    // 用于主线程通知解码线程继续解码
    SDL_cond* avCond;
    SDL_mutex* avMutex;

    AVFormatContext* formatContext;
    int videoIndex;                 // 视频流的索引
    int audioIndex;                 // 音频流的索引
    
    AVStream* videoStream;          // 视频流
    AVCodecParameters* videoParams; // 视频流参数
    const AVCodec* videoCodec;      // 视频解码器
    AVCodecContext* videoContext;   // 视频解码器上下文
    AVFrame* decodedVideoFrame;     // 解码后的视频帧
    int width;                      // 缩放后的宽度
    int height;                     // 缩放后的高度
    enum AVPixelFormat pixFormat;   // 缩放后的视频像素格式
    int videoBufferSize;            // 缩放后的视频缓冲区大小
    void* displayVideoBuffer;       // 缩放后的视频缓冲区
    AVFrame* displayVideoFrame;     // 缩放后的视频帧
    struct SwsContext* swsContext;  // 缩放算法上下文

    AVStream* audioStream;              // 音频流
    AVCodecParameters* audioParams;     // 音频流参数
    const AVCodec* audioCodec;          // 音频解码器
    AVCodecContext* audioContext;       // 音频解码器上下文
    AVFrame* decodedAudioFrame;         // 解码后的音频帧
    const AVChannelLayout* layout;      // 重采样后的声道布局
    enum AVSampleFormat sampleFormat;   // 重采样后的音频采样格式
    int rate;                           // 重采样后的采样频率
    int samples;                        // 重采样后的一个通道的采样数
    int audioBufferSize;                // 重采样后的音频缓冲区大小
    uint8_t* displayAudioBuffer;        // 重采样后的音频缓冲区
    AVFrame* displayAudioFrame;         // 重采样后的音频帧
    SwrContext* swrContext;             // 重采样上下文
}DecoderData;

void resetDecoderData(DecoderData* data)
{
    data->file = NULL;
    data->endMutex = NULL;
    data->end = false;

    data->videoMutex = NULL;
    data->videoQueue = NULL;
    data->videoPtsQueue = NULL;

    data->audioMutex = NULL;
    data->audioQueue = NULL;
    data->audioPtsQueue = NULL;

    data->formatContext = NULL;
    data->videoIndex = -1;
    data->audioIndex = -1;
    
    data->videoStream = NULL;
    data->videoParams = NULL;
    data->videoCodec = NULL;
    data->videoContext = NULL;
    data->decodedVideoFrame = NULL;
    data->width = 0;
    data->height = 0;
    data->pixFormat = AV_PIX_FMT_NONE;
    data->videoBufferSize = 0;
    data->displayVideoBuffer = NULL;
    data->displayVideoFrame = NULL;
    data->swsContext = NULL;

    data->audioStream = NULL;
    data->audioParams = NULL;
    data->audioCodec = NULL;
    data->audioContext = NULL;
    data->decodedAudioFrame = NULL;
    data->layout = NULL;
    data->sampleFormat = AV_SAMPLE_FMT_NONE;
    data->rate = 0;
    data->audioBufferSize = 0;
    data->audioBufferSize = 0;
    data->displayAudioBuffer = NULL;
    data->displayAudioFrame = NULL;
    data->swrContext = NULL;
}

// 创建
DecoderData* createDecoder()
{
    DecoderData* data = malloc(sizeof(DecoderData));
    if (data == NULL)
    {
        fprintf(stderr,  "%s:%d bad alloc\n", __FILE__, __LINE__);
        return NULL;
    }

    resetDecoderData(data);
    data->avCond = SDL_CreateCond();
    data->avMutex = SDL_CreateMutex();
    return data;
}

// 删除
void deleteDecoder(DecoderData* data)
{
    if (data == NULL)
        return;

    if (data->swrContext != NULL)
        swr_free(&(data->swrContext));

    if (data->displayAudioFrame != NULL)
        av_frame_free(&(data->displayAudioFrame));

    if (data->displayAudioBuffer != NULL)
        av_free(data->displayAudioBuffer);

    if (data->decodedAudioFrame != NULL)
        av_frame_free(&(data->decodedAudioFrame));

    if (data->audioContext != NULL)
    {
        avcodec_free_context(&(data->audioContext));
    }

    if (data->swsContext != NULL)
        sws_freeContext(data->swsContext);

    if (data->displayVideoFrame != NULL)
        av_frame_free(&(data->displayVideoFrame));

    if (data->displayVideoBuffer != NULL)
        av_free(data->displayVideoBuffer);

    if (data->decodedVideoFrame != NULL)
        av_frame_free(&(data->decodedVideoFrame));

    if (data->videoContext != NULL)
    {
        avcodec_free_context(&(data->videoContext));
    }

    if (data->formatContext != NULL)
    {
        avformat_close_input(&(data->formatContext));
        avformat_free_context(data->formatContext);
    }

    if (data->avMutex != NULL)
        SDL_DestroyMutex(data->avMutex);

    if (data->avCond != NULL)
        SDL_DestroyCond(data->avCond);

    if (data->audioQueue != NULL)
        deleteQueue(data->audioQueue);

    if (data->audioMutex != NULL)
        SDL_DestroyMutex(data->audioMutex);

    if (data->videoQueue != NULL)
        deleteQueue(data->videoQueue);

    if (data->videoMutex != NULL)
        SDL_DestroyMutex(data->videoMutex);

    if (data->endMutex != NULL)
        SDL_DestroyMutex(data->endMutex);
    
    free(data);
}

// 设置视频解码结束
void decoderSetEnd(DecoderData* data, bool n)
{
    SDL_LockMutex(data->endMutex);
    data->end = n;
    SDL_UnlockMutex(data->endMutex);
}

// 是否视频解码结束
int decoderIsEnd(const DecoderData* data)
{
    SDL_LockMutex(data->endMutex);
    int n = data->end;
    SDL_UnlockMutex(data->endMutex);
    return n;
}

// 压入一帧视频数据
void decoderPushVideo(DecoderData* data, void* videoBuffer, int64_t pts)
{
    SDL_LockMutex(data->videoMutex);
    pushQueue(data->videoQueue, videoBuffer);
    pushQueue(data->videoPtsQueue, &pts);
    SDL_UnlockMutex(data->videoMutex);
}

// 弹出一帧视频数据
void* decoderPopVideo(DecoderData* data, int64_t* pts)
{
    SDL_LockMutex(data->videoMutex);
    void* buffer = popQueue(data->videoQueue);
    int64_t* _pts = popQueue(data->videoPtsQueue);
    SDL_UnlockMutex(data->videoMutex);
    if (_pts != NULL)
    {
        *pts = *_pts;
        free(_pts);
    }

    return buffer;
}

// 获取视频队列缓存帧数
int decoderCountVideo(DecoderData* data)
{
    SDL_LockMutex(data->videoMutex);
    int n = countQueue(data->videoQueue);
    SDL_UnlockMutex(data->videoMutex);

    return n;
}

// 压入一帧音频数据
void decoderPushAudio(DecoderData* data, void* audioBuffer, int64_t pts)
{
    SDL_LockMutex(data->videoMutex);
    pushQueue(data->audioQueue, audioBuffer);
    pushQueue(data->audioPtsQueue, &pts);
    SDL_UnlockMutex(data->videoMutex);
}

// 弹出一帧音频数据
void* decoderPopAudio(DecoderData* data, int64_t* pts)
{
    SDL_LockMutex(data->videoMutex);
    void* buffer = popQueue(data->audioQueue);
    int64_t* _pts = popQueue(data->audioPtsQueue);
    SDL_UnlockMutex(data->videoMutex);
    if (_pts != NULL)
    {
        *pts = *_pts;
        free(_pts);
    }

    return buffer;
}

// 获取音频队列缓存帧数
int decoderCountAudio(DecoderData* data)
{
    SDL_LockMutex(data->audioMutex);
    int n = countQueue(data->audioQueue);
    SDL_UnlockMutex(data->audioMutex);

    return n;
}

// 等待队列空间
void decoderWaitBuffer(DecoderData* data)
{
    SDL_LockMutex(data->avMutex);
    SDL_CondWait(data->avCond, data->avMutex);
    SDL_UnlockMutex(data->avMutex);
}

// 通知解码器,队列有空间
void decoderNotifyBuffer(DecoderData* data)
{
    SDL_LockMutex(data->avMutex);
    SDL_CondSignal(data->avCond);
    SDL_UnlockMutex(data->avMutex);
}

// 解封装: 从 MP4、AVI 等封装格式中提取出 H.264、pcm 等音视频编码数据
bool decoderUnpack(DecoderData* data, const char* file)
{
    /* 打开文件 */
    data->file = file;
    if (avformat_open_input(&(data->formatContext), data->file, NULL, NULL) != 0)
    {
        fprintf(stderr, "avformat_open_input failed: %s\n", data->file);
        avformat_free_context(data->formatContext);
        data->formatContext = NULL;
        return false;
    }

    /* 读取流信息 */
    if (avformat_find_stream_info((data->formatContext), NULL) != 0)
    {
        fprintf(stderr, "avformat_find_stream_info failed\n");
        avformat_close_input(&(data->formatContext));
        avformat_free_context(data->formatContext);
        data->formatContext = NULL;
        return false;
    }

    /* 遍历 stream 查找音视频流的数据 */
    for (unsigned int i = 0; i < data->formatContext->nb_streams; i++)
    {
        if (data->formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            data->videoIndex = i;
        }

        if (data->formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            data->audioIndex = i;
        }
    }

    if (data->audioIndex == -1 && data->videoIndex == -1)
    {
        fprintf(stderr, "cannot find stream\n");
        avformat_free_context(data->formatContext);
        return false;
    }

    return true;
}

// 初始化视频解码器
bool decoderInitVideoCodec(DecoderData* data)
{
    data->videoStream = data->formatContext->streams[data->videoIndex];
    data->videoParams = data->videoStream->codecpar;

    switch (data->videoParams->codec_id)
    {
    case AV_CODEC_ID_H264:
        data->videoCodec = avcodec_find_decoder_by_name("h264_cuvid");
        break;
    case AV_CODEC_ID_HEVC:
        data->videoCodec = avcodec_find_decoder_by_name("h264_cuvid");
        break;
    case AV_CODEC_ID_AV1:
        data->videoCodec = avcodec_find_decoder_by_name("av1_cuvid");
        break;
    default:
        break; // 清警告
    }
    
    if (data->videoCodec == NULL)
    {
        data->videoCodec = avcodec_find_decoder(data->videoParams->codec_id);
    }

    if (data->videoCodec == NULL)
    {
        fprintf(stderr, "avcodec_find_decoder failed\n");
        return false;
    }

    data->videoContext = avcodec_alloc_context3(data->videoCodec);
    if (data->videoContext == NULL)
    {
        fprintf(stderr, "avcodec_alloc_context3 failed\n");
        return false;
    }

    // 使用 GPU 时需要手动设置
    data->videoContext->pkt_timebase = data->videoStream->time_base;

    if (avcodec_parameters_to_context(data->videoContext, data->videoParams) < 0)
    {
        fprintf(stderr, "avcodec_parameters_to_context failed\n");
        return false;
    }

    if (avcodec_open2(data->videoContext, data->videoCodec, NULL) < 0)
    {
        fprintf(stderr, "avcodec_open2 failed\n");
        return false;
    }

    return true;
}

// 初始化音频解码器
bool decoderInitAudioCodec(DecoderData* data)
{
    data->audioStream = data->formatContext->streams[data->audioIndex];
    data->audioParams = data->audioStream->codecpar;

    data->audioCodec = avcodec_find_decoder(data->audioParams->codec_id);
    if (data->audioCodec == NULL)
    {
        fprintf(stderr, "avcodec_find_decoder failed\n");
        return false;
    }

    data->audioContext = avcodec_alloc_context3(data->audioCodec);
    if (data->audioContext == NULL)
    {
        fprintf(stderr, "avcodec_alloc_context3 failed\n");
        return false;
    }

    if (avcodec_parameters_to_context(data->audioContext, data->audioParams) < 0)
    {
        fprintf(stderr, "avcodec_parameters_to_context failed\n");
        return false;
    }

    if (avcodec_open2(data->audioContext, data->audioCodec, NULL) < 0)
    {
        fprintf(stderr, "avcodec_open2 failed\n");
        return false;
    }

    return true;
}

// 初始化软件缩放算法
bool decoderInitSwScale(DecoderData* data, int width, int height, enum AVPixelFormat fmt)
{
    data->width = width;
    data->height = height;
    data->pixFormat = fmt;

    data->decodedVideoFrame = av_frame_alloc();
    data->displayVideoFrame = av_frame_alloc();

    // 计算缩放后需要的视频缓冲区大小
    data->videoBufferSize = av_image_get_buffer_size(
        data->pixFormat, 
        data->width, 
        data->height, 
        1
    );

    // 为缩放后的视频缓冲区分配内存
    data->displayVideoBuffer = av_malloc(data->videoBufferSize);
    av_image_fill_arrays(
        data->displayVideoFrame->data, 
        data->displayVideoFrame->linesize, 
        data->displayVideoBuffer, 
        data->pixFormat,
        data->width, 
        data->height,
        1
    );

    // 创建软件缩放算法上下文
    AVCodecParameters* params = avcodec_parameters_alloc(); // 使用 GPU 解码会导致像素格式改变
    avcodec_parameters_from_context(params, data->videoContext);

    // 新版本好像不存在这个现象了，大概是作为 BUG 修复了
    // 使用 qsv 解码器时，params->format 得到的是使用软件解码器时的格式
    // 但实际上解码器返回的是 NV12 格式，通过 data->videoCodec->pix_fmts 来获得该格式
    //      NV12 和 YUV422 格式一致，但是排列不同
    //      YUV422 按像素排列，例如: Y0 U0 Y1 V1 Y2 U2 Y3 V3
    //      NV12 按通道排列，例如: Y0 Y1 Y2 Y3 U0 V1 U2 V3
    const enum AVPixelFormat* pix_fmts = NULL;
    int n = 0;
    avcodec_get_supported_config(data->videoContext, data->videoCodec, AV_CODEC_CONFIG_PIX_FORMAT, 0, (const void**)&pix_fmts, &n);
    
    data->swsContext = sws_getContext(
        data->videoParams->width,                   // 缩放之前的尺寸
        data->videoParams->height,
        pix_fmts ? pix_fmts[0] : params->format,    // 缩放之前像素格式
        data->width,                                // 缩放后的尺寸
        data->height,
        data->pixFormat,                            // 缩放后的像素格式
        SWS_BICUBIC,                                // 缩放算法:双三次方插值
        NULL,
        NULL,
        NULL
    );
    avcodec_parameters_free(&params);

    // 创建视频数据队列
    data->videoQueue = createQueue(data->videoBufferSize);
    data->videoPtsQueue = createQueue(sizeof(int64_t));
    data->videoMutex = SDL_CreateMutex();

    return true;
}

// 初始化软件重采样算法
bool decoderInitSwResample(DecoderData* data, const AVChannelLayout* layout, enum AVSampleFormat fmt, int rate)
{
    // data->layout = layout;
    data->sampleFormat = fmt;
    data->rate = rate;

    // 为播放的音频帧分配内存
    data->displayAudioFrame = av_frame_alloc();
    data->decodedAudioFrame = av_frame_alloc();

    /* 初始化音频重采样 */
    data->swrContext = swr_alloc();
    swr_alloc_set_opts2(
        &(data->swrContext),
        layout,                             // 输出声道布局
        data->sampleFormat,                 // 输出音频数据格式
        data->rate,                         // 输出采样率
        &(data->audioParams->ch_layout),    // 输入声道布局
        data->audioParams->format,          // 输入格式
        data->audioParams->sample_rate,     // 输入采样频率
        0,
        NULL
    );

    swr_init(data->swrContext);

    // 计算重采样输出中，一个通道的采样个数
    data->samples = data->audioParams->frame_size * data->rate / data->audioParams->sample_rate;

    // 计算重采样输出缓存空间大小
    data->audioBufferSize = av_samples_get_buffer_size(
        NULL, 
        layout->nb_channels,   // 输出声道数
        data->samples,              // 一个通道的采样个数
        data->sampleFormat,         // 数据格式
        1
    );

    // 创建音频数据队列
    data->audioQueue = createQueue(data->audioBufferSize);
    data->audioPtsQueue = createQueue(sizeof(int64_t));
    data->audioMutex = SDL_CreateMutex();

    // 创建音频缓存
    data->displayAudioBuffer = av_malloc(data->audioBufferSize);

    return true;
}

// 重采样后的一个通道的采样数
int decoderSamples(DecoderData* data)
{
    return data->samples;
}

// 视频的帧率
double decoderFps(DecoderData* data)
{
    return data->videoStream->avg_frame_rate.num / (double)data->videoStream->avg_frame_rate.den;
}

// 解码
int decoderRun(DecoderData* data)
{
    AVPacket packet;
    const int cacheMax = 5;

    // 计算毫秒级的时间基数
    double videoTimebase = (double)(data->videoStream->time_base.num) / data->videoStream->time_base.den * 1000;
    double audioTimebase = (double)(data->audioStream->time_base.num) / data->audioStream->time_base.den * 1000;
    while (1)
    {
        if (decoderIsEnd(data))
            break;

        if (decoderCountVideo(data) > cacheMax && decoderCountAudio(data) > cacheMax)
        {
            decoderWaitBuffer(data);
            continue;
        }

        if (av_read_frame(data->formatContext, &packet) < 0)
            break;

        // 解码视频
        do
        {
            if (packet.stream_index != data->videoIndex)
                break;
            
            // 将 packet 发送给视频解码器解码
            int ret = avcodec_send_packet(data->videoContext, &packet);
            if (ret < 0) 
            {
                // EAGAIN 是当前数据不完整，需要后续的 packet 补充数据
                if (ret != AVERROR(EAGAIN))
                    fprintf(stderr, "avcodec_send_packet failed: %d\n", ret);

                break;
            }

            // 从解码器接收解码后的视频数据
            ret = avcodec_receive_frame(data->videoContext, data->decodedVideoFrame);
            if (ret < 0)
            {
                if (ret != AVERROR(EAGAIN))
                    fprintf(stderr, "avcodec_receive_frame failed\n");
                    
                break;
            }

            // 将解码后的数据进行缩放
            ret = sws_scale(
                data->swsContext, 
                (const unsigned char * const*)(data->decodedVideoFrame->data), 
                data->decodedVideoFrame->linesize, 
                0, 
                data->decodedVideoFrame->height, 
                data->displayVideoFrame->data, 
                data->displayVideoFrame->linesize
            );

            // 将最终显示的视频数据压入队列
            decoderPushVideo(data, data->displayVideoBuffer, data->decodedVideoFrame->pts * videoTimebase);

            // 释放 frame
            av_frame_unref(data->decodedVideoFrame);

            if (ret <= 0)
            {
                fprintf(stderr, "sws_scale failed\n");
                break;
            }
        } while (0);
        
        // 解码音频
        do
        {
            if (packet.stream_index != data->audioIndex)
                break;

            // 将 packet 发送给音频解码器解码
            int ret = avcodec_send_packet(data->audioContext, &packet);
            if (ret < 0) 
            {
                // EAGAIN 是当前数据不完整，需要后续的 packet 补充数据
                if (ret != AVERROR(EAGAIN))
                    fprintf(stderr, "avcodec_send_packet failed: %d\n", ret);

                break;
            }

            // 从解码器接收解码后的音频数据
            ret = avcodec_receive_frame(data->audioContext, data->decodedAudioFrame);
            if (ret < 0)
            {
                if (ret != AVERROR(EAGAIN))
                    fprintf(stderr, "avcodec_receive_frame failed\n");
                    
                break;
            }

            // 进行重采样
            ret = swr_convert(
                data->swrContext, 
                &(data->displayAudioBuffer), 
                data->audioParams->frame_size, 
                (const uint8_t**)(data->decodedAudioFrame->data), 
                data->decodedAudioFrame->nb_samples
            );

            decoderPushAudio(data, data->displayAudioBuffer, data->decodedAudioFrame->pts * audioTimebase);

            // 释放 frame
            av_frame_unref(data->decodedAudioFrame);

            if (ret < 0)
            {
                if (ret != AVERROR(EAGAIN))
                    fprintf(stderr, "swr_convert failed\n");
                    
                break;
            } 
            

        } while (0);

        // 释放 packet
        av_packet_unref(&packet);
    }
    
    decoderSetEnd(data, true);
    return EXIT_SUCCESS;
}