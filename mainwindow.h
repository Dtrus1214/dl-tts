#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

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
#if defined(Q_OS_WIN)
    bool nativeEvent(const QByteArray &eventType, void *message, long *result) override;
#endif

private slots:
    void onGetSelection();
    void showClipboardText();
    void onTtsPlay();
    void onTtsPause();
    void onTtsStop();
    void onTtsStateChanged(int state);
    void toggleWindowVisibility();
    void quitFromTray();
#if defined(Q_OS_WIN)
    void doCopyFromForeground();
    void updateLastForegroundWindow();
#endif

private:
    void setupUiDynamic();
    void setupWindowFrame();
    void setupTrayIcon();
    void registerGlobalHotkey();
    void unregisterGlobalHotkey();
    void simulateCopy();
    bool isInTitleBar(const QPoint &globalPos) const;

    QWidget *m_centralWidget = nullptr;
    QWidget *m_titleBar = nullptr;
    QLabel *m_labelTitle = nullptr;
    CustomButton *m_btnClose = nullptr;
    QLabel *m_labelHint = nullptr;
    CustomButton *m_btnGetSelection = nullptr;
    QLabel *m_labelSelected = nullptr;
    QPlainTextEdit *m_textSelected = nullptr;
    QLabel *m_labelStatus = nullptr;

    TtsEngine *m_ttsEngine = nullptr;
    CustomButton *m_btnPlay = nullptr;
    CustomButton *m_btnPause = nullptr;
    CustomButton *m_btnStop = nullptr;

    QPoint m_dragPosition;
    bool m_dragging = false;

    QSystemTrayIcon *m_trayIcon = nullptr;
    QMenu *m_trayMenu = nullptr;
    QAction *m_showHideAction = nullptr;

#if defined(Q_OS_WIN)
    static const int HOTKEY_ID = 1;
    bool m_hotkeyRegistered = false;
    HWND m_foregroundAtHotkey = 0;
    HWND m_lastKnownForeground = 0;
    QTimer *m_foregroundPollTimer = nullptr;
#endif
};

#endif // MAINWINDOW_H
