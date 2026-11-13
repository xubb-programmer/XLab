#ifndef SNAPSHOTSHOWWIDGET_H
#define SNAPSHOTSHOWWIDGET_H

#include <QWidget>
#include <QTimer>
namespace Ui {
class SnapShotShowWidget;
}

class SnapShotShowWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SnapShotShowWidget(QWidget *parent = 0);
    ~SnapShotShowWidget();

    void initData();

public slots:
    void sltShowSnapShot(QString imgPath);

private slots:
    void sltDoneBtnClicked();
    void sltDeleteBtnClicked();
private:
    Ui::SnapShotShowWidget *ui;
    QTimer *m_pTimer = nullptr;
    QString m_imgPath="";
};

#endif // SNAPSHOTSHOWWIDGET_H
