#include "mainwindow.h"
#include "custombutton.h"
#include "tts/ttsengine.h"

#include <QMouseEvent>
#include <QClipboard>
#include <QApplication>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSpacerItem>
#include <QSizePolicy>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QCloseEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QStyle>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#if defined(Q_OS_UNIX) && !defined(Q_OS_MAC)
#include <QGuiApplication>
#include <QWindow>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QtGui/qnativeinterface.h>
#endif
#include <xcb/xcb.h>
#include <xcb/xtest.h>
#include <xcb/xcb_keysyms.h>
/* Keysym values (avoid X11/keysymdef.h dependency) */
#ifndef XK_Control_L
#define XK_Control_L 0xffe3
#endif
#ifndef XK_c
#define XK_c         0x0063
#endif
#endif

static const int TITLE_BAR_HEIGHT = 40;
static const int WINDOW_RADIUS = 10;
static const int CONTENT_PADDING = 14;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUiDynamic();
    setupWindowFrame();

    connect(m_btnGetSelection, &QPushButton::clicked, this, &MainWindow::onGetSelection);
    connect(m_btnClose, &QPushButton::clicked, this, &QWidget::hide);

    m_ttsEngine = new TtsEngine(this);
    m_ttsEngine->initialize();
    connect(m_ttsEngine, &TtsEngine::stateChanged, this, &MainWindow::onTtsStateChanged);
    connect(m_btnPlay, &QPushButton::clicked, this, &MainWindow::onTtsPlay);
    connect(m_btnPause, &QPushButton::clicked, this, &MainWindow::onTtsPause);
    connect(m_btnStop, &QPushButton::clicked, this, &MainWindow::onTtsStop);
    onTtsStateChanged(m_ttsEngine->state());

    setupTrayIcon();
    registerGlobalHotkey();
#if defined(Q_OS_WIN)
    m_foregroundPollTimer = new QTimer(this);
    connect(m_foregroundPollTimer, &QTimer::timeout, this, &MainWindow::updateLastForegroundWindow);
    m_foregroundPollTimer->start(300);
#endif
}

MainWindow::~MainWindow()
{
    unregisterGlobalHotkey();
}

void MainWindow::setupUiDynamic()
{
    m_centralWidget = new QWidget(this);
    m_centralWidget->setObjectName("centralWidget");
    setCentralWidget(m_centralWidget);

    QVBoxLayout *rootLayout = new QVBoxLayout(m_centralWidget);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // ---- Title bar (draggable region) ----
    m_titleBar = new QWidget(m_centralWidget);
    m_titleBar->setObjectName("titleBar");
    m_titleBar->setFixedHeight(TITLE_BAR_HEIGHT);
    m_titleBar->setCursor(Qt::SizeAllCursor);

    QHBoxLayout *titleLayout = new QHBoxLayout(m_titleBar);
    titleLayout->setContentsMargins(CONTENT_PADDING, 0, 8, 0);
    titleLayout->setSpacing(8);

    m_labelTitle = new QLabel(tr("Selection"), m_titleBar);
    m_labelTitle->setObjectName("labelTitle");
    titleLayout->addWidget(m_labelTitle);

    titleLayout->addStretch();

    m_btnClose = new CustomButton(CustomButton::TitleBar, m_titleBar);
    m_btnClose->setObjectName("btnClose");
    m_btnClose->setText(QString());
    m_btnClose->setFixedSize(28, 28);
    titleLayout->addWidget(m_btnClose, 0, Qt::AlignVCenter);

    rootLayout->addWidget(m_titleBar);

    // ---- Content ----
    QWidget *content = new QWidget(m_centralWidget);
    content->setObjectName("content");
    QVBoxLayout *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(CONTENT_PADDING, 12, CONTENT_PADDING, CONTENT_PADDING);
    contentLayout->setSpacing(10);

    m_labelHint = new QLabel(
        tr("Select text in any app (Notepad, browser…), then press Ctrl+Shift+S or click below."),
        content);
    m_labelHint->setObjectName("labelHint");
    m_labelHint->setWordWrap(true);
    m_labelHint->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    contentLayout->addWidget(m_labelHint);

    m_btnGetSelection = new CustomButton(tr("Get selection (Ctrl+Shift+S)"), CustomButton::Primary, content);
    m_btnGetSelection->setObjectName("btnGetSelection");
    m_btnGetSelection->setMinimumHeight(36);
    contentLayout->addWidget(m_btnGetSelection);

    m_labelSelected = new QLabel(tr("Selected text:"), content);
    m_labelSelected->setObjectName("labelSelected");
    contentLayout->addWidget(m_labelSelected);

    m_textSelected = new QPlainTextEdit(content);
    m_textSelected->setObjectName("textSelected");
    m_textSelected->setReadOnly(true);
    m_textSelected->setMaximumBlockCount(100);
    m_textSelected->setPlaceholderText(tr("(none yet)"));
    m_textSelected->setMinimumHeight(60);
    contentLayout->addWidget(m_textSelected);

    QHBoxLayout *ttsLayout = new QHBoxLayout();
    ttsLayout->setSpacing(8);
    m_btnPlay = new CustomButton(tr("Play"), CustomButton::Primary, content);
    m_btnPlay->setObjectName("btnTtsPlay");
    m_btnPause = new CustomButton(tr("Pause"), CustomButton::Secondary, content);
    m_btnPause->setObjectName("btnTtsPause");
    m_btnStop = new CustomButton(tr("Stop"), CustomButton::Secondary, content);
    m_btnStop->setObjectName("btnTtsStop");
    ttsLayout->addWidget(m_btnPlay);
    ttsLayout->addWidget(m_btnPause);
    ttsLayout->addWidget(m_btnStop);
    contentLayout->addLayout(ttsLayout);

    m_labelStatus = new QLabel(content);
    m_labelStatus->setObjectName("labelStatus");
    contentLayout->addWidget(m_labelStatus);

    rootLayout->addWidget(content);
}

