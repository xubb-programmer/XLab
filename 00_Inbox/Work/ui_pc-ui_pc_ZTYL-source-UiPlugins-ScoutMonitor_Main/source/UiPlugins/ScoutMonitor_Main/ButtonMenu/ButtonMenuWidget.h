/*
* ButtonMenuWidget.h
* Author：王泽文
* Created：2025-7-29
* Last Modified：2025-7-29
* Description：定义软件一级菜单界面。
*/
#ifndef BUTTONMENUWIDGET_H
#define BUTTONMENUWIDGET_H

#include <QWidget>
#include "IUiPluginBase.h"
#include <QDomElement>
#include <QMap>
#include <QSettings>
#include "MenuWidget.h"
#include <QFile>
#include "UiPluginFactory.h"
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
/**
* @brief The ButtonMenuWidget 类
* 用于生成菜单插件界面。
*/
namespace Ui {
class ButtonMenuWidget;
}

class ButtonMenuWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ButtonMenuWidget(QWidget *parent = 0);
    ~ButtonMenuWidget();

    /**
     * @brief initUi
     * 虚函数的具体实现，界面初始化函数。
     * @param pPlugin 界面框架指针
     * @return 无返回值
     */
    void initUi(IUiPluginBase *pPlugin);

    /**
     * @brief initData
     * 虚函数的具体实现，数据初始化函数。
     * @return 无返回值
     */
    void initData();
    /**
     * @brief registerProtocols
     * 虚函数的具体实现，初始化插件信号注册函数。
     * @return 无返回值
     */
    virtual void registerProtocols();
    /**
     * @brief registerProtocols
     * 虚函数的具体实现，界面样式加载函数。
     * @param strStyleName  qss样式路径
     * @return 无返回值
     */
    virtual void resetQss(const QString& strStyleName);
    /**
     * @brief creatBtnStatus
     * 通过xml创建按钮
     * @param filepath xml文件读取路径
     * @return 读取结果返回
     */
    bool creatBtnStatus(QString filepath);
signals:
    /**
     * @brief signalPluginMsg
     * 插件信号
     */
    void signalPluginMsg(pluginmsg);
public slots:
    /**
     * @brief slotBtnSendMsg
     * 按钮点击发送信号
     * @param BtnBaseInfo Msg
     */
    void slotBtnSendMsg(BtnBaseInfo data);

    /**
     * @brief slotsignalPluginMsg
     * 信号接收处理函数
     * @param msg 信号结构体
     */
    void slotsignalPluginMsg(pluginmsg msg);
private:
    Ui::ButtonMenuWidget *ui;
    IUiPluginBase *m_pPlugin = nullptr;
    QMap<int,MenuWidget*>m_allBtnStatus;//记录id与按钮关系
    QMap<int,MenuWidget*>::iterator itor;//Map迭代器
    QSettings *m_pConfigFile=nullptr;//配置文件读取
};

#endif // BUTTONMENUWIDGET_H
