/**
 * @brief 单行设备展示：设备编号、设备名称、设备 IP；点击切换选中（由列表容器保证互斥）。
 */

#ifndef DEVICELISTITEMWIDGET_H
#define DEVICELISTITEMWIDGET_H

#include <QWidget>

namespace Ui {
class DeviceListItemWidget;
}

class DeviceListItemWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DeviceListItemWidget(QWidget *parent = 0);
    ~DeviceListItemWidget();

    QString serialNumber() const { return m_serialNumber; }
    QString deviceId() const { return m_deviceId; }
    QString deviceName() const { return m_deviceName; }
    QString deviceIp() const { return m_deviceIp; }

    /**
     * @brief applyDeviceFields 设置展示字段；若与当前展示一致则不改 UI。
     * @return 是否有任一展示字段发生变化（不含选中态）。
     */
    bool applyDeviceFields(const QString &serialNumber,
                           const QString &deviceId,
                           const QString &deviceName,
                           const QString &deviceIp);

    void setItemSelected(bool selected);

signals:
    void signalSelectRequested(const QString &serialNumber, const QString &deviceIp);

protected:
    void mousePressEvent(QMouseEvent *event) Q_DECL_OVERRIDE;
    void resizeEvent(QResizeEvent *event) Q_DECL_OVERRIDE;

private:
    Ui::DeviceListItemWidget *ui;

    QString m_serialNumber;
    QString m_deviceId;
    QString m_deviceName;
    QString m_deviceIp;
    bool m_selected = false;

    void refreshSelectionStyle();
    void updateDeviceNameDisplay();
};

#endif // DEVICELISTITEMWIDGET_H
