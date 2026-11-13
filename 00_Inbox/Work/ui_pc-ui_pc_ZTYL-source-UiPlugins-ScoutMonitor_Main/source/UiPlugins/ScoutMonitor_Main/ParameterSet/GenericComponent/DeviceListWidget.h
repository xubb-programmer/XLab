/**
 * @file DeviceListWidget.h
 * @brief 组播设备列表浮窗：展示设备信息、互斥选中、支持按设备编号定位。
 *
 * Windows 下 Qt::Tool 浮窗不会随主窗口最小化而自动隐藏；监听 exe 主界面
 * （MianBoardWidget）窗口状态，最小化时 hide、恢复时按先前可见性 show，
 * 并置位 m_exeMinThenMax 供绘制刷新。
 */

#ifndef DEVICELISTWIDGET_H
#define DEVICELISTWIDGET_H

#include <QWidget>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QTimer>
#include "Comm/CustomMoveWidget.h"

class QShowEvent;
class QHideEvent;

namespace Ui {
class DeviceListWidget;
}

class DeviceListItemWidget;

/**
 * @brief 组播设备列表：按序列号唯一一行；选中互斥；支持按设备编号选中；大数据量时批量刷新、哈希索引。
 */
class DeviceListWidget : public CustomMoveWidget
{
    Q_OBJECT

public:

    /** 单条设备组播快照（与 ParaContentWidget 解析字段一致） */
    struct DeviceRowSnapshot
    {
        QString serialNumber;  ///< 序列号（列表唯一键）
        QString deviceId;      ///< 设备编号
        QString deviceName;    ///< 设备名称
        QString deviceIp;      ///< 设备 IP
    };

    explicit DeviceListWidget(QWidget *parent = 0);
    ~DeviceListWidget();

    /**
     * @brief applyMulticastSnapshots 合并一批组播设备信息。
     *        已存在的序列号仅在设备编号/名称/IP 变化时刷新界面；合并后按序列号升序排列；超过 50 条时批量关闭重绘以提速。
     */
    void applyMulticastSnapshots(const QVector<DeviceRowSnapshot> &batch);

    /** @brief 清空列表全部设备项 */
    void clearAllDevices();

    /** @brief 按序列号移除单台设备列表项 */
    void removeDeviceBySerial(const QString &serialNumber);

    /**
     * @brief setSelectedByDeviceId 按设备编号选中对应项（多匹配时选第一个）；找不到则清除选中。
     * @param emitIp 为 true 时发出 signalDeviceIpSelected（用户点击列表时为 true；外部仅同步高亮时为 false）。
     */
    void setSelectedByDeviceId(const QString &deviceId, bool emitIp = true);

    /** @brief 当前选中项的序列号 */
    QString selectedSerialNumber() const { return m_selectedSerial; }

    /** @brief 当前选中项的设备 IP */
    QString selectedDeviceIp() const;

    /** @brief 当前上方设备列表中的实际设备行数 */
    int deviceCount() const { return m_itemBySerial.size(); }

    /** @brief 返回用于排查列表差异的设备行摘要 */
    QStringList diagnosticRows() const;

    /**
     * @brief exeMinThenMax 主程序是否经历「先最小化再恢复」。
     *        为 true 时表示可触发界面刷新；处理完毕后应调用 clearExeMinThenMax() 清零。
     */
    bool exeMinThenMax() const { return m_exeMinThenMax; }

    /** @brief 清除「先最小再最大」标志 */
    void clearExeMinThenMax() { m_exeMinThenMax = false; }

public slots:
    /** @brief 清空列表并启动“无设备”提示定时器 */
    void slotClearClicked();

signals:
    /** @brief 选中项变化时发出当前设备 IP（无选中时不发） */
    void signalDeviceIpSelected(const QString &deviceIp);

private slots:
    /** @brief 设备行点击回调：更新互斥选中并上报设备 IP */
    void slotItemSelectRequested(const QString &serialNumber, const QString &deviceIp);

    /** @brief 延迟绑定 exe 主窗口（MianBoardWidget）并安装 eventFilter */
    void slotBindExeMainWindow();

    /** @brief 应用激活状态变化时更新最小化/恢复标志（Windows 备用路径） */
    void slotExeAppStateChanged(Qt::ApplicationState state);

protected:
    /**
     * @brief 监听 exe 主窗口 WindowStateChange / Show，维护最小化恢复标志。
     *        浮窗可见时同时作为 qApp 过滤器：点击列表与入口按钮之外则隐藏。
     */
    bool eventFilter(QObject *watched, QEvent *event);
    void showEvent(QShowEvent *event);
    void hideEvent(QHideEvent *event);

private:
    /** @brief 按序列号删除列表行，并同步清理选中态与设备编号索引 */
    void removeItemBySerial(const QString &serialNumber);

    /** @brief 取消当前选中行的高亮显示 */
    void clearSelectionVisual();

    /**
     * @brief 按序列号设置选中项（互斥），并可选发出 signalDeviceIpSelected。
     * @param emitIp 为 true 时向外部上报当前选中设备 IP。
     */
    void setSelectedSerial(const QString &serialNumber, bool emitIp);

    /** @brief 保证设备编号唯一：若编号已被其他序列号占用，则删除旧行 */
    void reconcileDeviceIdUnique(const QString &deviceId, const QString &serialNumber);

    /** @brief 按序列号升序重排列表行（spacer 保持在末尾） */
    void reorderListBySerialNumber();

    /** @brief 主窗口进入最小化：记录可见性并隐藏浮窗 */
    void onExeMainWindowMinimized();

    /** @brief 主窗口从最小化恢复：按先前可见性 show 并刷新绘制 */
    void onExeMainWindowRestored();

    /**
     * @brief 点击落在列表外、且不是主界面设备入口按钮时，应隐藏浮窗。
     *        入口按钮留给 clicked 切换显示，避免按下时先藏、松开又打开。
     */
    bool shouldDismissForGlobalPos(const QPoint &globalPos) const;

private:
    Ui::DeviceListWidget *ui;
    QHash<QString, DeviceListItemWidget *> m_itemBySerial;   ///< 序列号 -> 列表行控件
    QHash<QString, QString> m_serialByDeviceId;              ///< 设备编号 -> 序列号
    QString m_selectedSerial;                                ///< 当前选中序列号
    QTimer *m_pTimer = nullptr;                              ///< 无设备时显示提示标签的定时器

    QWidget *m_exeMainWidget;           ///< exe 主界面（MianBoardWidget）指针，用于监听最小化/恢复
    bool m_exeWasMinimized;             ///< 主程序是否已进入最小化
    bool m_exeMinThenMax;               ///< 是否已发生「先最小化再恢复」
    bool m_wasVisibleBeforeMinimize;    ///< 最小化前浮窗是否处于显示状态
    bool m_appEventFilterInstalled;     ///< 是否已在 qApp 上安装点击外部隐藏的过滤器



};

#endif // DEVICELISTWIDGET_H
