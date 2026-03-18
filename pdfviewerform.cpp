#include "pdfviewerform.h"
#include "tts/ttsengine.h"
#include "custombutton.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QFrame>
#include <QStyle>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QEvent>
#include <QShowEvent>
#include <QResizeEvent>
#include <QClipboard>
#include <QApplication>
#include <QMenu>
#include <QWindow>
#include <QEventLoop>
#include <QTimer>
#include <QDateTime>
#include <QProgressBar>
#if defined(Q_OS_WIN)
#include <windows.h>
#endif
#include <memory>
#include <vector>
#include <algorithm>

#ifdef HAVE_POPPLER
#include <poppler/qt5/poppler-qt5.h>
#endif

#ifdef HAVE_POPPLER
namespace {

class PdfPageWidget;

static void clearOtherPdfPages(PdfPageWidget *self, QWidget *pagesRoot);
static void clearAllPdfSelections(QWidget *pagesRoot);

class PdfPageWidget final : public QWidget
{
public:
    PdfPageWidget(Poppler::Document *doc,
                  int pageIndex,
                  int xRes,
                  int yRes,
                  const QImage &rendered,
                  QWidget *pagesRoot,
                  QWidget *parent = nullptr)
        : QWidget(parent)
        , m_doc(doc)
        , m_pageIndex(pageIndex)
        , m_xRes(xRes)
        , m_yRes(yRes)
        , m_image(rendered)
        , m_pagesRoot(pagesRoot)
    {
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        setCursor(Qt::ArrowCursor);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setFixedSize(m_image.size());
        setContextMenuPolicy(Qt::DefaultContextMenu);

        buildTextBoxesCache();
    }

    QString selectedText() const { return m_selectedText; }

    void fullResetOtherPage()
    {
        m_hasSelection = false;
        m_selecting = false;
        m_dragStarted = false;
        m_selectedText.clear();
        m_selectedPixelRects.clear();
        m_selStart = QPoint();
        m_selEnd = QPoint();
        m_skipNextReleaseUpdate = false;
        m_clickChainCount = 0;
        m_lastDoubleClickEventMs = 0;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawImage(QPoint(0, 0), m_image);

        if (m_hasSelection) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(192, 221, 255, 140)); // crystal selection (#c0ddff)
            for (const QRect &r : m_selectedPixelRects) {
                if (!r.isNull() && r.isValid())
                    p.drawRect(r);
            }
        }
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton)
            return;
        setFocus(Qt::MouseFocusReason);

        clearOtherPdfPages(this, m_pagesRoot);

        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const QPoint pos = clampToPage(e->pos());

        /* Triple-click: third press right after a double-click (same area) → line */
        if (m_lastDoubleClickEventMs > 0
            && now - m_lastDoubleClickEventMs <= 700
            && (pos - m_lastDoubleClickPos).manhattanLength() <= 20) {
            m_lastDoubleClickEventMs = 0;
            m_clickChainCount = 0;
            m_selecting = false;
            m_dragStarted = false;
            const int idx = hitTestTextBoxIndex(pos);
            if (idx >= 0) {
                m_hasSelection = true;
                selectLineAtIndex(idx);
                m_skipNextReleaseUpdate = true;
            } else {
                m_hasSelection = false;
                m_selectedText.clear();
                m_selectedPixelRects.clear();
            }
            updateHoverCursor(pos);
            update();
            return;
        }

        /* Three quick single-clicks (no Qt double-click) → line */
        const qint64 chainMs = 900;
        if (now - m_lastChainClickMs <= chainMs
            && (pos - m_lastChainClickPos).manhattanLength() <= 14) {
            ++m_clickChainCount;
        } else {
            m_clickChainCount = 1;
        }
        m_lastChainClickMs = now;
        m_lastChainClickPos = pos;

        if (m_clickChainCount >= 3) {
            m_clickChainCount = 0;
            m_lastDoubleClickEventMs = 0;
            m_selecting = false;
            m_dragStarted = false;
            const int idx = hitTestTextBoxIndex(pos);
            if (idx >= 0) {
                m_hasSelection = true;
                selectLineAtIndex(idx);
                m_skipNextReleaseUpdate = true;
            } else {
                m_hasSelection = false;
                m_selectedText.clear();
                m_selectedPixelRects.clear();
            }
            updateHoverCursor(pos);
            update();
            return;
        }

        m_hasSelection = false;
        m_selectedText.clear();
        m_selectedPixelRects.clear();
        m_selecting = true;
        m_dragStarted = false;
        m_pressAnchor = pos;
        m_selStart = pos;
        m_selEnd = pos;
        updateHoverCursor(pos);
        update();
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        const QPoint pos = clampToPage(e->pos());
        if (!m_selecting) {
            updateHoverCursor(pos);
            return;
        }
        if (!m_dragStarted) {
            if ((pos - m_pressAnchor).manhattanLength() >= QApplication::startDragDistance()) {
                m_dragStarted = true;
                m_hasSelection = true;
            } else {
                update();
                return;
            }
        }
        m_selEnd = pos;
        updateSelectionFromBoxes();
        update();
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton)
            return;
        if (m_skipNextReleaseUpdate) {
            m_skipNextReleaseUpdate = false;
            m_selecting = false;
            m_dragStarted = false;
            update();
            return;
        }
        m_selecting = false;
        m_selEnd = clampToPage(e->pos());
        if (!m_dragStarted) {
            m_hasSelection = false;
            m_selectedText.clear();
            m_selectedPixelRects.clear();
        } else {
            updateSelectionFromBoxes();
        }
        m_dragStarted = false;
        update();
    }

    void mouseDoubleClickEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton)
            return;
        e->accept();
        setFocus(Qt::MouseFocusReason);
        m_clickChainCount = 0;

        const QPoint pos = clampToPage(e->pos());
        updateHoverCursor(pos);

        const int idx = hitTestTextBoxIndex(pos);
        if (idx < 0)
            return;

        m_selecting = false;
        m_dragStarted = false;
        m_hasSelection = true;
        selectWordAtIndex(idx);
        m_skipNextReleaseUpdate = true;
        m_lastDoubleClickEventMs = QDateTime::currentMSecsSinceEpoch();
        m_lastDoubleClickPos = pos;
        update();
    }

    void keyPressEvent(QKeyEvent *e) override
    {
        if (e->matches(QKeySequence::Copy)) {
            copySelectionToClipboard();
            e->accept();
            return;
        }
        QWidget::keyPressEvent(e);
    }

    void contextMenuEvent(QContextMenuEvent *e) override
    {
        QMenu menu(this);
        menu.setStyleSheet(QStringLiteral(
            "QMenu {"
            "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #f9fcff, stop:1 #e4f1ff);"
            "  border: 1px solid #d0e4ff;"
            "  border-radius: 8px;"
            "  padding: 4px;"
            "}"
            "QMenu::item { padding: 8px 28px; color: #1f3b5e; border-radius: 4px; }"
            "QMenu::item:selected { background: #c9dbff; color: #1f3b5e; }"));
        QAction *copyAct = menu.addAction(tr("Copy"));
        copyAct->setEnabled(!m_selectedText.trimmed().isEmpty());
        QAction *clearAct = menu.addAction(tr("Clear selection"));
        QAction *chosen = menu.exec(e->globalPos());
        if (chosen == copyAct) {
            copySelectionToClipboard();
        } else if (chosen == clearAct) {
            clearAllPdfSelections(m_pagesRoot);
        }
    }

