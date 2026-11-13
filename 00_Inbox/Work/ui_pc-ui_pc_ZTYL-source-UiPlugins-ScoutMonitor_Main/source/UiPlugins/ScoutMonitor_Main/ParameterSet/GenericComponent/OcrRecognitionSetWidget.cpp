#include "OcrRecognitionSetWidget.h"
#include "ui_OcrRecognitionSetWidget.h"
#include <QSettings>
#include <QFile>
#include <QTextCodec>

OcrRecognitionSetWidget::OcrRecognitionSetWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::OcrRecognitionSetWidget)
{
    ui->setupUi(this);
}

OcrRecognitionSetWidget::~OcrRecognitionSetWidget()
{
    delete ui;
}

/**
 * @brief initUi
 * 插件数据初始化交互
 * @param void
 * @return 无返回值
 */
void OcrRecognitionSetWidget::initUi(IUiPluginBase *plugin)
{
    m_pPlugin = plugin;
    initCfg();
    loadQssStyle();
}

/**
 * @brief initData
 * 插件数据初始化交互
 * @param void
 * @return 无返回值
 */
void OcrRecognitionSetWidget::initData()
{
    ;
}

/**
 * @brief initCfg
 * 读取配置文件获取发送时的基础UI
 * @param void
 * @return 无返回值
 */
void OcrRecognitionSetWidget::initCfg()
{
    QSettings settings(QApplication::applicationDirPath() + "/config/communication.ini" , QSettings::IniFormat);
    settings.setIniCodec(QTextCodec::codecForName("GBK"));
    settings.beginGroup("HTTP");
    m_strUrl = settings.value("url").toString();
    settings.endGroup();
}

/**
 * @brief loadQssStyle
 * 设置界面Qss初始化样式表
 * @param void
 * @return void
 */
void OcrRecognitionSetWidget::loadQssStyle()
{
    QString strPath = m_pPlugin->getStylePath() + QString("/OcrRecognitionSetWidget.qss");

    Q_ASSERT(!strPath.isEmpty() && "path is empty");
    QFile textFile(strPath);

    Q_ASSERT(textFile.exists() && "file can't open");
    textFile.open(QIODevice::ReadOnly | QIODevice::Text);
    QTextStream textStream(&textFile);

    QString style = textStream.readAll();
    this->setStyleSheet(style);
}

/**
 * @brief on_VideoResolution_width_spinBox_editingFinished
 * 输入OCR宽结束，执行指令下发
 * @param void
 * @return 无返回值
 */
void OcrRecognitionSetWidget::on_VideoResolution_width_spinBox_editingFinished()
{
    QJsonObject json;
    json[QString::fromLocal8Bit("x")] = ui->VideoPostion_x_spinBox->value();
    json[QString::fromLocal8Bit("y")] = ui->VideoPostion_y_spinBox->value();
    json[QString::fromLocal8Bit("width")] = ui->VideoResolution_width_spinBox->value();
    json[QString::fromLocal8Bit("height")] = ui->VideoResolution_height_spinBox->value();

    QString url = "http://" + m_strUrl + "/proxy/setting/pushStream";
    emit signalOcrpushStreamPara(url,json);
}

/**
 * @brief on_VideoResolution_height_spinBox_editingFinished
 * 输入OCR高结束，执行指令下发
 * @param void
 * @return 无返回值
 */
void OcrRecognitionSetWidget::on_VideoResolution_height_spinBox_editingFinished()
{
    QJsonObject json;
    json[QString::fromLocal8Bit("x")] = ui->VideoPostion_x_spinBox->value();
    json[QString::fromLocal8Bit("y")] = ui->VideoPostion_y_spinBox->value();
    json[QString::fromLocal8Bit("width")] = ui->VideoResolution_width_spinBox->value();
    json[QString::fromLocal8Bit("height")] = ui->VideoResolution_height_spinBox->value();

    QString url = "http://" + m_strUrl + "/proxy/setting/pushStream";
    emit signalOcrpushStreamPara(url,json);
}

/**
 * @brief on_VideoPostion_x_spinBox_editingFinished
 * 输入OCR-X结束，执行指令下发
 * @param void
 * @return 无返回值
 */
void OcrRecognitionSetWidget::on_VideoPostion_x_spinBox_editingFinished()
{
    QJsonObject json;
    json[QString::fromLocal8Bit("x")] = ui->VideoPostion_x_spinBox->value();
    json[QString::fromLocal8Bit("y")] = ui->VideoPostion_y_spinBox->value();
    json[QString::fromLocal8Bit("width")] = ui->VideoResolution_width_spinBox->value();
    json[QString::fromLocal8Bit("height")] = ui->VideoResolution_height_spinBox->value();

    QString url = "http://" + m_strUrl + "/proxy/setting/pushStream";
    emit signalOcrpushStreamPara(url,json);
}

/**
 * @brief on_VideoPostion_y_spinBox_editingFinished
 * 输入OCR-Y结束，执行指令下发
 * @param void
 * @return 无返回值
 */
void OcrRecognitionSetWidget::on_VideoPostion_y_spinBox_editingFinished()
{
    QJsonObject json;
    json[QString::fromLocal8Bit("x")] = ui->VideoPostion_x_spinBox->value();
    json[QString::fromLocal8Bit("y")] = ui->VideoPostion_y_spinBox->value();
    json[QString::fromLocal8Bit("width")] = ui->VideoResolution_width_spinBox->value();
    json[QString::fromLocal8Bit("height")] = ui->VideoResolution_height_spinBox->value();

    QString url = "http://" + m_strUrl + "/proxy/setting/pushStream";
    emit signalOcrpushStreamPara(url,json);
}

/**
 * @brief slotFinishedForOcrPushStreamPara
 * Ocr识别区域设置结果反馈
 * @param strData(反馈结果)
 * @return 无返回值
 */
void OcrRecognitionSetWidget::slotFinishedForOcrPushStreamPara(QString strData)
{
    QTextCodec *tc = QTextCodec::codecForName("UTF-8");
    Q_UNUSED(tc);
    QJsonDocument root_Doc = QJsonDocument::fromJson(strData.toUtf8().data());
    if(root_Doc.isObject())
    {
        int nCode = root_Doc.object().value("code").toInt();
        if(nCode == 0)
        {
            emit signalMessageInfoText(QString::fromUtf8(u8"OCR区域设置"),
                                       QString::fromUtf8(u8"OCR区域设置成功!"),
                                       MessageType_Success);
        }
        else
        {
            emit signalMessageInfoText(QString::fromUtf8(u8"OCR区域设置"),
                                       QString::fromUtf8(u8"OCR区域设置失败!"),
                                       MessageType_Warn);
        }
    }
}
