#include "documentformat.h"

#include <QSaveFile>

namespace DocumentFormat {
namespace {

bool strictUtf8(const QByteArray &bytes)
{
    const auto *data = reinterpret_cast<const unsigned char *>(bytes.constData());
    qsizetype i = 0;
    while (i < bytes.size()) {
        const unsigned char first = data[i++];
        if (first <= 0x7f)
            continue;
        int continuation = 0;
        unsigned int codePoint = 0;
        unsigned int minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) {
            continuation = 1; codePoint = first & 0x1f; minimum = 0x80;
        } else if (first >= 0xe0 && first <= 0xef) {
            continuation = 2; codePoint = first & 0x0f; minimum = 0x800;
        } else if (first >= 0xf0 && first <= 0xf4) {
            continuation = 3; codePoint = first & 0x07; minimum = 0x10000;
        } else {
            return false;
        }
        if (i + continuation > bytes.size())
            return false;
        for (int j = 0; j < continuation; ++j) {
            const unsigned char next = data[i++];
            if ((next & 0xc0) != 0x80)
                return false;
            codePoint = (codePoint << 6) | (next & 0x3f);
        }
        if (codePoint < minimum || codePoint > 0x10ffff ||
            (codePoint >= 0xd800 && codePoint <= 0xdfff))
            return false;
    }
    return true;
}

bool hasNul(const QString &text)
{
    return text.contains(QChar(u'\0'));
}

QString utf8ToQStringPreservingInitialBom(const QByteArray &bytes)
{
    QByteArray prefixed("x");
    prefixed.append(bytes);
    QString text = QString::fromUtf8(prefixed);
    text.remove(0, 1);
    return text;
}

bool decodeUtf16(const QByteArray &body, bool littleEndian, QString *text)
{
    if ((body.size() % 2) != 0)
        return false;
    QVector<char16_t> units;
    units.reserve(body.size() / 2);
    for (qsizetype i = 0; i < body.size(); i += 2) {
        const auto first = static_cast<unsigned char>(body.at(i));
        const auto second = static_cast<unsigned char>(body.at(i + 1));
        units.push_back(char16_t(littleEndian ? (first | (second << 8))
                                               : ((first << 8) | second)));
    }
    for (qsizetype i = 0; i < units.size(); ++i) {
        const char16_t unit = units.at(i);
        if (unit >= 0xd800 && unit <= 0xdbff) {
            if (++i >= units.size() || units.at(i) < 0xdc00 || units.at(i) > 0xdfff)
                return false;
        } else if (unit >= 0xdc00 && unit <= 0xdfff) {
            return false;
        }
    }
    units.prepend(u'x');
    *text = QString::fromUtf16(units.constData(), units.size());
    text->remove(0, 1);
    return !hasNul(*text);
}

QString decode1252(const QByteArray &bytes)
{
    static constexpr char16_t special[32] = {
        0x20ac, 0x0081, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
        0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008d, 0x017d, 0x008f,
        0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
        0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x009d, 0x017e, 0x0178
    };
    QString result;
    result.reserve(bytes.size());
    for (const unsigned char value : bytes) {
        if (value >= 0x80 && value <= 0x9f)
            result.append(QChar(special[value - 0x80]));
        else
            result.append(QChar(value));
    }
    return result;
}

bool append1252(QChar character, QByteArray *result)
{
    const ushort value = character.unicode();
    if (value <= 0x7f || (value >= 0xa0 && value <= 0xff)) {
        result->append(char(value));
        return true;
    }
    static constexpr char16_t special[32] = {
        0x20ac, 0x0081, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
        0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008d, 0x017d, 0x008f,
        0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
        0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x009d, 0x017e, 0x0178
    };
    for (int i = 0; i < 32; ++i) {
        if (special[i] == value) {
            result->append(char(0x80 + i));
            return true;
        }
    }
    return false;
}

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

} // namespace

