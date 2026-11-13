#ifndef CUSTOMMOVEWIDGET_H
#define CUSTOMMOVEWIDGET_H

#include <QWidget>
class CustomMoveWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(bool m_raise READ getRaise WRITE setRaise);
public:
    int getRaise(){
        return m_raise;
    }

    void setRaise(int r){
        m_raise = r;
    }
    bool m_raise = true;

    void show(){
        QWidget::show();
        raise();
    }

public:
    explicit CustomMoveWidget(QWidget *parent );

protected:
    void setTitle(QWidget *title);

    void getCurrentSeat(int index);

protected:
    void mousePressEvent(QMouseEvent *event);
    void mouseReleaseEvent(QMouseEvent *);
    void mouseMoveEvent(QMouseEvent *event);
    //
    bool getPos(QPoint point);
private:
    QPoint m_pWndPos;
    bool m_bWndClicked = false;
    QWidget *m_pTitle = nullptr;
    int m_CurrentSeat = 1;
    int m_posx = 0;
};

#endif // CUSTOMMOVEWIDGET_H
