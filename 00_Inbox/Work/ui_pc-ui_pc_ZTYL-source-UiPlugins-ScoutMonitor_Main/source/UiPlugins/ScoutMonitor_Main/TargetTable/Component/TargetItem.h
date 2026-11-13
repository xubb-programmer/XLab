#ifndef TARGETITEM_H
#define TARGETITEM_H

#include <QWidget>
#include "IUiPluginBase.h"
#include "definestruct.h"
#include "coordinate.h"

namespace Ui {
class TargetItem;
}

class QValidator;

class TargetItem : public QWidget
{
    Q_OBJECT

public:
    explicit TargetItem(QWidget *parent = 0);
    ~TargetItem();

    /**
     * @brief initUi
     * 界面初始化函数。
     * @param pPlugin 界面框架指针
     * @return 无返回值
     */
    void initUi(IUiPluginBase *plugin);

    /**
     * @brief setTargetListInfo
     * 将目标信息显示到界面。
     * @param TargetInfo(目标信息)
     * @return 无返回值
     */
    void setTargetListInfo(TargetInfo dData);

    /**
     * @brief getTargetNumber
     * 获取TargetItem对象上的目标编号。
     * @param void
     * @return （QString）目标编号信息
     */
    QString getTargetNumber();

    /**
     * @brief setModifyChecked Item编辑状态互斥
     * @param checked
     */
    void setModifyChecked(bool checked);

    /**
     * @brief loadQssStyleFile
     * 给予UI界面控件设置Qss图片样式。
     * @param void
     * @return 无返回值
     */
    void loadQssStyle();

    /**
     * @brief CoordinateTransfor 高斯坐标与经度、纬度转换
     * @param statue 0：高斯坐标转经度、纬度 1：经度、纬度转高斯坐标
     */
    void CoordinateTransfor(int statue);

    /**
     * @brief updateTargetInfo 更新目标信息
     */
    void updateTargetInfo();

    /** 根据当前上报车型刷新目标编号输入限制。 */
    void updateTargetNumberValidator();

    /** 校验当前车型下的目标编号是否可上报。 */
    bool isTargetNumberValid(const QString &targetNumber) const;

public slots:
    /**
     * @brief slotModify_pushButton
     * 点击目标编辑按钮开关，设置QLineEdit处于是否可编辑状态。
     * @param bCheck（true:QLineEdit设置为禁用状态 false:QLineEdit解除禁用状态）
     * @return 无返回值
     */
    void slotModify_pushButton(bool bCheck);

    /**
     * @brief slotReport_pushButton
     * 点击目标上报按钮，将目标信息上报到车长。
     * @param void
     * @return 无返回值
     */
    void slotReport_pushButton();

    /**
     * @brief slotNo_value_EditingFinished
     * 目标编号编辑完成后触发的执行槽函数。
     * @param void
     * @return 无返回值
     */
    void slotNo_value_EditingFinished();


    /**
     * @brief slotlon_value_EditingFinished
     * 目标经度编辑完成后触发的执行槽函数。
     * @param void
     * @return 无返回值
     */
    void slotlon_value_EditingFinished(QString);

    /**
     * @brief slotlat_value_EditingFinished
     * 目标纬度编辑完成后触发的执行槽函数。
     * @param void
     * @return 无返回值
     */
    void slotlat_value_EditingFinished(QString);

    /**
     * @brief slotX_value_EditingFinished
     * 目标X编辑完成后触发的执行槽函数。
     * @param void
     * @return 无返回值
     */
    void slotX_value_EditingFinished(QString);

    /**
     * @brief slotY_value_EditingFinished
     * 目标Y编辑完成后触发的执行槽函数。
     * @param void
     * @return 无返回值
     */
    void slotY_value_EditingFinished(QString);


    /**
     * @brief slotEle_value_EditingFinished
     * 目标高程编辑完成后触发的执行槽函数。
     * @param void
     * @return 无返回值
     */
    void slotEle_value_EditingFinished(QString);

    /**
     * @brief slotTime_value_EditingFinished
     * 目标发现时刻编辑完成后触发的执行槽函数。
     * @param void
     * @return 无返回值
     */
    void slotTime_value_EditingFinished();

    /**
     * @brief slotDelete_pushButton
     * 点击删除按钮执行的槽函数。
     * @param void
     * @return 无返回值
     */
    void slotDelete_pushButton();

signals:
    void signalReport_pushButton();
    void signalModifyTargetInfo(TargetInfo);
    void signalTargetReport(TargetInfo);
    void signalDeleteInfoTarget(QString);
    void signalModifyNoInfo(QString,TargetInfo); // 修改目标编号信息(par1：历史编号信息, par2：新目标信息)
    void signalModifyChecked(TargetItem *item, bool checked);
    void signalMessageInfoText(QString,QString,MessageType);

private:
    Ui::TargetItem *ui;
    IUiPluginBase *m_pPlugin = nullptr;
    TargetInfo m_structTargetInfo;
    QString m_strHistoryNumber = 0; // 记录修改前的目标编号
    QString tempType="";//类型名称
    QValidator *m_targetNumber002Validator = nullptr;
    QValidator *m_targetNumberDefaultValidator = nullptr;
};

#endif // TARGETITEM_H
