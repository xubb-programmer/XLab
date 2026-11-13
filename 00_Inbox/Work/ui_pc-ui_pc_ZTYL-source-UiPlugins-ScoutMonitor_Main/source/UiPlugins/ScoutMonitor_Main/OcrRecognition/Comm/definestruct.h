#ifndef DEFINESTRUCT_H
#define DEFINESTRUCT_H
#include <QString>
#define MAX_TARGET_NUM  20

struct TargetInfo
{
    QString targetNumber = "XX";
    char targetType = 0; //0:目标 1: 炸点
    double targetLon = 0;
    double targetLat = 0;
    double targerElevate = 0;
    double X_Value = 0;
    double Y_Value = 0;
    QString targetTime = "";
    QString result= "";//识别结果
};

enum MessageType
{
    MessageType_Success = 1,    //成功消息
    MessageType_Info,           //下一步操作
    MessageType_Warn,           //警告消息
    MessageType_Err             //错误消息
};

struct UavTargetInfoWithObserver1501
{
    unsigned char  flag;
    double         seriesId;       //目标批号
    unsigned char  effectiveFlag;
    unsigned short year;
    unsigned char  month;
    unsigned char  day;
    unsigned char  hour;
    unsigned char  minute;
    unsigned char  second;
    unsigned short millisecond;
    double         longitude;
    double         latitude;
    double         altitude;
    double         azimuth;
    float          azimuthError;
    double         pitch;
    float          pitchError;
    double         distance;
    double         distanceError;
    float          heading;
    float          speed;
    unsigned short type;
    float          altitudeError;
    float          posError;
    double         observerLongitude;
    double         observerLatitude;
    double         observerAltitude;
    float          observerLongitudeError;
    float          observerLatitudeError;
    float          observerAltitudeError;
    unsigned char  attachmentCount;
};

typedef struct pluginmsg
{
    int pluginid; //发送的时候填上该值，该值是自身插件号，当接收的信息插件号是自身时，表示插件接收到自身发送的信号，返回,不作处理
    int type; //下一级类型：1,菜单 2弹窗 3数据插件
    int curtype; //当前类型：1,菜单 2弹窗 3数据插件
    int uppermenuid; //0表示没有上级菜单，即一级菜单 其他值时上级菜单编号
    char uppermenuname[64];//上级菜单名，比如“雷达工作”
    int id; //当前功能编号
    char name[64]; //当前功能名
    char msg[128]; //附加信息,返回信息时使用
    int  len; //消息长度
    int state;	//操作状态,返回状态信息时使用(功能确认弹窗,1:取消，2:确定)
}pluginmsg;

#endif // DEFINESTRUCT_H
