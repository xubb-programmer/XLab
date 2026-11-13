/**
 * @file DeviceListWidget.cpp
 * @brief 组播设备列表浮窗实现。
 *
 * 主要功能：
 * 1. 接收组播快照增量刷新设备行；
 * 2. 设备行互斥选中并上报 IP；
 * 3. 监听 exe 主界面最小化/恢复：最小化时 hide，恢复时按先前状态 show 并刷新绘制。
 */

#include "DeviceListWidget.h"
#include "ui_DeviceListWidget.h"
#include "DeviceListItemWidget.h"
#include "FrameLogManager.h"
#include <QVBoxLayout>
#include <QLayoutItem>
#include <QSizePolicy>
#include <QApplication>
#include <QWindowStateChangeEvent>
#include <QMouseEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <QDebug>
#include <algorithm>

DeviceListWidget::DeviceListWidget(QWidget *parent) :
    CustomMoveWidget(parent),
    ui(new Ui::DeviceListWidget),
    m_exeMainWidget(0),
    m_exeWasMinimized(false),
    m_exeMinThenMax(false),
    m_wasVisibleBeforeMinimize(false),
    m_appEventFilterInstalled(false)
{
    ui->setupUi(this);

    setTitle(ui->DeviceListWidget_background);
    Qt::WindowFlags flags = Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool;
#if defined(Q_OS_LINUX)
    if (QGuiApplication::platformName() == QLatin1String("xcb"))
        flags |= Qt::X11BypassWindowManagerHint;
#endif
    setWindowFlags(flags);
    setAttribute(Qt::WA_TranslucentBackground, true);

    // 插件加载顺序下主窗口可能尚未创建，延迟绑定 MianBoardWidget
    QTimer::singleShot(0, this, SLOT(slotBindExeMainWindow()));

    // Windows 下 WindowStateChange 有时不进 eventFilter，用应用状态变化作备用
    connect(qApp, SIGNAL(applicationStateChanged(Qt::ApplicationState)),
            this, SLOT(slotExeAppStateChanged(Qt::ApplicationState)));

    connect(ui->pushButton_clear, SIGNAL(clicked()), this, SLOT(slotClearClicked()));
    connect(ui->pushButton_close, SIGNAL(clicked()), this, SLOT(hide()));

    ui->scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    ui->scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    // 无组播数据时，定时显示“无设备”提示
    m_pTimer = new QTimer(this);
    m_pTimer->setInterval(3000);
    connect(m_pTimer, &QTimer::timeout, this, [=]() {
        if(ui->label_nodevice->isHidden()){
            ui->label_name->show();
            ui->label_nodevice->show();
        }
    });
}

DeviceListWidget::~DeviceListWidget()
{
    if (m_appEventFilterInstalled) {
        qApp->removeEventFilter(this);
        m_appEventFilterInstalled = false;
    }
    if (m_exeMainWidget) {
        m_exeMainWidget->removeEventFilter(this);
    }
    clearAllDevices();
    delete ui;
}

QString DeviceListWidget::selectedDeviceIp() const
{
    DeviceListItemWidget *w = m_itemBySerial.value(m_selectedSerial);
    return w ? w->deviceIp() : QString();
}

QStringList DeviceListWidget::diagnosticRows() const
{
    QStringList serials = m_itemBySerial.keys();
    std::sort(serials.begin(), serials.end(), [](const QString &a, const QString &b) {
        return QString::compare(a, b, Qt::CaseInsensitive) < 0;
    });

    QStringList rows;
    rows.reserve(serials.size());
    for (const QString &serial : serials) {
        const DeviceListItemWidget *item = m_itemBySerial.value(serial);
        if (!item)
            continue;
        rows.append(QStringLiteral("SN=%1,ID=%2,Name=%3,IP=%4")
                    .arg(serial, item->deviceId(), item->deviceName(), item->deviceIp()));
    }
    return rows;
}

/** @brief 按序列号升序重排列表行（spacer 保持在末尾） */
void DeviceListWidget::reorderListBySerialNumber()
{
    QVBoxLayout *lay = ui->verticalLayout_list;
    if (!lay || m_itemBySerial.isEmpty()) {
        return;
    }

    QStringList serials = m_itemBySerial.keys();
    std::sort(serials.begin(), serials.end(), [](const QString &a, const QString &b) {
        return QString::compare(a, b, Qt::CaseInsensitive) < 0;
    });

    for (int i = 0; i < serials.size(); ++i) {
        DeviceListItemWidget *item = m_itemBySerial.value(serials.at(i));
        if (!item) {
            continue;
        }
        lay->removeWidget(item);
        lay->insertWidget(i, item);
    }
}

