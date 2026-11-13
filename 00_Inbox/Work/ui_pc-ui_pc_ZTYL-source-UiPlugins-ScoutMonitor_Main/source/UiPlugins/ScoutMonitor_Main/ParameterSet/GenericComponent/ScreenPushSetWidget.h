#ifndef SCREENPUSHSETWIDGET_H
#define SCREENPUSHSETWIDGET_H

#include <QWidget>
#include "IUiPluginBase.h"
#include <QSettings>
#include "definestruct.h"
#include "ScreenPushInstance.h"

class QIntValidator;
namespace Ui {
class ScreenPushSetWidget;
}

class ScreenPushSetWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ScreenPushSetWidget(QWidget *parent = 0);
    ~ScreenPushSetWidget();

    /**
     * @brief initUi
     * 插件初始化界面
     * @param plugin（插件指针）
     * @return 无返回值
     */
    void initUi(IUiPluginBase *plugin);

    /**
     * @brief loadQssStyle
     * 设置组件样式
     * @param void
     * @return 无返回值
     */
    void loadQssStyle();

    /**
     * @brief updateStreamInfo
     * 读取配置文件，更新推流数据信息
     * @param void
     * @return 无返回值
     */
    void updateStreamInfo();

private slots:

    /**
     * @brief sltUpdateOutPutUrl
     * 修改输出地址，更新配置文件
     * @param QString text 更新内容
     * @return 无返回值
     */
    void sltUpdateOutPutUrl(QString text);
    /**
     * @brief sltUpdateCaptureWidth
     * 修改采集视频宽度，更新配置文件
     * @param QString text 更新内容
     * @return 无返回值
     */
    void sltUpdateCaptureWidth(QString text);

    /**
     * @brief sltUpdateCaptureHeight
     * 修改采集视频高度，更新配置文件
     * @param QString text 更新内容
     * @return 无返回值
     */
    void sltUpdateCaptureHeight(QString text);

    /**
     * @brief sltUpdateCaptureFps
     * 修改采集视频帧率，更新配置文件
     * @param QString text 更新内容
     * @return 无返回值
     */
    void sltUpdateCaptureFps(QString text);

    /**
     * @brief sltUpdateCaptureX
     * 修改采集视频起始X，更新配置文件
     * @param int value 更新内容
     * @return 无返回值
     */
    void sltUpdateCaptureX(int value);

    /**
     * @brief sltUpdateCaptureY
     * 修改采集视频起始Y，更新配置文件
     * @param int value 更新内容
     * @return 无返回值
     */
    void sltUpdateCaptureY(int value);

    void sltScreenActivated(int index);
    void checkDeviceInputEvent();

    /**
     * @brief sltRtspClicked
     * 屏幕采集推rtsp流
     * @param bool flag 选中标志位
     * @return 无返回值
     */
    void sltRtspClicked(bool flag);

    /**
     * @brief sltRtmpClicked
     * 屏幕采集推rtmp流
     * @param bool flag 选中标志位
     * @return 无返回值
     */
    void sltRtmpClicked(bool flag);

    void checkWidthInputEvent();

    void checkHeightInputEvent();

    void checkUrlInputEvent();

    void checkFpsInputEvent();

signals:
    void signalMessageInfoText(QString,QString,MessageType);

private:
    /**
    * @brief getParaValue 函数
    * 获取参数
    * @param groupName:参数所属组名称 paraKey 参数键
    * @return 无
    */
    QVariant getParaValue(QString groupName,QString paraKey);

    /**
    * @brief setParaValue 函数
    * 设置参数
    * @param groupName:参数所属组名称 paraKey 参数键 value 参数值
    * @return 无
    */
    void setParaValue(QString groupName,QString paraKey,QString value);

    void refreshScreenList();
    void applyCaptureSelection();
    void saveCaptureSettings();

    bool m_savingDevice = false;
    QString m_deviceText;
    bool m_loading = false;
    QList<CaptureScreenInfo> m_screens;
    CaptureSelection m_capture;
    QIntValidator *m_widthValidator = nullptr;
    QIntValidator *m_heightValidator = nullptr;
    Ui::ScreenPushSetWidget *ui;
    IUiPluginBase *m_pPlugin = nullptr;
    std::unique_ptr<QSettings> m_pSettings;
};

#endif // SCREENPUSHSETWIDGET_H
