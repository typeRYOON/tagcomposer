#include <core/workflow.h>
#include <QCryptographicHash>

using namespace Qt::StringLiterals;

namespace tc {
namespace {

template <class... Ts>
struct Visitor : Ts... {
    using Ts::operator()...;
};
template <class... Ts>
Visitor(Ts...) -> Visitor<Ts...>;

} // namespace

QString seedBehaviorToString(SeedBehavior b)
{
    switch (b) {
    case SeedBehavior::Fixed:
        return u"fixed"_s;
    case SeedBehavior::Increment:
        return u"increment"_s;
    case SeedBehavior::Randomize:
        return u"randomize"_s;
    }
    return u"randomize"_s;
}

SeedBehavior seedBehaviorFromString(const QString& s)
{
    const QString key = s.toLower();
    if (key == "fixed"_L1) return SeedBehavior::Fixed;
    if (key == "increment"_L1) return SeedBehavior::Increment;
    return SeedBehavior::Randomize;
}

QString ImageEdits::hash() const
{
    if (!enabled) return {};

    QCryptographicHash digest(QCryptographicHash::Sha1);
    digest.addData(QByteArray::number(cropRect.x()));
    digest.addData(",");
    digest.addData(QByteArray::number(cropRect.y()));
    digest.addData(",");
    digest.addData(QByteArray::number(cropRect.width()));
    digest.addData(",");
    digest.addData(QByteArray::number(cropRect.height()));
    digest.addData(",");
    digest.addData(trimToCrop ? "1" : "0");
    digest.addData(",");
    digest.addData(maskId.toUtf8());
    return QString::fromLatin1(digest.result().toHex().left(12));
}

QString varTypeName(const WorkflowVarValue& value)
{
    return std::visit(Visitor{
                          [](const SeedVar&) { return u"seed"_s; },
                          [](const StringVar&) { return u"string"_s; },
                          [](const IntVar&) { return u"integer"_s; },
                          [](const FloatVar&) { return u"float"_s; },
                          [](const DirSearchVar&) { return u"dirSearch"_s; },
                          [](const LatentSizeVar&) { return u"latentSize"_s; },
                          [](const ImageVar&) { return u"image"_s; },
                          [](const WildcardVar&) { return u"wildcard"_s; },
                      },
                      value);
}

} // namespace tc
