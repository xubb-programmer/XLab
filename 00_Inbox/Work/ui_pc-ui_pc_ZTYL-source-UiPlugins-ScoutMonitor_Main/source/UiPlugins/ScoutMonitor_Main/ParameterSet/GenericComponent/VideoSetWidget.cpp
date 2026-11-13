#include "VideoSetWidget.h"
#include "ui_VideoSetWidget.h"
#include <QFile>
#include <QLineEdit>
#include <QComboBox>
#include "FrameLogManager.h"
#include <QSet>
#include <QMouseEvent>
#include <QMetaObject>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QAbstractItemView>
#include <QDesktopWidget>
#include <QLabel>
#include <QValidator>
#include <QRegularExpressionValidator>
#include <algorithm>
#include <QDateTime>
#include <QUrl>
#include <QApplication>
#include <QFont>
#include <QFontMetrics>
#include <QEvent>
#include <QSignalBlocker>
#include <QTimerEvent>
#include <QHideEvent>

QStringList Global::g_ZLMpushStreamList;
QHash<QString, QString> StreamUi::g_displayNameByUrl;

void StreamUi::initializeToolTipFont()
{
    static bool initialized = false;
    if (initialized)
        return;

    // Qt 5.10 首次创建 QTipLabel 时会先按 QApplication 字体计算 sizeHint，
    // 随后才应用父控件 QSS。当前 QSS 将提示字体设为 18px，若不预设字体，
    // 第一次提示框仍保留较小字体计算出的宽度，文本会被裁剪；复用后才恢复。
    QFont tipFont = QApplication::font("QTipLabel");
    tipFont.setPixelSize(18);
    QApplication::setFont(tipFont, "QTipLabel");
    initialized = true;
}

QString StreamUi::displayName(const QString &url)
{
    const QString actual = url.trimmed();
    return g_displayNameByUrl.value(actual, actual);
}

QString StreamUi::elidedComboText(const QComboBox *combo, const QString &text)
{
    if (!combo)
        return text;

    // 为右侧下拉按钮和文字内边距预留空间。三个目标下拉框均为固定宽度，
    // 因此这里不会改变控件尺寸，只处理最终显示字符串。
    const int textWidth = qMax(20, combo->width() - 48);
    return combo->fontMetrics().elidedText(text, Qt::ElideRight, textWidth);
}

void StreamUi::configureComboTextElision(QComboBox *combo)
{
    if (!combo || !combo->view())
        return;

    QAbstractItemView *view = combo->view();
    view->setTextElideMode(Qt::ElideRight);
    view->setMouseTracking(true);
    view->setAttribute(Qt::WA_AlwaysShowToolTips, false);
    if (view->viewport()) {
        view->viewport()->setMouseTracking(true);
        view->viewport()->setAttribute(Qt::WA_AlwaysShowToolTips, false);
    }
}

QString StreamUi::comboDisplayName(const QComboBox *combo, const QString &url)
{
    return elidedComboText(combo, displayName(url));
}

static QString streamAddressToolTip(const QString &url)
{
    const QString actual = url.trimmed();
    if (actual.isEmpty())
        return QString();
    const QString friendly = StreamUi::displayName(actual).trimmed();
    if (!friendly.isEmpty() && friendly != actual)
        return friendly;
    return actual;
}

static void markComboEditing(QComboBox *combo, bool on)
{
    if (combo)
        combo->setProperty("streamComboEditing", on);
}

static bool comboEditingProperty(const QComboBox *combo)
{
    return combo && combo->property("streamComboEditing").toBool();
}

static QString comboItemUrl(const QComboBox *combo, int index)
{
    if (!combo || index < 0)
        return QString();
    QString actual = combo->itemData(index, Qt::UserRole).toString().trimmed();
    if (actual.isEmpty())
        actual = combo->itemText(index).trimmed();
    return actual;
}

static bool currentHasAlias(const QComboBox *combo)
{
    if (!combo)
        return false;
    const QString actual = StreamUi::currentStreamUrl(combo).trimmed();
    if (actual.isEmpty())
        return false;
    const int index = combo->currentIndex();
    if (index >= 0 && comboItemUrl(combo, index) == actual) {
        const QString itemText = combo->itemText(index).trimmed();
        return !itemText.isEmpty() && itemText != actual;
    }
    const QString friendly = StreamUi::displayName(actual).trimmed();
    return !friendly.isEmpty() && friendly != actual;
}

void StreamUi::addStreamItem(QComboBox *combo, const QString &url, const QString &customDisplayName)
{
    if (!combo)
        return;
    const QString actual = url.trimmed();
    if (actual.isEmpty())
        return;
    const QString fullDisplayName = !customDisplayName.trimmed().isEmpty()
            ? customDisplayName.trimmed()
            : displayName(actual);
    // 列表项保存未省略全文；收起态的省略号只写到内部 QLineEdit，
    // 避免一点击编辑 Qt 又把带 “...” 的 itemText 拷回输入框。
    combo->addItem(fullDisplayName, actual);
}

void StreamUi::addStreamItem(QComboBox *combo, const StreamItem &item)
{
    addStreamItem(combo, item.url, item.displayName);
}

int StreamUi::findStreamUrl(const QComboBox *combo, const QString &url)
{
    if (!combo)
        return -1;
    const QString actual = url.trimmed();
    for (int i = 0; i < combo->count(); ++i) {
        QString itemUrl = combo->itemData(i, Qt::UserRole).toString().trimmed();
        if (itemUrl.isEmpty())
            itemUrl = combo->itemText(i).trimmed();
        if (itemUrl == actual)
            return i;
    }
    return -1;
}

namespace {

class StreamComboExternalToolTipFilter : public QObject
{
public:
    explicit StreamComboExternalToolTipFilter(QComboBox *combo)
        : QObject(combo), m_combo(combo)
    {
    }

    ~StreamComboExternalToolTipFilter() override
    {
        delete m_tip;
        m_tip = nullptr;
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (!m_combo || !m_combo->view() || !event
                || watched != m_combo->view()->viewport()) {
            return QObject::eventFilter(watched, event);
        }

        if (event->type() == QEvent::ToolTip)
            return true;

        if (event->type() == QEvent::MouseMove) {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
            const QModelIndex index = m_combo->view()->indexAt(mouseEvent->pos());
            if (!index.isValid()) {
                hideTip();
            } else if (index.row() != m_row || !m_tip || !m_tip->isVisible()) {
                showTip(index);
            }
        } else if (event->type() == QEvent::Leave
                   || event->type() == QEvent::Hide
                   || event->type() == QEvent::MouseButtonPress
                   || event->type() == QEvent::Wheel) {
            hideTip();
        }

        return QObject::eventFilter(watched, event);
    }

private:
    void showTip(const QModelIndex &index)
    {
        const QString tipText = streamAddressToolTip(
                    comboItemUrl(m_combo, index.row())).trimmed();
        if (tipText.isEmpty()) {
            hideTip();
            return;
        }

        if (!m_tip) {
            m_tip = new QLabel(nullptr, Qt::ToolTip | Qt::FramelessWindowHint);
            m_tip->setObjectName(QStringLiteral("StreamComboExternalToolTip"));
            m_tip->setTextFormat(Qt::PlainText);
            m_tip->setWordWrap(false);
            m_tip->setAttribute(Qt::WA_ShowWithoutActivating, true);
            m_tip->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            m_tip->setAttribute(Qt::WA_TranslucentBackground, false);
            m_tip->setAutoFillBackground(true);
            m_tip->setWindowOpacity(1.0);
            m_tip->setStyleSheet(QStringLiteral(
                "QLabel#StreamComboExternalToolTip{"
                "background-color:#0e1319;color:#ffffff;"
                "border:1px solid #1b99f5;border-radius:2px;"
                "padding:4px 8px;font:18px \"阿里巴巴普惠体\";}"));
        }

        m_tip->setText(tipText);
        m_tip->adjustSize();

        QAbstractItemView *view = m_combo->view();
        QWidget *viewport = view ? view->viewport() : nullptr;
        if (!viewport) {
            hideTip();
            return;
        }

        const QRect itemRect = view->visualRect(index);
        const QPoint popupTopLeft = viewport->mapToGlobal(QPoint(0, 0));
        const QRect popupRect(popupTopLeft, viewport->size());
        QPoint pos(popupRect.right() + 8,
                   viewport->mapToGlobal(itemRect.topLeft()).y());
        const QRect available = QApplication::desktop()->availableGeometry(viewport);
        if (pos.x() + m_tip->width() > available.right())
            pos.setX(popupRect.left() - m_tip->width() - 8);
        if (pos.x() < available.left())
            pos.setX(available.left());
        if (pos.y() + m_tip->height() > available.bottom())
            pos.setY(available.bottom() - m_tip->height());
        if (pos.y() < available.top())
            pos.setY(available.top());

        m_tip->move(pos);
        m_tip->show();
        m_tip->raise();
        m_row = index.row();
    }

