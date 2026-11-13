#ifndef VIDEOSETWIDGET_H
#define VIDEOSETWIDGET_H

#include <QWidget>
#include "IUiPluginBase.h"
#include "definestruct.h"
#include <QTimer>
#include <QHash>

class QComboBox;
class QLabel;
class QModelIndex;

namespace Global {
   extern QStringList g_ZLMpushStreamList;
   void initialize();
}

/** 推流下拉框的“展示名称 / 真实地址”适配。 */
namespace StreamUi {
    struct StreamItem {
        QString url;
        QString displayName;

        StreamItem() = default;
        StreamItem(const QString &u, const QString &d = QString())
            : url(u.trimmed()), displayName(d.trimmed().isEmpty() ? u.trimmed() : d.trimmed()) {}

        bool operator==(const StreamItem &other) const {
            return url == other.url && displayName == other.displayName;
        }
        bool operator!=(const StreamItem &other) const {
            return !(*this == other);
        }
        bool operator<(const StreamItem &other) const {
            if (url != other.url)
                return url < other.url;
            return displayName < other.displayName;
        }
    };
    typedef QList<StreamItem> StreamItemList;

    extern QHash<QString, QString> g_displayNameByUrl;

    /** 在首个 QToolTip 创建前设置提示字体，避免 Qt 5.10 首次按默认字体计算出过小宽度。 */
    void initializeToolTipFont();
    /** 按下拉框可见文字区域在末尾显示省略号，不修改原始数据。 */
    QString elidedComboText(const QComboBox *combo, const QString &text);
    /** 让弹出的下拉列表同样使用末尾省略号。 */
    void configureComboTextElision(QComboBox *combo);
    QString displayName(const QString &url);
    QString comboDisplayName(const QComboBox *combo, const QString &url);
    void addStreamItem(QComboBox *combo, const QString &url, const QString &customDisplayName = QString());
    void addStreamItem(QComboBox *combo, const StreamItem &item);
    int findStreamUrl(const QComboBox *combo, const QString &url);
    void setCurrentStreamUrl(QComboBox *combo, const QString &url);
    /** 显示当前地址，但当地址不在真实流列表中时不向下拉模型追加候选项。 */
    void setCurrentUnlistedStreamUrl(QComboBox *combo, const QString &url);
    QString currentStreamUrl(const QComboBox *combo);
    /** 同步收起状态的悬浮提示；有别名时只提示别名，不提示真实地址。 */
    void updateCurrentStreamToolTip(QComboBox *combo);
    bool isValidStreamUrl(const QString &url, QString *errorText = nullptr);
    /** 可编辑下拉框的内部输入框是否正在被用户编辑。 */
    bool isComboEditing(const QComboBox *combo);
    /** 进入编辑：无别名时把省略号还原为完整流地址；有别名时仍显示别名。 */
    void restoreFullEditText(QComboBox *combo);
    /** 离开编辑：恢复末尾省略号显示。 */
    void restoreElidedDisplay(QComboBox *combo);
    /** 给可编辑流地址下拉框安装“编辑显示全文、失焦恢复省略”行为。 */
    void bindEditableStreamCombo(QComboBox *combo);
    /** 给下拉列表绑定列表外侧的不透明自定义 Tooltip。 */
    void bindExternalStreamToolTip(QComboBox *combo);
}

namespace Ui {
class VideoSetWidget;
}

class VideoSetWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VideoSetWidget(QWidget *parent = 0);
    ~VideoSetWidget();

    /**
     * @brief initData
     * 插件数据初始化交互
     * @param void
     * @return 无返回值
     */
    void initUi(IUiPluginBase *plugin);

    /**
     * @brief getVideoSetData
     * 获取Video设置界面对应的存储信息
     * @param void
     * @return VideoSetData
     */
    VideoSetData getVideoSetData();

    /** 当前窗口号（不根据 UI 重算其它字段，供互斥逻辑等避免 getVideoSetData 副作用） */
    int smallScreenNum() const { return m_structData.SmallScreenNum; }

    /** 拉流开关 HTTP 请求进行中（按钮文案为「下发中」） */
    bool isStreamTogglePending() const;

    /**
     * @brief loadQssStyle
     * 设置界面Qss初始化样式表
     * @param void
     * @return void
     */
    void loadQssStyle();

    /**
     * @brief setVideoSetListInfo
     * 设置界面显示详情信息，同时也将详情储存到本地
     * @param dData(接收的正在拉流详情信息)
     * @param strListData(读取本地的下拉列表数据)
     * @return void
     */
    void setVideoSetListInfo(VideoSetData dData, const StreamUi::StreamItemList &strListData);

    /**
     * @brief setVideoComboxInfo
     * 设置界面combox显示详情信息，同时也将详情储存到本地
     * @param dData(视频数据)
     * @param strListData(详情信息)
     * @return void
     */
    void setVideoComboxInfo(VideoSetData dData, const StreamUi::StreamItemList &strListData);


    /**
     * @brief setDelVideoSetShowAndHide
     * 是否将“删除”按钮显示
     * @param bFlag(true:显示 false:不显示)
     * @return void
     */
    void setDelVideoSetShowAndHide(bool bFlag);

    /**
     * @brief setButtonChecked
     * 设置按钮是否为Checked状态样式
     * @param bFlag(true:Checked false:UnChecked)
     * @return void
     */
    void setButtonChecked(bool bFlag);

    /**
     * @brief setSwitchBigScreenpushButtonChecked
     * 设置按钮是否为Checked状态样式
     * @param bFlag(true:Checked false:UnChecked)
     * @return void
     */
    void setSwitchBigScreenpushButtonChecked(bool bFlag);

    /**
     * @brief notifySwitchBigScreenHttpFinished 大屏切换 HTTP 已返回（成功/失败/异常体），恢复可再次点击。
     */
    void notifySwitchBigScreenHttpFinished();

    /**
     * @brief notifyIdentifyHttpFinished 识别开关拉流 HTTP 已返回（与 kPullStreamReqIdentify 对应），恢复可再次点击。
     */
    void notifyIdentifyHttpFinished();

    /**
     * @brief notifyStreamToggleHttpFinished 拉流开关 HTTP 已返回，恢复可再次点击。
     */
    void notifyStreamToggleHttpFinished();

