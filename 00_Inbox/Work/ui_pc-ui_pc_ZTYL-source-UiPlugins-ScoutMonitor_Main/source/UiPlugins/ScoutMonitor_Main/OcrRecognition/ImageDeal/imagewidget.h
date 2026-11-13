#ifndef IMAGEWIDGET_H
#define IMAGEWIDGET_H

#include <QWidget>
#include <QPixmap>
#include <Comm/definestruct.h>

class ImageWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ImageWidget(QWidget *parent = nullptr);
    
    /**
     * @brief loadImage
     * 加载图片
     * @param imagePath(图片绝对路径)
     * @return bool(图片是否加载成功,true:成功 false:失败)
     */
    bool loadImage(const QString &imagePath);

    /**
     * @brief clearShow 清空背景图片
     */
    void clearShow();
    
protected:
    /**
     * @brief paintEvent
     * 重写绘图事件
     * @param event(触发事件)
     * @return 无返回值
     */
    void paintEvent(QPaintEvent *event) override;

signals:
    // 发送弹窗告警消息
    void signalMessageInfoText(QString,QString,MessageType);

private:
    QPixmap m_pixmap; // 存储图片
};

#endif // IMAGEWIDGET_H