    void hideTip()
    {
        m_row = -1;
        if (m_tip)
            m_tip->hide();
    }

    QComboBox *m_combo = nullptr;
    QLabel *m_tip = nullptr;
    int m_row = -1;
};

} // namespace

void StreamUi::bindExternalStreamToolTip(QComboBox *combo)
{
    if (!combo || !combo->view() || !combo->view()->viewport()
            || combo->property("externalStreamToolTipBound").toBool()) {
        return;
    }

    combo->setProperty("externalStreamToolTipBound", true);
    configureComboTextElision(combo);
    auto *filter = new StreamComboExternalToolTipFilter(combo);
    combo->view()->viewport()->installEventFilter(filter);
}

static QString ellipsisPrefix(const QString &text)
{
    QString prefix = text.trimmed();
    prefix.remove(QChar(0x2026));
    prefix.remove(QLatin1String("..."));
    return prefix.trimmed();
}

static bool comboTextExactMatchesItem(const QComboBox *combo, int index, const QString &text)
{
    if (!combo || index < 0 || text.isEmpty())
        return false;
    const QString actual = comboItemUrl(combo, index);
    const QString itemText = combo->itemText(index).trimmed();
    const QString friendly = StreamUi::displayName(actual).trimmed();
    const QString elidedFriendly = StreamUi::elidedComboText(combo, friendly).trimmed();
    const QString elidedActual = StreamUi::elidedComboText(combo, actual).trimmed();
    return text == itemText || text == friendly || text == actual
            || text == elidedFriendly || text == elidedActual;
}

static bool comboTextPrefixMatchesItem(const QComboBox *combo, int index, const QString &text)
{
    if (!combo || index < 0 || text.isEmpty())
        return false;
    const QString prefix = ellipsisPrefix(text);
    if (prefix.isEmpty() || prefix == text)
        return false;
    // 过短前缀（例如 “rtsp://192.”）会命中多条流，不能用来反查当前播放源。
    if (prefix.size() < 16)
        return false;
    const QString actual = comboItemUrl(combo, index);
    const QString friendly = StreamUi::displayName(actual).trimmed();
    const int minLen = qMin(prefix.size(), qMax(16, qMin(actual.size(), friendly.size()) / 2));
    if (prefix.size() < minLen)
        return false;
    return friendly.startsWith(prefix) || actual.startsWith(prefix);
}

void StreamUi::setCurrentStreamUrl(QComboBox *combo, const QString &url)
{
    if (!combo)
        return;
    const QString actual = url.trimmed();
    combo->setProperty("streamUnlistedCurrentUrl", QVariant());
    const int index = findStreamUrl(combo, actual);
    if (index >= 0) {
        combo->setCurrentIndex(index);
        if (currentHasAlias(combo) || !isComboEditing(combo))
            restoreElidedDisplay(combo);
        else
            updateCurrentStreamToolTip(combo);
        return;
    }

    // 当前窗口可能引用被顶部设备筛选排除的另一台设备流，手动输入的新地址也
    // 可能尚未出现在候选列表中。此时都补一条仅用于表示当前值的选项，并始终
    // 在 UserRole 保存完整地址。否则收起态写入 QLineEdit 的省略文本（如
    // "rtsp://192.168.10.130:5…"）会在停止投屏时被误当成真实 URL 下发。
    const QString friendly = displayName(actual);
    if (!actual.isEmpty()) {
        combo->addItem(friendly, actual);
        combo->setCurrentIndex(combo->count() - 1);
    } else if (combo->isEditable()) {
        combo->setEditText(actual);
    } else {
        combo->setCurrentText(actual);
    }
    if (!isComboEditing(combo) || currentHasAlias(combo))
        restoreElidedDisplay(combo);
    else
        updateCurrentStreamToolTip(combo);
}

void StreamUi::setCurrentUnlistedStreamUrl(QComboBox *combo, const QString &url)
{
    if (!combo)
        return;

    const QString actual = url.trimmed();
    if (actual.isEmpty() || findStreamUrl(combo, actual) >= 0) {
        setCurrentStreamUrl(combo, actual);
        return;
    }

    // 可编辑 QComboBox 允许当前显示值不属于下拉模型。单独保存
    // 真实 URL，输入框仅显示别名；这样未上报的强制切流地址不会伪装成可用流。
    const QString shown = elidedComboText(combo, displayName(actual));
    {
        // setCurrentIndex(-1) 会先发出一次空文本信号，可编辑 QComboBox 还可能
        // 根据中文别名自动匹配到列表中的 RTSP 项。将整个当前值更新作为
        // 一个原子操作，避免中间态触发协议按钮重算。
        const QSignalBlocker comboBlocker(combo);
        const QSignalBlocker editBlocker(combo->lineEdit());
        combo->setProperty("streamUnlistedCurrentUrl", actual);
        combo->setCurrentIndex(-1);
        if (combo->isEditable())
            combo->setEditText(shown);
        else
            combo->setCurrentText(shown);
    }
    updateCurrentStreamToolTip(combo);
}

QString StreamUi::currentStreamUrl(const QComboBox *combo)
{
    if (!combo)
        return QString();

    const QString text = combo->currentText().trimmed();
    const int index = combo->currentIndex();
    const QString unlisted = combo->property("streamUnlistedCurrentUrl").toString().trimmed();
    if (!unlisted.isEmpty()) {
        const QString friendly = displayName(unlisted).trimmed();
        const QString elidedFriendly = elidedComboText(combo, friendly).trimmed();
        // Qt 可能因显示别名相同而自动给可编辑下拉框分配一个列表索引。
        // 只要显示文本仍对应这个单独保存的当前值，就必须优先返回它，
        // 不能因 currentIndex 被改成同名 RTSP 项。
        if (text == unlisted || text == friendly || text == elidedFriendly)
            return unlisted;
    }
    // 可编辑下拉框收起态是省略文本；别名表更新后 itemText 可能已是别名，
    // 输入框仍是省略后的 URL。先精确匹配，再谨慎做前缀匹配，避免回退成第一项。
    if (index >= 0 && comboTextExactMatchesItem(combo, index, text)) {
        const QString actual = comboItemUrl(combo, index);
        if (!actual.isEmpty())
            return actual;
    }
    for (int i = 0; i < combo->count(); ++i) {
        if (!comboTextExactMatchesItem(combo, i, text))
            continue;
        const QString actual = comboItemUrl(combo, i);
        if (!actual.isEmpty())
            return actual;
    }
    if (index >= 0 && comboTextPrefixMatchesItem(combo, index, text)) {
        const QString actual = comboItemUrl(combo, index);
        if (!actual.isEmpty())
            return actual;
    }
    for (int i = 0; i < combo->count(); ++i) {
        if (!comboTextPrefixMatchesItem(combo, i, text))
            continue;
        const QString actual = comboItemUrl(combo, i);
        if (!actual.isEmpty())
            return actual;
    }
    return text;
}

void StreamUi::updateCurrentStreamToolTip(QComboBox *combo)
{
    if (!combo)
        return;

    const QString placeholder = QString::fromUtf8(u8"请选择或输入拉流地址!");
    const QString actual = currentStreamUrl(combo).trimmed();
    QString tip;
    if (!actual.isEmpty() && actual != placeholder)
        tip = streamAddressToolTip(actual);

    const int index = combo->currentIndex();
    if (tip.isEmpty() && index >= 0) {
        tip = combo->itemData(index, Qt::ToolTipRole).toString().trimmed();
        if (tip.isEmpty())
            tip = displayName(combo->itemData(index, Qt::UserRole).toString()).trimmed();
    }

    combo->setAttribute(Qt::WA_AlwaysShowToolTips, true);
    combo->setToolTip(tip);
    if (combo->lineEdit()) {
        combo->lineEdit()->setAttribute(Qt::WA_AlwaysShowToolTips, true);
        combo->lineEdit()->setToolTip(tip);
    }
}

