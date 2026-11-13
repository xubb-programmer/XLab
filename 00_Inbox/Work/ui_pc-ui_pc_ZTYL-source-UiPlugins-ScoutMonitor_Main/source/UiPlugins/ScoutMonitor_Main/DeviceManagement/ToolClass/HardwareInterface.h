#ifndef HARDWAREINTERFACE_H
#define HARDWAREINTERFACE_H

#include <QWidget>
#include "IUiPluginBase.h"

namespace Ui {
class HardwareInterface;
}

class HardwareInterface : public QWidget
{
    Q_OBJECT

public:
    explicit HardwareInterface(QWidget *parent = 0);
    ~HardwareInterface();

    /**
     * @brief initUi
     * 初始化界面
     * @param plugin(插件指针)
     * @return 无返回值
     */
    void initUi(IUiPluginBase *plugin);

    /**
     * @brief setIcon
     * 设置图标样式
     * @param strType(设备类型) strStatus(设备状态)
     * @return 无返回值
     */
    void setIcon(QString strType, QString strStatus);

    /**
     * @brief setDeviceName
     * 设置组件名称
     * @param strDeviceName(设备名称)
     * @return 无返回值
     */
    void setDeviceName(QString strDeviceName);

    /**
     * @brief getDeviceName
     * 获取组件名称
     * @param void
     * @return QString(组件名称)
     */
    QString getDeviceName();

    /**
     * @brief setDevice1Name
     * 设置组件1名称
     * @param QString(组件1名称)
     * @return void
     */
    void setDevice1Name(QString strDeviceName);

    // 获取组件1名称
    QString getDevice1Name();

    /**
     * @brief setDevice2Name
     * 设置组件2名称
     * @param QString(组件2名称)
     * @return void
     */
    void setDevice1Value(QString strDeviceName);

    /**
     * @brief setDevice2Name
     * 设置组件2名称
     * @param QString(组件2名称)
     * @return void
     */
    QString getDevice1Value();

    /**
     * @brief setDevice2Name
     * 设置组件2名称
     * @param QString(组件2名称)
     * @return void
     */
    void setDevice2Name(QString strDeviceName);

    /**
     * @brief getDevice2Name
     * 获取组件2名称
     * @param void
     * @return QString(组件2名称)
     */
    QString getDevice2Name();

    /**
     * @brief setDevice2Value
     * 设置组件2名称值
     * @param QString(组件2名称)
     * @return void
     */
    void setDevice2Value(QString strDeviceName);

    /**
     * @brief getDevice2Value
     * 获取组件2名称值
     * @param void
     * @return QString(组件2名称)
     */
    QString getDevice2Value(); 

    /**
     * @brief hideDevice1Name
     * 隐藏组件1名称
     * @param void
     * @return void
     */
    void hideDevice1Name();

    /**
     * @brief hideDevice1Value
     * 隐藏组件1名称值
     * @param void
     * @return void
     */
    void hideDevice1Value();

    /**
     * @brief hideDevice2Name
     * 隐藏组件2名称
     * @param void
     * @return void
     */
    void hideDevice2Name();

    /**
     * @brief hideDevice2Value
     * 隐藏组件2名称值
     * @param void
     * @return void
     */
    void hideDevice2Value();

    /**
     * @brief hideDeviceIcon
     * 隐藏组件图标
     * @param void
     * @return void
     */
    void hideDeviceIcon();

    /**
     * @brief loadQssStyle
     * 设置Qss样式表
     * @param void
     * @return void
     */
    void loadQssStyle();

private:
    Ui::HardwareInterface *ui;
    IUiPluginBase *m_pPlugin = nullptr;
    QString m_strDeviceType = "";
    QString m_strOnlineStatus = "";
};

#endif // HARDWAREINTERFACE_H