private:
    struct TextBoxItem {
        QRectF pdfRect;   // points (1/72 inch)
        QString text;
    };

    QPoint clampToPage(const QPoint &p) const
    {
        return QPoint(qBound(0, p.x(), width() - 1), qBound(0, p.y(), height() - 1));
    }

    QRect normalizedSelectionRect() const
    {
        if (!m_hasSelection)
            return QRect();
        return QRect(m_selStart, m_selEnd).normalized();
    }

    QRectF selectionRectPdfPoints() const
    {
        // Poppler page coordinates are in points (1/72 inch).
        // Our widget coordinates are pixels rendered at xRes/yRes (pixels per inch).
        const QRect selPx = normalizedSelectionRect();
        if (selPx.isNull() || !selPx.isValid())
            return QRectF();

        const qreal pxToPtX = 72.0 / qMax(1, m_xRes);
        const qreal pxToPtY = 72.0 / qMax(1, m_yRes);
        return QRectF(selPx.x() * pxToPtX,
                      selPx.y() * pxToPtY,
                      selPx.width() * pxToPtX,
                      selPx.height() * pxToPtY);
    }

    QRect pdfPointsToPixelRect(const QRectF &rPt) const
    {
        const qreal ptToPxX = qMax(1, m_xRes) / 72.0;
        const qreal ptToPxY = qMax(1, m_yRes) / 72.0;
        QRect rPx(qRound(rPt.x() * ptToPxX),
                  qRound(rPt.y() * ptToPxY),
                  qRound(rPt.width() * ptToPxX),
                  qRound(rPt.height() * ptToPxY));
        return rPx.intersected(rect());
    }

    void buildTextBoxesCache()
    {
        m_textBoxes.clear();
        if (!m_doc)
            return;
        std::unique_ptr<Poppler::Page> page(m_doc->page(m_pageIndex));
        if (!page)
            return;

        // textList() returns heap-allocated items; we must delete them.
        const QList<Poppler::TextBox *> list = page->textList();
        m_textBoxes.reserve(list.size());
        for (Poppler::TextBox *tb : list) {
            if (!tb)
                continue;
            const QString t = tb->text();
            const QRectF r = tb->boundingBox();
            if (!t.trimmed().isEmpty() && r.isValid() && !r.isNull()) {
                m_textBoxes.push_back(TextBoxItem{r, t});
            }
            delete tb;
        }
    }

    void updateHoverCursor(const QPoint &posPx)
    {
        const bool overText = hitTestText(posPx);
        const QCursor desired = overText ? Qt::IBeamCursor : Qt::ArrowCursor;
        if (cursor().shape() != desired.shape())
            setCursor(desired);
    }

    bool hitTestText(const QPoint &posPx) const
    {
        const QRectF ptRect(posPx.x() * (72.0 / qMax(1, m_xRes)),
                            posPx.y() * (72.0 / qMax(1, m_yRes)),
                            1.0,
                            1.0);
        for (const auto &b : m_textBoxes) {
            if (b.pdfRect.contains(ptRect.topLeft()))
                return true;
        }
        return false;
    }

    int hitTestTextBoxIndex(const QPoint &posPx) const
    {
        const QPointF pt(posPx.x() * (72.0 / qMax(1, m_xRes)),
                         posPx.y() * (72.0 / qMax(1, m_yRes)));
        for (int i = 0; i < static_cast<int>(m_textBoxes.size()); ++i) {
            if (m_textBoxes[static_cast<size_t>(i)].pdfRect.contains(pt))
                return i;
        }
        return -1;
    }

    void updateSelectionFromBoxes()
    {
        m_selectedText.clear();
        m_selectedPixelRects.clear();

        const QRectF selPt = selectionRectPdfPoints();
        if (selPt.isNull() || !selPt.isValid())
            return;

        QStringList parts;
        parts.reserve(static_cast<int>(m_textBoxes.size()));

        for (const auto &b : m_textBoxes) {
            if (b.pdfRect.intersects(selPt)) {
                parts.append(b.text);
                m_selectedPixelRects.push_back(pdfPointsToPixelRect(b.pdfRect));
            }
        }

        m_selectedText = parts.join(QLatin1Char(' ')).simplified();
    }

    void selectWordAtIndex(int idx)
    {
        if (idx < 0 || idx >= static_cast<int>(m_textBoxes.size()))
            return;
        m_selectedText.clear();
        m_selectedPixelRects.clear();
        const auto &b = m_textBoxes[static_cast<size_t>(idx)];
        m_selectedText = b.text.trimmed();
        m_selectedPixelRects.push_back(pdfPointsToPixelRect(b.pdfRect));
    }

    void selectLineAtIndex(int idx)
    {
        if (idx < 0 || idx >= static_cast<int>(m_textBoxes.size()))
            return;

        const QRectF clicked = m_textBoxes[static_cast<size_t>(idx)].pdfRect;
        const qreal clickedCenterY = clicked.center().y();
        const qreal clickedH = qMax<qreal>(1.0, clicked.height());
        const qreal yTol = qMax<qreal>(2.0, clickedH * 0.5);

        struct Hit {
            int i;
            qreal x;
        };
        std::vector<Hit> hits;
        hits.reserve(m_textBoxes.size());
        for (int i = 0; i < static_cast<int>(m_textBoxes.size()); ++i) {
            const QRectF r = m_textBoxes[static_cast<size_t>(i)].pdfRect;
            const qreal cy = r.center().y();
            const qreal h = qMax<qreal>(1.0, r.height());
            const qreal tol = qMax(yTol, h * 0.5);
            if (qAbs(cy - clickedCenterY) <= tol) {
                hits.push_back(Hit{i, r.x()});
            }
        }
        std::sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) {
            if (a.x == b.x)
                return a.i < b.i;
            return a.x < b.x;
        });

        m_selectedText.clear();
        m_selectedPixelRects.clear();
        QStringList parts;
        parts.reserve(static_cast<int>(hits.size()));
        for (const auto &h : hits) {
            const auto &b = m_textBoxes[static_cast<size_t>(h.i)];
            parts.append(b.text);
            m_selectedPixelRects.push_back(pdfPointsToPixelRect(b.pdfRect));
        }
        m_selectedText = parts.join(QLatin1Char(' ')).simplified();
    }

    void copySelectionToClipboard()
    {
        const QString text = m_selectedText.trimmed();
        if (text.isEmpty())
            return;
        QApplication::clipboard()->setText(text);
    }

    void clearSelection()
    {
        m_hasSelection = false;
        m_selStart = QPoint();
        m_selEnd = QPoint();
        m_selectedText.clear();
        m_selectedPixelRects.clear();
        update();
    }

    Poppler::Document *m_doc = nullptr;
    int m_pageIndex = -1;
    int m_xRes = 150;
    int m_yRes = 150;
    QImage m_image;
    QWidget *m_pagesRoot = nullptr;

    std::vector<TextBoxItem> m_textBoxes;
    std::vector<QRect> m_selectedPixelRects;

    qint64 m_lastDoubleClickEventMs = 0;
    QPoint m_lastDoubleClickPos;
    qint64 m_lastChainClickMs = 0;
    QPoint m_lastChainClickPos;
    int m_clickChainCount = 0;
    QPoint m_pressAnchor;
    bool m_dragStarted = false;

    bool m_skipNextReleaseUpdate = false;
    bool m_hasSelection = false;
    bool m_selecting = false;
    QPoint m_selStart;
    QPoint m_selEnd;
    QString m_selectedText;
};

