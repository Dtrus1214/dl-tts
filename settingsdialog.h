#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>

QT_BEGIN_NAMESPACE
class QListWidget;
class QStackedWidget;
class QComboBox;
class QSlider;
class QLabel;
class QButtonGroup;
class QLineEdit;
QT_END_NAMESPACE

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog() override = default;

signals:
    void settingsApplied();

private slots:
    void apply();
    void accept() override;
    void onSpeedChanged(int value);
    void checkLicenseDraft();

private:
    void buildUi();
    void loadFromSettings();
    void saveToSettings() const;

    QListWidget *m_nav = nullptr;
    QStackedWidget *m_stack = nullptr;

    // Speaker (draft)
    QComboBox *m_comboSpeaker = nullptr;

    // Speed
    QSlider *m_speedSlider = nullptr;
    QLabel *m_speedValueLabel = nullptr;

    // Repeat
    QButtonGroup *m_repeatGroup = nullptr;

    // Pause
    QSlider *m_pauseSentenceMs = nullptr;
    QLabel *m_pauseSentenceLabel = nullptr;
    QSlider *m_pauseParagraphMs = nullptr;
    QLabel *m_pauseParagraphLabel = nullptr;

    // License (draft)
    QLineEdit *m_licenseEdit = nullptr;
};

#endif // SETTINGSDIALOG_H