/**
 * @brief 合并一批组播设备快照到列表（按序列号去重，设备编号冲突时移除旧行；合并后按序列号升序显示）。
 */
void DeviceListWidget::applyMulticastSnapshots(const QVector<DeviceRowSnapshot> &batch)
{
    if (batch.isEmpty()) {
        return;
    }

    QVBoxLayout *lay = ui->verticalLayout_list;
    if (!lay) {
        return;
    }

    QWidget *container = ui->scrollAreaWidgetContents;

    const bool bulk = batch.size() > 50;
    if (bulk) {
        container->setUpdatesEnabled(false);
    }

    m_pTimer->stop();
    ui->label_name->hide();
    ui->label_nodevice->hide();

    const int beforeCount = m_itemBySerial.size();
    int skippedCount = 0;
    for (const DeviceRowSnapshot &snap : batch) {
        if (snap.serialNumber.isEmpty() || snap.deviceIp.isEmpty()) {
            ++skippedCount;
            continue;
        }

        reconcileDeviceIdUnique(snap.deviceId, snap.serialNumber);

        DeviceListItemWidget *item = m_itemBySerial.value(snap.serialNumber);
        if (!item) {
            item = new DeviceListItemWidget(container);
            connect(item, SIGNAL(signalSelectRequested(QString,QString)),
                    this, SLOT(slotItemSelectRequested(QString,QString)));

            // 插入到底部 spacer 之前，保证自上而下排列
            const int idx = lay->count() > 0 ? lay->count() - 1 : 0;
            lay->insertWidget(idx, item);
            m_itemBySerial.insert(snap.serialNumber, item);
            item->applyDeviceFields(snap.serialNumber, snap.deviceId, snap.deviceName, snap.deviceIp);
            if (!snap.deviceId.isEmpty()) {
                m_serialByDeviceId.insert(snap.deviceId, snap.serialNumber);
            }
        } else {
            const QString oldDeviceId = item->deviceId();
            item->applyDeviceFields(snap.serialNumber, snap.deviceId, snap.deviceName, snap.deviceIp);
            if (oldDeviceId != snap.deviceId) {
                if (m_serialByDeviceId.value(oldDeviceId) == snap.serialNumber) {
                    m_serialByDeviceId.remove(oldDeviceId);
                }
            }
            if (!snap.deviceId.isEmpty()) {
                m_serialByDeviceId.insert(snap.deviceId, snap.serialNumber);
            }
        }

        if (m_selectedSerial == snap.serialNumber) {
            item->setItemSelected(true);
        }
    }

    reorderListBySerialNumber();

    if (bulk) {
        container->setUpdatesEnabled(true);
    }

    if (!m_itemBySerial.isEmpty() && ui->verticalSpacer) {
        ui->verticalSpacer->changeSize(20, 0, QSizePolicy::Minimum, QSizePolicy::Fixed);
        lay->invalidate();
    }

    if (beforeCount != m_itemBySerial.size() || skippedCount > 0) {
        FrameLogManager::getInstance()->log(
            QStringLiteral("ParameterSetWidget"),
            QString::fromLocal8Bit("[设备列表诊断] 上方列表应用组播快照: batch=%1, before=%2, after=%3, skipped=%4, rows=[%5]")
                .arg(batch.size()).arg(beforeCount).arg(m_itemBySerial.size()).arg(skippedCount)
                .arg(diagnosticRows().join(QStringLiteral("; "))),
            IFrameLogManager::DEBUG_TYPE);
    }
}

/** @brief 同一设备编号只保留最新序列号对应行 */
void DeviceListWidget::reconcileDeviceIdUnique(const QString &deviceId, const QString &serialNumber)
{
    if (deviceId.isEmpty()) {
        return;
    }
    const QString existingSerial = m_serialByDeviceId.value(deviceId);
    if (!existingSerial.isEmpty() && existingSerial != serialNumber) {
        removeItemBySerial(existingSerial);
    }
}

