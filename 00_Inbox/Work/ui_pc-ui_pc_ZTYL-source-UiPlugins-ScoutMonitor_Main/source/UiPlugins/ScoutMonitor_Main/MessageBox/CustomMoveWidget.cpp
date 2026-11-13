#include "CustomMoveWidget.h"
#include <QMouseEvent>

CustomMoveWidget::CustomMoveWidget(QWidget *parent)
    : QWidget(parent)
{

}
void CustomMoveWidget::getCurrentSeat(int index)
{
    m_posx = index;
    //m_CurrentSeat = index;
}
void CustomMoveWidget::setTitle(QWidget *title)
{
    m_pTitle = title;
}

void CustomMoveWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        if (getPos(event->globalPos()))
        {
            m_pWndPos = event->globalPos() - this->pos();
            m_bWndClicked = true;
        }
    }
}

void CustomMoveWidget::mouseReleaseEvent(QMouseEvent *)
{
    m_bWndClicked = false;
}

void CustomMoveWidget::mouseMoveEvent(QMouseEvent *event)
{
    if(!m_pTitle)
        return;

    if (m_bWndClicked&&event->buttons()&Qt::LeftButton)
    {
        QPoint point = event->globalPos() - m_pWndPos;
        int x;
//        if(m_CurrentSeat == 1)
//        {
//            x = point.x() < 0 ? 0 :
//                point.x() >= (1920 - m_pTitle->width()) ? 1920 - m_pTitle->width() :
//                point.x();
//        }
//        else
//        {
//            x = point.x() < 1920 ? 1920 :
//                point.x() >= (3840 - m_pTitle->width()) ? 3840 - m_pTitle->width() :
//                point.x();
//        }

        if(m_posx<=1920 && m_posx>=0)
        {
            x = point.x() < 0 ? 0 :
                point.x() >= (1920 - m_pTitle->width()) ? 1920 - m_pTitle->width() :
                point.x();
        }
        else if(m_posx<3840 && m_posx>1920)
        {
            x = point.x() < 1920 ? 1920 :
                point.x() >= (3840 - m_pTitle->width()) ? 3840 - m_pTitle->width() :
                point.x();
        }



        int y = point.y() < 0 ? 0 :
                point.y() >= (1080 - m_pTitle->height()) ? 1080 - m_pTitle->height() :
                point.y();

        move(x,y);
    }
}

bool CustomMoveWidget::getPos(QPoint point)
{
    if (point.y() >= this->pos().y() && point.y() <= this->pos().y() + m_pTitle->height())
    {
        return true;
    }
    return false;
}
