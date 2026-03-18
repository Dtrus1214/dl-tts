#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QEvent>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

QT_BEGIN_NAMESPACE
class QTimer;
class QWidget;
class QLabel;
class QPlainTextEdit;
class QVBoxLayout;
class QHBoxLayout;
class QMouseEvent;
class QCloseEvent;
class QShowEvent;
class QHideEvent;
class QSystemTrayIcon;
class QMenu;
class QAction;
QT_END_NAMESPACE

class CustomButton;
class TtsEngine;
class PdfViewerForm;
class SettingsDialog;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
#if defined(Q_OS_WIN)
    bool nativeEvent(const QByteArray &eventType, void *message, long *result) override;
#endif

private slots:
    void showClipboardText();
    void onTtsPlay();
    void onTtsStop();
    void onTtsStateChanged(int state);
    void onSpeakerButtonClicked();
    void onSpeakerSelected(QAction *action);
    void playClipboardSelection();
    void toggleWindowVisibility();
    void openPdfViewer();
    void openSettingsDialog();
    void quitFromTray();
#if defined(Q_OS_WIN)
    void doCopyFromForeground();
    void updateLastForegroundWindow();
#endif

private:
    void setupUiDynamic();
    void setupWindowFrame();
    void setupTrayIcon();
    void loadAndApplySettings();
    void registerGlobalHotkey();
    void unregisterGlobalHotkey();
    void simulateCopy();
    bool isInTitleBar(const QPoint &globalPos) const;
    void setupSpeakerMenu();
    void updateSpeakerToolTip();

    QWidget *m_centralWidget = nullptr;
    QWidget *m_titleBar = nullptr;
    QLabel *m_labelTitle = nullptr;
    CustomButton *m_btnClose = nullptr;
    CustomButton *m_btnSettings = nullptr;
    QLabel *m_labelStatus = nullptr;

    TtsEngine *m_ttsEngine = nullptr;
    CustomButton *m_btnPlay = nullptr;
    CustomButton *m_btnStop = nullptr;
    CustomButton *m_btnSpeaker = nullptr;
    CustomButton *m_btnPdfViewer = nullptr;
    PdfViewerForm *m_pdfViewerForm = nullptr;
    QMenu *m_speakerMenu = nullptr;
    int m_currentSpeakerId = 0;

    QPoint m_dragPosition;
    bool m_dragging = false;

    QSystemTrayIcon *m_trayIcon = nullptr;
    QMenu *m_trayMenu = nullptr;
    QAction *m_showHideAction = nullptr;

    bool m_restoreMainWindowAfterPdf = false;

#if defined(Q_OS_WIN)
    bool m_playAfterCopy = false;
    static const int HOTKEY_ID = 1;
    bool m_hotkeyRegistered = false;
    HWND m_foregroundAtHotkey = 0;
    HWND m_lastKnownForeground = 0;
    QTimer *m_foregroundPollTimer = nullptr;
#endif
};

#endif // MAINWINDOW_H
