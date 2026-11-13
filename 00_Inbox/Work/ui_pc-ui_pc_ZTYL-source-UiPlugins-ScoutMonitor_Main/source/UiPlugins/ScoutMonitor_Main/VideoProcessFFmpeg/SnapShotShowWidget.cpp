#include "SnapShotShowWidget.h"
#include "ui_SnapShotShowWidget.h"
#include <QFile>
#include <QPushButton>
#include <QDebug>
SnapShotShowWidget::SnapShotShowWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SnapShotShowWidget)
{
    ui->setupUi(this);
    initData();
}

SnapShotShowWidget::~SnapShotShowWidget()
{
    delete ui;
}

void SnapShotShowWidget::initData()
{
    m_pTimer = new QTimer(this);
    m_pTimer->setSingleShot(true);
    m_pTimer->setInterval(6000);
    connect(m_pTimer,&QTimer::timeout,this,&SnapShotShowWidget::sltDoneBtnClicked);
    connect(ui->deleteBtn,&QPushButton::clicked,this,&SnapShotShowWidget::sltDeleteBtnClicked);
    connect(ui->doneBtn,&QPushButton::clicked,this,&SnapShotShowWidget::sltDoneBtnClicked);

}

void SnapShotShowWidget::sltShowSnapShot(QString imgPath)
{
    QImage img;
    if(img.load(imgPath)){
        if(m_pTimer && m_pTimer->isActive()){
            m_pTimer->stop();
        }
        this->show();
        this->raise();
        ui->label_img->setPixmap(QPixmap::fromImage(img).scaled(ui->label_img->size(),Qt::KeepAspectRatio,Qt::SmoothTransformation));
        if(m_pTimer){
            m_pTimer->start();
        }
        m_imgPath = imgPath;
    }
}

void SnapShotShowWidget::sltDoneBtnClicked()
{
    m_imgPath="";
    this->hide();
}

void SnapShotShowWidget::sltDeleteBtnClicked()
{
    bool ret = QFile::remove(m_imgPath);
    this->hide();
    m_imgPath="";
}
