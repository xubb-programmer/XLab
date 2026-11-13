#include "DeviceListItemWidget.h"
#include "ui_DeviceListItemWidget.h"
#include <QFontMetrics>
#include <QMouseEvent>
#include <QResizeEvent>

DeviceListItemWidget::DeviceListItemWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::DeviceListItemWidget)
{
    ui->setupUi(this);
    setCursor(Qt::PointingHandCursor);
    refreshSelectionStyle();
}

DeviceListItemWidget::~DeviceListItemWidget()
{
    delete ui;
}

bool DeviceListItemWidget::applyDeviceFields(const QString &serialNumber,
                                             const QString &deviceId,
                                             const QString &deviceName,
                                             const QString &deviceIp)
{
    const bool changed = (serialNumber != m_serialNumber)
            || (deviceId != m_deviceId)
            || (deviceName != m_deviceName)
            || (deviceIp != m_deviceIp);

    m_serialNumber = serialNumber;
    m_deviceId = deviceId;
    m_deviceName = deviceName;
    m_deviceIp = deviceIp;

    if (changed) {
        ui->label_deviceId->setText(deviceId);
        updateDeviceNameDisplay();
        ui->label_deviceName->setToolTip(deviceName);
        ui->label_deviceIp->setText(deviceIp);
    }
    return changed;
}

void DeviceListItemWidget::setItemSelected(bool selected)
{
    if (m_selected == selected) {
        return;
    }
    m_selected = selected;
    refreshSelectionStyle();
}

void DeviceListItemWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !m_serialNumber.isEmpty()) {
        emit signalSelectRequested(m_serialNumber, m_deviceIp);
    }
    QWidget::mousePressEvent(event);
}

void DeviceListItemWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateDeviceNameDisplay();
}

void DeviceListItemWidget::updateDeviceNameDisplay()
{
    if (!ui || !ui->label_deviceName)
        return;

    const int availableWidth = qMax(0, ui->label_deviceName->width() - 2);
    ui->label_deviceName->setText(
                ui->label_deviceName->fontMetrics().elidedText(
                    m_deviceName, Qt::ElideRight, availableWidth));
}

void DeviceListItemWidget::refreshSelectionStyle()
{
    if (m_selected) {
       ui->DeviceListItemWidget_background->setStyleSheet(QStringLiteral(
                          "QWidget#DeviceListItemWidget_background { background-color: #1F3D5B; border-radius: 4px;border:1px solid #30E0A1;;}"
                          ));
    } else {
       ui->DeviceListItemWidget_background-> setStyleSheet(QStringLiteral(
                          "QWidget#DeviceListItemWidget_background { background-color: #1F3D5B; border-radius: 4px;border:0px solid #30E0A1;; }"
                          ));
    }
    ui->label_icon->setVisible(m_selected);
}