bool StreamUi::isValidStreamUrl(const QString &url, QString *errorText)
{
    const QString value = url.trimmed();
    auto fail = [errorText](const QString &text) {
        if (errorText)
            *errorText = text;
        return false;
    };

    if (value.isEmpty())
        return fail(QString::fromUtf8(u8"流地址不能为空！"));
    if (value.contains(QRegularExpression(QStringLiteral("\\s"))))
        return fail(QString::fromUtf8(u8"流地址不能包含空白字符！"));

    const QUrl parsed(value, QUrl::StrictMode);
    if (!parsed.isValid() || parsed.scheme().isEmpty() || parsed.host().isEmpty())
        return fail(QString::fromUtf8(u8"请输入完整的网络流地址，例如 rtsp://192.168.0.10:554/live/stream！"));

    const QString scheme = parsed.scheme().toLower();
    const QStringList supportedSchemes = {
        QStringLiteral("rtsp"), QStringLiteral("rtsps"),
        QStringLiteral("rtmp"), QStringLiteral("rtmps"),
        QStringLiteral("http"), QStringLiteral("https"),
        QStringLiteral("udp"), QStringLiteral("tcp"), QStringLiteral("srt")
    };
    if (!supportedSchemes.contains(scheme))
        return fail(QString::fromUtf8(u8"不支持该流地址协议！"));

    if (parsed.path().isEmpty() && scheme != QStringLiteral("udp")
            && scheme != QStringLiteral("tcp") && scheme != QStringLiteral("srt")) {
        return fail(QString::fromUtf8(u8"流地址缺少流路径！"));
    }

    if (errorText)
        errorText->clear();
    return true;
}

bool StreamUi::isComboEditing(const QComboBox *combo)
{
    if (!combo || !combo->isEnabled() || !combo->isEditable() || !combo->lineEdit())
        return false;
    if (combo->lineEdit()->isReadOnly())
        return false;
    // 只认明确进入编辑（点击/按键）。仅获得焦点不能算编辑，
    // 否则一打开设置页就会挡住后续别名刷新。
    return comboEditingProperty(combo);
}

static void applyClosedElidedText(QComboBox *combo, bool force = false)
{
    if (!combo || !combo->isEditable() || !combo->lineEdit())
        return;
    if (!force && StreamUi::isComboEditing(combo))
        return;

    const QString placeholder = QString::fromUtf8(u8"请选择或输入拉流地址!");
    const QString actual = StreamUi::currentStreamUrl(combo).trimmed();
    if (actual.isEmpty() || actual == placeholder)
        return;

    // 无效/尚未输入完成的文本保持原样，避免失焦后变成无法继续修正的省略串。
    if (!StreamUi::isValidStreamUrl(actual))
        return;

    // 手工输入的地址不在候选模型中。收起态写入省略文本之前必须单独保存
    // 完整 URL，否则确认按钮只能读到带省略号的展示文本，最终导致拉流失败。
    if (StreamUi::findStreamUrl(combo, actual) < 0)
        combo->setProperty("streamUnlistedCurrentUrl", actual);

    const int index = combo->currentIndex();
    QString targetText;
    if (index >= 0 && comboItemUrl(combo, index) == actual) {
        targetText = combo->itemText(index).trimmed();
    }
    if (targetText.isEmpty()) {
        targetText = StreamUi::displayName(actual);
    }

    const QString elided = StreamUi::elidedComboText(combo, targetText);
    if (combo->lineEdit()->text() == elided)
        return;

    combo->blockSignals(true);
    combo->lineEdit()->blockSignals(true);
    combo->lineEdit()->setText(elided);
    combo->lineEdit()->blockSignals(false);
    combo->blockSignals(false);
}

static void syncStreamComboLock(QComboBox *combo)
{
    if (!combo || !combo->lineEdit())
        return;
    QLineEdit *edit = combo->lineEdit();
    // 收起态可能显示中文别名，URL 校验器会把展示文本判为非法；仅在非编辑时摘掉。
    const bool showingAlias = currentHasAlias(combo) && !StreamUi::isComboEditing(combo);
    if (showingAlias) {
        if (!combo->property("streamUrlValidatorSaved").toBool()) {
            combo->setProperty("streamUrlValidatorSaved", true);
            combo->setProperty("streamUrlValidator",
                               QVariant::fromValue(static_cast<QObject *>(
                                   const_cast<QValidator *>(edit->validator()))));
        }
        edit->setValidator(nullptr);
    } else if (combo->property("streamUrlValidatorSaved").toBool()) {
        QObject *obj = combo->property("streamUrlValidator").value<QObject *>();
        if (auto *validator = qobject_cast<QValidator *>(obj))
            edit->setValidator(validator);
    }
    edit->setReadOnly(false);
    edit->setFocusPolicy(Qt::StrongFocus);
    edit->unsetCursor();
}

static void applyAliasDisplay(QComboBox *combo)
{
    if (!combo || !combo->isEditable() || !combo->lineEdit())
        return;
    markComboEditing(combo, false);
    syncStreamComboLock(combo);
    applyClosedElidedText(combo, true);
    StreamUi::updateCurrentStreamToolTip(combo);
}

void StreamUi::restoreFullEditText(QComboBox *combo)
{
    if (!combo || !combo->isEditable() || !combo->lineEdit())
        return;

    const QString placeholder = QString::fromUtf8(u8"请选择或输入拉流地址!");
    const QString currentText = combo->lineEdit()->text().trimmed();
    if (currentText.isEmpty() || currentText == placeholder)
        return;

    const QString actual = currentStreamUrl(combo).trimmed();
    if (actual.isEmpty() || actual == placeholder)
        return;

    syncStreamComboLock(combo);

    if (combo->lineEdit()->text() == actual)
        return;

    combo->blockSignals(true);
    combo->lineEdit()->blockSignals(true);
    combo->lineEdit()->setText(actual);
    combo->lineEdit()->setCursorPosition(combo->lineEdit()->text().length());
    combo->lineEdit()->blockSignals(false);
    combo->blockSignals(false);
}

void StreamUi::restoreElidedDisplay(QComboBox *combo)
{
    if (!combo || !combo->isEditable())
        return;
    if (combo->lineEdit() && combo->lineEdit()->hasFocus()
            && !combo->lineEdit()->isReadOnly() && StreamUi::isComboEditing(combo))
        return;
    markComboEditing(combo, false);
    syncStreamComboLock(combo);
    applyClosedElidedText(combo, currentHasAlias(combo));
    updateCurrentStreamToolTip(combo);
}