EolInfo scanEols(const QByteArray &utf8)
{
    EolInfo info;
    EolKind first = EolKind::None;
    qsizetype firstCrLf = -1;
    qsizetype firstLf = -1;
    qsizetype firstCr = -1;
    for (qsizetype i = 0; i < utf8.size(); ++i) {
        const qsizetype position = i;
        EolKind found = EolKind::None;
        if (utf8.at(i) == '\r') {
            if (i + 1 < utf8.size() && utf8.at(i + 1) == '\n') {
                ++info.crlfCount; ++i; found = EolKind::CrLf;
                if (firstCrLf < 0) firstCrLf = position;
            } else {
                ++info.crCount; found = EolKind::Cr;
                if (firstCr < 0) firstCr = position;
            }
        } else if (utf8.at(i) == '\n') {
            ++info.lfCount; found = EolKind::Lf;
            if (firstLf < 0) firstLf = position;
        }
        if (first == EolKind::None && found != EolKind::None)
            first = found;
    }
    const int kinds = (info.crlfCount > 0) + (info.lfCount > 0) + (info.crCount > 0);
    if (kinds == 0) {
        info.kind = EolKind::None;
        info.insertion = EolKind::Lf;
        return info;
    }
    info.kind = kinds == 1 ? first : EolKind::Mixed;
    const int maximum = qMax(info.crlfCount, qMax(info.lfCount, info.crCount));
    // Among equally predominant conventions, use whichever appears first.
    qsizetype earliest = utf8.size();
    if (info.crlfCount == maximum && firstCrLf >= 0 && firstCrLf < earliest) {
        earliest = firstCrLf;
        info.insertion = EolKind::CrLf;
    }
    if (info.lfCount == maximum && firstLf >= 0 && firstLf < earliest) {
        earliest = firstLf;
        info.insertion = EolKind::Lf;
    }
    if (info.crCount == maximum && firstCr >= 0 && firstCr < earliest)
        info.insertion = EolKind::Cr;
    return info;
}

bool isValidUtf8Text(const QByteArray &bytes)
{
    return isStrictUtf8(bytes) && !bytes.contains('\0');
}

bool isStrictUtf8(const QByteArray &bytes)
{
    return strictUtf8(bytes);
}

DecodedDocument decode(const QByteArray &bytes)
{
    DecodedDocument result;
    QString text;
    if (bytes.startsWith("\xef\xbb\xbf")) {
        const QByteArray body = bytes.mid(3);
        if (!strictUtf8(body)) {
            result.error = QStringLiteral("Invalid UTF-8 after UTF-8 BOM");
            return result;
        }
        text = utf8ToQStringPreservingInitialBom(body);
        result.encoding = TextEncoding::Utf8Bom;
    } else if (bytes.startsWith("\xff\xfe")) {
        if (!decodeUtf16(bytes.mid(2), true, &text)) {
            result.error = QStringLiteral("Invalid UTF-16 LE document");
            return result;
        }
        result.encoding = TextEncoding::Utf16Le;
    } else if (bytes.startsWith("\xfe\xff")) {
        if (!decodeUtf16(bytes.mid(2), false, &text)) {
            result.error = QStringLiteral("Invalid UTF-16 BE document");
            return result;
        }
        result.encoding = TextEncoding::Utf16Be;
    } else {
        if (bytes.contains('\0')) {
            result.error = QStringLiteral("Binary file contains NUL bytes");
            return result;
        }
        if (strictUtf8(bytes)) {
            text = utf8ToQStringPreservingInitialBom(bytes);
            result.encoding = TextEncoding::Utf8;
        } else {
            text = decode1252(bytes);
            result.encoding = TextEncoding::Windows1252;
        }
    }
    if (hasNul(text)) {
        result.error = QStringLiteral("Binary or inappropriate text contains NUL characters");
        return result;
    }
    result.utf8 = text.toUtf8();
    result.eol = scanEols(result.utf8);
    result.success = true;
    return result;
}

