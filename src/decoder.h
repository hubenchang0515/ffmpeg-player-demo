#ifndef FFMPEG_PLAYER_DEMO_DECODER
#define FFMPEG_PLAYER_DEMO_DECODER

#include "queue.h"

typedef struct DecoderData DecoderData;

// 初始化
DecoderData* createDecoderData(const char* file);

// 删除 
void deleteDecoderData(DecoderData* data);

// 设置解码结束
void setEnd(DecoderData* data, bool n);

// 是否解码结束
int isEnd(const DecoderData* data);

// 压入一帧视频数据
void pushVideo(DecoderData* data, void* videoBuffer);

// 弹出一帧视频数据
void* popVideo(DecoderData* data);

// 压入一帧音频数据
void pushAudio(DecoderData* data, void* audioBuffer);

// 弹出一帧音频数据
void* popAudio(DecoderData* data);

// 解封装: 从 MP4、AVI 等封装格式中提取出 H.264、pcm 等音视频编码数据
bool unpack(DecoderData* data);

// 初始化视频解码器
bool initVideoCodec(DecoderData* data);

// 初始化音频解码器
bool initAudioCodec(DecoderData* data);

// 初始化软件缩放算法
bool initSwScale(DecoderData* data, int width, int height, enum AVPixelFormat fmt);

// 初始化软件重采样算法
bool initSwResample(DecoderData* data, int64_t layout, enum AVSampleFormat fmt, int rate);

// 重采样后的一个通道的采样数
int getSamples(DecoderData* data);

// 解码器线程
int decode(DecoderData* data);

#endif // FFMPEG_PLAYER_DEMO_DECODER