#include "TargetItem.h"
#include "ui_TargetItem.h"
#include <QFile>
#include <QMessageBox>
#include <QRegExp>
#include <QRegExpValidator>
#include <QValidator>
#include <QSettings>
#include <QApplication>

namespace {

class TargetNumber002Validator : public QValidator
{
public:
    explicit TargetNumber002Validator(QObject *parent = nullptr)
        : QValidator(parent)
    {
    }

    State validate(QString &input, int &position) const override
    {
        Q_UNUSED(position);
        if (input.isEmpty()) {
            return Intermediate;
        }
        if (input.size() > 5) {
            return Invalid;
        }
        for (const QChar ch : input) {
            if (!ch.isDigit()) {
                return Invalid;
            }
        }

        bool ok = false;
        const uint value = input.toUInt(&ok);
        if (!ok || value > 65535u) {
            return Invalid;
        }
        return value >= 1u ? Acceptable : Intermediate;
    }
};

} // namespace


TargetItem::TargetItem(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::TargetItem)
{
    ui->setupUi(this);
}

TargetItem::~TargetItem()
{
    delete ui;
}

/**
 * @brief initUi
 * 界面初始化函数。
 * @param pPlugin 界面框架指针
 * @return 无返回值
 */
void TargetItem::initUi(IUiPluginBase *plugin)
{
    m_pPlugin = plugin;
    m_pPlugin->setStyle("day");

    connect(ui->Report_pushButton,SIGNAL(clicked()),this,SLOT(slotReport_pushButton()));
    connect(ui->Modify_pushButton,SIGNAL(clicked(bool)),this,SLOT(slotModify_pushButton(bool)));
    connect(ui->Delete_pushButton,SIGNAL(clicked()),this,SLOT(slotDelete_pushButton()));

    // 批号作为目标的唯一标识，输入过程中不能逐字符刷新整个目标列表，
    // 否则 setTargetListInfo 会把尚未输入完成的内容立即覆盖。失焦后一次提交。
    connect(ui->No_value_label,SIGNAL(editingFinished()),this,SLOT(slotNo_value_EditingFinished()));
    connect(ui->lon_value_label,SIGNAL(textEdited(QString)),this,SLOT(slotlon_value_EditingFinished(QString)));
    connect(ui->lat_value_label,SIGNAL(textEdited(QString)),this,SLOT(slotlat_value_EditingFinished(QString)));
    connect(ui->X_value_label,SIGNAL(textEdited(QString)),this,SLOT(slotX_value_EditingFinished(QString)));
    connect(ui->Y_value_label,SIGNAL(textEdited(QString)),this,SLOT(slotY_value_EditingFinished(QString)));
    connect(ui->Ele_value_label,SIGNAL(textEdited(QString)),this,SLOT(slotEle_value_EditingFinished(QString)));
    connect(ui->Time_value_label,SIGNAL(editingFinished()),this,SLOT(slotTime_value_EditingFinished));
    loadQssStyle();


    // 正则表达式：允许 -180.0 ~ 180.0，最多6位小数
    QRegExp rx(R"(^-?(180(\.0{0,7})?|(1[0-7]\d|[1-9]\d?|0)(\.\d{1,7})?)$)");
    QRegExpValidator *validator = new QRegExpValidator(rx, ui->lon_value_label);//经度
    ui->lon_value_label->setValidator(validator);

    // 正则表达式：允许 -90.0 ~ 90.0，最多6位小数
    QRegExp rx1(R"(^-?(90(\.0{1,7})?|([0-8]?\d)(\.\d{1,7})?)$)");
    QRegExpValidator *validator1 = new QRegExpValidator(rx1, ui->lat_value_label);//纬度
    ui->lat_value_label->setValidator(validator1);

    QDoubleValidator *validator2=new QDoubleValidator(-9999999999999.0,999999999999.0,6,ui->Ele_value_label);//高程
    //设置输入格式
    validator2->setNotation(QDoubleValidator::StandardNotation);
    ui->Ele_value_label->setValidator(validator2);

    QRegExp rxNumber(QStringLiteral("^[0-9]{0,10}$"));
    m_targetNumberDefaultValidator = new QRegExpValidator(rxNumber, this);
    m_targetNumber002Validator = new TargetNumber002Validator(this);
    updateTargetNumberValidator();

    QDoubleValidator *validator4=new QDoubleValidator(-10001965.7,10001965.7,1,ui->X_value_label);//X
    //设置输入格式
    validator4->setNotation(QDoubleValidator::StandardNotation);
    ui->X_value_label->setValidator(validator4);

    QDoubleValidator *validator5=new QDoubleValidator(1165000.0,32500000.0,1,ui->Y_value_label);//Y
    //设置输入格式
    validator5->setNotation(QDoubleValidator::StandardNotation);
    ui->Y_value_label->setValidator(validator5);


//    ui->Time_value_label->setInputMask("99:99:99 99/99");

}

