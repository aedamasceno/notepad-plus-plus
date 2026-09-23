#pragma once

#include <QByteArray>
#include <QString>

namespace DocumentFormat {

enum class TextEncoding {
    Utf8,
    Utf8Bom,
    Utf16Le,
    Utf16Be,
    Windows1252
};

enum class EolKind {
    None,
    CrLf,
    Lf,
    Cr,
    Mixed
};

struct EolInfo {
    EolKind kind = EolKind::None;
    EolKind insertion = EolKind::Lf;
    int crlfCount = 0;
    int lfCount = 0;
    int crCount = 0;
};

struct DecodedDocument {
    bool success = false;
    QByteArray utf8;
    TextEncoding encoding = TextEncoding::Utf8;
    EolInfo eol;
    QString error;
};

DecodedDocument decode(const QByteArray &bytes);
bool isStrictUtf8(const QByteArray &bytes);
bool isValidUtf8Text(const QByteArray &bytes);
bool encode(const QByteArray &utf8, TextEncoding encoding, QByteArray *bytes,
            QString *error = nullptr);
bool saveAtomic(const QString &path, const QByteArray &utf8, TextEncoding encoding,
                QString *error = nullptr);
EolInfo scanEols(const QByteArray &utf8);
QString encodingName(TextEncoding encoding);
QString eolName(EolKind eol);
QString encodingKey(TextEncoding encoding);
bool encodingFromKey(const QString &key, TextEncoding *encoding);
QString eolKey(EolKind eol);
bool eolFromKey(const QString &key, EolKind *eol);

} // namespace DocumentFormat
