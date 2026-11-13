#ifndef MODELCONTENTWIDGET_H
#define MODELCONTENTWIDGET_H

#include <QWidget>
#include "IUiPluginBase.h"
#include "definestruct.h"
#include "ModelItemWdiget.h"

namespace Ui {
class ModelContentWidget;
}

class ModelContentWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ModelContentWidget(QWidget *parent = 0);
    ~ModelContentWidget();

    /**
     * @brief initUi
     * 插件初始化界面
     * @param plugin（插件指针）
     * @return 无返回值
     */
    void initUi(IUiPluginBase *plugin);

    /**
     * @brief loadQssStyle
     * 设置组件样式
     * @param void
     * @return 无返回值
     */
    void loadQssStyle();

    /**
     * @brief addModelUi
     * 添加模型Item界面
     * @param strData
     * @return 无返回值
     */
    void addModelUi(QString strData);

    /**
     * @brief updateModelItemData
     * 刷新模型Item界面列表
     * @param strData
     * @return 无返回值
     */
    void updateModelItemData(ModelData dData);

    /**
     * @brief updateModelItemUI
     * 刷新模型列表布局
     * @param void
     * @return 无返回值
     */
    void updateModelItemUI();

protected slots:
    /**
     * @brief slotUpFile_pushButton
     * 点击添加选择使用的模型
     * @param void
     * @return 无返回值
     */
    void slotUpFile_pushButton();

    /**
     * @brief slotUseModel
     * 点击使用模型
     * @param dData(模型信息)
     * @return 无返回值
     */
    void slotUseModel(ModelData dData);

private:
    Ui::ModelContentWidget *ui;
    IUiPluginBase *m_pPlugin = nullptr;
    int m_currentModelCnt = 0; //当前已经添加的模型数量
    QVector<ModelData> m_vModelIndoData; //用于存储当前添加的所有模型信息
    QList<ModelItemWdiget*> m_listModelItem; //用于存储所有模型界面指针
    int m_nScrollOffset = 10; //模型界面距边框的距离
};

#endif // MODELCONTENTWIDGET_H
