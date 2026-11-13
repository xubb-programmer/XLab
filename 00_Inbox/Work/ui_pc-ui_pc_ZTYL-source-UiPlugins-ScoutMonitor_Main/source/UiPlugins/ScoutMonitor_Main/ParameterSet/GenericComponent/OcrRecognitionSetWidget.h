#ifndef OCRRECOGNITIONSETWIDGET_H
#define OCRRECOGNITIONSETWIDGET_H

#include <QWidget>
#include <QObject>
#include "IUiPluginBase.h"
#include "definestruct.h"
#include <QJsonObject>
#include <QJsonDocument>


namespace Ui {
class OcrRecognitionSetWidget;
}

class OcrRecognitionSetWidget : public QWidget
{
    Q_OBJECT

public:
    explicit OcrRecognitionSetWidget(QWidget *parent = 0);
    ~OcrRecognitionSetWidget();

    /**
     * @brief initUi
     * 插件数据初始化交互
     * @param void
     * @return 无返回值
     */
    void initUi(IUiPluginBase *plugin);

    /**
     * @brief initData
     * 插件数据初始化交互
     * @param void
     * @return 无返回值
     */
    void initData();

    /**
     * @brief initCfg
     * 读取配置文件获取发送时的基础UI
     * @param void
     * @return 无返回值
     */
    void initCfg();

    /**
     * @brief loadQssStyle
     * 设置界面Qss初始化样式表
     * @param void
     * @return void
     */
    void loadQssStyle();

private slots:
    /**
     * @brief on_VideoResolution_width_spinBox_editingFinished
     * 输入OCR宽结束，执行指令下发
     * @param void
     * @return 无返回值
     */
    void on_VideoResolution_width_spinBox_editingFinished();

    /**
     * @brief on_VideoResolution_height_spinBox_editingFinished
     * 输入OCR高结束，执行指令下发
     * @param void
     * @return 无返回值
     */
    void on_VideoResolution_height_spinBox_editingFinished();

    /**
     * @brief on_VideoPostion_x_spinBox_editingFinished
     * 输入OCR-X结束，执行指令下发
     * @param void
     * @return 无返回值
     */
    void on_VideoPostion_x_spinBox_editingFinished();

    /**
     * @brief on_VideoPostion_y_spinBox_editingFinished
     * 输入OCR-Y结束，执行指令下发
     * @param void
     * @return 无返回值
     */
    void on_VideoPostion_y_spinBox_editingFinished();

    /**
     * @brief slotFinishedForOcrPushStreamPara
     * Ocr识别区域设置结果反馈
     * @param strData(反馈结果)
     * @return 无返回值
     */
    void slotFinishedForOcrPushStreamPara(QString strData);

signals:
    // 推流消息下发
    void signalOcrpushStreamPara(QString, QJsonObject);

    // 发送消息至MessageBox插件
    void signalMessageInfoText(QString,QString,MessageType);

private:
    Ui::OcrRecognitionSetWidget *ui;
    IUiPluginBase *m_pPlugin = nullptr;
    QString m_strUrl = "10.100.24.230"; // http通信基础Url
};

#endif // OCRRECOGNITIONSETWIDGET_H