private:
    /** deferUiUpdate=false：同步刷新；true：刷新完成后再 showPopup 响应本次点击 */
    void schedulePushStreamComboRefresh(bool deferUiUpdate);
    /** 在下拉框外侧显示不透明的自定义流名称提示，避免原生 QToolTip 覆盖列表。 */
    void showStreamItemToolTip(const QModelIndex &index);
    void hideStreamItemToolTip();

protected slots:
    /**
     * @brief slotVideoSet_pushButton
     * 点击“下发”按钮所执行的槽函数,用于下发拉流配置参数
     * @param bool
     * @return void
     */
    void slotVideoSet_pushButton(bool bFlag);

    /**
     * @brief slotDeleteItem_pushButton
     * 点击“删除”按钮所执行的槽函数,用于删除该Item界面
     * @param void
     * @return void
     */
    void slotDeleteItem_pushButton();

    /**
     * @brief slotIdentify_pushButton
     * 点击“识别”按钮所执行的槽函数,用于发送识别指令，同时互斥其他识别操作
     * @param bFlag(true:开 false:关)
     * @return void
     */
    void slotIdentify_pushButton(bool bFlag);

    /**
     * @brief slotVideoSet_comboBox combox事件
     */
    void slotVideoSet_comboBox(QString);

    /**
     * @brief slotUpdateList 更新下拉列表
     * @param forceUpdate true 时允许在点击下拉箭头后刷新；列表内容未变化时不重建 model
     */
    void slotUpdateList(const StreamUi::StreamItemList &strListData, bool forceUpdate = false);

    /**
     * @brief slotUpdateReadListData 更新读取本地的下拉列表数据
     */
    void slotUpdateReadListData(const StreamUi::StreamItemList &strListData);

    /**
     * @brief slotSwitchBigScreen_pushButton 大屏切换
     */
    void slotSwitchBigScreen_pushButton(bool);

//    /**
//     * @brief slotSendVideoSetTimeout 下发超时
//     */
//    void slotSendVideoSetTimeout();

    /**
     * @brief slotDeferredComboRefresh eventFilter 路径延迟刷新下拉并弹出
     */
    void slotDeferredComboRefresh();

    /** model 刷新完成后的下一轮事件循环再打开弹窗，避免 Windows 下拉视图残影。 */
    void slotShowComboPopupAfterRefresh();


signals:
    /**
     * @brief signalSendVideoSetInfo 下发拉流配置参数信号
     */
    void signalSendVideoSetInfo(VideoSetData,bool);

    /**
     * @brief signalDeleteItemVideoSetItem 删除
     */
    void signalDeleteItemVideoSetItem(VideoSetData);
    /**
     * @brief signalIdentifyVideoSetItem 识别
     */
    void signalIdentifyVideoSetItem(VideoSetData,bool);
    /**
     * @brief signalMessageBoxText 弹窗
     */
    void signalMessageBoxText(QString,QString,MessageType);
    /**
     * @brief signalupdateVideoSetDate 更新信息
     */
    void signalupdateVideoSetDate(VideoSetData);

    /**
     * @brief signalSwitchBigScreen 发送大屏切换信号
     */
    void signalSwitchBigScreen(VideoSetData,bool);

    /**
     * @brief signalRefreshpushStreamList 刷新推流列表信息
     */
    void signalRefreshpushStreamList();

protected:
    bool eventFilter(QObject *watched, QEvent *event);
    void hideEvent(QHideEvent *event) override;

private:
    Ui::VideoSetWidget *ui;
    IUiPluginBase *m_pPlugin = nullptr;
    VideoSetData m_structData;
    QString m_VideoIp;
    StreamUi::StreamItemList m_currentListData{};//当前下拉列表数据
    StreamUi::StreamItemList m_readListData{};//读取本地的下拉列表数据
    /** 大屏切换请求已发出、尚未收到 HTTP 完成回调时为 true，用于忽略重复点击 */
    bool m_switchBigScreenAwaitingReply = false;
    /** 识别开关请求已发出、尚未收到拉流设置 HTTP 回调时为 true，用于忽略重复点击 */
    bool m_identifyAwaitingReply = false;
    /** 拉流开关请求已发出、尚未收到 HTTP 完成回调时为 true，用于忽略重复点击 */
    bool m_streamToggleAwaitingReply = false;
    /** eventFilter 路径下是否已排队一次延迟刷新 */
    bool m_comboRefreshQueued = false;
    /** 延迟刷新执行期间又点击了下拉，需再跑一轮刷新+弹出 */
    bool m_comboRefreshPendingAgain = false;
    /** 下拉箭头点击计数，快速连点时仅最后一次刷新完成后弹出 */
    quint64 m_comboDropClickGeneration = 0;
    quint64 m_deferredRefreshForClickGen = 0;
    QLabel *m_streamItemToolTip = nullptr;
    int m_streamItemToolTipRow = -1;
    QTimer m_streamItemToolTipHideTimer;
    bool m_shuttingDown = false;
};

Q_DECLARE_METATYPE(StreamUi::StreamItem)
Q_DECLARE_METATYPE(StreamUi::StreamItemList)

#endif // VIDEOSETWIDGET_H
