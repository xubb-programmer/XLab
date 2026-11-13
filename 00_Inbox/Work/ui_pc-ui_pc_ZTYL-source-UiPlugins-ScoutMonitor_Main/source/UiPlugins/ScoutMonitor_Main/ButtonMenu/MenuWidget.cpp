#include "MenuWidget.h"
#include "ui_MenuWidget.h"

MenuWidget::MenuWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::MenuWidget)
{
    ui->setupUi(this);
    connect(ui->pushButton,SIGNAL(clicked(bool)),this,SLOT(slotBtnPressed()));
    this->setAttribute(Qt::WA_TranslucentBackground,true);
    ui->pushButton->move(0,0);
}

MenuWidget::~MenuWidget()
{
    delete ui;
}

void MenuWidget::changeBtnSize(int width, int height)
{
    this->setFixedSize(width,height);
    ui->pushButton->setMinimumSize(width,height);
    ui->pushButton->setMaximumSize(width,height);
}

void MenuWidget::changeBtnName(QString name)
{
    ui->pushButton->setText(name);
    this->update();
}

void MenuWidget::loadBtnQss(QString style, int level, QString picture,int padding,bool check)
{
    QString str=QString("QPushButton{color: #ffffff;font:24px; font-family: Alibaba PuHuiTi 2.0;padding-left:%4px;"
                        "border-image: url(:/resource/%1/%2/%3_normal.png);}"
                        "QPushButton::hover {color: #0df6ff;border-image: url(:/resource/%1/%2/%3_hover.png);}"
                        "QPushButton::pressed {color: #0df6ff;border-image: url(:/resource/%1/%2/%3_pressed.png);}"
                        "QPushButton::checked {color: #0df6ff;border-image: url(:/resource/%1/%2/%3_pressed.png);}")
            .arg(style).arg(level).arg(picture).arg(padding);
    ui->pushButton->setStyleSheet(str);
    ui->pushButton->setChecked(check);
    this->update();
}

void MenuWidget::cancelCheckStatus()
{
    ui->pushButton->setChecked(false);
}

void MenuWidget::slotBtnPressed()
{
    emit signalSendMsg(m_stBaseInfo);

}
