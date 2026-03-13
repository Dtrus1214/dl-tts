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
#include <QActionGroup>
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
static const int WINDOW_RADIUS = 14;
static const int CONTENT_PADDING = 14;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUiDynamic();
    setupWindowFrame();
    connect(m_btnClose, &QPushButton::clicked, this, &QWidget::hide);

    m_ttsEngine = new TtsEngine(this);
    m_ttsEngine->initialize();
    connect(m_ttsEngine, &TtsEngine::stateChanged, this, &MainWindow::onTtsStateChanged);
    connect(m_btnPlay, &QPushButton::clicked, this, &MainWindow::onTtsPlay);
    connect(m_btnStop, &QPushButton::clicked, this, &MainWindow::onTtsStop);
    connect(m_btnSpeaker, &QPushButton::clicked, this, &MainWindow::onSpeakerButtonClicked);
    setupSpeakerMenu();
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
    m_btnClose->setIconPath(QStringLiteral(":/icons/close.svg"));
    titleLayout->addWidget(m_btnClose, 0, Qt::AlignVCenter);

    rootLayout->addWidget(m_titleBar);

    // ---- Content ----
    QWidget *content = new QWidget(m_centralWidget);
    content->setObjectName("content");
    QVBoxLayout *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(CONTENT_PADDING, 12, CONTENT_PADDING, CONTENT_PADDING);
    contentLayout->setSpacing(10);

    QHBoxLayout *ttsLayout = new QHBoxLayout();
    ttsLayout->setSpacing(8);
    m_btnPlay = new CustomButton(CustomButton::Primary, content);
    m_btnPlay->setObjectName("btnTtsPlay");
    m_btnPlay->setFixedSize(32, 32);
    m_btnPlay->setIconPath(QStringLiteral(":/icons/play.svg"));
    m_btnStop = new CustomButton(CustomButton::Secondary, content);
    m_btnStop->setObjectName("btnTtsStop");
    m_btnStop->setFixedSize(32, 32);
    m_btnStop->setIconPath(QStringLiteral(":/icons/stop.svg"));
    m_btnSpeaker = new CustomButton(CustomButton::Secondary, content);
    m_btnSpeaker->setObjectName("btnSpeaker");
    m_btnSpeaker->setFixedSize(32, 32);
    m_btnSpeaker->setIconPath(QStringLiteral(":/icons/speaker.svg"));
    ttsLayout->addWidget(m_btnPlay);
    ttsLayout->addWidget(m_btnStop);
    ttsLayout->addWidget(m_btnSpeaker);
    contentLayout->addLayout(ttsLayout);

    rootLayout->addWidget(content);
}

void MainWindow::setupWindowFrame()
{
    setWindowTitle(tr("CrystalTts"));
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground); // so drop shadow is visible

    // Crystal-style palette: light, airy blues and white
    const char *sheet = R"(
        QMainWindow {
            background-color: transparent;
        }
        QWidget#centralWidget {
            background-color: #ffffff;
            border: 1px solid #d0e4ff;
            border-radius: %1px;
        }
        QWidget#titleBar {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                              stop:0 #e4f3ff,
                                              stop:1 #c0ddff);
            border-top-left-radius: %1px;
            border-top-right-radius: %1px;
        }
        QWidget#content {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                              stop:0 #f9fcff,
                                              stop:1 #e4f1ff);
            border-bottom-left-radius: %1px;
            border-bottom-right-radius: %1px;
        }
        QLabel#labelTitle {
            color: #1f3b5e;
            font-size: 13px;
            font-weight: 600;
        }
        QLabel#labelStatus {
            color: #4d6580;
            font-size: 11px;
        }
        QPlainTextEdit#textSelected {
            background-color: #ffffff;
            color: #123456;
            border: 1px solid #d0e4ff;
            border-radius: 6px;
            padding: 8px;
            font-size: 12px;
            selection-background-color: #c0ddff;
        }
    )";
    setStyleSheet(QString::fromUtf8(sheet).arg(WINDOW_RADIUS));

    // No window mask: rounded shape is drawn by Qt with antialiasing (soft edges).
    // Shadow removed to avoid a dark rectangular area; content has transparent corners.
    clearMask();
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
            m_playAfterCopy = false;
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
    if (m_playAfterCopy) {
        QTimer::singleShot(150, this, &MainWindow::playClipboardSelection);
    } else {
        QTimer::singleShot(150, this, &MainWindow::showClipboardText);
    }
}
#endif

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
    if (!m_labelStatus)
        return;
    if (text.isEmpty())
        m_labelStatus->setText(tr("Last: 0 characters (no selection copied?)"));
    else
        m_labelStatus->setText(tr("Last: %1 character(s) from clipboard").arg(n));
}