static void clearOtherPdfPages(PdfPageWidget *self, QWidget *pagesRoot)
{
    if (!pagesRoot || !self)
        return;
    for (QWidget *cw : pagesRoot->findChildren<QWidget *>()) {
        auto *pw = dynamic_cast<PdfPageWidget *>(cw);
        if (pw && pw != self)
            pw->fullResetOtherPage();
    }
}

static void clearAllPdfSelections(QWidget *pagesRoot)
{
    if (!pagesRoot)
        return;
    for (QWidget *cw : pagesRoot->findChildren<QWidget *>()) {
        auto *pw = dynamic_cast<PdfPageWidget *>(cw);
        if (pw)
            pw->fullResetOtherPage();
    }
}

} // namespace
#endif

#if !defined(Q_OS_WIN) && QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
namespace {

class PdfResizeEdge final : public QWidget
{
public:
    PdfResizeEdge(PdfViewerForm *pdf, Qt::Edges edges, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_pdf(pdf)
        , m_edges(edges)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
        setStyleSheet(QStringLiteral("background: transparent;"));
    }

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton || !m_pdf)
            return;
        if (m_pdf->windowState() & Qt::WindowMaximized)
            return;
        QWindow *wh = m_pdf->windowHandle();
        if (wh && m_edges != Qt::Edges()) {
            wh->startSystemResize(m_edges);
            e->accept();
        }
    }
};

} // namespace
#endif

PdfViewerForm::PdfViewerForm(TtsEngine *ttsEngine, QWidget *parent)
    : QWidget(parent)
    , m_ttsEngine(ttsEngine)
{
    setupUi();
}

PdfViewerForm::~PdfViewerForm()
{
    clearPdf();
}

namespace {
static const int PDF_WINDOW_RADIUS = 14;
static const int PDF_TITLE_HEIGHT = 40;
static const int PDF_CONTENT_PADDING = 14;
}