void MainWindow::setupWindowFrame()
{
    setWindowTitle(tr("Selection"));
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground); // so drop shadow is visible

    const int w = 380;
    const int h = 300;
    setFixedSize(w, h);
    setMinimumSize(340, 240);

    // Production-style palette: dark theme with clear hierarchy
    const char *sheet = R"(
        QMainWindow {
            background-color: transparent;
        }
        QWidget#centralWidget {
            background-color: #252526;
            border: 1px solid #3c3c3c;
            border-radius: %1px;
        }
        QWidget#titleBar {
            background-color: #2d2d30;
            border-top-left-radius: %1px;
            border-top-right-radius: %1px;
        }
        QWidget#content {
            background-color: #252526;
            border-bottom-left-radius: %1px;
            border-bottom-right-radius: %1px;
        }
        QLabel#labelTitle {
            color: #e0e0e0;
            font-size: 13px;
            font-weight: 600;
        }
        QLabel#labelHint, QLabel#labelSelected {
            color: #cccccc;
            font-size: 12px;
        }
        QLabel#labelStatus {
            color: #858585;
            font-size: 11px;
        }
        QPlainTextEdit#textSelected {
            background-color: #1e1e1e;
            color: #e0e0e0;
            border: 1px solid #3c3c3c;
            border-radius: 6px;
            padding: 8px;
            font-size: 12px;
            selection-background-color: #264f78;
        }
    )";
    setStyleSheet(QString::fromUtf8(sheet).arg(WINDOW_RADIUS));

    // Optional: subtle shadow for floating effect (Qt 5.14)
    QGraphicsDropShadowEffect *shadow = new QGraphicsDropShadowEffect(m_centralWidget);
    shadow->setBlurRadius(20);
    shadow->setColor(QColor(0, 0, 0, 80));
    shadow->setOffset(0, 4);
    m_centralWidget->setGraphicsEffect(shadow);
}

bool MainWindow::isInTitleBar(const QPoint &globalPos) const
{
    if (!m_titleBar || !m_centralWidget)
        return false;
    QPoint local = m_centralWidget->mapFromGlobal(globalPos);
    return m_titleBar->geometry().contains(local);
}

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && isInTitleBar(event->globalPos())) {
        m_dragPosition = event->globalPos() - frameGeometry().topLeft();
        m_dragging = true;
        event->accept();
    }
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPos() - m_dragPosition);
        event->accept();
    } else {
        m_dragging = false;
    }
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        m_dragging = false;
    QMainWindow::mouseReleaseEvent(event);
}

