#ifndef MODELITEMWDIGET_H
#define MODELITEMWDIGET_H

#include <QWidget>
#include "definestruct.h"
#include "IUiPluginBase.h"

namespace Ui {
class ModelItemWdiget;
}

class ModelItemWdiget : public QWidget
{
    Q_OBJECT

public:
    explicit ModelItemWdiget(QWidget *parent = 0);
    ~ModelItemWdiget();

    /**
     * @brief initUi
     * 插件界面初始化
     * @param void
     * @return 无返回值
     */
    void initUi(IUiPluginBase *plugin);

    /**
     * @brief loadQssStyle
     * 加载Qss样式
     * @param void
     * @return 无返回值
     */
    void loadQssStyle();

    /**
     * @brief setModelItemListInfo
     * 设置模型算法子项详情信息
     * @param dData（详情信息内容）
     * @return 无返回值
     */
    void setModelItemListInfo(ModelData dData);

    /**
     * @brief getModelData
     * 获取Item对象中存储的数据
     * @param void
     * @return ModelData(模型算法子项详情属性)
     */
    ModelData getModelData();

protected slots:
    /**
     * @brief slotStartUseModel
     * 点击启用模型
     * @param void
     * @return void
     */
    void slotStartUseModel();

signals:
    void signalUseModel(ModelData);

private:
    Ui::ModelItemWdiget *ui;
    IUiPluginBase *m_pPlugin = nullptr;
    ModelData m_structModelData; //
};

#endif // MODELITEMWDIGET_H
