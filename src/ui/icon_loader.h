#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

namespace passvault::ui {

class IconLoader {
 public:
    static QIcon Load(const QString& name);
    static QIcon Load(const QString& name, const QColor& color, int size = 20);

    static void ClearCache();
};

}  // namespace passvault::ui