namespace {

bool isComboArrowClick(QComboBox *combo, const QPoint &pos)
{
    if (!combo || !combo->style())
        return false;
    QStyleOptionComboBox opt;
    opt.initFrom(combo);
    QRect r = combo->style()->subControlRect(
                QStyle::CC_ComboBox, &opt, QStyle::SC_ComboBoxArrow, combo);
    if (r.isEmpty()) {
        const int dropW = 37;
        r = QRect(combo->width() - dropW, 0, dropW, combo->height());
    }
    return r.contains(pos);
}

bool textShouldRestoreUrl(QComboBox *combo, const QString &text)
{
    if (!combo || currentHasAlias(combo))
        return false;
    const int index = combo->currentIndex();
    if (index < 0)
        return false;
    QString actual = combo->itemData(index, Qt::UserRole).toString().trimmed();
    if (actual.isEmpty())
        actual = combo->itemText(index).trimmed();
    const QString friendly = StreamUi::displayName(actual);
    const QString elided = StreamUi::elidedComboText(combo, friendly);
    // 无别名时，收起态是省略号，编辑时还原成完整 URL。
    return text == elided || text.contains(QChar(0x2026));
}

class StreamComboEditFilter : public QObject
{
public:
    explicit StreamComboEditFilter(QComboBox *combo)
        : QObject(combo)
        , m_combo(combo)
    {
        if (combo && combo->lineEdit()) {
            QComboBox *boundCombo = combo;
            QObject::connect(combo->lineEdit(), &QLineEdit::textChanged,
                             [boundCombo](const QString &text) {
                if (!boundCombo || !StreamUi::isComboEditing(boundCombo))
                    return;
                if (textShouldRestoreUrl(boundCombo, text))
                    StreamUi::restoreFullEditText(boundCombo);
            });
            QObject::connect(combo,
                             static_cast<void (QComboBox::*)(int)>(&QComboBox::activated),
                             [this, boundCombo](int) {
                m_ignoreFocusInRestore = true;
                markComboEditing(boundCombo, false);
                applyAliasDisplay(boundCombo);
            });
            QObject::connect(combo,
                             static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
                             [boundCombo](int) {
                if (!StreamUi::isComboEditing(boundCombo))
                    applyAliasDisplay(boundCombo);
            });
        }
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (!m_combo || !event)
            return QObject::eventFilter(watched, event);

        QLineEdit *edit = m_combo->lineEdit();
        const bool onEdit = (edit && watched == edit);
        const bool onCombo = (watched == m_combo);
        if (!onEdit && !onCombo)
            return QObject::eventFilter(watched, event);

        const QEvent::Type type = event->type();
        if (type == QEvent::MouseButtonPress || type == QEvent::MouseButtonRelease
                || type == QEvent::MouseButtonDblClick) {
            QMouseEvent *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                QPoint pos = mouse->pos();
                if (onEdit)
                    pos = m_combo->mapFromGlobal(edit->mapToGlobal(mouse->pos()));
                if (!isComboArrowClick(m_combo, pos) && m_combo->isEnabled()) {
                    m_ignoreFocusInRestore = false;
                    if (currentHasAlias(m_combo)) {
                        // 点击别名后仍显示别名。真正发生删除、输入或粘贴时，
                        // textEdited 会清除旧地址缓存，后续文本便按手工地址处理。
                        applyAliasDisplay(m_combo);
                        markComboEditing(m_combo, true);
                    } else {
                        markComboEditing(m_combo, true);
                        StreamUi::restoreFullEditText(m_combo);
                        scheduleRestoreFull();
                    }
                }
            }
        } else if (type == QEvent::KeyPress && onEdit) {
            m_ignoreFocusInRestore = false;
            markComboEditing(m_combo, true);
            // 别名状态下让按键直接作用于当前可见文字，不能在按键前改写为
            // 真实 URL；无别名的省略地址才需要先还原全文。
            if (!currentHasAlias(m_combo))
                StreamUi::restoreFullEditText(m_combo);
        } else if (type == QEvent::FocusIn && onEdit) {
            if (m_focusOutTimerId) {
                killTimer(m_focusOutTimerId);
                m_focusOutTimerId = 0;
            }
            if (m_combo->view() && m_combo->view()->isVisible())
                return QObject::eventFilter(watched, event);
            if (m_ignoreFocusInRestore) {
                m_ignoreFocusInRestore = false;
                applyAliasDisplay(m_combo);
                return QObject::eventFilter(watched, event);
            }
            if (comboEditingProperty(m_combo)) {
                StreamUi::restoreFullEditText(m_combo);
                return QObject::eventFilter(watched, event);
            }
            // 打开设置页时输入框常会自动获得焦点，这里只还原显示，不进入编辑态。
            StreamUi::restoreElidedDisplay(m_combo);
        } else if (type == QEvent::FocusOut && onEdit) {
            if (m_restoreTimerId) {
                killTimer(m_restoreTimerId);
                m_restoreTimerId = 0;
            }
            if (m_restoreRetryTimerId) {
                killTimer(m_restoreRetryTimerId);
                m_restoreRetryTimerId = 0;
            }
            if (m_focusOutTimerId)
                killTimer(m_focusOutTimerId);
            m_focusOutTimerId = startTimer(0);
        }
        return QObject::eventFilter(watched, event);
    }

protected:
    void timerEvent(QTimerEvent *event) override
    {
        if (!event)
            return;
        if (event->timerId() == m_restoreTimerId
                || event->timerId() == m_restoreRetryTimerId) {
            const int id = event->timerId();
            killTimer(id);
            if (id == m_restoreTimerId)
                m_restoreTimerId = 0;
            else
                m_restoreRetryTimerId = 0;
            if (m_combo && StreamUi::isComboEditing(m_combo))
                StreamUi::restoreFullEditText(m_combo);
        } else if (event->timerId() == m_focusOutTimerId) {
            killTimer(m_focusOutTimerId);
            m_focusOutTimerId = 0;
            if (!m_combo || !m_combo->lineEdit())
                return;
            if (m_combo->lineEdit()->hasFocus()
                    && StreamUi::isComboEditing(m_combo))
                return;
            if (m_combo->view() && m_combo->view()->isVisible())
                return;
            StreamUi::restoreElidedDisplay(m_combo);
        }
    }

private:
    void scheduleRestoreFull()
    {
        if (m_restoreTimerId)
            killTimer(m_restoreTimerId);
        if (m_restoreRetryTimerId)
            killTimer(m_restoreRetryTimerId);
        // 0ms：等 QComboBox 把 itemText 写进编辑框后再覆盖。
        // 1ms：个别样式会再延迟一次 setText，补一枪。
        m_restoreTimerId = startTimer(0);
        m_restoreRetryTimerId = startTimer(1);
    }

    QComboBox *m_combo;
    int m_restoreTimerId = 0;
    int m_restoreRetryTimerId = 0;
    int m_focusOutTimerId = 0;
    bool m_ignoreFocusInRestore = false;
};

} // namespace StreamComboEditFilter

void StreamUi::bindEditableStreamCombo(QComboBox *combo)
{
    if (!combo || !combo->lineEdit())
        return;
    if (combo->property("streamComboBound").toBool())
        return;
    combo->setProperty("streamComboBound", true);
    combo->setInsertPolicy(QComboBox::NoInsert);
    combo->setAttribute(Qt::WA_AlwaysShowToolTips, true);
    combo->lineEdit()->setAttribute(Qt::WA_AlwaysShowToolTips, true);
    auto *filter = new StreamComboEditFilter(combo);
    combo->lineEdit()->installEventFilter(filter);
    combo->installEventFilter(filter);
    QObject::connect(combo->lineEdit(), &QLineEdit::textEdited, combo,
                     [combo](const QString &) {
        // 用户开始手工输入后，不再把显示文本解析为之前的未上报地址。
        combo->setProperty("streamUnlistedCurrentUrl", QVariant());
    });
    QObject::connect(combo,
                     static_cast<void (QComboBox::*)(int)>(&QComboBox::activated),
                     combo, [combo](int) {
        combo->setProperty("streamUnlistedCurrentUrl", QVariant());
    });
}

namespace {

void setComboBoxRegExpValidator(QComboBox *combo, QValidator *validator)
{
    if (!combo || !validator)
        return;
#if QT_VERSION >= QT_VERSION_CHECK(5, 12, 0)
    combo->setValidator(validator);
#else
    if (combo->lineEdit())
        combo->lineEdit()->setValidator(validator);
#endif
}

bool comboPopupIsVisible(QComboBox *combo)
{
    if (!combo)
        return false;
    QAbstractItemView *view = combo->view();
    return view && view->isVisible();
}

void closeComboPopupBeforeModelReset(QComboBox *combo)
{
    if (comboPopupIsVisible(combo))
        combo->hidePopup();
}

QRect comboDropDownButtonRect(QComboBox *combo)
{
    if (!combo || !combo->style())
        return QRect();

    QStyleOptionComboBox opt;
    opt.initFrom(combo);
    // Qt5 使用 SC_ComboBoxArrow；无 SC_ComboBoxDropDown
    const QStyle::SubControl dropSubControl = QStyle::SC_ComboBoxArrow;
    QRect r = combo->style()->subControlRect(
        QStyle::CC_ComboBox, &opt, dropSubControl, combo);
    if (r.isEmpty()) {
        const int dropW = 37;
        r = QRect(combo->width() - dropW, 0, dropW, combo->height());
    }
    return r;
}

} // namespace

VideoSetWidget::VideoSetWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::VideoSetWidget)
{
    ui->setupUi(this);
    m_streamItemToolTipHideTimer.setSingleShot(true);
    m_streamItemToolTipHideTimer.setInterval(5000);
    connect(&m_streamItemToolTipHideTimer, &QTimer::timeout,
            this, &VideoSetWidget::hideStreamItemToolTip);
}

VideoSetWidget::~VideoSetWidget()
{
    m_shuttingDown = true;
    hideStreamItemToolTip();
    delete m_streamItemToolTip;
    m_streamItemToolTip = nullptr;
    if (ui && ui->VideoSet_comboBox)
        ui->VideoSet_comboBox->removeEventFilter(this);
    delete ui;
    ui = nullptr;
}

