#ifndef PULLBUTTONUIWIDGET_H
#define PULLBUTTONUIWIDGET_H

#include <QWidget>
#include "IUiPluginBase.h"


namespace Ui {
class PullButtonUiWidget;
}

class PullButtonUiWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PullButtonUiWidget(QWidget *parent = 0);
    ~PullButtonUiWidget();

    /**
     * @brief initUi
     * 界面初始化函数。
     * @param pPlugin 界面框架指针
     * @return 无返回值
     */
    void initUi(IUiPluginBase *pPlugin);

    /**
     * @brief initData
     * 插件数据初始化函数。
     * @param void
     * @return 无返回值
     */
    void initData();

    /**
     * @brief resetQss
     * 切肤函数。
     * @param void
     * @return 无返回值
     */
    virtual void resetQss(const QString& strStyleName);

    /**
     * @brief registerProtocols
     * 虚函数的具体实现，初始化插件信号注册函数。
     * @param void
     * @return 无返回值
     */
    virtual void registerProtocols();

    /**
     * @brief loadQssStyleFile
     * 设置主设置界面Qss样式。
     * @param void
     * @return 无返回值
     */
    void loadQssStyleFile();

protected slots:
    /**
     * @brief slotShowAndHide_Btn
     * 点击目标列表显隐按钮触发的槽函数。
     * @param void
     * @return 无返回值
     */
    void slotShowAndHide_Btn();

signals:
    void signalShowTable(bool bStatus);

private:
    Ui::PullButtonUiWidget *ui;
    IUiPluginBase *m_pPlugin = nullptr; //界面插件指针
    bool m_bHide = true; // 默认目标列表为隐藏的
};

#endif // PULLBUTTONUIWIDGET_H
