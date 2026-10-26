#include "ui/detail_panel.h"

#include <QColor>
#include <QDateTime>
#include <QDesktopServices>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStackedLayout>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include "ui/icon_loader.h"
#include "ui/elided_label.h"
#include "ui/theme_manager.h"

namespace passvault::ui {

namespace {

constexpr int kIconButtonSize = 30;
constexpr int kIconSize = 16;
constexpr int kHeaderIconSize = 22;
constexpr int kAvatarSize = 48;

QLabel* MakeLabel(const QString& text, const QString& object_name) {
    auto* label = new QLabel(text);
    label->setObjectName(object_name);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setMinimumWidth(0);
    return label;
}

QToolButton* MakeIconButton(const QString& icon_name, const QString& tooltip) {
    auto* button = new QToolButton();
    button->setObjectName(QStringLiteral("DetailFieldButton"));
    button->setToolTip(tooltip);
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedSize(kIconButtonSize, kIconButtonSize);
    const QColor color =
        ThemeManager::Instance()->Color(QStringLiteral("text-tertiary"));
    button->setIcon(IconLoader::Load(icon_name, color, kIconSize));
    button->setIconSize(QSize(kIconSize, kIconSize));
    return button;
}

QFrame* MakeDivider() {
    auto* line = new QFrame();
    line->setObjectName(QStringLiteral("DetailCardDivider"));
    line->setFixedHeight(1);
    return line;
}

}  // namespace

DetailPanel::DetailPanel(QWidget* parent)
    : QFrame(parent) {
    setObjectName(QStringLiteral("DetailPanel"));
    setFrameShape(QFrame::NoFrame);
    setMinimumWidth(360);
    setMaximumWidth(460);
    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(45);
    shadow->setColor(ThemeManager::Instance()->Color(QStringLiteral("shadow")));
    shadow->setOffset(-18, 0);
    setGraphicsEffect(shadow);
    BuildUi();
    ClearEntry();
    connect(ThemeManager::Instance(), &ThemeManager::ThemeChanged, this,
            [this](Theme) { RefreshThemeAssets(); });
}

DetailPanel::~DetailPanel() = default;

void DetailPanel::BuildUi() {
    stack_ = new QStackedLayout(this);
    stack_->setContentsMargins(0, 0, 0, 0);
    stack_->setSpacing(0);

    empty_page_ = BuildEmptyPage();
    content_page_ = BuildContentPage();
    stack_->addWidget(empty_page_);
    stack_->addWidget(content_page_);
}

QWidget* DetailPanel::BuildEmptyPage() {
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("DetailEmpty"));
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);
    layout->addStretch(1);

    empty_icon_ = new QLabel(page);
    empty_icon_->setObjectName(QStringLiteral("DetailEmptyIcon"));
    empty_icon_->setAlignment(Qt::AlignCenter);
    const QColor color =
        ThemeManager::Instance()->Color(QStringLiteral("text-quaternary"));
    const QIcon vault_icon = IconLoader::Load(QStringLiteral("vault"), color, 48);
    empty_icon_->setPixmap(vault_icon.pixmap(48, 48));
    layout->addWidget(empty_icon_, 0, Qt::AlignCenter);

    auto* title = MakeLabel(QStringLiteral("选择一条密码查看详情"),
                            QStringLiteral("DetailEmptyText"));
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title, 0, Qt::AlignCenter);

    auto* subtitle = MakeLabel(QStringLiteral("侧边选中或新建一条即可开始"),
                               QStringLiteral("DetailEmptyText"));
    subtitle->setAlignment(Qt::AlignCenter);
    layout->addWidget(subtitle, 0, Qt::AlignCenter);

    layout->addStretch(2);
    return page;
}