void MainWindow::setupTrayIcon()
{
    m_trayMenu = new QMenu(this);
    m_showHideAction = m_trayMenu->addAction(tr("Show CrystalTts"), this, &MainWindow::toggleWindowVisibility);
    m_trayMenu->addAction(tr("Quit"), this, &MainWindow::quitFromTray);

    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setContextMenu(m_trayMenu);
    m_trayIcon->setIcon(style()->standardIcon(QStyle::SP_MediaVolume));
    m_trayIcon->setToolTip(tr("CrystalTts - Text selection for TTS"));
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick || reason == QSystemTrayIcon::Trigger)
            toggleWindowVisibility();
    });
    m_trayIcon->show();
}

void MainWindow::toggleWindowVisibility()
{
    if (isVisible()) {
        hide();
        m_showHideAction->setText(tr("Show CrystalTts"));
    } else {
        show();
        raise();
        activateWindow();
        m_showHideAction->setText(tr("Hide CrystalTts"));
    }
}

void MainWindow::quitFromTray()
{
    m_trayIcon->hide();
    qApp->quit();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    event->accept();
    hide(); // minimize to tray; quit only via tray menu
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (m_showHideAction)
        m_showHideAction->setText(tr("Hide CrystalTts"));
}

void MainWindow::hideEvent(QHideEvent *event)
{
    QMainWindow::hideEvent(event);
    if (m_showHideAction)
        m_showHideAction->setText(tr("Show CrystalTts"));
}

#if defined(Q_OS_WIN)
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, long *result)
{
    if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG") {
        MSG *msg = static_cast<MSG *>(message);
        if (msg->message == WM_HOTKEY && msg->wParam == static_cast<WPARAM>(HOTKEY_ID)) {
            m_foregroundAtHotkey = GetForegroundWindow();
            QTimer::singleShot(20, this, &MainWindow::doCopyFromForeground);
            *result = 0;
            return true;
        }
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}

void MainWindow::doCopyFromForeground()
{
    HWND us = reinterpret_cast<HWND>(winId());
    if (m_foregroundAtHotkey && m_foregroundAtHotkey != us && IsWindow(m_foregroundAtHotkey)) {
        DWORD targetThread = GetWindowThreadProcessId(m_foregroundAtHotkey, NULL);
        DWORD ourThread = GetCurrentThreadId();
        if (targetThread != ourThread) {
            AttachThreadInput(ourThread, targetThread, TRUE);
            SetForegroundWindow(m_foregroundAtHotkey);
            AttachThreadInput(ourThread, targetThread, FALSE);
        } else {
            SetForegroundWindow(m_foregroundAtHotkey);
        }
        Sleep(60);
    }
    m_foregroundAtHotkey = 0;
    simulateCopy();
    QTimer::singleShot(150, this, &MainWindow::showClipboardText);
}
#endif

void MainWindow::onGetSelection()
{
#if defined(Q_OS_WIN)
    HWND current = reinterpret_cast<HWND>(winId());
    HWND fg = GetForegroundWindow();
    if (fg == current && m_lastKnownForeground && IsWindow(m_lastKnownForeground))
        m_foregroundAtHotkey = m_lastKnownForeground;
    else
        m_foregroundAtHotkey = fg;
    QTimer::singleShot(20, this, &MainWindow::doCopyFromForeground);
#elif defined(Q_OS_UNIX) && !defined(Q_OS_MAC)
    simulateCopy();
    QTimer::singleShot(150, this, &MainWindow::showClipboardText);
#else
    showClipboardText();
#endif
}

#if defined(Q_OS_WIN)
void MainWindow::updateLastForegroundWindow()
{
    HWND fg = GetForegroundWindow();
    HWND us = reinterpret_cast<HWND>(winId());
    if (fg && fg != us && IsWindow(fg))
        m_lastKnownForeground = fg;
}
#endif

void MainWindow::showClipboardText()
{
    QString text = QApplication::clipboard()->text(QClipboard::Clipboard).trimmed();
    int n = text.length();
    m_textSelected->setPlainText(text);
    if (text.isEmpty()) {
        m_textSelected->setPlaceholderText(tr("(No text — select something and try again)"));
        m_labelStatus->setText(tr("Last: 0 characters (no selection copied?)"));
    } else {
        m_textSelected->setPlaceholderText(QString());
        m_labelStatus->setText(tr("Last: %1 character(s) from clipboard").arg(n));
    }
}

