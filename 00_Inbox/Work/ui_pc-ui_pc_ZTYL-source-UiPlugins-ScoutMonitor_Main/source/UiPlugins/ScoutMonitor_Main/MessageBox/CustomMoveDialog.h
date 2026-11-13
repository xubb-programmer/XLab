#ifndef CUSTOMMOVEDIALOG_H
#define CUSTOMMOVEDIALOG_H

#include <QWidget>
#include <QDialog>
class CustomMoveDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CustomMoveDialog(QDialog *parent );

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

#endif // CUSTOMMOVEDIALOG_H
