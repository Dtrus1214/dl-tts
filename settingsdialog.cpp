#include "settingsdialog.h"

#include <QListWidget>
#include <QStackedWidget>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QComboBox>
#include <QListView>
#include <QSlider>
#include <QLabel>
#include <QGroupBox>
#include <QRadioButton>
#include <QButtonGroup>
#include <QLineEdit>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QSettings>
#include <QMessageBox>
#include <QStyledItemDelegate>

static constexpr const char *kSettingsGroup = "settings";
static constexpr const char *kSpeakerIdKey = "speakerId";
static constexpr const char *kTtsSpeedKey = "ttsSpeedPercent";
static constexpr const char *kRepeatModeKey = "repeatMode";
static constexpr const char *kPauseSentenceKey = "pauseSentenceMs";
static constexpr const char *kPauseParagraphKey = "pauseParagraphMs";
static constexpr const char *kLicenseKey = "licenseKey";

class FixedHeightItemDelegate final : public QStyledItemDelegate
{
public:
    explicit FixedHeightItemDelegate(int heightPx, QObject *parent = nullptr)
        : QStyledItemDelegate(parent)
        , m_heightPx(heightPx)
    {
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize s = QStyledItemDelegate::sizeHint(option, index);
        s.setHeight(qMax(s.height(), m_heightPx));
        return s;
    }

private:
    int m_heightPx = 24;
};

// repeatMode: 1,2,3 or 0=forever
static int repeatModeToValue(const QButtonGroup *g)
{
    if (!g)
        return 1;
    const int id = g->checkedId();
    if (id == 0)
        return 0;
    if (id == 2 || id == 3)
        return id;
    return 1;
}

static void setRepeatModeFromValue(QButtonGroup *g, int v)
{
    if (!g)
        return;
    if (v == 0) {
        if (QAbstractButton *b = g->button(0))
            b->setChecked(true);
        return;
    }
    if (QAbstractButton *b = g->button(v))
        b->setChecked(true);
    else if (QAbstractButton *b1 = g->button(1))
        b1->setChecked(true);
}

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Settings"));
    setModal(true);
    resize(600, 400);

    buildUi();
    loadFromSettings();
}

