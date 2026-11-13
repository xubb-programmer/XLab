#ifndef PARACONTENTWIDGET_H
#define PARACONTENTWIDGET_H

#include <QWidget>

namespace Ui {
class ParaContentWidget;
}

class ParaContentWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ParaContentWidget(QWidget *parent = 0);
    ~ParaContentWidget();

private:
    Ui::ParaContentWidget *ui;
};

#endif // PARACONTENTWIDGET_H
