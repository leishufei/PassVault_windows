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
#include <QStyle>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include "ui/icon_loader.h"
#include "ui/elided_label.h"
#include "ui/theme_manager.h"

namespace passvault::ui {

namespace {

// Field actions share the value's line box so they sit level with the text.
constexpr int kIconButtonSize = 24;
constexpr int kValueLineHeight = kIconButtonSize;
constexpr int kIconSize = 16;
constexpr int kHeaderActionSize = 32;
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
    button->setAccessibleName(tooltip);
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
    // Full row width with centered text: a centered wrapping label would be
    // sized from its hint and could break a short line early.
    layout->addWidget(title);

    auto* subtitle = MakeLabel(QStringLiteral("侧边选中或新建一条即可开始"),
                               QStringLiteral("DetailEmptyText"));
    subtitle->setAlignment(Qt::AlignCenter);
    layout->addWidget(subtitle);

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
    outer->setContentsMargins(24, 24, 24, 24);
    outer->setSpacing(24);

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
    title_col->setSpacing(8);
    title_col->setAlignment(Qt::AlignTop);

    header_title_ = new ElidedLabel();
    header_title_->setObjectName(QStringLiteral("DetailTitle"));
    header_title_->setTextFormat(Qt::PlainText);
    title_col->addWidget(header_title_);
    header_tag_ = MakeLabel(QString(), QStringLiteral("DetailTag"));
    header_tag_->setWordWrap(false);
    header_tag_->setMaximumWidth(120);
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
    header_edit_->setFixedSize(kHeaderActionSize, kHeaderActionSize);
    header_edit_->setToolTip(QStringLiteral("编辑"));
    header_edit_->setIcon(IconLoader::Load(
        QStringLiteral("pencil"),
        ThemeManager::Instance()->Color(QStringLiteral("text-secondary")),
        kIconSize));
    header_edit_->setIconSize(QSize(kIconSize, kIconSize));
    connect(header_edit_, &QPushButton::clicked, this, [this]() {
        if (!entry_) return;
        if (entry_->is_deleted) emit RestoreRequested(entry_->id);
        else emit EditRequested(entry_->id);
    });
    action_layout->addWidget(header_edit_);

    category_button_ = MakeIconButton(QStringLiteral("folder"),
                                      QStringLiteral("更改分类"));
    category_button_->setObjectName(QStringLiteral("DetailCategoryButton"));
    connect(category_button_, &QToolButton::clicked, this, [this]() {
        if (entry_ && !entry_->is_deleted) emit CategoryChangeRequested(entry_->id);
    });
    action_layout->addWidget(category_button_);

    header_favorite_ = new QToolButton(actions);
    header_favorite_->setObjectName(QStringLiteral("DetailHeaderButton"));
    header_favorite_->setCheckable(true);
    header_favorite_->setCursor(Qt::PointingHandCursor);
    header_favorite_->setFixedSize(kHeaderActionSize, kHeaderActionSize);
    header_favorite_->setToolTip(QStringLiteral("收藏"));
    header_favorite_->setAccessibleName(QStringLiteral("收藏"));
    header_favorite_->setIcon(IconLoader::Load(
        QStringLiteral("star"),
        ThemeManager::Instance()->Color(QStringLiteral("text-quaternary")),
        kIconSize));
    header_favorite_->setIconSize(QSize(kIconSize, kIconSize));
    connect(header_favorite_, &QToolButton::toggled, this, [this](bool checked) {
        if (!entry_ || entry_->is_deleted) return;
        emit FavoriteToggleRequested(entry_->id, checked);
    });
    action_layout->addWidget(header_favorite_);

    header_delete_ = new QToolButton(actions);
    header_delete_->setObjectName(QStringLiteral("DetailHeaderDeleteButton"));
    header_delete_->setCursor(Qt::PointingHandCursor);
    header_delete_->setFixedSize(kHeaderActionSize, kHeaderActionSize);
    header_delete_->setToolTip(QStringLiteral("删除密码"));
    header_delete_->setAccessibleName(QStringLiteral("删除密码"));
    header_delete_->setIcon(IconLoader::Load(
        QStringLiteral("trash-2"),
        ThemeManager::Instance()->Color(QStringLiteral("danger")), kIconSize));
    header_delete_->setIconSize(QSize(kIconSize, kIconSize));
    connect(header_delete_, &QToolButton::clicked, this, [this]() {
        if (entry_) emit DeleteRequested(entry_->id);
    });
    action_layout->addWidget(header_delete_);
    header_close_ = MakeIconButton(QStringLiteral("x"), QStringLiteral("关闭详情"));
    header_close_->setObjectName(QStringLiteral("DetailCloseButton"));
    header_close_->setFixedSize(kHeaderActionSize, kHeaderActionSize);
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
    info_layout->setSpacing(16);