void SettingsDialog::buildUi()
{
    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(12);

    // Crystal-style palette to match MainWindow
    setStyleSheet(QStringLiteral(R"(
        QDialog {
            background-color: #ffffff;
            font-size: 13px;
        }

        QListWidget#settingsNav {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                              stop:0 #e4f3ff,
                                              stop:1 #c0ddff);
            border: 1px solid #d0e4ff;
            border-radius: 12px;
            padding: 6px;
            outline: 0;
        }
        QListWidget#settingsNav::item {
            color: #1f3b5e;
            padding: 12px 10px;
            margin: 2px 0px;
            border-radius: 10px;
        }
        QListWidget#settingsNav::item:selected {
            background: #ffffff;
            border: 1px solid #bfe0ff;
            color: #1f3b5e;
            font-weight: 600;
        }
        QListWidget#settingsNav::item:hover:!selected {
            background: rgba(255, 255, 255, 140);
        }

        QStackedWidget {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                              stop:0 #f9fcff,
                                              stop:1 #e4f1ff);
            border: 1px solid #d0e4ff;
            border-radius: 12px;
        }

        QLabel {
            color: #1f3b5e;
        }

        QGroupBox {
            border: 1px solid #d0e4ff;
            border-radius: 10px;
            margin-top: 10px;
            padding: 10px;
            background: rgba(255, 255, 255, 160);
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            padding: 0 6px;
            color: #1f3b5e;
            font-weight: 600;
        }

        QLineEdit {
            background: #ffffff;
            color: #123456;
            border: 1px solid #d0e4ff;
            border-radius: 8px;
            padding: 6px 10px;
            min-height: 20px;
        }
        QComboBox {
            background: #ffffff;
            color: #123456;
            border: 1px solid #d0e4ff;
            border-radius: 8px;
            padding: 6px 10px;
            min-height: 20px;
        }
        QLineEdit:focus, QComboBox:focus {
            border: 1px solid #63a9ff;
        }

        QComboBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: top right;
            width: 30px;
            border-left: 1px solid #d0e4ff;
            border-top-right-radius: 8px;
            border-bottom-right-radius: 8px;
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #f9fcff, stop:1 #e4f1ff);
        }
        QComboBox::down-arrow {
            width: 12px;
            height: 12px;
            image: url(:/icons/chevron-down.svg);
        }
        QComboBox QAbstractItemView {
            background: #ffffff;
            border: 1px solid #d0e4ff;
            border-radius: 10px;
            padding: 4px 0;
            selection-background-color: #c9dbff;
            selection-color: #1f3b5e;
            outline: 0;
        }
        QComboBox QAbstractItemView::item {
            min-height: 20px;
            padding: 4px 10px;
        }

        QRadioButton {
            color: #1f3b5e;
            spacing: 10px;
            padding: 2px 0;
        }
        QRadioButton::indicator {
            width: 18px;
            height: 18px;
            image: url(:/icons/radio-off.svg);
        }
        QRadioButton::indicator:checked {
            image: url(:/icons/radio-on.svg);
        }
        QRadioButton::indicator:disabled {
            image: url(:/icons/radio-off.svg);
            opacity: 0.55;
        }

        QSlider::groove:horizontal {
            height: 8px;
            border-radius: 4px;
            background: rgba(31, 59, 94, 35);
        }
        QSlider::sub-page:horizontal {
            border-radius: 4px;
            background: #63a9ff;
        }
        QSlider::handle:horizontal {
            width: 16px;
            margin: -7px 0;
            border-radius: 8px;
            background: #ffffff;
            border: 1px solid #bfe0ff;
        }

        QPushButton {
            background: #ffffff;
            border: 1px solid #d0e4ff;
            border-radius: 10px;
            padding: 9px 14px;
            color: #1f3b5e;
            font-weight: 600;
        }
        QPushButton:hover {
            background: #f2f8ff;
        }
        QPushButton:pressed {
            background: #e4f1ff;
        }
    )"));

    QHBoxLayout *center = new QHBoxLayout();
    center->setContentsMargins(0, 0, 0, 0);
    center->setSpacing(10);

    m_nav = new QListWidget(this);
    m_nav->setObjectName(QStringLiteral("settingsNav"));
    m_nav->setSelectionMode(QAbstractItemView::SingleSelection);
    m_nav->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_nav->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_nav->setFixedWidth(165);

    m_stack = new QStackedWidget(this);

    center->addWidget(m_nav);
    center->addWidget(m_stack, 1);
    root->addLayout(center, 1);

    connect(m_nav, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);

    auto addPage = [this](QWidget *page, const QString &title) {
        m_stack->addWidget(page);
        m_nav->addItem(title);
    };

    // ---- Speaker (draft) ----
    {
        QWidget *page = new QWidget(m_stack);
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(12, 12, 12, 12);
        v->setSpacing(10);

        QLabel *hint = new QLabel(tr("Speaker selection is a draft UI (engine integration pending)."), page);
        hint->setWordWrap(true);
        v->addWidget(hint);
        v->addSpacing(10);

        QFormLayout *form = new QFormLayout();
        m_comboSpeaker = new QComboBox(page);
        {
            // Make dropdown row height match control height (QSS alone is not reliable here).
            auto *view = new QListView(m_comboSpeaker);
            view->setUniformItemSizes(true);
            view->setSpacing(2);
            view->setFrameShape(QFrame::NoFrame);
            view->setItemDelegate(new FixedHeightItemDelegate(24, view));
            view->setStyleSheet(QStringLiteral(R"(
                QListView {
                    background: #ffffff;
                    border: 1px solid #d0e4ff;
                    border-radius: 10px;
                    padding: 4px 0;
                    outline: 0;
                }
                QListView::item {
                    padding: 6px 10px;
                    color: #1f3b5e;
                }
                QListView::item:selected {
                    background: #c9dbff;
                    color: #1f3b5e;
                }
            )"));
            m_comboSpeaker->setView(view);
        }
        m_comboSpeaker->addItems({QStringLiteral("man1"), QStringLiteral("man2"),
                                  QStringLiteral("woman1"), QStringLiteral("woman2")});
        form->addRow(tr("Speaker"), m_comboSpeaker);
        v->addLayout(form);
        v->addStretch(1);

        addPage(page, tr("Speaker"));
    }

    // ---- TTS speed ----
    {
        QWidget *page = new QWidget(m_stack);
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(12, 12, 12, 12);
        v->setSpacing(10);

        QLabel *hint = new QLabel(tr("TTS speed changes how fast audio is generated."), page);
        hint->setWordWrap(true);
        v->addWidget(hint);
        v->addSpacing(10);

        QHBoxLayout *row = new QHBoxLayout();
        QLabel *lbl = new QLabel(tr("Speed"), page);
        m_speedSlider = new QSlider(Qt::Horizontal, page);
        m_speedSlider->setRange(50, 200); // percent
        m_speedSlider->setSingleStep(5);
        m_speedSlider->setPageStep(10);
        m_speedValueLabel = new QLabel(page);
        m_speedValueLabel->setMinimumWidth(70);
        m_speedValueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget(lbl);
        row->addWidget(m_speedSlider, 1);
        row->addWidget(m_speedValueLabel);
        v->addLayout(row);
        v->addStretch(1);

        connect(m_speedSlider, &QSlider::valueChanged, this, &SettingsDialog::onSpeedChanged);

        addPage(page, tr("Speed"));
    }

    // ---- Repeat ----
    {
        QWidget *page = new QWidget(m_stack);
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(12, 12, 12, 12);
        v->setSpacing(10);

        QLabel *hint = new QLabel(tr("Repeat options are stored (playback integration can be added next)."), page);
        hint->setWordWrap(true);
        v->addWidget(hint);
        v->addSpacing(10);

        QGroupBox *box = new QGroupBox(tr("Repeat"), page);
        QVBoxLayout *bv = new QVBoxLayout(box);
        m_repeatGroup = new QButtonGroup(box);
        m_repeatGroup->setExclusive(true);

        auto *r1 = new QRadioButton(tr("1 time"), box);
        auto *r2 = new QRadioButton(tr("2 times"), box);
        auto *r3 = new QRadioButton(tr("3 times"), box);
        auto *rf = new QRadioButton(tr("Forever"), box);
        m_repeatGroup->addButton(r1, 1);
        m_repeatGroup->addButton(r2, 2);
        m_repeatGroup->addButton(r3, 3);
        m_repeatGroup->addButton(rf, 0);
        bv->addWidget(r1);
        bv->addWidget(r2);
        bv->addWidget(r3);
        bv->addWidget(rf);
        v->addWidget(box);
        v->addStretch(1);

        addPage(page, tr("Repeat"));
    }

    // ---- Pause ----
    {
        QWidget *page = new QWidget(m_stack);
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(12, 12, 12, 12);
        v->setSpacing(10);

        QLabel *hint = new QLabel(tr("Pause values are stored (sentence/paragraph splitting can be added next)."), page);
        hint->setWordWrap(true);
        v->addWidget(hint);
        v->addSpacing(10);

        auto pauseRow = [page](const QString &labelText, QSlider **sliderOut, QLabel **valueOut,
                               int minMs, int maxMs, int stepMs) {
            QHBoxLayout *row = new QHBoxLayout();
            QLabel *lbl = new QLabel(labelText, page);
            QSlider *sl = new QSlider(Qt::Horizontal, page);
            sl->setRange(minMs, maxMs);
            sl->setSingleStep(stepMs);
            sl->setPageStep(stepMs * 2);
            QLabel *val = new QLabel(page);
            val->setMinimumWidth(70);
            val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            row->addWidget(lbl);
            row->addWidget(sl, 1);
            row->addWidget(val);
            *sliderOut = sl;
            *valueOut = val;
            return row;
        };

        QVBoxLayout *pauseLayout = new QVBoxLayout();
        pauseLayout->setSpacing(10);
        pauseLayout->addLayout(pauseRow(tr("Between sentences"), &m_pauseSentenceMs, &m_pauseSentenceLabel, 0, 5000, 50));
        pauseLayout->addSpacing(8);
        pauseLayout->addLayout(pauseRow(tr("Between paragraphs"), &m_pauseParagraphMs, &m_pauseParagraphLabel, 0, 10000, 100));
        v->addLayout(pauseLayout);

        connect(m_pauseSentenceMs, &QSlider::valueChanged, this, [this](int ms) {
            if (m_pauseSentenceLabel)
                m_pauseSentenceLabel->setText(QStringLiteral("%1 ms").arg(ms));
        });
        connect(m_pauseParagraphMs, &QSlider::valueChanged, this, [this](int ms) {
            if (m_pauseParagraphLabel)
                m_pauseParagraphLabel->setText(QStringLiteral("%1 ms").arg(ms));
        });
        v->addStretch(1);

        addPage(page, tr("Pause"));
    }

    // ---- License (draft) ----
    {
        QWidget *page = new QWidget(m_stack);
        QVBoxLayout *v = new QVBoxLayout(page);
        v->setContentsMargins(12, 12, 12, 12);
        v->setSpacing(10);

        QLabel *hint = new QLabel(tr("License check is a draft UI (server/validation integration pending)."), page);
        hint->setWordWrap(true);
        v->addWidget(hint);
        v->addSpacing(10);

        QHBoxLayout *row = new QHBoxLayout();
        m_licenseEdit = new QLineEdit(page);
        m_licenseEdit->setPlaceholderText(tr("Enter license key"));
        QPushButton *btnCheck = new QPushButton(tr("Check"), page);
        row->addWidget(m_licenseEdit, 1);
        row->addWidget(btnCheck);
        v->addLayout(row);
        v->addStretch(1);

        connect(btnCheck, &QPushButton::clicked, this, &SettingsDialog::checkLicenseDraft);

        addPage(page, tr("License"));
    }

    if (m_nav->count() > 0)
        m_nav->setCurrentRow(0);

    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, &SettingsDialog::apply);
}

