#include "imagewidget.h"
#include <QPainter>
#include <QMessageBox>
#include <QScreen>
#include <QApplication>
#include <QDebug>

ImageWidget::ImageWidget(QWidget *parent) : QWidget(parent)
{
    // 设置初始大小
    QScreen *screen =QApplication::primaryScreen();
    QSize screenSize =screen->size();
    setMinimumSize(705*(((double)((double)screenSize.width()-1024.0)/screenSize.width())+1.0),
                   490*(((double)(screenSize.height()-768)/screenSize.height())+1));
}

bool ImageWidget::loadImage(const QString &imagePath)
{

    // 加载图片并判断是否成功
    if (!m_pixmap.load(imagePath)) {
        // 发送弹窗告警消息
        emit signalMessageInfoText(QString::fromUtf8(u8"警告"),
                                   QString::fromUtf8(u8"无法加载图片:") + imagePath,
                                   MessageType_Warn);
        return false;
    }
    
    // 图片加载成功后刷新界面
    update();
    return true;
}

void ImageWidget::paintEvent(QPaintEvent *event)
{
    // 先调用父类的paintEvent，确保控件正常绘制
    QWidget::paintEvent(event);

    
    // 创建画家对象，指定绘图设备为当前窗口
    QPainter painter(this);

    //清空图片
    painter.eraseRect(rect());

    painter.fillRect(rect(),QColor("#183757"));

    // 如果没有图片，直接返回
    if (m_pixmap.isNull()) {
        return;
    }
    //开启平滑缩放，优质画面
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    //1.计算保持宽高比的目标尺寸
    QSize scaledSize=m_pixmap.size().scaled(rect().size(),Qt::KeepAspectRatio);

    //2.构建目标矩形并居中
    QRect targetRect(0,0,scaledSize.width(),scaledSize.height());
    targetRect.moveCenter(rect().center());
    // 绘制图片，保持图片比例并适应窗口大小
    painter.drawPixmap(targetRect, m_pixmap);
}

void ImageWidget::clearShow()
{
    m_pixmap.load("");
    update();

}