void PdfViewerForm::applyCrystalStyle()
{
    const char *sheet = R"(
        QWidget#pdfViewerShell {
            background-color: #ffffff;
            border: 1px solid #d0e4ff;
            border-radius: %1px;
        }
        QWidget#pdfViewerTitleBar {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                              stop:0 #e4f3ff, stop:1 #c0ddff);
            border-top-left-radius: %1px;
            border-top-right-radius: %1px;
        }
        QWidget#pdfViewerContent {
            background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                              stop:0 #f9fcff, stop:1 #e4f1ff);
            border-bottom-left-radius: %1px;
            border-bottom-right-radius: %1px;
        }
        QLabel#pdfViewerTitleLabel {
            color: #1f3b5e;
            font-size: 13px;
            font-weight: 600;
        }
        QLabel#pdfPlaceholder {
            color: #4d6580;
            font-size: 13px;
        }
        QScrollArea#pdfScrollArea {
            background-color: rgba(255, 255, 255, 0.5);
            border: 1px solid #d0e4ff;
            border-radius: 8px;
        }
        QWidget#pdfScrollViewport {
            background: transparent;
        }
        QScrollBar:vertical {
            background: #e8f2ff;
            width: 10px;
            margin: 4px 2px 4px 0;
            border-radius: 5px;
        }
        QScrollBar::handle:vertical {
            background: #b8d4f5;
            min-height: 28px;
            border-radius: 5px;
        }
        QScrollBar::handle:vertical:hover {
            background: #9ec4f0;
        }
        QScrollBar:horizontal {
            background: #e8f2ff;
            height: 10px;
            margin: 0 4px 2px 4px;
            border-radius: 5px;
        }
        QScrollBar::handle:horizontal {
            background: #b8d4f5;
            min-width: 28px;
            border-radius: 5px;
        }
        QScrollBar::add-line, QScrollBar::sub-line {
            width: 0; height: 0;
        }
        QProgressBar#pdfLoadProgress {
            border: 1px solid #d0e4ff;
            border-radius: 6px;
            background-color: #e8f2ff;
            text-align: center;
            color: #1f3b5e;
            font-size: 11px;
            font-weight: 600;
            min-height: 22px;
            max-height: 22px;
        }
        QProgressBar#pdfLoadProgress::chunk {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #9ec4f0, stop:1 #6ba3e8);
            border-radius: 5px;
            margin: 1px;
        }
    )";
    setStyleSheet(QString::fromUtf8(sheet).arg(PDF_WINDOW_RADIUS));
}

bool PdfViewerForm::isInTitleBar(const QPoint &globalPos) const
{
    if (!m_centralShell || !m_titleBarWidget)
        return false;
    const QPoint local = m_centralShell->mapFromGlobal(globalPos);
    return m_titleBarWidget->geometry().contains(local);
}

bool PdfViewerForm::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_titleBarWidget || watched == m_labelTitle) {
        switch (event->type()) {
        case QEvent::MouseButtonDblClick: {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                m_dragging = false;
                if (m_titleBarWidget)
                    m_titleBarWidget->releaseMouse();
                onToggleMaximize();
                return true;
            }
            break;
        }
        case QEvent::MouseButtonPress: {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                m_dragPosition = me->globalPos() - frameGeometry().topLeft();
                m_dragging = true;
                if (m_titleBarWidget)
                    m_titleBarWidget->grabMouse();
                return true;
            }
            break;
        }
        case QEvent::MouseMove:
            if (m_dragging) {
                auto *me = static_cast<QMouseEvent *>(event);
                if (me->buttons() & Qt::LeftButton) {
                    move(me->globalPos() - m_dragPosition);
                    return true;
                }
            }
            break;
        case QEvent::MouseButtonRelease:
            if (static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
                m_dragging = false;
                if (m_titleBarWidget)
                    m_titleBarWidget->releaseMouse();
            }
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void PdfViewerForm::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && isInTitleBar(event->globalPos())) {
        m_dragPosition = event->globalPos() - frameGeometry().topLeft();
        m_dragging = true;
        if (m_titleBarWidget)
            m_titleBarWidget->grabMouse();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void PdfViewerForm::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPos() - m_dragPosition);
        event->accept();
        return;
    }
    m_dragging = false;
    QWidget::mouseMoveEvent(event);
}

void PdfViewerForm::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        if (m_titleBarWidget)
            m_titleBarWidget->releaseMouse();
    }
    QWidget::mouseReleaseEvent(event);
}

void PdfViewerForm::onHideWindow()
{
    hide();
}

void PdfViewerForm::onToggleMaximize()
{
    if (windowState() & Qt::WindowMaximized) {
        showNormal();
        if (m_restoredGeometry.isValid())
            setGeometry(m_restoredGeometry);
    } else {
        m_restoredGeometry = geometry();
        showMaximized();
    }
    updateMaximizeButton();
    layoutResizeEdgeWidgets();
#if !defined(Q_OS_WIN) && QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    for (QWidget *w : m_resizeEdgeWidgets)
        w->raise();
#endif
}

void PdfViewerForm::updateMaximizeButton()
{
    if (!m_btnWinMaximize)
        return;
    if (windowState() & Qt::WindowMaximized) {
        m_btnWinMaximize->setIconPath(QStringLiteral(":/icons/restore.svg"));
        m_btnWinMaximize->setToolTip(tr("Restore down"));
    } else {
        m_btnWinMaximize->setIconPath(QStringLiteral(":/icons/maximize.svg"));
        m_btnWinMaximize->setToolTip(tr("Maximize"));
    }
}

void PdfViewerForm::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::WindowStateChange) {
        updateMaximizeButton();
        layoutResizeEdgeWidgets();
#if !defined(Q_OS_WIN) && QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        for (QWidget *w : m_resizeEdgeWidgets)
            w->raise();
#endif
    }
    QWidget::changeEvent(e);
}

void PdfViewerForm::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    layoutResizeEdgeWidgets();
#if !defined(Q_OS_WIN) && QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    for (QWidget *w : m_resizeEdgeWidgets)
        w->raise();
#endif
}

