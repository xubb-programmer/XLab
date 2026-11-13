#ifndef MENUWIDGET_H
#define MENUWIDGET_H

#include <QWidget>

#include <QMap>
#include <QDebug>
typedef struct BtnStatusInfo//按钮状态信息
{
    int stateid;//状态id
    QString name="";//按钮名称
    QString qss="";//样式
    int width=0;//按钮长度
    int height=0;//按钮高度
    int level=0;//按钮等级
    int x=0;//按钮x位置
    int y=0;//按钮y位置
    int pad=0;//左移位置
    bool check=false;//选中状态
}BtnStatusInfo;

typedef struct BtnBaseInfo//按钮基本信息
{
    int id=0;//发送信号id
    int stateid;//状态id
    QString name="";//按钮名称
}BtnBaseInfo;

namespace Ui {
class MenuWidget;
}

class MenuWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MenuWidget(QWidget *parent = 0);
    ~MenuWidget();

    /**
     * @brief changeBtnSize
     * 根修改菜单按钮大小
     * @param width 宽度
     * @param height 高度
     */
    void changeBtnSize(int width,int height);
    /**
     * @brief changeBtnName
     * 根修改菜单按钮名称
     * @param name 按钮名称
     */
    void changeBtnName(QString name);
    /**
     * @brief loadBtnQss
     * 按钮样式加载
     * @param style 样式文件下名称
     * @param level 按钮等级
     * @param picture 图片名称
     */
    void loadBtnQss(QString style,int level, QString picture,int padding,bool check);

    /**
     * @brief cancelCheckStatus
     * 取消选中状态
     */
    void cancelCheckStatus();
signals:
    /**
     * @brief signalSendMsg
     * 信号发送
     * @param id 信号id
     */
    void signalSendMsg(BtnBaseInfo data);
public slots:
    /**
     * @brief slotBtnPressed
     * 鼠标点击事件
     */
    void slotBtnPressed();
private:
    Ui::MenuWidget *ui;
public:
    BtnBaseInfo m_stBaseInfo;//按钮基础信息
    QMap<int,BtnStatusInfo> m_statusInfo;//按钮状态配置文件信息
    QMap<int,BtnStatusInfo>::iterator m_it;//map迭代器
};

#endif // MENUWIDGET_H