void DeviceListWidget::removeItemBySerial(const QString &serialNumber)
{
    DeviceListItemWidget *w = m_itemBySerial.take(serialNumber);
    if (!w) {
        return;
    }

    if (m_selectedSerial == serialNumber) {
        m_selectedSerial.clear();
    }

    QStringList idsToDrop;
    QHash<QString, QString>::iterator it = m_serialByDeviceId.begin();
    while (it != m_serialByDeviceId.end()) {
        if (it.value() == serialNumber) {
            idsToDrop.append(it.key());
            ++it;
        } else {
            ++it;
        }
    }
    foreach (const QString &id, idsToDrop) {
        m_serialByDeviceId.remove(id);
    }

    QVBoxLayout *lay = ui->verticalLayout_list;
    lay->removeWidget(w);
    delete w;

    if (m_itemBySerial.isEmpty() && ui->verticalSpacer) {
        ui->verticalSpacer->changeSize(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);
        lay->invalidate();
    }
}

void DeviceListWidget::removeDeviceBySerial(const QString &serialNumber)
{
    if (serialNumber.isEmpty())
        return;
    removeItemBySerial(serialNumber);
}

void DeviceListWidget::clearAllDevices()
{
    clearSelectionVisual();
    m_selectedSerial.clear();
    m_serialByDeviceId.clear();

    QVBoxLayout *lay = ui->verticalLayout_list;
    if (!lay) {
        m_itemBySerial.clear();
        return;
    }

    ui->scrollAreaWidgetContents->setUpdatesEnabled(false);
    const QList<DeviceListItemWidget *> toDelete = m_itemBySerial.values();
    m_itemBySerial.clear();
    foreach (DeviceListItemWidget *w, toDelete) {
        lay->removeWidget(w);
        delete w;
    }
    ui->scrollAreaWidgetContents->setUpdatesEnabled(true);

    if (ui->verticalSpacer) {
        ui->verticalSpacer->changeSize(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);
        lay->invalidate();
    }
}

void DeviceListWidget::setSelectedByDeviceId(const QString &deviceId, bool emitIp)
{
    const QString key = deviceId.trimmed();
    if (key.isEmpty()) {
        clearSelectionVisual();
        m_selectedSerial.clear();
        return;
    }

    const QString serial = m_serialByDeviceId.value(key);
    if (serial.isEmpty()) {
        clearSelectionVisual();
        m_selectedSerial.clear();
        return;
    }

    setSelectedSerial(serial, emitIp);
}

void DeviceListWidget::slotClearClicked()
{
    FrameLogManager::getInstance()->log(
        QStringLiteral("ParameterSetWidget"),
        QString::fromLocal8Bit("[设备列表诊断] 上方列表执行清空: before=%1, rows=[%2]")
            .arg(m_itemBySerial.size())
            .arg(diagnosticRows().join(QStringLiteral("; "))),
        IFrameLogManager::DEBUG_TYPE);
    clearAllDevices();
    m_pTimer->start();
}

/** @brief 响应 DeviceListItemWidget::signalSelectRequested，切换选中并发出 IP */
void DeviceListWidget::slotItemSelectRequested(const QString &serialNumber, const QString &deviceIp)
{
    Q_UNUSED(deviceIp);
    setSelectedSerial(serialNumber, true);
}

void DeviceListWidget::clearSelectionVisual()
{
    DeviceListItemWidget *prev = m_itemBySerial.value(m_selectedSerial);
    if (prev) {
        prev->setItemSelected(false);
    }
}

void DeviceListWidget::setSelectedSerial(const QString &serialNumber, bool emitIp)
{
    if (m_selectedSerial == serialNumber) {
        if (emitIp) {
            DeviceListItemWidget *w = m_itemBySerial.value(serialNumber);
            if (w) {
                emit signalDeviceIpSelected(w->deviceIp());
            }
        }
        return;
    }

    clearSelectionVisual();
    m_selectedSerial = serialNumber;

    DeviceListItemWidget *w = m_itemBySerial.value(serialNumber);
    if (w) {
        w->setItemSelected(true);
        if (emitIp) {
            emit signalDeviceIpSelected(w->deviceIp());
        }
    }
}

/**
 * @brief 查找 exe 主界面 MianBoardWidget，并在其上安装 eventFilter。
 */
void DeviceListWidget::slotBindExeMainWindow()
{
    if (m_exeMainWidget) {
        return;
    }

    const QWidgetList tops = QApplication::topLevelWidgets();
    for (int i = 0; i < tops.size(); ++i) {
        QWidget *w = tops.at(i);
        if (w->inherits("MianBoardWidget")) {
            m_exeMainWidget = w;
            m_exeMainWidget->installEventFilter(this);
            break;
        }
    }
}