void PdfViewerForm::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    if (!m_restoredGeometry.isValid())
        m_restoredGeometry = geometry();
    layoutResizeEdgeWidgets();
#if !defined(Q_OS_WIN) && QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    for (QWidget *w : m_resizeEdgeWidgets)
        w->raise();
#endif
}

void PdfViewerForm::layoutResizeEdgeWidgets()
{
#if defined(Q_OS_WIN)
    return;
#else
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
    return;
#else
    if (m_resizeEdgeWidgets.isEmpty())
        return;
    const bool maxed = (windowState() & Qt::WindowMaximized);
    const int t = qMax(6, int(6 * devicePixelRatioF()));
    const int ww = width();
    const int hh = height();
    if (maxed || ww < t * 3 || hh < t * 3) {
        for (QWidget *w : m_resizeEdgeWidgets)
            w->setVisible(false);
        return;
    }
    for (QWidget *w : m_resizeEdgeWidgets)
        w->setVisible(true);
    if (m_resizeEdgeWidgets.size() != 8)
        return;
    m_resizeEdgeWidgets[0]->setGeometry(0, 0, t, t);
    m_resizeEdgeWidgets[1]->setGeometry(t, 0, ww - 2 * t, t);
    m_resizeEdgeWidgets[2]->setGeometry(ww - t, 0, t, t);
    m_resizeEdgeWidgets[3]->setGeometry(0, t, t, hh - 2 * t);
    m_resizeEdgeWidgets[4]->setGeometry(ww - t, t, t, hh - 2 * t);
    m_resizeEdgeWidgets[5]->setGeometry(0, hh - t, t, t);
    m_resizeEdgeWidgets[6]->setGeometry(t, hh - t, ww - 2 * t, t);
    m_resizeEdgeWidgets[7]->setGeometry(ww - t, hh - t, t, t);
#endif
#endif
}

#if defined(Q_OS_WIN)
bool PdfViewerForm::nativeEvent(const QByteArray &eventType, void *message, long *result)
{
    if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG") {
        MSG *msg = static_cast<MSG *>(message);
        if (msg->message == WM_NCHITTEST) {
            if ((windowState() & Qt::WindowMaximized) || (windowState() & Qt::WindowFullScreen))
                return QWidget::nativeEvent(eventType, message, result);
            const QPoint g = mapToGlobal(QPoint(0, 0));
            const int bw = qMax(8, int(8 * devicePixelRatioF()));
            const int ww = width();
            const int hh = height();
            const int x = static_cast<int>(static_cast<short>(LOWORD(msg->lParam)));
            const int y = static_cast<int>(static_cast<short>(HIWORD(msg->lParam)));
            const int gx = g.x();
            const int gy = g.y();
            const bool left = x < gx + bw;
            const bool right = x >= gx + ww - bw;
            const bool top = y < gy + bw;
            const bool bottom = y >= gy + hh - bw;
            if (top && left) {
                *result = HTTOPLEFT;
                return true;
            }
            if (top && right) {
                *result = HTTOPRIGHT;
                return true;
            }
            if (bottom && left) {
                *result = HTBOTTOMLEFT;
                return true;
            }
            if (bottom && right) {
                *result = HTBOTTOMRIGHT;
                return true;
            }
            if (left) {
                *result = HTLEFT;
                return true;
            }
            if (right) {
                *result = HTRIGHT;
                return true;
            }
            if (top) {
                *result = HTTOP;
                return true;
            }
            if (bottom) {
                *result = HTBOTTOM;
                return true;
            }
        }
    }
    return QWidget::nativeEvent(eventType, message, result);
}
#endif

