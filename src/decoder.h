#ifndef FFMPEG_PLAYER_DEMO_DECODER
#define FFMPEG_PLAYER_DEMO_DECODER

#include "queue.h"

typedef struct DecoderData DecoderData;

// 初始化
DecoderData* createDecoderData(const char* file, int width, int height);

// 删除 
void deleteDecoderData(DecoderData* data);

// 解码器线程
int decoder(void* userdata);

// 退出
void setExit(DecoderData* data, int n);

// 读取是否退出
int isExit(const DecoderData* data);

// 压入一帧视频数据
void pushVideo(DecoderData* data, void* videoBuffer);

// 弹出一帧视频数据
void* popVideo(DecoderData* data);

#endif // FFMPEG_PLAYER_DEMO_DECODER