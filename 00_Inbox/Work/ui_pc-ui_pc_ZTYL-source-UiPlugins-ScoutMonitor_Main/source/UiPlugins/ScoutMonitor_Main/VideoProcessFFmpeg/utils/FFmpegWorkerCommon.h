#ifndef FFMPEGWORKERCOMMON_H
#define FFMPEGWORKERCOMMON_H
#pragma once
#include "SafeQueue.h"
#include <QStringList>

extern "C" {
    #include "libavcodec/avcodec.h"
    #include "libavformat/avformat.h"
    #include "libavutil/pixfmt.h"
    #include "libswscale/swscale.h"
    #include "libavutil/imgutils.h"
    #include "SDL2/SDL.h"
    #include "libavutil/time.h"
}

static char av_error[64] = {}; // 定义错误码转换
#define av_err2str_cpp(errnum) av_make_error_string(av_error,64,errnum)

#define SDL_EVENT_LOCKSCREEN (SDL_USEREVENT + 1);
#define SDL_EVENT_UNLOCKSCREEN (SDL_USEREVENT + 2);

const int g_MAX_QUEUE_SIZE=100;

typedef struct AVIOInterrupCB{
    int (*callback)(void*);
    void* opaque;
}AVIOInterrupCB;


enum MessageType
{
    MessageType_Success = 1,
    MessageType_Info,
    MessageType_Warn,
    MessageType_Err
};

enum ErrorType
{
    // 初始化阶段
    Error_Init_Fail = 0,           // avformat_open_input / find_stream_info 失败
    Error_Init_NoVideoStream = 1,   // 找不到视频流
    Error_Init_NoExtradata = 2,         // RTMP extradata 缺失（SPS/PPS 未就绪）
    Error_Init_CodecNotFound = 3,       // 找不到解码器
    Error_Init_CodecOpenFail ,       // avcodec_open2 失败
    Error_Init_InvalidResolution,   // 分辨率为 0
    // 运行阶段
    Error_End_Of_File,              // 流结束 / 网络断开
    Error_Read_Timeout,             // 看门狗超时（av_read_frame 长时间无数据）
    // 截图
    Error_Snapshot_Invaild_Frame,   //截图数据为空
    Error_Snapshot_Fail,            // 截图路径保存失败
    // 录像
    Error_Record_InitOutput_Fail,   //录像初始化失败
    Error_Record_Invaild_Path,      //无效的录像地址
    Error_Low_Disk_Space,           //空间不足
    // 转发
    Error_Relay_InitOutput_Fail,    //上报初始化失败
    Error_Relay_Invaild_URL,        // 上报无效地址
    // 投屏
    Error_PushScreenStream          //屏幕推流失败
};

const QStringList ERROR_KEYWORDS = {
     "error",
     "invalid",
     "failed",
     "Unknown encoder"
};

enum VideoStatus
{
    Status_Idle = 0,        // 空闲（未启动）
    Status_Connecting,      // 正在连接 / 拉流中
    Status_Playing,         // 正在播放
    Status_Reconnecting,    // 断线重连中
    Status_Failed,          // 拉流失败（不再重连，需手动处理）
    Status_Stopped,         // 主动停止
};

inline QString errorTypeToString(ErrorType err)
{
    switch (err) {
    case Error_Init_Fail:
        return QString::fromLocal8Bit("初始化失败：无法打开媒体地址");
    case Error_Init_NoVideoStream:
        return QString::fromLocal8Bit("初始化失败：未找到视频流");
    case Error_Init_NoExtradata:
        return QString::fromLocal8Bit("初始化失败：视频编码参数未就绪，服务端尚未推流");
    case Error_Init_CodecNotFound:
        return QString::fromLocal8Bit("初始化失败：不支持的视频编码格式");
    case Error_Init_CodecOpenFail:
        return QString::fromLocal8Bit("初始化失败：解码器打开失败");
    case Error_Init_InvalidResolution:
        return QString::fromLocal8Bit("初始化失败：视频分辨率无效");
    case Error_End_Of_File:
        return QString::fromLocal8Bit("网络断开：视频流已中断");
    case Error_Read_Timeout:
        return QString::fromLocal8Bit("网络超时：长时间未收到视频数据");
    case Error_Snapshot_Invaild_Frame:
        return QString::fromLocal8Bit("截图失败：当前无有效视频帧");
    case Error_Snapshot_Fail:
        return QString::fromLocal8Bit("截图失败：文件保存错误，请检查路径权限");
    case Error_Record_InitOutput_Fail:
        return QString::fromLocal8Bit("录像失败：请先开启视频播放");
    case Error_Record_Invaild_Path:
        return QString::fromLocal8Bit("录像失败：录像保存路径无效");
    case Error_Relay_InitOutput_Fail:
        return QString::fromLocal8Bit("转发失败：请先开启视频播放");
    case Error_Relay_Invaild_URL:
        return QString::fromLocal8Bit("转发失败：转发地址无效或为空");
    default:
        return QString::fromLocal8Bit("未知错误");
    }
}

inline QString videoStatusToString(VideoStatus status)
{
    switch (status) {
    case Status_Idle:         return QString::fromLocal8Bit("空闲");
    case Status_Connecting:   return QString::fromLocal8Bit("正在拉流中...");
    case Status_Playing:      return QString::fromLocal8Bit("正在播放");
    case Status_Reconnecting: return QString::fromLocal8Bit("正在断线重连...");
    case Status_Failed:       return QString::fromLocal8Bit("拉流失败");
    case Status_Stopped:      return QString::fromLocal8Bit("已停止");
    default:                  return QString::fromLocal8Bit("未知状态");
    }
}

#endif // FFMPEGWORKERCOMMON_H
