#ifndef NETWORKITEMWIDGET_H
#define NETWORKITEMWIDGET_H

#include <QWidget>
#include "IUiPluginBase.h"
#include "definestruct.h"
#include "Comm/httpworker.h"

namespace Ui {
class NetWorkItemWidget;
}

class NetWorkItemWidget : public QWidget
{
    Q_OBJECT

public:
    explicit NetWorkItemWidget(QWidget *parent = 0);
    ~NetWorkItemWidget();

    /**
     * @brief initUi 插件数据初始化交互
     * @param plugin 基类界面
     * @param num 界面编号
     * @return 无返回值
     */
    void initUi(IUiPluginBase *plugin, int num);

    /**
     * @brief initCfg
     * 读取配置文件获取发送时的基础UI
     * @param void
     * @return 无返回值
     */
    void initCfg();

    /**
     * @brief initData
     * 插件数据初始化交互
     * @param void
     * @return 无返回值
     */
    void initData();

    /**
     * @brief loadQssStyle
     * 设置界面Qss初始化样式表
     * @param status 0离线 1在线
     * @return void
     */
    void loadQssStyle(int status);

    /**
     * @brief updateStatu 更新网口在线离线状态
     * @param netdata 状态数据
     */
    void updateStatu(NetSetData netdata);

signals:
    /**
     * @brief signalSendNetsetData 下发网络设置参数
     * @param netdata 网络设置参数
     */
    void signalSendNetsetData(NetSetData netdata);

protected slots:
    /**
     * @brief slotNetSet_pushButton
     * 点击“下发”按钮，下发网络设置信息
     * @param void
     * @return void
     */
    void slotNetSet_pushButton(bool);

private:
    Ui::NetWorkItemWidget *ui;
    IUiPluginBase *m_pPlugin = nullptr;//基类界面

};

#endif // NETWORKITEMWIDGET_H
