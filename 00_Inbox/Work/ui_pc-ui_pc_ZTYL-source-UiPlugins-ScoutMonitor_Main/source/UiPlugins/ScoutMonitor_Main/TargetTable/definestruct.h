#ifndef DEFINESTRUCT_H
#define DEFINESTRUCT_H
#include <QString>
#define MAX_TARGET_NUM  30

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
};

enum MessageType
{
    MessageType_Success = 1,    //成功消息
    MessageType_Info,           //下一步操作
    MessageType_Warn,           //警告消息
    MessageType_Err             //错误消息
};

#pragma pack(push,1)

/** TCP 目标上报ZZC帧，总长 29 字节   */
struct TcpGDTargetProtocolFrame
{
    uint8_t  frameType;     ///< Byte1  固定 0x02
    char     frameId[14];   ///< Byte2~15 帧ID，yyyyMMddHHmmss
    int16_t  azimuth;       ///< Byte16~17 方位，short，1LSB=0.01°
    int32_t  longitude;     ///< Byte18~21 经度，int32，1LSB=0.0000001°，东经为正
    int32_t  latitude;      ///< Byte22~25 纬度，int32，1LSB=0.0000001°，北纬为正
    int16_t  altitude;      ///< Byte26~27 海拔，short，1LSB=0.1m
    int16_t  distance;      ///< Byte28~29 距离，short，1LSB=1m

};

/** TCP 文件信息上报ZZC帧，总长 21 字节 */
struct TcpGDTargetFileFrame
{
    uint8_t  frameType;     ///< Byte1  固定 0x01
    char     frameId[20];   ///< Byte2~21 帧ID，yyyyMMddHHmmss_2.jpg

};

//1501
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


struct BurstTargetInfoWithObserver1501
{
    unsigned char  flag;
    double         burstId;       //炸点编号
    unsigned char  burstCnt;
    unsigned char  burstType;
    double         burstCode;
    double         longitude;
    double         latitude;
    int            altitude;
    unsigned short depthDistance; //纵深距离
    unsigned short frontDistance; //正面距离
    unsigned short year;
    unsigned char  month;
    unsigned char  day;
    unsigned char  hour;
    unsigned char  minute;
    unsigned char  second;
    unsigned short millisecond;
};

// OutHeader19 + InnerHeader30 + TargetInfo_HL
struct OutHeader19
{
    char     version  = 0x32;
    char     type     = 0x10;
    uint16_t len      = 0x00;   //网络报文长度  内头 + 外头 + 正文长
    uint16_t num      = 0;      //用户报序号填0
    char conv         = 0;      //预留0

    uint16_t s_xtbs = 0xbb33;   //源系统标识 若无可随便写与目的系统标识不同即可如 0xbb33
    uint16_t s_zjbs = 0xbb33;   //源主机标识 若无可随便写与目的系统标识不同即可如 0xbb33
    uint16_t s_stbs = 0xbb33;   //源实体标识 若无可随便写与目的系统标识不同即可如 0xbb33

    uint16_t t_xtbs = 0x2880;   //目的系统标识
    uint16_t t_zjbs = 0x7901;   //目的主机标识
    uint16_t t_stbs = 0x6009;   //目的实体标识
};

struct InnerHeader30
{
    int8_t   N_0          = 0;
    int8_t   len          = 30;
    uint16_t banwen_len   = 0;      //正文长度 结构体
    int8_t   msgtype_low  = 0x62;   //0x62
    int8_t   workModel    = 2;

    int8_t s_systemIdlow  = 0x33 ;  //源系统标识
    int8_t s_systemIdHigh = 0xbb;

    int8_t s_hostIdlow    = 0x33;   //源主机标识
    int8_t s_hostIdHigh   = 0xbb ;

    int8_t s_entityIdlow  = 0x33 ;  //源实体标识
    int8_t s_entityIdHigh = 0xbb;

    int32_t N1   = 0;  //预留0 发送自报位时，30字节内头的N1填写IP
    int8_t  N_16 = 0;
    int8_t  N_17 = 0;

    int8_t  msgtype_high     = 0x62;
    int8_t  N_19             = 0;

    int8_t  dst_systemIdlow  = 0x80;  //目的系统标识 可变（目前时0x2880）
    int8_t  dst_systemIdHigh = 0x28;

    int8_t  dst_hostIdlow    = 0x01;
    int8_t  dst_hostIdHigh   = 0x79;

    int8_t  dst_entityIdlow  = 0x09;
    int8_t  dst_entityIdHigh = 0x60;

    uint32_t spair = 0;
};

//目标上报 海陆(HL)Marine
struct TargetInfo_Marine
{
    uint16_t  infoId           = 1002;  //目标报文id
    uint32_t  sourcePlatformId = 0;     //来源平台编号
    uint8_t   name[20]         = {0};   //来源名称
    uint8_t   envType          = 0;     //环境类别 ok  7
    int64_t   longitude        = 0;     //经度 ok
    int64_t   latitude         = 0;     //纬度 ok
    uint16_t  altitude         = 0;     //高度  ok
    uint32_t  moveDirection    = 0;     //运动方向  ok
    uint32_t  moveSpeed        = 0;     //运动速度  ok

    int32_t    targetType      = 0;     //目标类型
    uint8_t    targetAttribute = 0;     //敌我属性  ok
    uint8_t    targetCountry   = 0;     //国家地区   ok
    uint32_t   targetId        = 0;     //目标标号序号
    uint8_t    targetName[20]  = {0};   //目标名称   ok
    uint32_t   targetZM        = 0;     //目标正面距离  ok
    uint32_t   targetZS         = 0;     //目标纵深距离  ok
    uint32_t   targetSL         = 0;     //目标数量   ok
    uint32_t   targetTime       = 0;     //时间

    uint32_t   target_fxsd      = 0;      //发现手段     dab ok
    uint32_t   target_kkxpg     = 0;      //可靠性评估    0 高度可信 1 中度 2 低可 3 待详查
    uint32_t   target_fh        = 0;      //目标防护      0土木质结构 1砖混结构  2钢景混凝土  3轻装甲 4重装甲 5 未定义 15 不明
    uint32_t   target_ybx       = 0;      //隐蔽性   不填  默认0
    uint32_t   target_zcxw      = 0;      //战场行为  10 侦察  不显示

    uint32_t   target_zt        = 0;      //目标状态   ok
    uint32_t   target_dx        = 0;      //目标队形  1101  单列队形 不显示
    uint8_t    target_bz[50]    = {0};    //备注
};


//自报位置 海陆(HL)Marine
struct SelfPos_Marine
{
    uint16_t  infoId            = 1001;    //目标报文id
    uint32_t  sourcePlatformId  = 18101000;     //来源平台编号
    uint8_t   name[20]          = {0x0};   //来源名称
    uint8_t   type              = 18;      //平台类型  反无车
    uint8_t   weaponType        = 101;       //弹药类型
    uint8_t   gzsl              = 2;       //挂载数量
    uint8_t   envi              = 3;       //环境类别 默认陆
    uint64_t  longitude         = 0;       //经度
    uint64_t  latitude          = 0;       //纬度
    uint16_t  altitude          = 0;       //高度 米
    uint32_t  time              = 0;       //时间   epoch seconds
    uint32_t  moveDirection     = 12500;   //运动方向   对端未显示 0
    uint32_t  moveSpeed         = 56;      //运动速度   对端未显示 0

    uint16_t kjTime             = 20;      //开机时间
    uint16_t xhTime             = 300;     //续航时间

};

#pragma pack(pop)
#endif // DEFINESTRUCT_H
