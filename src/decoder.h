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
void decoderPushVideo(DecoderData* data, void* videoBuffer);

// 弹出一帧视频数据
void* decoderPopVideo(DecoderData* data);

// 压入一帧音频数据
void decoderPushAudio(DecoderData* data, void* audioBuffer);

// 弹出一帧音频数据
void* decoderPopAudio(DecoderData* data);

// 解封装: 从 MP4、AVI 等封装格式中提取出 H.264、pcm 等音视频编码数据
bool decoderUnpack(DecoderData* data, const char* file);

// 初始化视频解码器
bool decoderInitVideoCodec(DecoderData* data);

// 初始化音频解码器
bool decoderInitAudioCodec(DecoderData* data);

// 初始化软件缩放算法
bool decoderInitSwScale(DecoderData* data, int width, int height, enum AVPixelFormat fmt);

// 初始化软件重采样算法
bool decoderInitSwResample(DecoderData* data, int64_t layout, enum AVSampleFormat fmt, int rate);

// 重采样后的一个通道的采样数
int decoderSamples(DecoderData* data);

// 解码器线程
int decoderRun(DecoderData* data);

#endif // FFMPEG_PLAYER_DEMO_DECODER