    auto build_row = [&](const QString& label_text, QLabel* value,
                         std::initializer_list<QToolButton**> buttons) {
        auto* row = new QWidget(info_card);
        row->setObjectName(QStringLiteral("DetailCardRow"));
        auto* col = new QVBoxLayout(row);
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(6);
        auto* label = MakeLabel(label_text, QStringLiteral("DetailFieldLabel"));
        col->addWidget(label);

        // Value and its actions form one line: the buttons are exactly the
        // height of the value's line box and aligned with its first line.
        auto* value_line = new QHBoxLayout();
        value_line->setContentsMargins(0, 0, 0, 0);
        value_line->setSpacing(4);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        auto value_policy = value->sizePolicy();
        value_policy.setHorizontalPolicy(QSizePolicy::Ignored);
        value->setSizePolicy(value_policy);
        value->setMinimumHeight(kValueLineHeight);
        value->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        value_line->addWidget(value, 1);
        for (QToolButton** btn_ptr : buttons) {
            value_line->addWidget(*btn_ptr, 0, Qt::AlignTop);
        }
        col->addLayout(value_line);
        info_layout->addWidget(row);
        return row;
    };

    username_copy_ = MakeIconButton(QStringLiteral("copy"),
                                    QStringLiteral("复制用户名"));
    connect(username_copy_, &QToolButton::clicked, this, [this]() {
        if (entry_) emit CopyUsernameRequested(entry_->id);
    });
    username_value_ = MakeLabel(QString(), QStringLiteral("DetailFieldValue"));
    username_value_->setObjectName(QStringLiteral("DetailFieldValue"));
    username_row_ =
        build_row(QStringLiteral("用户名"), username_value_, {&username_copy_});

    username_divider_ = MakeDivider();
    info_layout->addWidget(username_divider_);

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

    website_divider_ = MakeDivider();
    info_layout->addWidget(website_divider_);

    website_open_ = MakeIconButton(QStringLiteral("external-link"),
                                   QStringLiteral("打开网站"));
    connect(website_open_, &QToolButton::clicked, this, [this]() {
        if (entry_) emit OpenWebsiteRequested(entry_->id);
    });
    website_value_ = MakeLabel(QString(), QStringLiteral("DetailFieldValueLink"));
    website_value_->setObjectName(QStringLiteral("DetailFieldValueLink"));
    website_copy_ = MakeIconButton(QStringLiteral("copy"),
                                   QStringLiteral("复制网址"));
    connect(website_copy_, &QToolButton::clicked, this, [this]() {
        if (entry_) emit CopyWebsiteRequested(entry_->id);
    });
    website_row_ = build_row(QStringLiteral("网址"), website_value_,
                             {&website_open_, &website_copy_});
    website_value_->setTextFormat(Qt::RichText);
    website_value_->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
    connect(website_value_, &QLabel::linkActivated, this, [this](const QString&) {
        if (entry_) emit OpenWebsiteRequested(entry_->id);
    });

    notes_divider_ = MakeDivider();
    info_layout->addWidget(notes_divider_);
    notes_value_ = MakeLabel(QString(), QStringLiteral("DetailNotesValue"));
    notes_row_ = build_row(QStringLiteral("备注"), notes_value_, {});

    outer->addWidget(info_card);

    auto* stamp_card = new QFrame(page);
    stamp_card->setObjectName(QStringLiteral("DetailStampCard"));
    stamp_card->setFrameShape(QFrame::NoFrame);
    auto* stamp_layout = new QVBoxLayout(stamp_card);
    stamp_layout->setContentsMargins(0, 0, 0, 0);
    stamp_layout->setSpacing(12);