void MainWindow::onTtsPlay()
{
    if (!m_ttsEngine || !m_ttsEngine->isAvailable())
        return;
    QString text = m_textSelected->toPlainText().trimmed();
    if (text.isEmpty())
        return;
    if (m_ttsEngine->state() == 2) // Paused
        m_ttsEngine->resume();
    else
        m_ttsEngine->speak(text);
}

void MainWindow::onTtsPause()
{
    if (m_ttsEngine && m_ttsEngine->isAvailable())
        m_ttsEngine->pause();
}

void MainWindow::onTtsStop()
{
    if (m_ttsEngine && m_ttsEngine->isAvailable())
        m_ttsEngine->stop();
}

void MainWindow::onTtsStateChanged(int state)
{
    if (!m_btnPlay || !m_btnPause || !m_btnStop)
        return;
    bool available = m_ttsEngine && m_ttsEngine->isAvailable();
    m_btnPlay->setEnabled(available);
    m_btnPause->setEnabled(available && (state == 1));   // Speaking
    m_btnStop->setEnabled(available && (state == 1 || state == 2 || state == 4)); // Speaking, Paused, or Loading
}

void MainWindow::registerGlobalHotkey()
{
#if defined(Q_OS_WIN)
    if (m_hotkeyRegistered) return;
    HWND hwnd = reinterpret_cast<HWND>(winId());
    m_hotkeyRegistered = RegisterHotKey(hwnd, HOTKEY_ID, MOD_CONTROL | MOD_SHIFT, 0x53);
#endif
}

void MainWindow::unregisterGlobalHotkey()
{
#if defined(Q_OS_WIN)
    if (!m_hotkeyRegistered) return;
    UnregisterHotKey(reinterpret_cast<HWND>(winId()), HOTKEY_ID);
    m_hotkeyRegistered = false;
#endif
}

void MainWindow::simulateCopy()
{
#if defined(Q_OS_WIN)
    INPUT inputs[4] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 0x43;
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = 0x43;
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_CONTROL;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, inputs, sizeof(INPUT));
#elif defined(Q_OS_UNIX) && !defined(Q_OS_MAC)
    xcb_connection_t *conn = nullptr;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (auto *x11 = qApp->nativeInterface<QNativeInterface::QX11Application>())
        conn = x11->connection();
#else
    if (QWindow *win = windowHandle())
        conn = static_cast<xcb_connection_t *>(qApp->platformNativeInterface()->nativeResourceForWindow("connection", win));
#endif
    if (!conn) return;
    xcb_key_symbols_t *syms = xcb_key_symbols_alloc(conn);
    if (!syms) return;
    xcb_keycode_t *ctrl_keycodes = xcb_key_symbols_get_keycode(syms, XK_Control_L);
    xcb_keycode_t *c_keycodes = xcb_key_symbols_get_keycode(syms, XK_c);
    if (!ctrl_keycodes || !c_keycodes) {
        free(ctrl_keycodes);
        free(c_keycodes);
        xcb_key_symbols_free(syms);
        return;
    }
    xcb_keycode_t ctrl_kc = ctrl_keycodes[0];
    xcb_keycode_t c_kc = c_keycodes[0];
    free(ctrl_keycodes);
    free(c_keycodes);
    xcb_key_symbols_free(syms);

    /* Ctrl down, C down, C up, Ctrl up (XTest sends to focused window) */
    xcb_test_fake_input(conn, XCB_KEY_PRESS, ctrl_kc, XCB_CURRENT_TIME, XCB_NONE, 0, 0, 0);
    xcb_test_fake_input(conn, XCB_KEY_PRESS, c_kc, XCB_CURRENT_TIME, XCB_NONE, 0, 0, 0);
    xcb_test_fake_input(conn, XCB_KEY_RELEASE, c_kc, XCB_CURRENT_TIME, XCB_NONE, 0, 0, 0);
    xcb_test_fake_input(conn, XCB_KEY_RELEASE, ctrl_kc, XCB_CURRENT_TIME, XCB_NONE, 0, 0, 0);
    xcb_flush(conn);
#endif
}