void PdfViewerForm::setupUi()
{
    setWindowTitle(tr("PDF Viewer — CrystalTts"));
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
    setMinimumSize(520, 420);
    resize(680, 580);

    QVBoxLayout *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_centralShell = new QWidget(this);
    m_centralShell->setObjectName(QStringLiteral("pdfViewerShell"));
    QVBoxLayout *shellLayout = new QVBoxLayout(m_centralShell);
    shellLayout->setContentsMargins(0, 0, 0, 0);
    shellLayout->setSpacing(0);

    m_titleBarWidget = new QWidget(m_centralShell);
    m_titleBarWidget->setObjectName(QStringLiteral("pdfViewerTitleBar"));
    m_titleBarWidget->setFixedHeight(PDF_TITLE_HEIGHT);
    m_titleBarWidget->setCursor(Qt::SizeAllCursor);

    QHBoxLayout *titleLayout = new QHBoxLayout(m_titleBarWidget);
    titleLayout->setContentsMargins(PDF_CONTENT_PADDING, 0, 8, 0);
    titleLayout->setSpacing(8);

    m_labelTitle = new QLabel(tr("PDF Viewer"), m_titleBarWidget);
    m_labelTitle->setObjectName(QStringLiteral("pdfViewerTitleLabel"));
    titleLayout->addWidget(m_labelTitle);
    titleLayout->addStretch();

    m_btnWinMaximize = new CustomButton(CustomButton::TitleBar, m_titleBarWidget);
    m_btnWinMaximize->setObjectName(QStringLiteral("btnPdfWinMaximize"));
    m_btnWinMaximize->setFixedSize(28, 28);
    m_btnWinMaximize->setIconPath(QStringLiteral(":/icons/maximize.svg"));
    m_btnWinMaximize->setToolTip(tr("Maximize"));
    connect(m_btnWinMaximize, &QPushButton::clicked, this, &PdfViewerForm::onToggleMaximize);
    titleLayout->addWidget(m_btnWinMaximize, 0, Qt::AlignVCenter);

    m_btnWinClose = new CustomButton(CustomButton::TitleBar, m_titleBarWidget);
    m_btnWinClose->setObjectName(QStringLiteral("btnPdfWinClose"));
    m_btnWinClose->setFixedSize(28, 28);
    m_btnWinClose->setIconPath(QStringLiteral(":/icons/close.svg"));
    m_btnWinClose->setToolTip(tr("Close window"));
    connect(m_btnWinClose, &QPushButton::clicked, this, &PdfViewerForm::onHideWindow);
    titleLayout->addWidget(m_btnWinClose, 0, Qt::AlignVCenter);

    m_titleBarWidget->installEventFilter(this);
    m_labelTitle->installEventFilter(this);

    shellLayout->addWidget(m_titleBarWidget);

    QWidget *content = new QWidget(m_centralShell);
    content->setObjectName(QStringLiteral("pdfViewerContent"));
    QVBoxLayout *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(PDF_CONTENT_PADDING, 12, PDF_CONTENT_PADDING, PDF_CONTENT_PADDING);
    contentLayout->setSpacing(10);

    QHBoxLayout *toolbarLayout = new QHBoxLayout();
    toolbarLayout->setSpacing(8);

#ifdef HAVE_POPPLER
    m_btnOpenPdf = new CustomButton(tr("Open"), CustomButton::Secondary, content);
    m_btnOpenPdf->setObjectName(QStringLiteral("btnOpenPdf"));
    m_btnOpenPdf->setMinimumHeight(32);
    m_btnOpenPdf->setMinimumWidth(72);
    connect(m_btnOpenPdf, &QPushButton::clicked, this, &PdfViewerForm::onOpenPdf);
    toolbarLayout->addWidget(m_btnOpenPdf);

    m_btnClosePdf = new CustomButton(tr("Close PDF"), CustomButton::Secondary, content);
    m_btnClosePdf->setObjectName(QStringLiteral("btnClosePdf"));
    m_btnClosePdf->setMinimumHeight(32);
    m_btnClosePdf->setEnabled(false);
    connect(m_btnClosePdf, &QPushButton::clicked, this, &PdfViewerForm::onClosePdf);
    toolbarLayout->addWidget(m_btnClosePdf);

    m_btnPlayTts = new CustomButton(CustomButton::Primary, content);
    m_btnPlayTts->setObjectName(QStringLiteral("btnPlayTts"));
    m_btnPlayTts->setFixedSize(32, 32);
    m_btnPlayTts->setIconPath(QStringLiteral(":/icons/play.svg"));
    m_btnPlayTts->setEnabled(false);
    m_btnPlayTts->setToolTip(tr("Play TTS"));
    connect(m_btnPlayTts, &QPushButton::clicked, this, &PdfViewerForm::onPlayTts);
    toolbarLayout->addWidget(m_btnPlayTts);

    m_btnZoomOut = new CustomButton(CustomButton::Secondary, content);
    m_btnZoomOut->setObjectName(QStringLiteral("btnPdfZoomOut"));
    m_btnZoomOut->setFixedSize(32, 32);
    m_btnZoomOut->setIconPath(QStringLiteral(":/icons/zoom-out.svg"));
    m_btnZoomOut->setToolTip(tr("Zoom out"));
    m_btnZoomOut->setEnabled(false);
    connect(m_btnZoomOut, &QPushButton::clicked, this, &PdfViewerForm::onZoomOut);
    toolbarLayout->addWidget(m_btnZoomOut);

    m_btnZoomIn = new CustomButton(CustomButton::Secondary, content);
    m_btnZoomIn->setObjectName(QStringLiteral("btnPdfZoomIn"));
    m_btnZoomIn->setFixedSize(32, 32);
    m_btnZoomIn->setIconPath(QStringLiteral(":/icons/zoom-in.svg"));
    m_btnZoomIn->setToolTip(tr("Zoom in"));
    m_btnZoomIn->setEnabled(false);
    connect(m_btnZoomIn, &QPushButton::clicked, this, &PdfViewerForm::onZoomIn);
    toolbarLayout->addWidget(m_btnZoomIn);
#else
    QLabel *noPopplerLabel = new QLabel(
        tr("PDF support not built. Set POPPLER_DIR in CrystalTts.pro and rebuild with Poppler Qt5 development files."),
        content);
    noPopplerLabel->setObjectName(QStringLiteral("pdfNoPopplerLabel"));
    noPopplerLabel->setWordWrap(true);
    noPopplerLabel->setAlignment(Qt::AlignCenter);
    noPopplerLabel->setStyleSheet(QStringLiteral("color: #4d6580; font-size: 12px;"));
    toolbarLayout->addWidget(noPopplerLabel);
#endif
    toolbarLayout->addStretch();
    contentLayout->addLayout(toolbarLayout);

#ifdef HAVE_POPPLER
    m_pdfLoadProgress = new QProgressBar(content);
    m_pdfLoadProgress->setObjectName(QStringLiteral("pdfLoadProgress"));
    m_pdfLoadProgress->setTextVisible(true);
    m_pdfLoadProgress->setAlignment(Qt::AlignCenter);
    m_pdfLoadProgress->setFormat(tr("Loading PDF… %p%"));
    m_pdfLoadProgress->setRange(0, 100);
    m_pdfLoadProgress->setValue(0);
    m_pdfLoadProgress->setVisible(false);
    contentLayout->addWidget(m_pdfLoadProgress);
#endif

    m_scrollArea = new QScrollArea(content);
    m_scrollArea->setObjectName(QStringLiteral("pdfScrollArea"));
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setAlignment(Qt::AlignCenter);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    if (QWidget *vp = m_scrollArea->viewport())
        vp->setObjectName(QStringLiteral("pdfScrollViewport"));

    m_pagesContainer = new QWidget(m_scrollArea);
    m_pagesContainer->setObjectName(QStringLiteral("pdfPagesContainer"));
    m_pagesLayout = new QVBoxLayout(m_pagesContainer);
    m_pagesLayout->setContentsMargins(6, 6, 6, 6);
    m_pagesLayout->setSpacing(14);
    m_pagesLayout->setAlignment(Qt::AlignTop | Qt::AlignHCenter);

#ifdef HAVE_POPPLER
    {
        QLabel *placeholder = new QLabel(tr("Open a PDF file to view and listen."), m_pagesContainer);
        placeholder->setObjectName(QStringLiteral("pdfPlaceholder"));
        placeholder->setAlignment(Qt::AlignCenter);
        m_pagesLayout->addWidget(placeholder);
    }
#endif

    m_scrollArea->setWidget(m_pagesContainer);
    contentLayout->addWidget(m_scrollArea, 1);

    shellLayout->addWidget(content);
    rootLayout->addWidget(m_centralShell);

#if !defined(Q_OS_WIN) && QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    {
        const Qt::Edges edgeSpec[] = {
            Qt::TopEdge | Qt::LeftEdge,
            Qt::TopEdge,
            Qt::TopEdge | Qt::RightEdge,
            Qt::LeftEdge,
            Qt::RightEdge,
            Qt::BottomEdge | Qt::LeftEdge,
            Qt::BottomEdge,
            Qt::BottomEdge | Qt::RightEdge,
        };
        for (Qt::Edges e : edgeSpec)
            m_resizeEdgeWidgets.append(new PdfResizeEdge(this, e, this));
    }
