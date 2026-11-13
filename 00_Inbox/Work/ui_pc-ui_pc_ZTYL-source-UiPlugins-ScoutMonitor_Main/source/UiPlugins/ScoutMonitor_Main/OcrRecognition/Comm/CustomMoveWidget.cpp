#include "CustomMoveWidget.h"
#include <QMouseEvent>

CustomMoveWidget::CustomMoveWidget(QWidget *parent)
    : QWidget(parent)
{

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
        move(point.x(),point.y());
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