/**
 * @brief 主窗口进入最小化：记录当前可见性并隐藏浮窗。
 */
void DeviceListWidget::onExeMainWindowMinimized()
{
    if (m_exeWasMinimized) {
        return;
    }
    m_wasVisibleBeforeMinimize = this->isVisible();
    if (m_wasVisibleBeforeMinimize) {
        this->hide();
    }
    m_exeWasMinimized = true;
    m_exeMinThenMax = false;
}

/**
 * @brief 主窗口从最小化恢复：恢复先前可见性并 hide/show 刷新透明 Tool 窗口绘制。
 */
void DeviceListWidget::onExeMainWindowRestored()
{
    if (!m_exeWasMinimized) {
        return;
    }
    m_exeMinThenMax = true;
    m_exeWasMinimized = false;
    if (m_wasVisibleBeforeMinimize) {
        this->show();
        this->raise();
        this->hide();
        this->show();
        m_wasVisibleBeforeMinimize = false;
    }
}

/**
 * @brief （方式一）根据主窗口 windowState 与应用程序激活状态，隐藏/恢复浮窗（Windows 备用路径）。
 */
void DeviceListWidget::slotExeAppStateChanged(Qt::ApplicationState state)
{
    if (!m_exeMainWidget) {
        slotBindExeMainWindow();
    }
    if (!m_exeMainWidget) {
        return;
    }

    if (m_exeMainWidget->windowState() & Qt::WindowMinimized) {
        onExeMainWindowMinimized();
        return;
    }
    if (m_exeWasMinimized && state == Qt::ApplicationActive) {
        onExeMainWindowRestored();
    }
}

/**
 * @brief 显示时安装应用级过滤器，以便捕获点在浮窗之外的鼠标按下。
 */
void DeviceListWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!m_appEventFilterInstalled) {
        qApp->installEventFilter(this);
        m_appEventFilterInstalled = true;
    }
}

/**
 * @brief 隐藏时拆除应用级过滤器。
 */
void DeviceListWidget::hideEvent(QHideEvent *event)
{
    if (m_appEventFilterInstalled) {
        qApp->removeEventFilter(this);
        m_appEventFilterInstalled = false;
    }
    QWidget::hideEvent(event);
}

bool DeviceListWidget::shouldDismissForGlobalPos(const QPoint &globalPos) const
{
    if (frameGeometry().contains(globalPos)) {
        return false;
    }

    for (QWidget *w = QApplication::widgetAt(globalPos); w; w = w->parentWidget()) {
        if (w == this) {
            return false;
        }
        if (w->objectName() == QLatin1String("pushButton_device")) {
            return false;
        }
    }
    return true;
}

/**
 * @brief （方式二）主窗口 eventFilter：捕获最小化与恢复；可见时捕获点击外部以隐藏浮窗。
 */
bool DeviceListWidget::eventFilter(QObject *watched, QEvent *event)
{
    if(!this->isVisible())
            return CustomMoveWidget::eventFilter(watched, event);

    if (event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::NonClientAreaMouseButtonPress) {
        const QMouseEvent *me = static_cast<const QMouseEvent *>(event);
        if (shouldDismissForGlobalPos(me->globalPos())) {
            QTimer::singleShot(0, this, SLOT(hide()));
        }
    }

    if (m_exeMainWidget && watched == m_exeMainWidget) {
        if (event->type() == QEvent::WindowStateChange) {
            QWindowStateChangeEvent *e = static_cast<QWindowStateChangeEvent *>(event);
            const Qt::WindowStates oldState = e->oldState();
            const Qt::WindowStates newState = m_exeMainWidget->windowState();
            if (!(oldState & Qt::WindowMinimized) && (newState & Qt::WindowMinimized)) {
                onExeMainWindowMinimized();
            } else if (m_exeWasMinimized
                       && (oldState & Qt::WindowMinimized)
                       && !(newState & Qt::WindowMinimized)) {
                onExeMainWindowRestored();
            }
        } else if (event->type() == QEvent::Show && m_exeWasMinimized) {
            // Windows 备用：恢复时可能先收到 Show
            onExeMainWindowRestored();
        }
        return false;
    }

    return false;
}
