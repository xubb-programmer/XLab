#ifndef COMMUNICATIONINIUTILS_H
#define COMMUNICATIONINIUTILS_H

#include <QByteArray>
#include <QFile>
#include <QSaveFile>
#include <QSettings>
#include <QString>
#include <QTextCodec>

namespace CommunicationIniUtils {

inline bool containsPercentByte(const QByteArray &value)
{
    auto isHex = [](char ch) {
        return (ch >= '0' && ch <= '9')
                || (ch >= 'a' && ch <= 'f')
                || (ch >= 'A' && ch <= 'F');
    };

    for (int i = 0; i + 2 < value.size(); ++i) {
        if (value.at(i) == '%' && isHex(value.at(i + 1)) && isHex(value.at(i + 2)))
            return true;
    }
    return false;
}

/**
 * 将从 INI 原始读取到的键名字节解析为正确的 QString 字符串。
 * 兼容处理：
 * 1) QSettings 在 UTF-8 下转义的 %E8%A7...
 * 2) QSettings 在 GBK 下转义的 %CA%D3...
 * 3) 文件本身为未转义的 UTF-8 (含带/不带 BOM)
 * 4) 文件本身为 Windows XP 记事本默认保存的 ANSI (GBK) 编码
 */
inline QString decodePullStreamKeyBytes(const QByteArray &rawBytes)
{
    if (rawBytes.isEmpty())
        return QString();

    QByteArray bytes = rawBytes.trimmed();
    if (bytes.startsWith("\xEF\xBB\xBF"))
        bytes = bytes.mid(3);

    // 如果包含百分号转义，先做百分号还原
    if (containsPercentByte(bytes)) {
        const QByteArray percentDecoded = QByteArray::fromPercentEncoding(bytes);
        if (!percentDecoded.isEmpty()) {
            // 1) 优先尝试 UTF-8
            QTextCodec *utf8Codec = QTextCodec::codecForName("UTF-8");
            if (utf8Codec) {
                QTextCodec::ConverterState state;
                const QString text = utf8Codec->toUnicode(percentDecoded.constData(), percentDecoded.size(), &state);
                if (state.invalidChars == 0 && !text.isEmpty())
                    return text;
            }
            // 2) 尝试 GBK / Local8Bit
            QTextCodec *gbkCodec = QTextCodec::codecForName("GBK");
            if (!gbkCodec)
                gbkCodec = QTextCodec::codecForLocale();
            if (gbkCodec) {
                QTextCodec::ConverterState state;
                const QString text = gbkCodec->toUnicode(percentDecoded.constData(), percentDecoded.size(), &state);
                if (state.invalidChars == 0 && !text.isEmpty())
                    return text;
            }
            return QString::fromLocal8Bit(percentDecoded);
        }
    }

    // 未做百分号转义的纯文本字节
    // 1) 优先尝试 UTF-8
    QTextCodec *utf8Codec = QTextCodec::codecForName("UTF-8");
    if (utf8Codec) {
        QTextCodec::ConverterState state;
        const QString text = utf8Codec->toUnicode(bytes.constData(), bytes.size(), &state);
        if (state.invalidChars == 0 && !text.isEmpty())
            return text;
    }

    // 2) 若包含非法 UTF-8 字节（如 Windows XP 记事本保存的 ANSI/GBK），回退到 GBK / 本地编码
    QTextCodec *gbkCodec = QTextCodec::codecForName("GBK");
    if (!gbkCodec)
        gbkCodec = QTextCodec::codecForLocale();
    if (gbkCodec) {
        QTextCodec::ConverterState state;
        const QString text = gbkCodec->toUnicode(bytes.constData(), bytes.size(), &state);
        if (state.invalidChars == 0 && !text.isEmpty())
            return text;
    }

    return QString::fromLocal8Bit(bytes);
}

inline QString decodePullStreamKey(const QString &value)
{
    return decodePullStreamKeyBytes(value.toUtf8());
}

/**
 * QSettings 会把 INI 键名中的中文写成 %XX（可能是 UTF-8 或 GBK 百分号转义）。
 * 该分组供用户直接查看编辑，因此在保存后将 PullStream 键名统一恢复为可读 UTF-8。
 */
inline bool restoreReadablePullStreamKeys(const QString &filePath)
{
    QFile input(filePath);
    if (!input.open(QIODevice::ReadOnly))
        return false;
    const QByteArray original = input.readAll();
    input.close();

    QList<QByteArray> lines = original.split('\n');
    bool inPullStreamGroup = false;
    bool changed = false;

    for (QByteArray &line : lines) {
        QByteArray lineWithoutCr = line;
        const bool hasCr = lineWithoutCr.endsWith('\r');
        if (hasCr)
            lineWithoutCr.chop(1);

        const QByteArray trimmed = lineWithoutCr.trimmed();
        if (trimmed.startsWith('[') && trimmed.endsWith(']')) {
            inPullStreamGroup = trimmed.toLower() == QByteArrayLiteral("[pullstream]");
            continue;
        }
        if (!inPullStreamGroup || trimmed.isEmpty()
                || trimmed.startsWith(';') || trimmed.startsWith('#')) {
            continue;
        }

        const int separatorIndex = lineWithoutCr.indexOf('=');
        if (separatorIndex <= 0)
            continue;

        int keyStart = 0;
        while (keyStart < separatorIndex
               && (lineWithoutCr.at(keyStart) == ' ' || lineWithoutCr.at(keyStart) == '\t')) {
            ++keyStart;
        }
        int keyEnd = separatorIndex;
        while (keyEnd > keyStart
               && (lineWithoutCr.at(keyEnd - 1) == ' ' || lineWithoutCr.at(keyEnd - 1) == '\t')) {
            --keyEnd;
        }

        const QByteArray rawKey = lineWithoutCr.mid(keyStart, keyEnd - keyStart);
        const QString decodedText = decodePullStreamKeyBytes(rawKey);
        if (decodedText.isEmpty())
            continue;

        const QByteArray utf8DecodedKey = decodedText.toUtf8();
        if (utf8DecodedKey == rawKey)
            continue;

        lineWithoutCr.replace(keyStart, keyEnd - keyStart, utf8DecodedKey);
        line = lineWithoutCr + (hasCr ? QByteArrayLiteral("\r") : QByteArray());
        changed = true;
    }

    if (!changed)
        return true;

    QByteArray restored;
    for (int i = 0; i < lines.size(); ++i) {
        if (i > 0)
            restored.append('\n');
        restored.append(lines.at(i));
    }
    QSaveFile output(filePath);
    if (!output.open(QIODevice::WriteOnly) || output.write(restored) != restored.size())
        return false;
    return output.commit();
}

inline void syncPreservingPullStreamKeys(QSettings &settings)
{
    settings.sync();
    restoreReadablePullStreamKeys(settings.fileName());
}

} // namespace CommunicationIniUtils

#endif // COMMUNICATIONINIUTILS_H