#endif

    applyCrystalStyle();
    updateMaximizeButton();
}

void PdfViewerForm::onOpenPdf()
{
#ifdef HAVE_POPPLER
    QString path = QFileDialog::getOpenFileName(this, tr("Open PDF"), QString(),
                                                tr("PDF files (*.pdf);;All files (*)"));
    if (path.isEmpty())
        return;
    loadPdf(path);
#else
    (void)this;
#endif
}

void PdfViewerForm::onClosePdf()
{
    clearPdf();
}

void PdfViewerForm::onPlayTts()
{
    if (!m_ttsEngine || !m_ttsEngine->isAvailable()) {
        QMessageBox::warning(this, tr("TTS"), tr("TTS engine is not available."));
        return;
    }
    if (m_extractedText.trimmed().isEmpty()) {
        QMessageBox::information(this, tr("TTS"), tr("No text to speak. Open a PDF first."));
        return;
    }
    m_ttsEngine->speak(m_extractedText);
}

#ifdef HAVE_POPPLER
double PdfViewerForm::computeFitToWindowDpi() const
{
    if (!m_doc || m_doc->numPages() <= 0)
        return 150.0;

    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    int vw = m_scrollArea->viewport()->width();
    int vh = m_scrollArea->viewport()->height();
    const int reserve = 64; // margins, frame, scrollbar
    if (vw < 100)
        vw = qMax(400, width() - 140);
    if (vh < 100)
        vh = qMax(280, height() - 200);
    vw = qMax(160, vw - reserve);
    vh = qMax(160, vh - reserve);

    double minDpi = 1e9;
    const int numPages = m_doc->numPages();
    for (int i = 0; i < numPages; ++i) {
        std::unique_ptr<Poppler::Page> page(m_doc->page(i));
        if (!page)
            continue;
        const QSizeF sz = page->pageSizeF();
        if (sz.width() < 1.0 || sz.height() < 1.0)
            continue;
        const double dw = vw * 72.0 / sz.width();
        const double dh = vh * 72.0 / sz.height();
        minDpi = qMin(minDpi, qMin(dw, dh));
    }
    if (minDpi > 1e8)
        minDpi = 150.0;
    return qBound(48.0, minDpi, 288.0);
}

void PdfViewerForm::rebuildPdfPageWidgets(QProgressBar *progress, int progressStart, int progressEnd)
{
    if (!m_doc)
        return;

    QLayoutItem *item;
    while ((item = m_pagesLayout->takeAt(0)) != nullptr) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }

    const int dpi = qBound(36, qRound(m_baseFitDpi * m_zoomFactor), 500);
    const int numPages = m_doc->numPages();
    const bool showProg = progress && progressEnd > progressStart && numPages > 0;

    for (int i = 0; i < numPages; ++i) {
        std::unique_ptr<Poppler::Page> page(m_doc->page(i));
        if (!page)
            continue;

        QImage image = page->renderToImage(dpi, dpi);
        if (image.isNull())
            continue;

        QFrame *frame = new QFrame(m_pagesContainer);
        frame->setObjectName(QStringLiteral("pdfPageFrame"));
        frame->setStyleSheet(QStringLiteral(
            "QFrame#pdfPageFrame {"
            "  background: #ffffff;"
            "  border: 1px solid #d0e4ff;"
            "  border-radius: 8px;"
            "}"));
        QVBoxLayout *frameLayout = new QVBoxLayout(frame);
        frameLayout->setContentsMargins(4, 4, 4, 4);
        frameLayout->setSpacing(0);

        PdfPageWidget *pageWidget = new PdfPageWidget(m_doc, i, dpi, dpi, image, m_pagesContainer, frame);
        frameLayout->addWidget(pageWidget);
        m_pagesLayout->addWidget(frame);

        if (showProg) {
            const int v = progressStart + (progressEnd - progressStart) * (i + 1) / numPages;
            progress->setValue(v);
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }
    }

    if (progress && progressEnd > progressStart && numPages <= 0)
        progress->setValue(progressEnd);
}

void PdfViewerForm::onZoomIn()
{
    if (!m_doc)
        return;
    m_zoomFactor *= 1.2;
    const double maxFactor = 500.0 / qMax(1.0, m_baseFitDpi);
    if (m_zoomFactor > maxFactor)
        m_zoomFactor = maxFactor;
    rebuildPdfPageWidgets();
}