QWidget* DetailPanel::BuildContentPage() {
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("DetailScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* page = new QWidget;
    page->setObjectName(QStringLiteral("DetailContent"));
    scroll->setWidget(page);
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(28, 28, 28, 28);
    outer->setSpacing(20);

    auto* header = new QWidget(page);
    header->setObjectName(QStringLiteral("DetailHeader"));
    auto* header_layout = new QHBoxLayout(header);
    header_layout->setContentsMargins(0, 0, 0, 0);
    header_layout->setSpacing(12);

    header_icon_ = new QLabel(header);
    header_icon_->setObjectName(QStringLiteral("DetailIcon"));
    header_icon_->setFixedSize(kAvatarSize, kAvatarSize);
    header_icon_->setAlignment(Qt::AlignCenter);
    header_layout->addWidget(header_icon_, 0, Qt::AlignTop);

    auto* title_col = new QVBoxLayout();
    title_col->setContentsMargins(0, 0, 0, 0);
    title_col->setSpacing(4);
    title_col->setAlignment(Qt::AlignTop);

    header_title_ = new ElidedLabel();
    header_title_->setObjectName(QStringLiteral("DetailTitle"));
    header_title_->setTextFormat(Qt::PlainText);
    title_col->addWidget(header_title_);
    header_tag_ = MakeLabel(QString(), QStringLiteral("DetailTag"));
    header_tag_->setWordWrap(false);
    header_tag_->setMaximumWidth(100);
    header_tag_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
    title_col->addWidget(header_tag_, 0, Qt::AlignLeft);

    header_layout->addLayout(title_col, 1);

    auto* actions = new QWidget(header);
    actions->setObjectName(QStringLiteral("DetailHeaderActions"));
    auto* action_layout = new QHBoxLayout(actions);
    action_layout->setContentsMargins(0, 0, 0, 0);
    action_layout->setSpacing(4);

    header_edit_ = new QPushButton(actions);
    header_edit_->setObjectName(QStringLiteral("DetailHeaderButton"));
    header_edit_->setCursor(Qt::PointingHandCursor);
    header_edit_->setFlat(true);
    header_edit_->setIcon(IconLoader::Load(
        QStringLiteral("pencil"),
        ThemeManager::Instance()->Color(QStringLiteral("text-secondary")),
        kIconSize));
    header_edit_->setIconSize(QSize(kIconSize, kIconSize));
    connect(header_edit_, &QPushButton::clicked, this, [this]() {
        if (entry_) emit EditRequested(entry_->id);
    });
    action_layout->addWidget(header_edit_);

    header_favorite_ = new QToolButton(actions);
    header_favorite_->setObjectName(QStringLiteral("DetailHeaderButton"));
    header_favorite_->setCheckable(true);
    header_favorite_->setToolTip(QStringLiteral("收藏"));
    header_favorite_->setAccessibleName(QStringLiteral("收藏"));
    header_favorite_->setIcon(IconLoader::Load(
        QStringLiteral("star"),
        ThemeManager::Instance()->Color(QStringLiteral("text-quaternary")),
        kHeaderIconSize));
    header_favorite_->setIconSize(QSize(kHeaderIconSize, kHeaderIconSize));
    connect(header_favorite_, &QToolButton::toggled, this, [this](bool checked) {
        if (!entry_) return;
        emit FavoriteToggleRequested(entry_->id, checked);
    });
    action_layout->addWidget(header_favorite_);

    header_delete_ = new QToolButton(actions);
    header_delete_->setObjectName(QStringLiteral("DetailHeaderDeleteButton"));
    header_delete_->setToolTip(QStringLiteral("删除密码"));
    header_delete_->setAccessibleName(QStringLiteral("删除密码"));
    header_delete_->setIcon(IconLoader::Load(
        QStringLiteral("trash-2"),
        ThemeManager::Instance()->Color(QStringLiteral("danger")), kIconSize));
    header_delete_->setIconSize(QSize(20, 20));
    connect(header_delete_, &QToolButton::clicked, this, [this]() {
        if (entry_) emit DeleteRequested(entry_->id);
    });
    action_layout->addWidget(header_delete_);
    header_close_ = MakeIconButton(QStringLiteral("x"), QStringLiteral("关闭详情"));
    header_close_->setObjectName(QStringLiteral("DetailCloseButton"));
    connect(header_close_, &QToolButton::clicked, this,
            &DetailPanel::CloseRequested);
    action_layout->addWidget(header_close_);
    header_layout->addWidget(actions, 0, Qt::AlignTop);

    outer->addWidget(header);

    risky_banner_ = new QWidget(page);
    risky_banner_->setObjectName(QStringLiteral("DetailRiskyBanner"));
    auto* risky_layout = new QHBoxLayout(risky_banner_);
    risky_layout->setContentsMargins(12, 10, 12, 10);
    risky_layout->setSpacing(10);
    risky_icon_ = new QLabel(risky_banner_);
    risky_icon_->setObjectName(QStringLiteral("DetailRiskyIcon"));
    const QColor danger =
        ThemeManager::Instance()->Color(QStringLiteral("danger"));
    risky_icon_->setPixmap(
        IconLoader::Load(QStringLiteral("shield-alert"), danger, 20)
            .pixmap(20, 20));
    risky_layout->addWidget(risky_icon_);
    auto* risky_text = MakeLabel(
        QStringLiteral("此密码可能已在其他条目中使用，建议尽快更换。"),
        QStringLiteral("DetailRiskyBannerText"));
    risky_text->setWordWrap(true);
    risky_layout->addWidget(risky_text, 1);
    risky_banner_->hide();
    outer->addWidget(risky_banner_);

    auto* info_card = new QFrame(page);
    info_card->setObjectName(QStringLiteral("DetailCard"));
    info_card->setFrameShape(QFrame::NoFrame);
    auto* info_layout = new QVBoxLayout(info_card);
    info_layout->setContentsMargins(0, 0, 0, 0);
    info_layout->setSpacing(0);

    auto build_row = [&](const QString& label_text, QLabel* value,
                         std::initializer_list<QToolButton**> buttons) {
        auto* row = new QWidget(info_card);
        row->setObjectName(QStringLiteral("DetailCardRow"));
        auto* row_layout = new QHBoxLayout(row);
        row_layout->setContentsMargins(0, 0, 0, 0);
        row_layout->setSpacing(8);

        auto* col = new QVBoxLayout();
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(4);
        auto* label = MakeLabel(label_text, QStringLiteral("DetailFieldLabel"));
        col->addWidget(label);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        auto value_policy = value->sizePolicy();
        value_policy.setHorizontalPolicy(QSizePolicy::Ignored);
        value->setSizePolicy(value_policy);
        col->addWidget(value);
        row_layout->addLayout(col, 1);

        for (QToolButton** btn_ptr : buttons) {
            row_layout->addWidget(*btn_ptr);
        }
        info_layout->addWidget(row);
    };

    title_value_ = MakeLabel(QString(), QStringLiteral("DetailTitleValue"));
    build_row(QStringLiteral("标题"), title_value_, {});
    info_layout->addWidget(MakeDivider());

    username_copy_ = MakeIconButton(QStringLiteral("copy"),
                                    QStringLiteral("复制用户名"));
    connect(username_copy_, &QToolButton::clicked, this, [this]() {
        if (entry_) emit CopyUsernameRequested(entry_->id);
    });
    username_value_ = MakeLabel(QString(), QStringLiteral("DetailFieldValue"));
    username_value_->setObjectName(QStringLiteral("DetailFieldValue"));
    build_row(QStringLiteral("用户名"), username_value_, {&username_copy_});

    info_layout->addWidget(MakeDivider());

    password_toggle_ = MakeIconButton(QStringLiteral("eye"),
                                      QStringLiteral("显示密码"));
    connect(password_toggle_, &QToolButton::clicked, this, [this]() {
        password_visible_ = !password_visible_;
        UpdatePasswordDisplay();
    });
    password_copy_ = MakeIconButton(QStringLiteral("copy"),
                                    QStringLiteral("复制密码"));
    connect(password_copy_, &QToolButton::clicked, this, [this]() {
        if (entry_) emit CopyPasswordRequested(entry_->id);
    });
    password_value_ = MakeLabel(QString(),
                                QStringLiteral("DetailFieldValueMono"));
    build_row(QStringLiteral("密码"), password_value_,
              {&password_toggle_, &password_copy_});

    info_layout->addWidget(MakeDivider());

    website_open_ = MakeIconButton(QStringLiteral("external-link"),
                                   QStringLiteral("打开网站"));
    connect(website_open_, &QToolButton::clicked, this, [this]() {
        if (entry_) emit OpenWebsiteRequested(entry_->id);
    });
    website_value_ = MakeLabel(QString(), QStringLiteral("DetailFieldValueLink"));
    website_value_->setObjectName(QStringLiteral("DetailFieldValueLink"));
    build_row(QStringLiteral("网址"), website_value_, {&website_open_});

    info_layout->addWidget(MakeDivider());
    category_value_ = MakeLabel(QString(), QStringLiteral("DetailCategoryValue"));
    build_row(QStringLiteral("分类"), category_value_, {});
    info_layout->addWidget(MakeDivider());
    notes_value_ = MakeLabel(QString(), QStringLiteral("DetailNotesValue"));
    build_row(QStringLiteral("备注"), notes_value_, {});
    info_layout->addWidget(MakeDivider());
    package_value_ = MakeLabel(QString(), QStringLiteral("DetailPackageValue"));
    build_row(QStringLiteral("应用包名"), package_value_, {});

    outer->addWidget(info_card);

    auto* stamp_card = new QFrame(page);
    stamp_card->setObjectName(QStringLiteral("DetailStampCard"));
    stamp_card->setFrameShape(QFrame::NoFrame);
    auto* stamp_layout = new QVBoxLayout(stamp_card);
    stamp_layout->setContentsMargins(0, 0, 0, 0);
    stamp_layout->setSpacing(8);

    auto make_stamp_row = [&](const QString& label_text, QLabel** value_ptr) {
        auto* row = new QHBoxLayout();
        row->setContentsMargins(0, 0, 0, 0);
        row->addWidget(
            MakeLabel(label_text, QStringLiteral("DetailStampLabel")));
        row->addStretch(1);
        auto* value = MakeLabel(QString(), QStringLiteral("DetailStampValue"));
        row->addWidget(value);
        *value_ptr = value;
        stamp_layout->addLayout(row);
    };
    make_stamp_row(QStringLiteral("创建时间"), &created_value_);
    make_stamp_row(QStringLiteral("更新时间"), &updated_value_);
    outer->addWidget(stamp_card);

    outer->addStretch(1);
    return scroll;
}

void DetailPanel::SetCategories(QList<model::Category> categories) {
    categories_ = std::move(categories);
    if (entry_) RefreshView();
}

void DetailPanel::SetEntry(const model::PasswordEntry& entry,
                           const std::optional<QString>& decrypted_password) {
    entry_ = entry;
    password_plain_ = decrypted_password;
    password_visible_ = false;
    stack_->setCurrentWidget(content_page_);
    RefreshView();
}

void DetailPanel::ClearEntry() {
    entry_.reset();
    password_plain_.reset();
    header_title_->SetFullText(QString());
    for (auto* label : {title_value_, username_value_, password_value_,
                        website_value_, notes_value_, category_value_, package_value_}) {
        label->clear();
        label->setAccessibleName(QString());
    }
    password_visible_ = false;
    stack_->setCurrentWidget(empty_page_);
}

bool DetailPanel::HasEntry() const { return entry_.has_value(); }

std::int64_t DetailPanel::entry_id() const {
    return entry_ ? entry_->id : -1;
}

void DetailPanel::RefreshView() {
    if (!entry_) return;
    const auto& e = *entry_;

    header_icon_->setText(AvatarInitials(e));
    header_title_->SetFullText(e.title.isEmpty() ? QStringLiteral("(无标题)")
                                                 : e.title);
    const QString tag = CategoryName(e.category_id);
    if (tag.isEmpty()) {
        header_tag_->hide();
    } else {
        header_tag_->setText(tag);
        header_tag_->ensurePolished();
        header_tag_->setMinimumWidth(
            qMin(100, header_tag_->fontMetrics().horizontalAdvance(tag) + 16));
        header_tag_->show();
    }

    QSignalBlocker blocker(header_favorite_);
    header_favorite_->setChecked(e.is_favorite);
    const QColor star_color = e.is_favorite
        ? ThemeManager::Instance()->Color(QStringLiteral("warning"))
        : ThemeManager::Instance()->Color(QStringLiteral("text-quaternary"));
    header_favorite_->setIcon(
        IconLoader::Load(QStringLiteral("star"), star_color, kHeaderIconSize));

    risky_banner_->setVisible(false);

    username_value_->setText(e.username.isEmpty()
                                     ? QStringLiteral("(无用户名)")
                                     : e.username);
    username_copy_->setEnabled(!e.username.isEmpty());

    UpdatePasswordDisplay();

    if (e.website.isEmpty()) {
        website_value_->setText(QString());
        website_open_->setEnabled(false);
    } else {
        website_value_->setText(e.website);
        website_open_->setEnabled(true);
    }

    created_value_->setText(FormatTimestamp(e.created_at));
    updated_value_->setText(FormatTimestamp(e.updated_at));
    notes_value_->setText(e.notes);
    title_value_->setText(e.title);
    category_value_->setText(CategoryName(e.category_id));
    package_value_->setText(e.app_package_name);
    for (auto* label : {title_value_, username_value_, website_value_,
                        notes_value_, category_value_, package_value_}) {
        label->setAccessibleName(label->text());
    }
    header_edit_->setEnabled(!e.is_deleted && password_plain_.has_value());
    header_delete_->setEnabled(!e.is_deleted);
    header_favorite_->setEnabled(!e.is_deleted);
}

void DetailPanel::RefreshThemeAssets() {
    auto* theme = ThemeManager::Instance();
    if (auto* shadow =
            qobject_cast<QGraphicsDropShadowEffect*>(graphicsEffect())) {
        shadow->setColor(theme->Color(QStringLiteral("shadow")));
    }
    const auto icon = [theme](const QString& name, const QString& token,
                              int size) {
        return IconLoader::Load(name, theme->Color(token), size);
    };

    if (empty_icon_) {
        empty_icon_->setPixmap(
            icon(QStringLiteral("vault"), QStringLiteral("text-quaternary"),
                 48)
                .pixmap(48, 48));
    }
    if (header_edit_) {
        header_edit_->setIcon(icon(QStringLiteral("pencil"),
                                   QStringLiteral("text-secondary"),
                                   kIconSize));
    }
    if (header_delete_) {
        header_delete_->setIcon(icon(QStringLiteral("trash-2"),
                                     QStringLiteral("danger"), kIconSize));
    }
    if (header_close_) {
        header_close_->setIcon(icon(QStringLiteral("x"),
                                    QStringLiteral("text-tertiary"), kIconSize));
    }
    if (header_favorite_) {
        const QString token = entry_ && entry_->is_favorite
            ? QStringLiteral("warning")
            : QStringLiteral("text-quaternary");
        header_favorite_->setIcon(
            icon(QStringLiteral("star"), token, kHeaderIconSize));
    }
    if (risky_icon_) {
        risky_icon_->setPixmap(
            icon(QStringLiteral("shield-alert"), QStringLiteral("danger"), 20)
                .pixmap(20, 20));
    }
    if (username_copy_) {
        username_copy_->setIcon(icon(QStringLiteral("copy"),
                                     QStringLiteral("text-tertiary"),
                                     kIconSize));
    }
    if (password_copy_) {
        password_copy_->setIcon(icon(QStringLiteral("copy"),
                                     QStringLiteral("text-tertiary"),
                                     kIconSize));
    }
    if (website_open_) {
        website_open_->setIcon(icon(QStringLiteral("external-link"),
                                    QStringLiteral("text-tertiary"),
                                    kIconSize));
    }
    if (entry_) UpdatePasswordDisplay();
}

void DetailPanel::UpdatePasswordDisplay() {
    if (!entry_) return;
    if (!password_plain_.has_value() || password_plain_->isEmpty()) {
        password_value_->setText(password_plain_.has_value()
                                     ? QString()
                                     : QStringLiteral("(无法解密)"));
        password_copy_->setEnabled(false);
        password_toggle_->setEnabled(false);
        return;
    }
    password_copy_->setEnabled(true);
    password_toggle_->setEnabled(true);
    if (password_visible_) {
        password_value_->setText(*password_plain_);
        password_toggle_->setIcon(IconLoader::Load(
            QStringLiteral("eye-off"),
            ThemeManager::Instance()->Color(QStringLiteral("text-tertiary")),
            kIconSize));
        password_toggle_->setToolTip(QStringLiteral("隐藏密码"));
    } else {
        password_value_->setText(QString(password_plain_->length(),
                                          QChar(0x2022)));
        password_toggle_->setIcon(IconLoader::Load(
            QStringLiteral("eye"),
            ThemeManager::Instance()->Color(QStringLiteral("text-tertiary")),
            kIconSize));
        password_toggle_->setToolTip(QStringLiteral("显示密码"));
    }
}

QString DetailPanel::CategoryName(std::int64_t category_id) const {
    if (category_id == 0) return QStringLiteral("未分类");
    for (const auto& c : categories_) {
        if (c.id == category_id) return c.name;
    }
    return {};
}

QString DetailPanel::FormatTimestamp(std::int64_t ms_since_epoch) {
    if (ms_since_epoch <= 0) return QStringLiteral("—");
    const QDateTime dt = QDateTime::fromMSecsSinceEpoch(ms_since_epoch);
    return dt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QString DetailPanel::AvatarInitials(const model::PasswordEntry& entry) {
    const QString source =
        !entry.title.isEmpty() ? entry.title
                                : !entry.website.isEmpty() ? entry.website
                                                            : entry.username;
    if (source.isEmpty()) return QStringLiteral("?");
    const QChar first = source.at(0).toUpper();
    return QString(first);
}

}  // namespace passvault::ui