void MainWindow::onTtsPlay()
{
    if (!m_ttsEngine || !m_ttsEngine->isAvailable())
        return;

    int s = m_ttsEngine->state();
    if (s == TtsEngine::Loading)
        return;

    // Toggle behaviour: if speaking -> pause, if paused -> resume
    if (s == TtsEngine::Speaking) {
        m_ttsEngine->pause();
        return;
    }
    if (s == TtsEngine::Paused) {
        m_ttsEngine->resume();
        return;
    }

#if defined(Q_OS_WIN)
    HWND current = reinterpret_cast<HWND>(winId());
    HWND fg = GetForegroundWindow();
    if (fg == current && m_lastKnownForeground && IsWindow(m_lastKnownForeground))
        m_foregroundAtHotkey = m_lastKnownForeground;
    else
        m_foregroundAtHotkey = fg;
    m_playAfterCopy = true;
    QTimer::singleShot(20, this, &MainWindow::doCopyFromForeground);
#elif defined(Q_OS_UNIX) && !defined(Q_OS_MAC)
    m_playAfterCopy = true;
    simulateCopy();
    QTimer::singleShot(150, this, &MainWindow::playClipboardSelection);
#else
    playClipboardSelection();
#endif
}

void MainWindow::onTtsStop()
{
    if (m_ttsEngine && m_ttsEngine->isAvailable())
        m_ttsEngine->stop();
}

void MainWindow::playClipboardSelection()
{
    if (!m_ttsEngine || !m_ttsEngine->isAvailable())
        return;

    QString text = QApplication::clipboard()->text(QClipboard::Clipboard).trimmed();
    if (text.isEmpty())
        return;

    if (m_ttsEngine->state() == TtsEngine::Paused) {
        m_ttsEngine->resume();
    } else {
        m_ttsEngine->speak(text);
    }
}

void MainWindow::setupSpeakerMenu()
{
    if (!m_btnSpeaker)
        return;

    m_speakerMenu = new QMenu(this);
    m_speakerMenu->setObjectName("speakerMenu");

    // Crystal-style: light blue/white to match main UI
    m_speakerMenu->setStyleSheet(QStringLiteral(
        "QMenu#speakerMenu {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #f9fcff, stop:1 #e4f1ff);"
        "  border: 1px solid #d0e4ff;"
        "  border-radius: 8px;"
        "  padding: 4px 0;"
        "}"
        "QMenu#speakerMenu::item {"
        "  padding: 6px 24px 6px 12px;"
        "  color: #1f3b5e;"
        "  font-size: 12px;"
        "}"
        "QMenu#speakerMenu::item:selected {"
        "  background: #c9dbff;"
        "  color: #1f3b5e;"
        "}"
        "QMenu#speakerMenu::item:checked {"
        "  background: #e1efff;"
        "  font-weight: 600;"
        "}"
        "QMenu#speakerMenu::item:checked:selected {"
        "  background: #c9dbff;"
        "}"
        "QMenu#speakerMenu::indicator {"
        "  width: 12px;"
        "  left: 4px;"
        "}"
        "QMenu#speakerMenu::indicator:checked {"
        "  background: #2e8cff;"
        "  border-radius: 2px;"
        "}"
    ));

    QActionGroup *group = new QActionGroup(m_speakerMenu);
    group->setExclusive(true);

    const QStringList names = { QStringLiteral("man1"), QStringLiteral("man2"),
                                QStringLiteral("woman1"), QStringLiteral("woman2") };
    for (int i = 0; i < names.size(); ++i) {
        QAction *a = m_speakerMenu->addAction(names.at(i));
        a->setCheckable(true);
        a->setData(i);
        group->addAction(a);
        if (i == m_currentSpeakerId)
            a->setChecked(true);
    }
    connect(m_speakerMenu, &QMenu::triggered, this, &MainWindow::onSpeakerSelected);
    updateSpeakerToolTip();
}

void MainWindow::onSpeakerButtonClicked()
{
    if (!m_btnSpeaker || !m_speakerMenu)
        return;
    m_speakerMenu->exec(m_btnSpeaker->mapToGlobal(QPoint(0, m_btnSpeaker->height())));
}

void MainWindow::onSpeakerSelected(QAction *action)
{
    if (!action || !action->data().isValid())
        return;
    m_currentSpeakerId = action->data().toInt();
    action->setChecked(true);
    updateSpeakerToolTip();
    // TODO: pass m_currentSpeakerId to TtsEngine when it supports speaker selection
}

void MainWindow::updateSpeakerToolTip()
{
    if (!m_btnSpeaker)
        return;
    const QStringList names = { QStringLiteral("man1"), QStringLiteral("man2"),
                                QStringLiteral("woman1"), QStringLiteral("woman2") };
    QString name = (m_currentSpeakerId >= 0 && m_currentSpeakerId < names.size())
                   ? names.at(m_currentSpeakerId) : names.first();
    m_btnSpeaker->setToolTip(tr("Speaker: %1 (click to change)").arg(name));
}

void MainWindow::onTtsStateChanged(int state)
{
    if (!m_btnPlay || !m_btnStop)
        return;
    bool available = m_ttsEngine && m_ttsEngine->isAvailable();
    bool speaking = (state == TtsEngine::Speaking);
    bool paused = (state == TtsEngine::Paused);
    bool loading = (state == TtsEngine::Loading);

    // Update play button icon (play vs pause)
    if (speaking || paused)
        m_btnPlay->setIconPath(QStringLiteral(":/icons/pause.svg"));
    else
        m_btnPlay->setIconPath(QStringLiteral(":/icons/play.svg"));

    m_btnPlay->setEnabled(available && !loading);
    m_btnStop->setEnabled(available && (speaking || paused || loading)); // Speaking, Paused, or Loading
    if (m_btnSpeaker)
        m_btnSpeaker->setEnabled(available && !speaking && !loading);
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