bool encode(const QByteArray &utf8, TextEncoding encoding, QByteArray *bytes, QString *error)
{
    if (!bytes) {
        setError(error, QStringLiteral("No output buffer"));
        return false;
    }
    if (!strictUtf8(utf8)) {
        setError(error, QStringLiteral("Editor buffer is not valid UTF-8"));
        return false;
    }
    const QString text = utf8ToQStringPreservingInitialBom(utf8);
    if (hasNul(text)) {
        setError(error, QStringLiteral("Editor buffer contains NUL characters"));
        return false;
    }
    QByteArray result;
    if (encoding == TextEncoding::Utf8 || encoding == TextEncoding::Utf8Bom) {
        if (encoding == TextEncoding::Utf8Bom)
            result.append("\xef\xbb\xbf", 3);
        result.append(utf8);
    } else if (encoding == TextEncoding::Windows1252) {
        result.reserve(text.size());
        for (const QChar character : text) {
            if (!append1252(character, &result)) {
                setError(error, QStringLiteral("Text contains characters not representable in Windows-1252"));
                return false;
            }
        }
    } else {
        const bool littleEndian = encoding == TextEncoding::Utf16Le;
        result.append(littleEndian ? QByteArray("\xff\xfe", 2) : QByteArray("\xfe\xff", 2));
        result.reserve(2 + text.size() * 2);
        for (const QChar character : text) {
            const ushort unit = character.unicode();
            result.append(char(littleEndian ? unit & 0xff : unit >> 8));
            result.append(char(littleEndian ? unit >> 8 : unit & 0xff));
        }
    }
    *bytes = result;
    return true;
}

bool saveAtomic(const QString &path, const QByteArray &utf8, TextEncoding encoding, QString *error)
{
    QByteArray bytes;
    if (!encode(utf8, encoding, &bytes, error))
        return false;
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, file.errorString());
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        setError(error, file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        setError(error, file.errorString());
        return false;
    }
    return true;
}

QString encodingName(TextEncoding encoding)
{
    switch (encoding) {
    case TextEncoding::Utf8: return QStringLiteral("UTF-8");
    case TextEncoding::Utf8Bom: return QStringLiteral("UTF-8 BOM");
    case TextEncoding::Utf16Le: return QStringLiteral("UTF-16 LE");
    case TextEncoding::Utf16Be: return QStringLiteral("UTF-16 BE");
    case TextEncoding::Windows1252: return QStringLiteral("Windows-1252");
    }
    return {};
}

QString eolName(EolKind eol)
{
    switch (eol) {
    case EolKind::None: return QStringLiteral("No EOL");
    case EolKind::CrLf: return QStringLiteral("Windows (CR LF)");
    case EolKind::Lf: return QStringLiteral("Unix (LF)");
    case EolKind::Cr: return QStringLiteral("Macintosh (CR)");
    case EolKind::Mixed: return QStringLiteral("Mixed EOL");
    }
    return {};
}

QString encodingKey(TextEncoding encoding)
{
    switch (encoding) {
    case TextEncoding::Utf8: return QStringLiteral("utf8");
    case TextEncoding::Utf8Bom: return QStringLiteral("utf8-bom");
    case TextEncoding::Utf16Le: return QStringLiteral("utf16-le");
    case TextEncoding::Utf16Be: return QStringLiteral("utf16-be");
    case TextEncoding::Windows1252: return QStringLiteral("windows-1252");
    }
    return {};
}

bool encodingFromKey(const QString &key, TextEncoding *encoding)
{
    if (!encoding) return false;
    if (key == QStringLiteral("utf8")) *encoding = TextEncoding::Utf8;
    else if (key == QStringLiteral("utf8-bom")) *encoding = TextEncoding::Utf8Bom;
    else if (key == QStringLiteral("utf16-le")) *encoding = TextEncoding::Utf16Le;
    else if (key == QStringLiteral("utf16-be")) *encoding = TextEncoding::Utf16Be;
    else if (key == QStringLiteral("windows-1252")) *encoding = TextEncoding::Windows1252;
    else return false;
    return true;
}

QString eolKey(EolKind eol)
{
    switch (eol) {
    case EolKind::None: return QStringLiteral("none");
    case EolKind::CrLf: return QStringLiteral("crlf");
    case EolKind::Lf: return QStringLiteral("lf");
    case EolKind::Cr: return QStringLiteral("cr");
    case EolKind::Mixed: return QStringLiteral("mixed");
    }
    return {};
}

bool eolFromKey(const QString &key, EolKind *eol)
{
    if (!eol) return false;
    if (key == QStringLiteral("none")) *eol = EolKind::None;
    else if (key == QStringLiteral("crlf")) *eol = EolKind::CrLf;
    else if (key == QStringLiteral("lf")) *eol = EolKind::Lf;
    else if (key == QStringLiteral("cr")) *eol = EolKind::Cr;
    else if (key == QStringLiteral("mixed")) *eol = EolKind::Mixed;
    else return false;
    return true;
}

} // namespace DocumentFormat