void SettingsDialog::loadFromSettings()
{
    QSettings s;
    s.beginGroup(QLatin1String(kSettingsGroup));

    const int speakerId = s.value(QLatin1String(kSpeakerIdKey), 0).toInt();
    if (m_comboSpeaker) {
        const int idx = qBound(0, speakerId, m_comboSpeaker->count() - 1);
        m_comboSpeaker->setCurrentIndex(idx);
    }

    const int speedPct = s.value(QLatin1String(kTtsSpeedKey), 100).toInt();
    if (m_speedSlider)
        m_speedSlider->setValue(qBound(50, speedPct, 200));

    const int repeatMode = s.value(QLatin1String(kRepeatModeKey), 1).toInt();
    setRepeatModeFromValue(m_repeatGroup, repeatMode);

    if (m_pauseSentenceMs)
        m_pauseSentenceMs->setValue(qBound(0, s.value(QLatin1String(kPauseSentenceKey), 150).toInt(), 5000));
    if (m_pauseParagraphMs)
        m_pauseParagraphMs->setValue(qBound(0, s.value(QLatin1String(kPauseParagraphKey), 400).toInt(), 10000));

    if (m_licenseEdit)
        m_licenseEdit->setText(s.value(QLatin1String(kLicenseKey), QString()).toString());

    s.endGroup();
}

