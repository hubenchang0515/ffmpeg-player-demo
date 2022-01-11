#include <stdio.h>
#include <stdlib.h>

#include <SDL2/SDL.h>               // libsdl2-dev

/* ffmpeg */
#include <libavformat/avformat.h>   // libavformat-dev  : Audio-Video Foramt - 用于音视频文件封装、解封装
#include <libavcodec/avcodec.h>     // libavcodec-dev   : Audio-Video Codec - 用于音视频数据编解码
#include <libavutil/imgutils.h>     // libavutil-dev    : Audio-Video Utilities - 一些实用函数
#include <libswscale/swscale.h>     // libswscale-dev   : Software Scale - 软件缩放算法

#include "decoder.h"

int decoder(void* userdata)
{
    DecoderData* data = userdata;
    data->exit = 0;

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

    // 为解码视频分配缓存
    AVFrame* decodedVideoFrame = av_frame_alloc();
    AVFrame* displayVideoFrame = av_frame_alloc();
    int bufferSize = av_image_get_buffer_size(
        AV_PIX_FMT_YUV420P, 
        data->width, 
        data->height, 
        1
    );
    void* displayVideoBuffer = av_malloc(bufferSize);
    av_image_fill_arrays(
        displayVideoFrame->data, 
        displayVideoFrame->linesize, 
        displayVideoBuffer, 
        AV_PIX_FMT_YUV420P,
        data->width, 
        data->height,
        1
    );

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

    AVPacket packet;
    while (1)
    {
        if (av_read_frame(formatContext, &packet) < 0)
            break;

        SDL_LockMutex(data->exitMutex);
        int exit = data->exit;
        SDL_UnlockMutex(data->exitMutex);
        if (exit)
        {
            break;
        }

        // 播放视频
        if (packet.stream_index == videoIndex)
        {
            // 将 packet 发送给解码器解码
            int ret = avcodec_send_packet(videoContext, &packet);
            av_packet_unref(&packet);
            if (ret < 0 && ret != AVERROR(EAGAIN)) // EAGAIN 是当前数据不完整，需要后续的 packet 补充数据
            {
                fprintf(stderr, "avcodec_send_packet failed: %d\n", ret);
                break;
            }

            // 从解码器接收解码后的数据帧
            ret = avcodec_receive_frame(videoContext, decodedVideoFrame);
            if (ret < 0 && ret != AVERROR(EAGAIN))
            {
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

            if (ret <= 0)
            {
                fprintf(stderr, "sws_scale failed\n");
                continue;
            }

            SDL_LockMutex(data->renderMutex);
            SDL_UpdateTexture(data->texture, NULL, displayVideoBuffer, data->width);
            SDL_UnlockMutex(data->renderMutex);
        }
    }
    

    /* 释放资源 */
    sws_freeContext(swsContext);
    av_frame_free(&displayVideoFrame);
    av_frame_free(&decodedVideoFrame);
    avcodec_free_context(&audioContext);
    avcodec_free_context(&videoContext);
    avformat_free_context(formatContext);
    
    SDL_LockMutex(data->exitMutex);
    data->exit = 1;
    SDL_UnlockMutex(data->exitMutex);

    return EXIT_SUCCESS;
}