    auto make_stamp_row = [&](const QString& label_text, QLabel** value_ptr) {
        auto* row = new QHBoxLayout();
        row->setContentsMargins(0, 0, 0, 0);
        auto* label = MakeLabel(label_text, QStringLiteral("DetailStampLabel"));
        label->setWordWrap(false);
        row->addWidget(label);
        row->addStretch(1);
        auto* value = MakeLabel(QString(), QStringLiteral("DetailStampValue"));
        // Timestamps are single-line metadata; never split date and time.
        value->setWordWrap(false);
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
    if (!entry_ || entry_->uuid != entry.uuid || entry_->id != entry.id) {
        password_visible_ = default_password_visible_;
    }
    entry_ = entry;
    password_plain_ = decrypted_password;
    stack_->setCurrentWidget(content_page_);
    RefreshView();
}

void DetailPanel::SetFavorite(bool favorite) {
    if (!entry_) return;
    entry_->is_favorite = favorite;
    RefreshView();
}

void DetailPanel::SetDefaultPasswordVisible(bool visible) {
    default_password_visible_ = visible;
    password_visible_ = visible;
    UpdatePasswordDisplay();
}

void DetailPanel::ClearEntry() {
    entry_.reset();
    password_plain_.reset();
    header_title_->SetFullText(QString());
    for (auto* label : {username_value_, password_value_, website_value_,
                        notes_value_}) {
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
    header_icon_->setProperty(
        "colorIndex", static_cast<unsigned int>(e.icon_color) % 6);
    header_icon_->style()->unpolish(header_icon_);
    header_icon_->style()->polish(header_icon_);
    header_title_->SetFullText(e.title.isEmpty() ? QStringLiteral("(无标题)")
                                                 : e.title);
    const QString tag = CategoryName(e.category_id);
    header_tag_->setText(tag);
    header_tag_->ensurePolished();
    header_tag_->setMinimumWidth(
        qMin(120, header_tag_->fontMetrics().horizontalAdvance(tag) + 16));
    header_tag_->show();

    QSignalBlocker blocker(header_favorite_);
    header_favorite_->setChecked(e.is_favorite);
    const QColor star_color = e.is_favorite
        ? ThemeManager::Instance()->Color(QStringLiteral("warning"))
        : ThemeManager::Instance()->Color(QStringLiteral("text-quaternary"));
    header_favorite_->setIcon(
        IconLoader::Load(QStringLiteral("star"), star_color, kIconSize));

    risky_banner_->setVisible(false);

    const bool has_username = !e.username.isEmpty();
    username_value_->setText(e.username);
    username_copy_->setEnabled(has_username);
    if (username_row_) username_row_->show();
    if (username_divider_) username_divider_->show();

    UpdatePasswordDisplay();

    const bool has_website = !e.website.isEmpty();
    website_value_->setText(e.website.isEmpty() ? QString() :
        QStringLiteral("<a href=\"website\" style=\"color:%1;text-decoration:none\">%2</a>")
            .arg(ThemeManager::Instance()->ColorHex(QStringLiteral("primary")),
                 e.website.toHtmlEscaped()));
    website_open_->setEnabled(has_website);
    website_copy_->setEnabled(has_website);
    if (website_row_) website_row_->show();
    if (website_divider_) website_divider_->show();

    created_value_->setText(FormatTimestamp(e.created_at));
    updated_value_->setText(FormatTimestamp(e.ContentUpdatedAt()));
    notes_value_->setText(e.notes);
    if (notes_row_) notes_row_->show();
    if (notes_divider_) notes_divider_->show();
    website_value_->setAccessibleName(e.website);
    for (auto* label : {username_value_, notes_value_}) {
        label->setAccessibleName(label->text());
    }
    header_edit_->setEnabled(e.is_deleted || password_plain_.has_value());
    header_edit_->setFixedSize(kHeaderActionSize, kHeaderActionSize);
    header_edit_->setText(QString());
    header_edit_->setIcon(IconLoader::Load(
        e.is_deleted ? QStringLiteral("rotate-ccw") : QStringLiteral("pencil"),
        ThemeManager::Instance()->Color(QStringLiteral("text-secondary")),
        kIconSize));
    category_button_->hide();
    category_button_->setEnabled(!e.is_deleted);
    category_button_->setIcon(IconLoader::Load(
        QStringLiteral("folder"),
        ThemeManager::Instance()->Color(QStringLiteral("text-secondary")),
        kIconSize));
    header_edit_->setToolTip(e.is_deleted ? QStringLiteral("还原")
                                          : QStringLiteral("编辑"));
    header_edit_->setAccessibleName(e.is_deleted ? QStringLiteral("还原")
                                                : QStringLiteral("编辑"));
    header_edit_->hide();
    header_delete_->hide();
    header_delete_->setEnabled(true);
    header_delete_->setToolTip(e.is_deleted ? QStringLiteral("永久删除")
                                           : QStringLiteral("删除密码"));
    header_delete_->setAccessibleName(header_delete_->toolTip());
    header_favorite_->hide();
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
            icon(QStringLiteral("star"), token, kIconSize));
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
    if (password_toggle_) {
        password_toggle_->setIcon(icon(
            password_visible_ ? QStringLiteral("eye-off")
                              : QStringLiteral("eye"),
            QStringLiteral("text-tertiary"), kIconSize));
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
    if (website_copy_) {
        website_copy_->setIcon(icon(QStringLiteral("copy"),
                                    QStringLiteral("text-tertiary"),
                                    kIconSize));
    }
    if (entry_) RefreshView();
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