void PdfViewerForm::onZoomOut()
{
    if (!m_doc)
        return;
    m_zoomFactor /= 1.2;
    const double minFactor = 36.0 / qMax(1.0, m_baseFitDpi);
    if (m_zoomFactor < minFactor)
        m_zoomFactor = minFactor;
    rebuildPdfPageWidgets();
}

void PdfViewerForm::loadPdf(const QString &path)
{
    clearPdf();

    m_doc = Poppler::Document::load(path);
    if (!m_doc) {
        QMessageBox::warning(this, tr("PDF"), tr("Could not load PDF: %1").arg(path));
        return;
    }
    if (m_doc->isLocked()) {
        delete m_doc;
        m_doc = nullptr;
        QMessageBox::warning(this, tr("PDF"), tr("PDF is locked."));
        return;
    }
    m_doc->setRenderHint(Poppler::Document::Antialiasing, true);
    m_doc->setRenderHint(Poppler::Document::TextAntialiasing, true);

    m_currentPdfPath = path;
    m_extractedText.clear();
    const int numPages = m_doc->numPages();

    auto hideLoadProgress = [this]() {
        if (m_pdfLoadProgress) {
            m_pdfLoadProgress->setVisible(false);
            m_pdfLoadProgress->setRange(0, 100);
            m_pdfLoadProgress->setValue(0);
        }
        if (m_btnOpenPdf)
            m_btnOpenPdf->setEnabled(true);
    };

    if (m_btnOpenPdf)
        m_btnOpenPdf->setEnabled(false);
    if (m_pdfLoadProgress) {
        if (numPages <= 0) {
            m_pdfLoadProgress->setRange(0, 0);
            m_pdfLoadProgress->setFormat(tr("Loading PDF…"));
        } else {
            m_pdfLoadProgress->setRange(0, 2 * numPages + 1);
            m_pdfLoadProgress->setFormat(tr("Loading PDF… %p%"));
        }
        m_pdfLoadProgress->setValue(0);
        m_pdfLoadProgress->setVisible(true);
    }
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    for (int i = 0; i < numPages; ++i) {
        std::unique_ptr<Poppler::Page> page(m_doc->page(i));
        if (page) {
            m_extractedText += page->text(QRectF());
            m_extractedText += QLatin1String("\n\n");
        }
        if (m_pdfLoadProgress && numPages > 0) {
            m_pdfLoadProgress->setValue(i + 1);
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }
    }
    m_extractedText = m_extractedText.trimmed();

    m_zoomFactor = 1.0;
    if (m_pdfLoadProgress && numPages > 0)
        m_pdfLoadProgress->setValue(numPages + 1);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    m_baseFitDpi = computeFitToWindowDpi();
    rebuildPdfPageWidgets(m_pdfLoadProgress, numPages + 1, 2 * numPages + 1);

    hideLoadProgress();

    QTimer::singleShot(0, this, [this]() {
        if (!m_doc || qAbs(m_zoomFactor - 1.0) > 1e-6)
            return;
        const double nd = computeFitToWindowDpi();
        if (qAbs(nd - m_baseFitDpi) > 10.0) {
            m_baseFitDpi = nd;
            rebuildPdfPageWidgets();
        }
    });

    if (m_btnClosePdf)
        m_btnClosePdf->setEnabled(true);
    if (m_btnPlayTts)
        m_btnPlayTts->setEnabled(true);
    if (m_btnZoomIn)
        m_btnZoomIn->setEnabled(true);
    if (m_btnZoomOut)
        m_btnZoomOut->setEnabled(true);

    if (!m_extractedText.isEmpty() && m_ttsEngine && m_ttsEngine->isAvailable())
        m_ttsEngine->speak(m_extractedText);
}

void PdfViewerForm::clearPdf()
{
    m_currentPdfPath.clear();
    m_extractedText.clear();

#ifdef HAVE_POPPLER
    if (m_doc) {
        delete m_doc;
        m_doc = nullptr;
    }
#endif

    QLayoutItem *item;
    while ((item = m_pagesLayout->takeAt(0)) != nullptr) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }

#ifdef HAVE_POPPLER
    QLabel *placeholder = new QLabel(tr("Open a PDF file to view and listen."), m_pagesContainer);
    placeholder->setObjectName("pdfPlaceholder");
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setStyleSheet("color: #6b7a8a; font-size: 13px;");
    m_pagesLayout->addWidget(placeholder);
#endif

    if (m_btnClosePdf)
        m_btnClosePdf->setEnabled(false);
    if (m_btnPlayTts)
        m_btnPlayTts->setEnabled(false);
#ifdef HAVE_POPPLER
    if (m_btnZoomIn)
        m_btnZoomIn->setEnabled(false);
    if (m_btnZoomOut)
        m_btnZoomOut->setEnabled(false);
    m_baseFitDpi = 150.0;
    m_zoomFactor = 1.0;
#endif
}

QString PdfViewerForm::extractTextFromPdf(const QString &path)
{
    Poppler::Document *doc = Poppler::Document::load(path);
    if (!doc || doc->isLocked()) {
        if (doc)
            delete doc;
        return QString();
    }

    QString text;
    const int numPages = doc->numPages();
    for (int i = 0; i < numPages; ++i) {
        std::unique_ptr<Poppler::Page> page(doc->page(i));
        if (page) {
            text += page->text(QRectF());  // null rect = all text on page
            text += QLatin1String("\n\n");
        }
    }
    delete doc;
    return text.trimmed();
}
#else
void PdfViewerForm::loadPdf(const QString &)
{
}

void PdfViewerForm::clearPdf()
{
}

QString PdfViewerForm::extractTextFromPdf(const QString &)
{
    return QString();
}
#endif
