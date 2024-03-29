#ifndef FFMPEG_PLAYER_DEMO_DECODER
#define FFMPEG_PLAYER_DEMO_DECODER

#include "queue.h"

typedef struct DecoderData DecoderData;

// 初始化
DecoderData* createDecoder();

// 删除 
void deleteDecoder(DecoderData* data);

// 设置解码结束
void decoderSetEnd(DecoderData* data, bool n);

// 是否解码结束
int decoderIsEnd(const DecoderData* data);

// 压入一帧视频数据
void decoderPushVideo(DecoderData* data, void* videoBuffer, int64_t pts);

// 弹出一帧视频数据
void* decoderPopVideo(DecoderData* data, int64_t* pts);

// 获取视频队列缓存帧数
int decoderCountVideo(DecoderData* data);

// 压入一帧音频数据
void decoderPushAudio(DecoderData* data, void* audioBuffer, int64_t pts);

// 弹出一帧音频数据
void* decoderPopAudio(DecoderData* data, int64_t* pts);

// 获取音频队列缓存帧数
int decoderCountAudio(DecoderData* data);

// 等待队列空间
void decoderWaitBuffer(DecoderData* data);

// 通知解码器,队列有空间
void decoderNotifyBuffer(DecoderData* data);

// 解封装: 从 MP4、AVI 等封装格式中提取出 H.264、pcm 等音视频编码数据
bool decoderUnpack(DecoderData* data, const char* file);

// 初始化视频解码器
bool decoderInitVideoCodec(DecoderData* data);

// 初始化音频解码器
bool decoderInitAudioCodec(DecoderData* data);

// 初始化软件缩放算法
bool decoderInitSwScale(DecoderData* data, int width, int height, enum AVPixelFormat fmt);

// 初始化软件重采样算法
bool decoderInitSwResample(DecoderData* data, const AVChannelLayout* layout, enum AVSampleFormat fmt, int rate);

// 重采样后的一个通道的采样数
int decoderSamples(DecoderData* data);

// 视频的帧率
double decoderFps(DecoderData* data);

// 解码器线程
int decoderRun(DecoderData* data);

#endif // FFMPEG_PLAYER_DEMO_DECODER