void SettingsDialog::saveToSettings() const
{
    QSettings s;
    s.beginGroup(QLatin1String(kSettingsGroup));

    if (m_comboSpeaker)
        s.setValue(QLatin1String(kSpeakerIdKey), m_comboSpeaker->currentIndex());
    if (m_speedSlider)
        s.setValue(QLatin1String(kTtsSpeedKey), m_speedSlider->value());
    if (m_repeatGroup)
        s.setValue(QLatin1String(kRepeatModeKey), repeatModeToValue(m_repeatGroup));
    if (m_pauseSentenceMs)
        s.setValue(QLatin1String(kPauseSentenceKey), m_pauseSentenceMs->value());
    if (m_pauseParagraphMs)
        s.setValue(QLatin1String(kPauseParagraphKey), m_pauseParagraphMs->value());
    if (m_licenseEdit)
        s.setValue(QLatin1String(kLicenseKey), m_licenseEdit->text().trimmed());

    s.endGroup();
    s.sync();
}

void SettingsDialog::apply()
{
    saveToSettings();
    emit settingsApplied();
}

void SettingsDialog::accept()
{
    apply();
    QDialog::accept();
}

void SettingsDialog::onSpeedChanged(int value)
{
    if (!m_speedValueLabel)
        return;
    m_speedValueLabel->setText(QStringLiteral("%1%").arg(value));
}

void SettingsDialog::checkLicenseDraft()
{
    const QString key = m_licenseEdit ? m_licenseEdit->text().trimmed() : QString();
    if (key.isEmpty()) {
        QMessageBox::warning(this, tr("License"), tr("Please enter a license key."));
        return;
    }
    QMessageBox::information(this, tr("License"), tr("Draft: key stored locally.\nValidation can be implemented next."));
}