void VideoSetWidget::showStreamItemToolTip(const QModelIndex &index)
{
    if (m_shuttingDown || !ui || !ui->VideoSet_comboBox || !index.isValid()) {
        hideStreamItemToolTip();
        return;
    }

    const QString actual = comboItemUrl(ui->VideoSet_comboBox, index.row());
    const QString itemText = ui->VideoSet_comboBox->itemText(index.row()).trimmed();
    QString tip = itemText;
    if (tip.isEmpty() || tip == actual)
        tip = streamAddressToolTip(actual).trimmed();
    if (tip.isEmpty()) {
        hideStreamItemToolTip();
        return;
    }

    if (!m_streamItemToolTip) {
        m_streamItemToolTip = new QLabel(nullptr, Qt::ToolTip | Qt::FramelessWindowHint);
        m_streamItemToolTip->setObjectName(QStringLiteral("StreamItemToolTip"));
        m_streamItemToolTip->setTextFormat(Qt::PlainText);
        m_streamItemToolTip->setWordWrap(false);
        m_streamItemToolTip->setAttribute(Qt::WA_ShowWithoutActivating, true);
        m_streamItemToolTip->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        m_streamItemToolTip->setAttribute(Qt::WA_TranslucentBackground, false);
        m_streamItemToolTip->setAutoFillBackground(true);
        m_streamItemToolTip->setWindowOpacity(1.0);
        m_streamItemToolTip->setStyleSheet(QStringLiteral(
            "QLabel#StreamItemToolTip{"
            "background-color:#0e1319;color:#ffffff;"
            "border:1px solid #1b99f5;border-radius:2px;"
            "padding:4px 8px;font:18px \"阿里巴巴普惠体\";}"));
    }

    m_streamItemToolTip->setText(tip);
    m_streamItemToolTip->adjustSize();

    QAbstractItemView *view = ui->VideoSet_comboBox->view();
    QWidget *viewport = view ? view->viewport() : nullptr;
    if (!viewport) {
        hideStreamItemToolTip();
        return;
    }

    const QRect itemRect = view->visualRect(index);
    const QPoint popupTopLeft = viewport->mapToGlobal(QPoint(0, 0));
    const QRect popupRect(popupTopLeft, viewport->size());
    QPoint pos(popupRect.right() + 8,
               viewport->mapToGlobal(itemRect.topLeft()).y());

    const QRect available = QApplication::desktop()->availableGeometry(viewport);
    if (pos.x() + m_streamItemToolTip->width() > available.right())
        pos.setX(popupRect.left() - m_streamItemToolTip->width() - 8);
    if (pos.x() < available.left())
        pos.setX(available.left());
    if (pos.y() + m_streamItemToolTip->height() > available.bottom())
        pos.setY(available.bottom() - m_streamItemToolTip->height());
    if (pos.y() < available.top())
        pos.setY(available.top());

    m_streamItemToolTip->move(pos);
    m_streamItemToolTip->show();
    m_streamItemToolTip->raise();
    m_streamItemToolTipRow = index.row();
    // 自定义 tooltip 是无父窗口的顶层 QLabel，不具备 QToolTip 的自动超时；
    // 即使平台没有送达 popup 的 Hide/Leave，也保证最终能够自行收起。
    m_streamItemToolTipHideTimer.start();
}

void VideoSetWidget::hideStreamItemToolTip()
{
    m_streamItemToolTipHideTimer.stop();
    m_streamItemToolTipRow = -1;
    if (m_streamItemToolTip)
        m_streamItemToolTip->hide();
}

/**
 * @brief initData
 * 插件数据初始化交互
 * @param void
 * @return 无返回值
 */
void VideoSetWidget::initUi(IUiPluginBase *plugin)
{
    StreamUi::initializeToolTipFont();
    m_pPlugin = plugin;
    m_pPlugin->setStyle("day");
    connect(ui->VideoSet_pushButton,SIGNAL(clicked(bool)),this,SLOT(slotVideoSet_pushButton(bool)));
    connect(ui->DeleteItem_pushButton,SIGNAL(clicked()),this,SLOT(slotDeleteItem_pushButton()));
    connect(ui->Identify_pushButton,SIGNAL(clicked(bool)),this,SLOT(slotIdentify_pushButton(bool)));
    connect(ui->SwitchBigScreen_pushButton,SIGNAL(clicked(bool)),this,SLOT(slotSwitchBigScreen_pushButton(bool)));
    connect(ui->VideoSet_comboBox,SIGNAL(currentTextChanged(QString)),this,SLOT(slotVideoSet_comboBox(QString)));

    ui->VideoSet_comboBox->installEventFilter(this);

    loadQssStyle();
    // 只设置一次完整、合法的状态样式。旧代码在每次设备切换时反复设置
    // 缺少右花括号的 QSS，Qt 5 的样式缓存会因此出现正常项不绘制、仅 hover 可见。
    ui->VideoSet_comboBox->setStyleSheet(QStringLiteral(
        "QComboBox#VideoSet_comboBox::drop-down{width:37px;height:37px;}"
        "QComboBox#VideoSet_comboBox:disabled::drop-down{width:0px;height:0px;}"));
    ui->VideoSet_comboBox->setEditable(true);
    StreamUi::configureComboTextElision(ui->VideoSet_comboBox);
    if (ui->VideoSet_comboBox->view()) {
        // popup 的 Hide/Close 通常发给 QAbstractItemView，而不是其 viewport；
        // 两者都监听，覆盖点击外部、Esc 和程序化 hidePopup 等关闭路径。
        ui->VideoSet_comboBox->view()->installEventFilter(this);
        if (ui->VideoSet_comboBox->view()->viewport())
            ui->VideoSet_comboBox->view()->viewport()->installEventFilter(this);
    }
    connect(qApp, &QGuiApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState state) {
        if (state != Qt::ApplicationActive)
            hideStreamItemToolTip();
    });
    ui->VideoSet_comboBox->lineEdit()->setAttribute(Qt::WA_InputMethodEnabled, false);
    StreamUi::bindEditableStreamCombo(ui->VideoSet_comboBox);
    ui->SwitchBigScreen_pushButton->setToolTip(QString::fromUtf8(u8"切换至大屏"));

    // 正则表达式：除汉字及中文符号、全角字符外均可输入
    QRegularExpression rx(
        R"([^\x{3000}-\x{303F}\x{3400}-\x{4DBF}\x{4E00}-\x{9FFF}\x{F900}-\x{FAFF}\x{FE30}-\x{FE4F}\x{FF00}-\x{FFEF}]+)");
    QRegularExpressionValidator *validator = new QRegularExpressionValidator(rx, ui->VideoSet_comboBox);
    setComboBoxRegExpValidator(ui->VideoSet_comboBox, validator);

    ui->DeleteItem_pushButton->hide();

}

/**
 * @brief getVideoSetData
 * 获取Video设置界面对应的存储信息interesting
 * @param void
 * @return VideoSetData
 */
bool VideoSetWidget::isStreamTogglePending() const
{
    return m_streamToggleAwaitingReply;
}

VideoSetData VideoSetWidget::getVideoSetData()
{
    m_structData.VideoIp = StreamUi::currentStreamUrl(ui->VideoSet_comboBox);
    m_structData.VideoName=ui->VideoName_label->text().trimmed();
    m_structData.bIsStreamPullOn=ui->VideoSet_pushButton->isChecked();
    m_structData.bisBigScreenShow=ui->SwitchBigScreen_pushButton->isChecked();
    m_structData.bIsIdentifiy=ui->Identify_pushButton->isChecked();

    return m_structData;
}

/**
 * @brief loadQssStyle
 * 设置界面Qss初始化样式表
 * @param void
 * @return void
 */
void VideoSetWidget::loadQssStyle()
{
    QString strPath = m_pPlugin->getStylePath() + QString("/VideoSetWidget.qss");

    Q_ASSERT(!strPath.isEmpty() && "path is empty");
    QFile textFile(strPath);

    Q_ASSERT(textFile.exists() && "file can't open");
    textFile.open(QIODevice::ReadOnly | QIODevice::Text);
    QTextStream textStream(&textFile);

    QString style = textStream.readAll();
    this->setStyleSheet(style);
}

/**
 * @brief setVideoSetListInfo
 * 设置界面显示详情信息，同时也将详情储存到本地
 * @param dData(接收的正在拉流详情信息)
 * @param strListData(读取本地的下拉列表数据)
 * @return void
 */
void VideoSetWidget::setVideoSetListInfo(VideoSetData dData, const StreamUi::StreamItemList &strListData)
{ 
    if(dData.SmallScreenNum!=m_structData.SmallScreenNum)
        return;

    hideStreamItemToolTip();
    closeComboPopupBeforeModelReset(ui->VideoSet_comboBox);
    m_currentListData.clear();
    ui->VideoSet_comboBox->blockSignals(true);
    ui->VideoSet_comboBox->clear();
    for (const StreamUi::StreamItem &item : strListData) {
        StreamUi::addStreamItem(ui->VideoSet_comboBox, item);
    }
    m_currentListData = strListData;

    StreamUi::setCurrentStreamUrl(ui->VideoSet_comboBox, dData.VideoIp);
    ui->VideoSet_comboBox->blockSignals(false);
    ui->VideoSet_comboBox->setDisabled(true);

    ui->windowNum_label->setText(QString::fromUtf8(u8"窗口")+QString::number(dData.SmallScreenNum));
    ui->VideoName_label->setText(dData.VideoName);
    ui->Identify_pushButton->setDisabled(false);
    ui->Identify_pushButton->setChecked(dData.bIsIdentifiy);
    ui->SwitchBigScreen_pushButton->setChecked(dData.bisBigScreenShow);
    ui->SwitchBigScreen_pushButton->setDisabled(false);

    m_VideoIp=dData.VideoIp;
    m_structData= dData;

    ui->VideoSet_pushButton->setChecked(true);
    m_readListData.clear();
    m_readListData=strListData;

    if(dData.connectstatus=="reconnecting"){
        ui->VideoSet_pushButton->setText(QString::fromUtf8(u8"    重连中"));
    }else{
        ui->VideoSet_pushButton->setText(QString::fromUtf8(u8"    结束"));
    }

}