void TargetItem::updateTargetNumberValidator()
{
    QSettings settings(QApplication::applicationDirPath() + "/config/communication.ini",
                       QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("Address"));
    const QString carType = settings.value(QStringLiteral("cartype"),
                                           QStringLiteral("033")).toString().trimmed();
    settings.endGroup();

    ui->No_value_label->setValidator(carType == QLatin1String("002")
                                     ? m_targetNumber002Validator
                                     : m_targetNumberDefaultValidator);
}

bool TargetItem::isTargetNumberValid(const QString &targetNumber) const
{
    const QString number = targetNumber.trimmed();
    if (number.isEmpty()) {
        return false;
    }

    QSettings settings(QApplication::applicationDirPath() + "/config/communication.ini",
                       QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("Address"));
    const QString carType = settings.value(QStringLiteral("cartype"),
                                           QStringLiteral("033")).toString().trimmed();
    settings.endGroup();

    bool ok = false;
    const qulonglong value = number.toULongLong(&ok);
    if (!ok) {
        return false;
    }
    if (carType == QLatin1String("002")) {
        return value >= 1u && value <= 65535u;
    }
    return number.size() <= 10;
}

/**
 * @brief setTargetListInfo
 * 将目标信息显示到界面。
 * @param TargetInfo(目标信息)
 * @return 无返回值
 */
void TargetItem::setTargetListInfo(TargetInfo dData)
{

    ui->No_value_label->setText(dData.targetNumber);
    ui->Type_comboBox->setCurrentIndex(dData.targetType);
    ui->lon_value_label->setText(QString::number(dData.targetLon,'f',7));
    ui->lat_value_label->setText(QString::number(dData.targetLat,'f',7));
    ui->Ele_value_label->setText(QString::number(dData.targerElevate,'f',1));
    ui->Time_value_label->setText(dData.targetTime);

    CoordinateTransfor(1);
//    ui->X_value_label->setText(QString::number(dData.X_Value,'f',1));
//    ui->Y_value_label->setText(QString::number(dData.Y_Value,'f',1));

    // 将目标信息存储到内存
    m_structTargetInfo.targetNumber = dData.targetNumber;
    m_structTargetInfo.targetType = dData.targetType;
    m_structTargetInfo.targetLon = dData.targetLon;
    m_structTargetInfo.targetLat = dData.targetLat;
    m_structTargetInfo.targerElevate = dData.targerElevate;
    m_structTargetInfo.targetTime = dData.targetTime;
//    m_structTargetInfo.X_Value = dData.X_Value;
//    m_structTargetInfo.Y_Value = dData.Y_Value;

}

/**
 * @brief getTargetNumber
 * 获取TargetItem对象上的目标编号。
 * @param void
 * @return （QString）目标编号信息
 */
QString TargetItem::getTargetNumber()
{
    return ui->No_value_label->text();
}

/**
 * @brief setModifyChecked Item编辑状态互斥
 * @param checked
 */
void TargetItem::setModifyChecked(bool checked)
{
    ui->Modify_pushButton->setChecked(checked);
    slotModify_pushButton(checked);
}

void TargetItem::slotModify_pushButton(bool bCheck)
{

    if(bCheck)
    {
        updateTargetNumberValidator();
        ui->No_value_label->setDisabled(false);
        ui->Type_comboBox->setDisabled(false);
        ui->lon_value_label->setDisabled(false);
        ui->lat_value_label->setDisabled(false);
        ui->Ele_value_label->setDisabled(false);
//        ui->Time_value_label->setDisabled(false);
        ui->X_value_label->setDisabled(false);
        ui->Y_value_label->setDisabled(false);

        if(ui->lon_value_label->text()=="0.0000000"){
            ui->lon_value_label->clear();
            ui->lon_value_label->setPlaceholderText(QString::fromLocal8Bit("请输入-180~180之间的数值"));
        }

        if(ui->lat_value_label->text()=="0.0000000"){
            ui->lat_value_label->clear();
            ui->lat_value_label->setPlaceholderText(QString::fromLocal8Bit("请输入-90~90之间的数值"));
        }

    }
    else
    {
        ui->No_value_label->setDisabled(true);
        ui->Type_comboBox->setDisabled(true);
        ui->lon_value_label->setDisabled(true);
        ui->lat_value_label->setDisabled(true);
        ui->Ele_value_label->setDisabled(true);
        ui->Time_value_label->setDisabled(true);
        ui->X_value_label->setDisabled(true);
        ui->Y_value_label->setDisabled(true);

        if(ui->lon_value_label->text()==""){
            ui->lon_value_label->clear();
            ui->lon_value_label->setText("0.0000000");
        }

        if(ui->lat_value_label->text()==""){
            ui->lat_value_label->clear();
            ui->lat_value_label->setText("0.0000000");
        }
    }

    updateTargetInfo();
    emit signalModifyChecked(this, bCheck);

}

