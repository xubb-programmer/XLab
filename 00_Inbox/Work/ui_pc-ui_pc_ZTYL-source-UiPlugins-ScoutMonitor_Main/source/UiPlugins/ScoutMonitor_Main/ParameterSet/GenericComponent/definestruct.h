#ifndef DEFINESTRUCT_H
#define DEFINESTRUCT_H
#include <QString>

#define MAX_VIDEOSET_NUM  4

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

struct VideoSetData
{
    QString VideoName = ""; //视频标签名称
    bool bisBigScreenShow=false;//大屏显示
    int SmallScreenNum=0;//小屏窗口号
    QString VideoIp = ""; //视频IP  rtsp://192.168.0.160:554/stream1
    bool bIsIdentifiy = false; //识别是否已开启
    bool bIsStreamPullOn = false; //拉流是否开启
    QString connectstatus = "failed "; //连接状态 connected、reconnecting、failed
};

struct NetSetData
{
    QString netName="";//网络名称
    QString ip = ""; //IP地址
    QString subnet = ""; //子网掩码
    QString gateway = ""; //网关
    bool status=false;//网口状态
};

struct UrlComponents
{
    QString ip = ""; //IP地址
    QString port = ""; //端口
};

struct CarInfo
{
    QString name = ""; //车辆名称，如002、033
    QString ip = "";   //车辆上报IP
    int port = 0;      //车辆上报端口
};

struct ModelData
{
    int nSerialNumber = 0;//序号
    QString strModelName = "";//模型名称
    QString strCreateTime = "";//创建时间
    bool m_Status = false; // 模型启用状态
};

enum MessageType
{
    MessageType_Success = 1,    //成功消息
    MessageType_Info,           //下一步操作
    MessageType_Warn,           //警告消息
    MessageType_Err             //错误消息
};

enum ResponsResult
{
    Respons_Close = 1,          //关闭
    Respons_Open   //开启
};

#endif // DEFINESTRUCT_H
