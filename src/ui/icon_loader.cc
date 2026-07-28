#include "ui/icon_loader.h"

#include <QFile>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

#include "ui/theme_manager.h"

namespace passvault::ui {

namespace {

QMutex& CacheMutex() {
    static QMutex m;
    return m;
}

QHash<QString, QIcon>& Cache() {
    static QHash<QString, QIcon> c;
    return c;
}

QByteArray ReadSvg(const QString& name) {
    QFile f(QStringLiteral(":/icons/%1.svg").arg(name));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}

QIcon RenderColored(QByteArray svg, const QColor& color, int size) {
    if (svg.isEmpty()) return {};
    const QByteArray hex = color.name(QColor::HexRgb).toLatin1();
    svg.replace("currentColor", hex);
    svg.replace("stroke=\"#000000\"", QByteArray("stroke=\"") + hex + "\"");
    svg.replace("stroke=\"#000\"", QByteArray("stroke=\"") + hex + "\"");
    svg.replace("fill=\"#000000\"", QByteArray("fill=\"") + hex + "\"");
    svg.replace("fill=\"#000\"", QByteArray("fill=\"") + hex + "\"");

    QSvgRenderer renderer(svg);
    if (!renderer.isValid()) return {};
    const int px = size > 0 ? size : 20;
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    renderer.render(&p);
    p.end();
    return QIcon(pm);
}

}  // namespace

QIcon IconLoader::Load(const QString& name) {
    const QColor color = ThemeManager::Instance()->Color(
        QStringLiteral("text-primary"));
    return Load(name, color.isValid() ? color : QColor(230, 230, 230), 20);
}

QIcon IconLoader::Load(const QString& name, const QColor& color, int size) {
    const QString key = QStringLiteral("%1|%2|%3")
                            .arg(name, color.name(QColor::HexArgb))
                            .arg(size);
    {
        QMutexLocker lock(&CacheMutex());
        auto it = Cache().constFind(key);
        if (it != Cache().constEnd()) return it.value();
    }
    QIcon icon = RenderColored(ReadSvg(name), color, size);
    {
        QMutexLocker lock(&CacheMutex());
        Cache().insert(key, icon);
    }
    return icon;
}

void IconLoader::ClearCache() {
    QMutexLocker lock(&CacheMutex());
    Cache().clear();
}

}  // namespace passvault::ui