/**
 * @brief slotReport_pushButton
 * 点击目标上报按钮，将目标信息上报到车长。
 * @param void
 * @return 无返回值
 */
void TargetItem::slotReport_pushButton()
{
    updateTargetNumberValidator();
    const QString targetNumber = ui->No_value_label->text().trimmed();
    if (!isTargetNumberValid(targetNumber)) {
        emit signalMessageInfoText(QString::fromUtf8(u8"目标上报"),
                                   QString::fromUtf8(u8"002协议目标编号请输入1~65535之间的整数！"),
                                   MessageType_Warn);
        ui->No_value_label->setFocus();
        ui->No_value_label->selectAll();
        return;
    }

    m_structTargetInfo.targetNumber = targetNumber;
    m_structTargetInfo.targetType = ui->Type_comboBox->currentIndex();
    m_structTargetInfo.targetLon = ui->lon_value_label->text().toDouble();
    m_structTargetInfo.targetLat = ui->lat_value_label->text().toDouble();
    m_structTargetInfo.targerElevate = ui->Ele_value_label->text().toDouble();
    m_structTargetInfo.targetTime = ui->Time_value_label->text();

    emit signalTargetReport(m_structTargetInfo);
}

void TargetItem::slotNo_value_EditingFinished()
{
    const QString strData = ui->No_value_label->text().trimmed();
    if (!isTargetNumberValid(strData)) {
        emit signalMessageInfoText(QString::fromUtf8(u8"目标编号"),
                                   QString::fromUtf8(u8"002协议目标编号请输入1~65535之间的整数！"),
                                   MessageType_Warn);
        ui->No_value_label->setText(m_structTargetInfo.targetNumber);
        return;
    }
    m_strHistoryNumber = m_structTargetInfo.targetNumber;
    m_structTargetInfo.targetNumber = strData;

    m_structTargetInfo.targetType = ui->Type_comboBox->currentIndex();
    m_structTargetInfo.targetLon = ui->lon_value_label->text().toDouble();
    m_structTargetInfo.targetLat = ui->lat_value_label->text().toDouble();
    m_structTargetInfo.targerElevate = ui->Ele_value_label->text().toDouble();
    m_structTargetInfo.targetTime = ui->Time_value_label->text();

    signalModifyNoInfo(m_strHistoryNumber,m_structTargetInfo);
}


void TargetItem::slotlon_value_EditingFinished(QString strData)
{
    m_structTargetInfo.targetLon = ui->lon_value_label->text().toDouble();
    CoordinateTransfor(1);
//    emit signalModifyTargetInfo(m_structTargetInfo);
}

void TargetItem::slotlat_value_EditingFinished(QString strData)
{
    m_structTargetInfo.targetLat = ui->lat_value_label->text().toDouble();
    CoordinateTransfor(1);
    //    emit signalModifyTargetInfo(m_structTargetInfo);
}

/**
 * @brief slotX_value_EditingFinished
 * 目标X编辑完成后触发的执行槽函数。
 * @param void
 * @return 无返回值
 */
void TargetItem::slotX_value_EditingFinished(QString)
{
    if(ui->X_value_label->text().isEmpty() || ui->X_value_label->text()=="-" ||ui->X_value_label->text()==".")return;

    bool ok;
    double Xval=ui->X_value_label->text().toDouble(&ok);
    if(!ok){
        ui->X_value_label->backspace();
        return;
    }

    if(Xval<-10001965.7){
        ui->X_value_label->setText("-10001965.7");
        ui->X_value_label->setCursorPosition(ui->X_value_label->text().length());
    }else if(Xval>10001965.7){
        ui->X_value_label->setText("10001965.7");
        ui->X_value_label->setCursorPosition(ui->X_value_label->text().length());
    }

    m_structTargetInfo.X_Value = ui->X_value_label->text().toDouble();
    CoordinateTransfor(0);
}

/**
 * @brief slotY_value_EditingFinished
 * 目标Y编辑完成后触发的执行槽函数。
 * @param void
 * @return 无返回值
 */
void TargetItem::slotY_value_EditingFinished(QString)
{
    if(ui->Y_value_label->text().isEmpty() || ui->Y_value_label->text()==".")return;

    bool ok;
    double Yval=ui->Y_value_label->text().toDouble(&ok);

    if(!ok){
        ui->Y_value_label->backspace();
        return;
    }

    // 输入过程中允许中间态，避免刚输入首位数字就被回填到下限
    if(Yval>32500000.0){
        ui->Y_value_label->setText("32500000.0");
        ui->Y_value_label->setCursorPosition(ui->Y_value_label->text().length());
    }

    m_structTargetInfo.Y_Value = ui->Y_value_label->text().toDouble();
    CoordinateTransfor(0);
}