/**
 * @brief setVideoComboxInfo
 * 接收手动添加时，并刷新Combox下拉
 * @param dData(视频数据)
 * @param strListData(详情信息)
 * @return void
 */
void VideoSetWidget::setVideoComboxInfo(VideoSetData dData, const StreamUi::StreamItemList &strListData)
{
    if (StreamUi::isComboEditing(ui->VideoSet_comboBox)) {
        ui->DeleteItem_pushButton->hide();
        ui->VideoName_label->setText(dData.VideoName);
        m_readListData = strListData;
        if (!dData.bIsStreamPullOn) {
            ui->VideoSet_pushButton->setText(QString::fromUtf8(u8"    投屏"));
            ui->VideoSet_pushButton->setChecked(false);
            m_switchBigScreenAwaitingReply = false;
            m_identifyAwaitingReply = false;
            ui->SwitchBigScreen_pushButton->setDisabled(true);
            ui->SwitchBigScreen_pushButton->setChecked(false);
            ui->Identify_pushButton->setDisabled(true);
            ui->Identify_pushButton->setChecked(false);
            ui->windowNum_label->setText(QString::fromUtf8(u8"窗口")+QString::number(dData.SmallScreenNum));
            m_structData.VideoName = dData.VideoName;
            m_structData.SmallScreenNum = dData.SmallScreenNum;
            m_structData.bIsStreamPullOn = false;
            m_structData.bIsIdentifiy = dData.bIsIdentifiy;
            m_structData.bisBigScreenShow = dData.bisBigScreenShow;
            m_structData.VideoIp = StreamUi::currentStreamUrl(ui->VideoSet_comboBox);
            m_VideoIp = m_structData.VideoIp;
        }
        return;
    }

    ui->DeleteItem_pushButton->hide();
    ui->VideoName_label->setText(dData.VideoName);
//    qDebug()<<"hhhhhhhmmmm55555555555"<<dData.SmallScreenNum<<dData.VideoIp<<dData.VideoName<<dData.bisBigScreenShow<<dData.bIsIdentifiy;

    hideStreamItemToolTip();
    closeComboPopupBeforeModelReset(ui->VideoSet_comboBox);
    ui->VideoSet_comboBox->blockSignals(true);
    ui->VideoSet_comboBox->clear();
    ui->VideoSet_comboBox->setDisabled(false);
    m_readListData.clear();
    m_currentListData.clear();
    for (const StreamUi::StreamItem &item : strListData) {
        StreamUi::addStreamItem(ui->VideoSet_comboBox, item);
    }
    m_readListData = strListData;
    m_currentListData = strListData;


    const QString placeholder = QString::fromUtf8(u8"请选择或输入拉流地址!");
    QString displayIp = dData.VideoIp.trimmed();
    if (displayIp.isEmpty())
        displayIp = m_VideoIp.trimmed();

    const int filteredIndex = StreamUi::findStreamUrl(ui->VideoSet_comboBox, displayIp);
    if (filteredIndex >= 0) {
        ui->VideoSet_comboBox->setCurrentIndex(filteredIndex);
        m_structData.VideoIp = displayIp;
    } else if (dData.bIsStreamPullOn && !displayIp.isEmpty()) {
        // 正在拉流的窗口需要保留当前状态显示；此时下拉框随后会被禁用。
        StreamUi::setCurrentStreamUrl(ui->VideoSet_comboBox, displayIp);
        m_structData.VideoIp = displayIp;
    } else {
        // 未拉流窗口的旧地址若不属于当前设备，不得补入筛选后的下拉列表。
        ui->VideoSet_comboBox->setEditText(placeholder);
        m_structData.VideoIp.clear();
    }

    if(dData.bIsStreamPullOn){
        StreamUi::setCurrentStreamUrl(ui->VideoSet_comboBox, displayIp);
        ui->VideoSet_comboBox->setDisabled(true);

        ui->windowNum_label->setText(QString::fromUtf8(u8"窗口")+QString::number(dData.SmallScreenNum));
        ui->VideoName_label->setText(dData.VideoName);
        ui->Identify_pushButton->setDisabled(false);
        ui->Identify_pushButton->setChecked(dData.bIsIdentifiy);
        ui->SwitchBigScreen_pushButton->setChecked(dData.bisBigScreenShow);
        ui->SwitchBigScreen_pushButton->setDisabled(false);

        m_structData= dData;

        ui->VideoSet_pushButton->setChecked(true);
        m_readListData.clear();
        m_readListData=strListData;

//        ui->VideoSet_pushButton->setText(QString::fromUtf8(u8"    结束"));
        if(dData.connectstatus=="reconnecting"){
            ui->VideoSet_pushButton->setText(QString::fromUtf8(u8"    重连中"));
        }else{
            ui->VideoSet_pushButton->setText(QString::fromUtf8(u8"    结束"));
        }
    }else{
        ui->VideoSet_pushButton->setText(QString::fromUtf8(u8"    投屏"));
        ui->VideoSet_pushButton->setChecked(false);
        m_switchBigScreenAwaitingReply = false;
        m_identifyAwaitingReply = false;
        ui->SwitchBigScreen_pushButton->setDisabled(true);
        ui->SwitchBigScreen_pushButton->setChecked(false);
        ui->Identify_pushButton->setDisabled(true);
        ui->Identify_pushButton->setChecked(false);
        ui->windowNum_label->setText(QString::fromUtf8(u8"窗口")+QString::number(dData.SmallScreenNum));
        m_structData.VideoName = dData.VideoName;
        m_structData.SmallScreenNum = dData.SmallScreenNum;
        m_structData.bIsStreamPullOn = false;
        m_structData.bIsIdentifiy = dData.bIsIdentifiy;
        m_structData.bisBigScreenShow = dData.bisBigScreenShow;
    }

    const QString comboUrl = StreamUi::currentStreamUrl(ui->VideoSet_comboBox);
    if (ui->VideoSet_comboBox->currentText().trimmed() != placeholder
            && StreamUi::isValidStreamUrl(comboUrl))
        m_VideoIp = comboUrl;
    StreamUi::restoreElidedDisplay(ui->VideoSet_comboBox);
    ui->VideoSet_comboBox->blockSignals(false);

}

/**
 * @brief setDelVideoSetShowAndHide
 * 是否将“删除”按钮显示
 * @param bFlag(true:显示 false:不显示)
 * @return void
 */
void VideoSetWidget::setDelVideoSetShowAndHide(bool bFlag)
{
    if(bFlag)
    {
        ui->DeleteItem_pushButton->show();
    }
    else
    {
        ui->DeleteItem_pushButton->hide();
    }
}

/**
 * @brief setButtonChecked
 * 设置按钮是否为Checked状态样式
 * @param bFlag(true:Checked false:UnChecked)
 * @return void
 */
void VideoSetWidget::setButtonChecked(bool bFlag)
{
    ui->Identify_pushButton->setChecked(bFlag);
    m_structData.bIsIdentifiy = bFlag;
}

/**
 * @brief setSwitchBigScreenpushButtonChecked
 * 设置按钮是否为Checked状态样式
 * @param bFlag(true:Checked false:UnChecked)
 * @return void
 */
void VideoSetWidget::setSwitchBigScreenpushButtonChecked(bool bFlag)
{
    ui->SwitchBigScreen_pushButton->setChecked(bFlag);
    m_structData.bisBigScreenShow = bFlag;
}

void VideoSetWidget::notifySwitchBigScreenHttpFinished()
{
    m_switchBigScreenAwaitingReply = false;
    const bool streamOn = ui->VideoSet_pushButton->isChecked();
    ui->SwitchBigScreen_pushButton->setEnabled(streamOn);
}

void VideoSetWidget::notifyIdentifyHttpFinished()
{
    m_identifyAwaitingReply = false;
    const bool streamOn = ui->VideoSet_pushButton->isChecked();
    ui->Identify_pushButton->setEnabled(streamOn);
}

void VideoSetWidget::notifyStreamToggleHttpFinished()
{
    m_streamToggleAwaitingReply = false;
    ui->VideoSet_pushButton->setEnabled(true);
}

/**
 * @brief slotVideoSet_pushButton
 * 点击“下发”按钮所执行的槽函数,用于下发拉流配置参数
 * @param void
 * @return void
 */