void TargetItem::slotEle_value_EditingFinished(QString)
{
    m_structTargetInfo.targerElevate = ui->Ele_value_label->text().toDouble();

//    emit signalModifyTargetInfo(m_structTargetInfo);
}

void TargetItem::slotTime_value_EditingFinished()
{
    m_structTargetInfo.targetTime = ui->Time_value_label->text();

    emit signalModifyTargetInfo(m_structTargetInfo);
}

void TargetItem::slotDelete_pushButton()
{
    emit signalDeleteInfoTarget(QString::fromLocal8Bit("删除目标信息:%1").arg(ui->No_value_label->text()));
}

/**
 * @brief CoordinateTransfor 高斯坐标与经度、纬度转换
 * @param statue 0：高斯坐标转经度、纬度 1：经度、纬度转高斯坐标
 */
void TargetItem::CoordinateTransfor(int statu)
{
    double jingdu = 0, weidu = 0, gaocheng = 0;
    double X = 0, Y = 0, H = 0;
    if(statu){

        ui->lon_value_label->setStyleSheet("color:white");
        ui->lat_value_label->setStyleSheet("color:white");

        jingdu = ui->lon_value_label->text().toDouble();;//经度
        weidu  = ui->lat_value_label->text().toDouble();//纬度
        gaocheng = ui->Ele_value_label->text().toDouble();//高程
        CoordinateTrans(jingdu,weidu,gaocheng,X, Y, H,false,true); //经度、纬度转高斯坐标
        ui->X_value_label->setText(QString::number(X,'f',1));
        ui->Y_value_label->setText(QString::number(Y,'f',1));
        ui->Ele_value_label->setText(QString::number(H,'f',1));
        m_structTargetInfo.X_Value=X;
        m_structTargetInfo.Y_Value=Y;
        m_structTargetInfo.targerElevate=H;

    }else{
        X=ui->X_value_label->text().toDouble();;//X
        Y=ui->Y_value_label->text().toDouble();;//Y
        H = ui->Ele_value_label->text().toDouble();//Z
        CoordinateTrans(X, Y,H,jingdu,weidu,gaocheng, true,false); //高斯坐标转经度、纬度


        ui->lon_value_label->setStyleSheet("color:white");
        ui->lat_value_label->setStyleSheet("color:white");
        if(jingdu<-180.0){
            ui->lon_value_label->setText(QString::number(-180.0000000,'f',7));
            ui->lon_value_label->setStyleSheet("color:red");
        }else if(jingdu>180.0){
            ui->lon_value_label->setText(QString::number(180.0000000,'f',7));
            ui->lon_value_label->setStyleSheet("color:red");
        }else{
            ui->lon_value_label->setText(QString::number(jingdu,'f',7));
        }


        if(weidu<-90.0){
            ui->lat_value_label->setText(QString::number(-90.0000000,'f',7));
            ui->lat_value_label->setStyleSheet("color:red");
        }else if(weidu>90.0){
            ui->lat_value_label->setText(QString::number(90.0000000,'f',7));
            ui->lat_value_label->setStyleSheet("color:red");
        }else{
            ui->lat_value_label->setText(QString::number(weidu,'f',7));
        }

//        ui->Ele_value_label->setText(QString::number(gaocheng,'f',1));
        m_structTargetInfo.targetLon=jingdu;
        m_structTargetInfo.targetLat=weidu;
        m_structTargetInfo.targerElevate=H;
    }
}


void TargetItem::updateTargetInfo()
{
    //更新目标信息
    m_structTargetInfo.targetNumber = ui->No_value_label->text();
    m_structTargetInfo.targetType = ui->Type_comboBox->currentIndex();
    m_structTargetInfo.targetLon = ui->lon_value_label->text().toDouble();
    m_structTargetInfo.targetLat = ui->lat_value_label->text().toDouble();
    m_structTargetInfo.targerElevate = ui->Ele_value_label->text().toDouble();
    m_structTargetInfo.targetTime = ui->Time_value_label->text();
    emit signalModifyTargetInfo(m_structTargetInfo);
}

/**
 * @brief loadQssStyleFile
 * 给予UI界面控件设置Qss图片样式。
 * @param void
 * @return 无返回值
 */
void TargetItem::loadQssStyle()
{
    QString strPath = m_pPlugin->getStylePath() + QString("/TargetItem.qss");
    Q_ASSERT(!strPath.isEmpty() && "path is empty");
    QFile textFile(strPath);
    Q_ASSERT(textFile.exists() && "file can't open");
    textFile.open(QIODevice::ReadOnly | QIODevice::Text);
    QTextStream textStream(&textFile);
    QString style = textStream.readAll();
    this->setStyleSheet(style);
}