void VideoSetWidget::slotVideoSet_pushButton(bool bFlag)
{
    if (m_streamToggleAwaitingReply)
        return;

    QString selectedUrl = StreamUi::currentStreamUrl(ui->VideoSet_comboBox).trimmed();
    if (!bFlag) {
        // 停止投屏应使用已启动任务保存的完整地址，不能依赖输入框中的省略显示。
        // 定时刷新或控件收起后，QLineEdit 可能只显示带省略号的文本。
        const QString activeUrl = m_structData.VideoIp.trimmed();
        if (StreamUi::isValidStreamUrl(activeUrl)) {
            selectedUrl = activeUrl;
        } else {
            const QString lastUrl = m_VideoIp.trimmed();
            if (StreamUi::isValidStreamUrl(lastUrl))
                selectedUrl = lastUrl;
        }
    }
    if (selectedUrl.isEmpty() ||
            ui->VideoSet_comboBox->currentText().trimmed()==QString::fromUtf8(u8"请选择或输入拉流地址!")){
        emit signalMessageBoxText(QString::fromUtf8(u8"提示"),
                                  QString::fromUtf8(u8"下发地址不能为空！"),
                                  MessageType_Warn);
        ui->VideoSet_pushButton->setChecked(false);
        return;
    }

    if (bFlag) {
        QString errorText;
        if (!StreamUi::isValidStreamUrl(selectedUrl, &errorText)) {
            emit signalMessageBoxText(QString::fromUtf8(u8"提示"), errorText, MessageType_Warn);
            ui->VideoSet_pushButton->setChecked(false);
            return;
        }
    }

    // 撤销 QPushButton 默认可勾选自动切换（等待 HTTP 期间保持禁用）
    ui->VideoSet_pushButton->blockSignals(true);
    ui->VideoSet_pushButton->setChecked(!bFlag);
    ui->VideoSet_pushButton->blockSignals(false);

    ui->VideoSet_comboBox->blockSignals(true);
    if(!bFlag){
//        ui->VideoSet_comboBox->setDisabled(false);
//        ui->VideoSet_comboBox->setStyleSheet(R"(QComboBox::drop-down{width:37px;height:37px;})");
        StreamUi::setCurrentStreamUrl(ui->VideoSet_comboBox, selectedUrl);
    }else{
//        ui->VideoSet_comboBox->setStyleSheet(R"(QComboBox::drop-down{width:0px;height:0px;)");
//        ui->VideoSet_comboBox->setDisabled(true);
        StreamUi::setCurrentStreamUrl(ui->VideoSet_comboBox, selectedUrl);
    }
    ui->VideoSet_comboBox->blockSignals(false);


    m_structData.VideoIp = StreamUi::currentStreamUrl(ui->VideoSet_comboBox);
    if (m_structData.VideoIp.isEmpty() && !m_VideoIp.trimmed().isEmpty())
        m_structData.VideoIp = m_VideoIp.trimmed();
    m_structData.VideoName=ui->VideoName_label->text().trimmed();
    m_structData.bisBigScreenShow=ui->SwitchBigScreen_pushButton->isChecked();
    m_structData.bIsIdentifiy=ui->Identify_pushButton->isChecked();
    m_structData.bIsStreamPullOn=bFlag;
    // 停流清空旧状态；开流用 connected，HTTP 回调先显示「结束」，仅轮询上报 reconnecting 时才切「重连中」
    m_structData.connectstatus = bFlag ? QStringLiteral("connected") : QStringLiteral("failed");

    m_streamToggleAwaitingReply = true;
    ui->VideoSet_pushButton->setEnabled(false);
    ui->VideoSet_pushButton->setText(QString::fromUtf8(u8"    投屏中"));
//    emit signalupdateVideoSetDate(m_structData);
    emit signalSendVideoSetInfo(m_structData,bFlag);
}

/**
 * @brief slotDeleteItem_pushButton
 * 点击“删除”按钮所执行的槽函数,用于删除该Item界面
 * @param void
 * @return void
 */
void VideoSetWidget::slotDeleteItem_pushButton()
{
    if(ui->Identify_pushButton->isChecked() || ui->VideoSet_pushButton->isChecked())
    {
        emit signalMessageBoxText(QString::fromUtf8(u8"提示"),
                                  QString::fromUtf8(u8"识别开关或拉流开关开启中，禁止删除！"),
                                  MessageType_Warn);
        return;
    }

    if (m_streamToggleAwaitingReply)
    {
        emit signalMessageBoxText(QString::fromUtf8(u8"提示"),
                                  QString::fromUtf8(u8"指令下发中，禁止删除！"),
                                  MessageType_Warn);
        return;
    }


    emit signalDeleteItemVideoSetItem(m_structData);
}

/**
 * @brief slotIdentify_pushButton
 * 点击“识别”按钮所执行的槽函数,用于发送识别指令，同时互斥其他识别操作
 * @param void
 * @return void
 */
void VideoSetWidget::slotIdentify_pushButton(bool bFlag)
{
    // 撤销 QPushButton 默认可勾选自动切换（等待中重复点击也需恢复点击前 checked）
    ui->Identify_pushButton->blockSignals(true);
    ui->Identify_pushButton->setChecked(!bFlag);
    ui->Identify_pushButton->blockSignals(false);
    if (m_identifyAwaitingReply)
        return;

    m_identifyAwaitingReply = true;
    ui->Identify_pushButton->setEnabled(false);

    m_structData.VideoIp = StreamUi::currentStreamUrl(ui->VideoSet_comboBox);
    m_structData.VideoName=ui->VideoName_label->text().trimmed();
    m_structData.bIsIdentifiy=bFlag;
    m_structData.bisBigScreenShow=ui->SwitchBigScreen_pushButton->isChecked();
    m_structData.bIsStreamPullOn=ui->VideoSet_pushButton->isChecked();

    emit signalIdentifyVideoSetItem(m_structData,bFlag);
    // 选中态由 ParaContentWidget::slotIdentifyVideoSetItem / 拉流查询刷新设置；
    // 此处若再 setChecked(!bFlag) 会覆盖父槽刚写入的状态，且会导致查询返回后 needUiUpdate 为假、按钮长期不选中。
    m_VideoIp = StreamUi::currentStreamUrl(ui->VideoSet_comboBox);
}


void VideoSetWidget::slotVideoSet_comboBox(QString text)
{
    Q_UNUSED(text);
    m_structData.VideoName=ui->VideoName_label->text().trimmed();
    m_structData.VideoIp = StreamUi::currentStreamUrl(ui->VideoSet_comboBox);
    m_VideoIp = m_structData.VideoIp;
    StreamUi::updateCurrentStreamToolTip(ui->VideoSet_comboBox);
}

/**
 * @brief slotUpdateList 更新下拉列表
 */
void VideoSetWidget::slotUpdateList(const StreamUi::StreamItemList &strListData, bool forceUpdate)
{
    // 正在手工编辑时不要重建下拉，否则会把完整文本重新刷成省略号。
    // 点击下拉箭头的 forceUpdate 仍允许刷新，以便弹出最新列表。
    if (!forceUpdate && StreamUi::isComboEditing(ui->VideoSet_comboBox))
        return;

    // 四个窗口只使用父控件传入的设备筛选结果。全局列表专供顶部客户端
    // 拉流地址使用，不能在这里覆盖筛选列表，否则会混入其它设备的流。
    StreamUi::StreamItemList effectiveList = strListData;

    StreamUi::StreamItemList sortedEffective = effectiveList;
    StreamUi::StreamItemList sortedCurrent = m_currentListData;
    std::sort(sortedEffective.begin(), sortedEffective.end());
    std::sort(sortedCurrent.begin(), sortedCurrent.end());

    bool displayNamesChanged = ui->VideoSet_comboBox->count() != effectiveList.size();
    if (!displayNamesChanged) {
        for (int i = 0; i < effectiveList.size(); ++i) {
            const QString currentText = ui->VideoSet_comboBox->itemText(i).trimmed();
            const QString currentUrl = comboItemUrl(ui->VideoSet_comboBox, i);
            if (currentText != effectiveList.at(i).displayName
                    || currentUrl != effectiveList.at(i).url) {
                displayNamesChanged = true;
                break;
            }
        }
    }

    const bool listContentsChanged = m_currentListData.size() != effectiveList.size()
        || sortedEffective != sortedCurrent
        || displayNamesChanged;

    if (listContentsChanged) {
        const bool popupWasVisible = comboPopupIsVisible(ui->VideoSet_comboBox);
        QString displayIp = m_structData.VideoIp.trimmed();
        if (displayIp.isEmpty())
            displayIp = m_VideoIp.trimmed();
        if (!StreamUi::isValidStreamUrl(displayIp))
            displayIp = StreamUi::currentStreamUrl(ui->VideoSet_comboBox).trimmed();

        const QString previousText = ui->VideoSet_comboBox->currentText().trimmed();

        if (popupWasVisible) {
            hideStreamItemToolTip();
            ui->VideoSet_comboBox->hidePopup();
        }
        ui->VideoSet_comboBox->blockSignals(true);
        ui->VideoSet_comboBox->clear();
        for (const StreamUi::StreamItem &item : effectiveList) {
            StreamUi::addStreamItem(ui->VideoSet_comboBox, item);
        }
        const QString placeholder = QString::fromUtf8(u8"请选择或输入拉流地址!");

        // 优先匹配 (previousText, displayIp) 完全一致的选项，避免同地址多个配置项相互跳变
        int filteredIndex = -1;
        if (!previousText.isEmpty() && !displayIp.isEmpty()) {
            for (int i = 0; i < ui->VideoSet_comboBox->count(); ++i) {
                if (ui->VideoSet_comboBox->itemText(i).trimmed() == previousText
                        && comboItemUrl(ui->VideoSet_comboBox, i) == displayIp) {
                    filteredIndex = i;
                    break;
                }
            }
        }
        if (filteredIndex < 0) {
            filteredIndex = StreamUi::findStreamUrl(ui->VideoSet_comboBox, displayIp);
        }

        if (filteredIndex >= 0) {
            ui->VideoSet_comboBox->setCurrentIndex(filteredIndex);
            m_structData.VideoIp = displayIp;
        } else if (m_structData.bIsStreamPullOn && !displayIp.isEmpty()) {
            // 运行中的窗口保持当前流状态；控件禁用，用户无法展开该临时项。
            StreamUi::setCurrentStreamUrl(ui->VideoSet_comboBox, displayIp);
            m_structData.VideoIp = displayIp;
        } else if (ui->VideoSet_comboBox->isEnabled()) {
            // 空闲窗口严格只展示当前设备筛选出的候选流。
            ui->VideoSet_comboBox->setEditText(placeholder);
            m_structData.VideoIp.clear();
        }
        ui->VideoSet_comboBox->blockSignals(false);

        m_currentListData.clear();
        m_currentListData = effectiveList;

    }

    if (!StreamUi::isComboEditing(ui->VideoSet_comboBox))
        StreamUi::restoreElidedDisplay(ui->VideoSet_comboBox);
    else
        StreamUi::updateCurrentStreamToolTip(ui->VideoSet_comboBox);

}

/**
 * @brief slotUpdateReadListData 更新读取本地的下拉列表数据
 */
void VideoSetWidget::slotUpdateReadListData(const StreamUi::StreamItemList &strListData)
{
    m_readListData.clear();
    m_readListData=strListData;
    slotUpdateList(strListData);
}


/**
 * @brief slotSwitchBigScreen_pushButton 大屏切换
 */
void VideoSetWidget::slotSwitchBigScreen_pushButton(bool bFlag)
{
    if (m_switchBigScreenAwaitingReply)
        return;

    ui->SwitchBigScreen_pushButton->blockSignals(true);
    ui->SwitchBigScreen_pushButton->setChecked(!bFlag);
    ui->SwitchBigScreen_pushButton->blockSignals(false);

    m_switchBigScreenAwaitingReply = true;
    ui->SwitchBigScreen_pushButton->setEnabled(false);

    m_structData.VideoIp = StreamUi::currentStreamUrl(ui->VideoSet_comboBox);
    m_structData.VideoName=ui->VideoName_label->text().trimmed();
    m_structData.bIsStreamPullOn=ui->VideoSet_pushButton->isChecked();
    m_structData.bisBigScreenShow=bFlag;
    m_structData.bIsIdentifiy=ui->Identify_pushButton->isChecked();

    emit signalSwitchBigScreen(m_structData,bFlag);
    // 选中态由 ParaContentWidget::slotSwitchBigScreenSetItem / 拉流查询刷新设置；
    // 此处若再 setChecked(!bFlag) 会覆盖父槽刚写入的状态，且会导致查询返回后 needUiUpdate 为假、按钮长期不选中。
}

//void VideoSetWidget::slotSendVideoSetTimeout()
//{
//    notifyStreamToggleHttpFinished();
//    ui->VideoSet_pushButton->setText(QString::fromUtf8(u8"    下发"));
//}

void Global::initialize()
{
    g_ZLMpushStreamList.clear();
    StreamUi::g_displayNameByUrl.clear();
}

bool VideoSetWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (!event)
        return QObject::eventFilter(watched, event);

    QAbstractItemView *const streamView = ui && ui->VideoSet_comboBox
            ? ui->VideoSet_comboBox->view() : nullptr;
    if (streamView && watched == streamView
            && (event->type() == QEvent::Hide
                || event->type() == QEvent::Close
                || event->type() == QEvent::WindowDeactivate)) {
        hideStreamItemToolTip();
    }

    if (ui && ui->VideoSet_comboBox && watched == ui->VideoSet_comboBox
            && (event->type() == QEvent::Hide
                || event->type() == QEvent::WindowDeactivate)) {
        hideStreamItemToolTip();
    }

    // 禁止 Qt 原生半透明 QToolTip 在 popup 内覆盖列表，改为在列表外侧显示
    // 自定义不透明提示窗；移出、关闭或点击时立即隐藏。
    if (streamView && watched == streamView->viewport()) {
        if (event->type() == QEvent::ToolTip)
            return true;
        if (event->type() == QEvent::MouseMove) {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
            const QModelIndex index = ui->VideoSet_comboBox->view()->indexAt(mouseEvent->pos());
            if (!index.isValid()) {
                hideStreamItemToolTip();
            } else if (index.row() != m_streamItemToolTipRow
                       || !m_streamItemToolTip || !m_streamItemToolTip->isVisible()) {
                showStreamItemToolTip(index);
            }
        } else if (event->type() == QEvent::Leave
                   || event->type() == QEvent::Hide
                   || event->type() == QEvent::MouseButtonPress
                   || event->type() == QEvent::Wheel) {
            hideStreamItemToolTip();
        }
    }

#if defined(Q_OS_WIN)
        if (watched != ui->VideoSet_comboBox)
            return QObject::eventFilter(watched, event);

        if (event->type() != QEvent::MouseButtonRelease)
            return QObject::eventFilter(watched, event);

        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() != Qt::LeftButton)
            return QObject::eventFilter(watched, event);

        if (!comboDropDownButtonRect(ui->VideoSet_comboBox).contains(mouseEvent->pos()))
            return QObject::eventFilter(watched, event);

        ++m_comboDropClickGeneration;
        schedulePushStreamComboRefresh(true);
        return true;
#else
    if(watched==ui->VideoSet_comboBox && event->type()==QEvent::MouseButtonPress){
        QMouseEvent *mouseEvent=static_cast<QMouseEvent*>(event);
        if(mouseEvent->button()==Qt::LeftButton){
            ++m_comboDropClickGeneration;
            schedulePushStreamComboRefresh(true);
        }
    }
    return QObject::eventFilter(watched,event);
#endif

}

void VideoSetWidget::hideEvent(QHideEvent *event)
{
    hideStreamItemToolTip();
    QWidget::hideEvent(event);
}

void VideoSetWidget::slotDeferredComboRefresh()
{
    if (m_shuttingDown)
        return;

    slotUpdateList(m_readListData, true);

    if (m_comboRefreshPendingAgain) {
        m_comboRefreshPendingAgain = false;
        emit signalRefreshpushStreamList();
        QMetaObject::invokeMethod(this, "slotDeferredComboRefresh", Qt::QueuedConnection);
        return;
    }

    m_comboRefreshQueued = false;

    if (m_deferredRefreshForClickGen != m_comboDropClickGeneration)
        return;

    if (ui->VideoSet_comboBox->isEnabled())
        QMetaObject::invokeMethod(this, "slotShowComboPopupAfterRefresh", Qt::QueuedConnection);
}

void VideoSetWidget::slotShowComboPopupAfterRefresh()
{
    if (m_shuttingDown || !ui || !ui->VideoSet_comboBox
            || !ui->VideoSet_comboBox->isEnabled()
            || m_deferredRefreshForClickGen != m_comboDropClickGeneration) {
        return;
    }

    // 鼠标按下阶段可能已经由 QComboBox 自己展开；此时禁止再次 showPopup，
    // 否则部分 Windows 绘制后端会复用未清空的 backing store，产生文字重影。
    if (!comboPopupIsVisible(ui->VideoSet_comboBox))
        ui->VideoSet_comboBox->showPopup();
}


void VideoSetWidget::schedulePushStreamComboRefresh(bool deferUiUpdate)
{
    if (m_shuttingDown)
        return;

    emit signalRefreshpushStreamList();

    if (!deferUiUpdate) {
        slotUpdateList(m_readListData, true);
        return;
    }

    m_deferredRefreshForClickGen = m_comboDropClickGeneration;

    if (m_comboRefreshQueued) {
        m_comboRefreshPendingAgain = true;
        return;
    }

    m_comboRefreshQueued = true;
    m_comboRefreshPendingAgain = false;
    QMetaObject::invokeMethod(this, "slotDeferredComboRefresh", Qt::QueuedConnection);
}
