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
#include <QCursor>
#include <QGuiApplication>
#include <QMenu>
#include <QScreen>
#include <QWindow>
#include <QEventLoop>
#include <QTimer>
#include <QDateTime>
#include <QProgressBar>
#include <QScrollBar>
#include <QRegularExpression>
#include <QFileInfo>
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
    int pageIndex() const { return m_pageIndex; }

    void setPlaybackHighlightRectsPt(const QVector<QRectF> &rectsPt)
    {
        m_playbackHighlightRectsPx.clear();
        m_playbackHighlightRectsPx.reserve(rectsPt.size());
        for (const QRectF &rPt : rectsPt)
            m_playbackHighlightRectsPx.push_back(pdfPointsToPixelRect(rPt));
        update();
    }

    void clearPlaybackHighlight()
    {
        if (!m_playbackHighlightRectsPx.empty()) {
            m_playbackHighlightRectsPx.clear();
            update();
        }
    }

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

        if (!m_playbackHighlightRectsPx.empty()) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 230, 153, 150)); // warm highlight
            for (const QRect &r : m_playbackHighlightRectsPx) {
                if (!r.isNull() && r.isValid())
                    p.drawRect(r);
            }
        }

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
        m_anchorTextBoxIndex = hitTestNearestTextBoxIndex(pos);
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
        updateSelectionFromTextRange();
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
            updateSelectionFromTextRange();
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

    int hitTestNearestTextBoxIndex(const QPoint &posPx) const
    {
        const int direct = hitTestTextBoxIndex(posPx);
        if (direct >= 0)
            return direct;

        if (m_textBoxes.empty())
            return -1;

        const QPointF posPt(posPx.x() * (72.0 / qMax(1, m_xRes)),
                            posPx.y() * (72.0 / qMax(1, m_yRes)));

        int bestIndex = -1;
        double bestDistanceSquared = 0.0;

        for (int i = 0; i < static_cast<int>(m_textBoxes.size()); ++i) {
            const QRectF r = m_textBoxes[static_cast<size_t>(i)].pdfRect;
            const QPointF c = r.center();
            const double dx = c.x() - posPt.x();
            const double dy = c.y() - posPt.y();
            const double d2 = dx * dx + dy * dy;
            if (bestIndex < 0 || d2 < bestDistanceSquared) {
                bestIndex = i;
                bestDistanceSquared = d2;
            }
        }
        return bestIndex;
    }

    std::vector<int> computeReadingOrderIndices() const
    {
        std::vector<int> order;
        order.reserve(m_textBoxes.size());
        for (int i = 0; i < static_cast<int>(m_textBoxes.size()); ++i)
            order.push_back(i);

        if (order.size() <= 1)
            return order;

        // Approximate reading order: group by line (Y), then sort by X within line.
        // We use a tolerance to avoid tiny baseline differences splitting lines.
        std::sort(order.begin(), order.end(), [this](int ai, int bi) {
            const QRectF a = m_textBoxes[static_cast<size_t>(ai)].pdfRect;
            const QRectF b = m_textBoxes[static_cast<size_t>(bi)].pdfRect;

            const qreal ay = a.center().y();
            const qreal by = b.center().y();

            const qreal aLineHeight = qMax<qreal>(1.0, a.height());
            const qreal bLineHeight = qMax<qreal>(1.0, b.height());
            const qreal lineTolerance = qMax<qreal>(2.0, qMin(aLineHeight, bLineHeight) * 0.6);

            if (qAbs(ay - by) > lineTolerance)
                return ay < by;

            const qreal ax = a.x();
            const qreal bx = b.x();
            if (ax != bx)
                return ax < bx;

            const qreal aw = a.width();
            const qreal bw = b.width();
            if (aw != bw)
                return aw < bw;

            return ai < bi;
        });

        return order;
    }

    void updateSelectionFromTextRange()
    {
        m_selectedText.clear();
        m_selectedPixelRects.clear();

        if (m_textBoxes.empty() || m_anchorTextBoxIndex < 0) {
            m_hasSelection = false;
            return;
        }

        const int currentTextBoxIndex = hitTestNearestTextBoxIndex(m_selEnd);
        if (currentTextBoxIndex < 0) {
            m_hasSelection = false;
            return;
        }

        const std::vector<int> readingOrder = computeReadingOrderIndices();
        std::vector<int> textBoxIndexToReadingPos(m_textBoxes.size(), -1);
        for (int pos = 0; pos < static_cast<int>(readingOrder.size()); ++pos)
            textBoxIndexToReadingPos[static_cast<size_t>(readingOrder[static_cast<size_t>(pos)])] = pos;

        const int anchorPos = textBoxIndexToReadingPos[static_cast<size_t>(m_anchorTextBoxIndex)];
        const int currentPos = textBoxIndexToReadingPos[static_cast<size_t>(currentTextBoxIndex)];
        if (anchorPos < 0 || currentPos < 0) {
            m_hasSelection = false;
            return;
        }

        const int startPos = qMin(anchorPos, currentPos);
        const int endPos = qMax(anchorPos, currentPos);

        QStringList parts;
        parts.reserve(endPos - startPos + 1);
        for (int pos = startPos; pos <= endPos; ++pos) {
            const int idx = readingOrder[static_cast<size_t>(pos)];
            const auto &b = m_textBoxes[static_cast<size_t>(idx)];
            const QString t = b.text.trimmed();
            if (!t.isEmpty())
                parts.append(t);
            m_selectedPixelRects.push_back(pdfPointsToPixelRect(b.pdfRect));
        }

        m_selectedText = parts.join(QLatin1Char(' ')).simplified();
        m_hasSelection = !m_selectedText.isEmpty();
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
    std::vector<QRect> m_playbackHighlightRectsPx;

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

    int m_anchorTextBoxIndex = -1;
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
#ifdef HAVE_POPPLER
    if (m_ttsEngine)
        connect(m_ttsEngine, &TtsEngine::stateChanged, this, &PdfViewerForm::onTtsStateChanged);
#endif

    if (m_ttsEngine) {
        connect(m_ttsEngine, &TtsEngine::exportFinished, this,
                [this](bool ok, const QString &wavPath, const QString &err) {
                    if (m_pdfLoadProgress) {
                        m_pdfLoadProgress->setVisible(false);
                        m_pdfLoadProgress->setRange(0, 100);
                        m_pdfLoadProgress->setValue(0);
                    }
                    if (m_btnSaveAudio)
                        m_btnSaveAudio->setEnabled(!m_currentPdfPath.isEmpty());
                    if (m_btnOpenPdf)
                        m_btnOpenPdf->setEnabled(true);
                    if (m_btnClosePdf)
                        m_btnClosePdf->setEnabled(!m_currentPdfPath.isEmpty());

                    if (!ok) {
                        QMessageBox::warning(this, tr("Export audio"),
                                             err.isEmpty() ? tr("Export failed.") : err);
                    } else {
                        QMessageBox::information(this, tr("Export audio"),
                                                 tr("Saved WAV to:\n%1").arg(wavPath));
                    }
                });
    }
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

    const QScreen *targetScreen = QGuiApplication::screenAt(QCursor::pos());
    if (!targetScreen)
        targetScreen = QGuiApplication::primaryScreen();
    if (targetScreen) {
        const QRect availableDesktopGeometry = targetScreen->availableGeometry();
        if (availableDesktopGeometry.isValid() && !availableDesktopGeometry.isNull())
            setGeometry(availableDesktopGeometry);
        else
            resize(680, 580);
    } else {
        resize(680, 580);
    }

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
    m_btnOpenPdf = new CustomButton(CustomButton::Secondary, content);
    m_btnOpenPdf->setObjectName(QStringLiteral("btnOpenPdf"));
    m_btnOpenPdf->setFixedSize(32, 32);
    m_btnOpenPdf->setIconPath(QStringLiteral(":/icons/folder-open.svg"));
    m_btnOpenPdf->setToolTip(tr("Open PDF"));
    connect(m_btnOpenPdf, &QPushButton::clicked, this, &PdfViewerForm::onOpenPdf);
    toolbarLayout->addWidget(m_btnOpenPdf);

    m_btnClosePdf = new CustomButton(CustomButton::Secondary, content);
    m_btnClosePdf->setObjectName(QStringLiteral("btnClosePdf"));
    m_btnClosePdf->setFixedSize(32, 32);
    m_btnClosePdf->setIconPath(QStringLiteral(":/icons/document-close.svg"));
    m_btnClosePdf->setToolTip(tr("Close PDF"));
    m_btnClosePdf->setEnabled(false);
    connect(m_btnClosePdf, &QPushButton::clicked, this, &PdfViewerForm::onClosePdf);
    toolbarLayout->addWidget(m_btnClosePdf);

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

    m_btnPlayTts = new CustomButton(CustomButton::Primary, content);
    m_btnPlayTts->setObjectName(QStringLiteral("btnPlayTts"));
    m_btnPlayTts->setFixedSize(32, 32);
    m_btnPlayTts->setIconPath(QStringLiteral(":/icons/play.svg"));
    m_btnPlayTts->setEnabled(false);
    m_btnPlayTts->setToolTip(tr("Play TTS"));
    connect(m_btnPlayTts, &QPushButton::clicked, this, &PdfViewerForm::onPlayTts);
    toolbarLayout->addWidget(m_btnPlayTts);

    m_btnSaveAudio = new CustomButton(CustomButton::Secondary, content);
    m_btnSaveAudio->setObjectName(QStringLiteral("btnSaveAudio"));
    m_btnSaveAudio->setFixedSize(32, 32);
    m_btnSaveAudio->setIconPath(QStringLiteral(":/icons/save.svg"));
    m_btnSaveAudio->setEnabled(false);
    m_btnSaveAudio->setToolTip(tr("Save audio…"));
    connect(m_btnSaveAudio, &QPushButton::clicked, this, &PdfViewerForm::onSaveAudio);
    toolbarLayout->addWidget(m_btnSaveAudio);

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
#ifdef HAVE_POPPLER
    if (!m_doc) {
        QMessageBox::information(this, tr("TTS"), tr("Open a PDF first."));
        return;
    }
    if (m_sentenceTtsActive) {
        stopSentenceTts();
        return;
    }
    rebuildSentenceCues();
    if (m_sentenceCues.isEmpty()) {
        QMessageBox::information(this, tr("TTS"), tr("No readable text found in this PDF."));
        return;
    }
    startSentenceTts(0);
#else
    if (m_extractedText.trimmed().isEmpty()) {
        QMessageBox::information(this, tr("TTS"), tr("No text to speak. Open a PDF first."));
        return;
    }
    m_ttsEngine->speak(m_extractedText);
#endif
}

void PdfViewerForm::onSaveAudio()
{
    if (!m_ttsEngine || !m_ttsEngine->isAvailable()) {
        QMessageBox::warning(this, tr("Export audio"), tr("TTS engine is not available."));
        return;
    }
#ifdef HAVE_POPPLER
    if (!m_doc) {
        QMessageBox::information(this, tr("Export audio"), tr("Open a PDF first."));
        return;
    }
    if (m_sentenceCues.isEmpty())
        rebuildSentenceCues();
#endif

    QStringList chunks;
#ifdef HAVE_POPPLER
    chunks.reserve(m_sentenceCues.size());
    for (const auto &cue : m_sentenceCues) {
        const QString t = cue.text.trimmed();
        if (!t.isEmpty())
            chunks.append(t);
    }
#else
    const QString text = m_extractedText.trimmed();
    if (!text.isEmpty())
        chunks.append(text);
#endif

    if (chunks.isEmpty()) {
        QMessageBox::information(this, tr("Export audio"), tr("No text to export."));
        return;
    }

    const QString base = QFileInfo(m_currentPdfPath).completeBaseName().trimmed();
    const QString suggested = base.isEmpty() ? QStringLiteral("crystaltts.wav")
                                             : (base + QStringLiteral(".wav"));
    const QString outPath = QFileDialog::getSaveFileName(
        this,
        tr("Save audio"),
        suggested,
        tr("WAV audio (*.wav);;All files (*)"));
    if (outPath.isEmpty())
        return;

    if (m_btnSaveAudio)
        m_btnSaveAudio->setEnabled(false);
    if (m_btnOpenPdf)
        m_btnOpenPdf->setEnabled(false);
    if (m_btnClosePdf)
        m_btnClosePdf->setEnabled(false);

    if (m_pdfLoadProgress) {
        m_pdfLoadProgress->setVisible(true);
        m_pdfLoadProgress->setRange(0, 0);
        m_pdfLoadProgress->setFormat(tr("Exporting audio…"));
    }

    // Export sentence-by-sentence to avoid huge single-text synthesis.
    m_ttsEngine->exportWavChunks(chunks, outPath);
}

#ifdef HAVE_POPPLER
namespace {
struct TextBoxPt {
    QRectF rectPt;
    QString text;
};

struct TextItemPt {
    QString text;
    QRectF rectPt;
    double fontSize = 0.0; // heuristic: rect height
    bool isBold = false;
};

struct LinePt {
    QVector<TextItemPt> items;
    QString text;
    QRectF rectPt;
    double avgFontSize = 0.0;
    bool mostlyBold = false;

    double left() const { return rectPt.left(); }
    double right() const { return rectPt.right(); }
    double top() const { return rectPt.top(); }
    double bottom() const { return rectPt.bottom(); }
};

struct ParagraphPt {
    QVector<LinePt> lines;
    QString text;
    QRectF rectPt;
    double avgFontSize = 0.0;
    bool mostlyBold = false;
};

enum class BlockTypePt { Title, Subtitle, Body };

static QRectF uniteRectsPt(const QRectF &a, const QRectF &b)
{
    if (a.isNull())
        return b;
    if (b.isNull())
        return a;
    return a.united(b);
}

static QString normalizeSpacesLocal(QString s)
{
    s.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return s.trimmed();
}

static bool endsWithTerminalPunctuationLocal(const QString &s)
{
    const QString t = s.trimmed();
    return t.endsWith(QLatin1Char('.')) || t.endsWith(QLatin1Char('!')) || t.endsWith(QLatin1Char('?'));
}

static bool isLikelyHeadingTextLocal(const QString &s)
{
    const QString t = s.trimmed();
    if (t.isEmpty())
        return false;
    if (t.length() > 120)
        return false;
    if (endsWithTerminalPunctuationLocal(t))
        return false;
    return true;
}

static std::vector<int> computeReadingOrderForCue(const std::vector<TextBoxPt> &boxes)
{
    std::vector<int> order;
    order.reserve(boxes.size());
    for (int i = 0; i < static_cast<int>(boxes.size()); ++i)
        order.push_back(i);

    std::sort(order.begin(), order.end(), [&boxes](int ai, int bi) {
        const QRectF a = boxes[static_cast<size_t>(ai)].rectPt;
        const QRectF b = boxes[static_cast<size_t>(bi)].rectPt;

        const qreal ay = a.center().y();
        const qreal by = b.center().y();

        const qreal aLineHeight = qMax<qreal>(1.0, a.height());
        const qreal bLineHeight = qMax<qreal>(1.0, b.height());
        const qreal lineTolerance = qMax<qreal>(2.0, qMin(aLineHeight, bLineHeight) * 0.6);

        if (qAbs(ay - by) > lineTolerance)
            return ay < by;

        const qreal ax = a.x();
        const qreal bx = b.x();
        if (ax != bx)
            return ax < bx;

        return ai < bi;
    });

    return order;
}

static QVector<TextItemPt> extractTextItemsPt(Poppler::Page *page)
{
    QVector<TextItemPt> out;
    if (!page)
        return out;
    const QList<Poppler::TextBox *> boxes = page->textList();
    out.reserve(boxes.size());
    for (Poppler::TextBox *box : boxes) {
        if (!box)
            continue;
        TextItemPt item;
        item.text = normalizeSpacesLocal(box->text());
        item.rectPt = box->boundingBox();
        item.fontSize = item.rectPt.height();
        item.isBold = false;
        if (!item.text.isEmpty() && item.rectPt.isValid() && !item.rectPt.isNull())
            out.push_back(item);
        delete box;
    }
    return out;
}

static bool sameVisualLineLocal(const TextItemPt &a, const TextItemPt &b, double yTolerance)
{
    return qAbs(a.rectPt.center().y() - b.rectPt.center().y()) <= yTolerance;
}

static QVector<LinePt> groupItemsIntoLinesLocal(QVector<TextItemPt> items)
{
    QVector<LinePt> lines;
    if (items.isEmpty())
        return lines;

    std::sort(items.begin(), items.end(), [](const TextItemPt &a, const TextItemPt &b) {
        const double ay = a.rectPt.center().y();
        const double by = b.rectPt.center().y();
        if (!qFuzzyCompare(ay + 1.0, by + 1.0))
            return ay < by;
        return a.rectPt.left() < b.rectPt.left();
    });

    double sumH = 0.0;
    for (const auto &it : items)
        sumH += it.rectPt.height();
    const double avgHeight = items.isEmpty() ? 0.0 : (sumH / items.size());
    const double yTolerance = qMax(2.0, avgHeight * 0.4);

    QVector<QVector<TextItemPt>> buckets;
    for (const TextItemPt &item : items) {
        bool placed = false;
        for (auto &bucket : buckets) {
            if (!bucket.isEmpty() && sameVisualLineLocal(bucket.first(), item, yTolerance)) {
                bucket.push_back(item);
                placed = true;
                break;
            }
        }
        if (!placed)
            buckets.push_back({item});
    }

    lines.reserve(buckets.size());
    for (auto &bucket : buckets) {
        std::sort(bucket.begin(), bucket.end(), [](const TextItemPt &a, const TextItemPt &b) {
            return a.rectPt.left() < b.rectPt.left();
        });

        LinePt line;
        QRectF united;
        double fontSum = 0.0;
        int boldCount = 0;
        QStringList parts;
        for (const auto &it : bucket) {
            line.items.push_back(it);
            united = uniteRectsPt(united, it.rectPt);
            fontSum += it.fontSize;
            if (it.isBold)
                ++boldCount;
            parts << it.text;
        }
        line.rectPt = united;
        line.avgFontSize = bucket.isEmpty() ? 0.0 : fontSum / bucket.size();
        line.mostlyBold = !bucket.isEmpty() && (boldCount >= (bucket.size() / 2.0));
        line.text = normalizeSpacesLocal(parts.join(QLatin1Char(' ')));
        lines.push_back(line);
    }
    return lines;
}

static QString mergeLineTextsLocal(QString left, QString right)
{
    left = left.trimmed();
    right = right.trimmed();
    if (left.isEmpty())
        return right;
    if (right.isEmpty())
        return left;
    if (left.endsWith(QLatin1Char('-'))) {
        left.chop(1);
        return left + right;
    }
    return left + QLatin1Char(' ') + right;
}

static double computeMedianLineGapLocal(const QVector<LinePt> &lines)
{
    QVector<double> gaps;
    for (int i = 1; i < lines.size(); ++i) {
        const double gap = lines[i].top() - lines[i - 1].bottom();
        if (gap >= 0.0)
            gaps.push_back(gap);
    }
    if (gaps.isEmpty())
        return 4.0;
    std::sort(gaps.begin(), gaps.end());
    return gaps[gaps.size() / 2];
}

static double computeTypicalLeftLocal(const QVector<LinePt> &lines)
{
    if (lines.isEmpty())
        return 0.0;
    QVector<double> lefts;
    lefts.reserve(lines.size());
    for (const auto &l : lines)
        lefts.push_back(l.left());
    std::sort(lefts.begin(), lefts.end());
    return lefts[lefts.size() / 2];
}

static bool isParagraphContinuationLocal(const LinePt &prev, const LinePt &curr, double medianLineGap, double indentThreshold)
{
    const QString prevText = prev.text.trimmed();
    if (prevText.isEmpty())
        return true;

    auto startsWithBulletMarker = [](const QString &t) {
        const QString s = t.trimmed();
        return s.startsWith(QChar(0x2022)) /* • */
            || s.startsWith(QChar(0x25CF)) /* ● */
            || s.startsWith(QLatin1Char('-'))
            || s.startsWith(QLatin1Char('*'));
    };

    auto startsWithOrderedListMarker = [](const QString &t) {
        const QString s = t.trimmed();
        if (s.size() < 2)
            return false;
        int i = 0;
        while (i < s.size() && s[i].isDigit())
            ++i;
        if (i == 0 || i >= s.size())
            return false;
        return s[i] == QLatin1Char('.') || s[i] == QLatin1Char(')') || s[i] == QLatin1Char(':');
    };

    auto firstNonSpace = [](const QString &t) -> QChar {
        for (int i = 0; i < t.size(); ++i) {
            if (!t[i].isSpace())
                return t[i];
        }
        return QChar();
    };

    const QString currText = curr.text.trimmed();

    // Prevent title/subtitle/tagline from being merged into one paragraph:
    // a noticeable font-size change between adjacent lines is usually a block boundary.
    if (prev.avgFontSize > 0.0 && curr.avgFontSize > 0.0) {
        const double ratio = curr.avgFontSize / prev.avgFontSize;
        const double invRatio = prev.avgFontSize / curr.avgFontSize;
        const double fontChange = qMax(ratio, invRatio);
        if (fontChange >= 1.18) {
            const bool bothShort = prevText.size() <= 140 && currText.size() <= 140;
            if (bothShort)
                return false;
        }
    }

    // Avoid merging a heading line with the first body sentence.
    // Headings are usually short and don't end with terminal punctuation.
    const bool prevLooksLikeHeading = isLikelyHeadingTextLocal(prevText) && prevText.size() <= 120;
    if (prevLooksLikeHeading && !startsWithBulletMarker(currText) && !startsWithOrderedListMarker(currText)) {
        const QChar c0 = firstNonSpace(curr.text);
        const bool currLooksLikeSentence = (c0.isUpper() || c0.isDigit());
        const bool prevEmphasized = prev.mostlyBold || (prev.avgFontSize > 0.0 && curr.avgFontSize > 0.0
                                                       && prev.avgFontSize >= curr.avgFontSize * 1.06);
        if (currLooksLikeSentence && prevEmphasized)
            return false;
    }

    const double verticalGap = curr.top() - prev.bottom();
    if (verticalGap > qMax(8.0, medianLineGap * 1.8))
        return false;

    const double indentDelta = curr.left() - prev.left();
    if (indentDelta > indentThreshold) {
        // Hanging-indent continuation for bullets / list items.
        // Example:
        //   ● Plus Plan: Includes ... everything you
        //     need for smarter...
        const bool prevIsListItem = startsWithBulletMarker(prevText) || startsWithOrderedListMarker(prevText);
        if (!prevIsListItem)
            return false;
        // If we're in a list item, only keep merging when the previous line didn't already look complete.
        if (endsWithTerminalPunctuationLocal(prevText))
            return false;
    }

    if (endsWithTerminalPunctuationLocal(prevText))
        return false;

    return true;
}

static QVector<ParagraphPt> groupLinesIntoParagraphsLocal(const QVector<LinePt> &lines)
{
    QVector<ParagraphPt> paragraphs;
    if (lines.isEmpty())
        return paragraphs;

    const double medianLineGap = computeMedianLineGapLocal(lines);
    const double typicalLeft = computeTypicalLeftLocal(lines);
    const double indentThreshold = 12.0;

    ParagraphPt current;
    auto flush = [&]() {
        if (current.lines.isEmpty())
            return;
        QRectF united;
        double fontSum = 0.0;
        int boldCount = 0;
        QString text;
        for (const auto &line : current.lines) {
            united = uniteRectsPt(united, line.rectPt);
            fontSum += line.avgFontSize;
            if (line.mostlyBold)
                ++boldCount;
            text = text.isEmpty() ? line.text.trimmed() : mergeLineTextsLocal(text, line.text);
        }
        current.rectPt = united;
        current.avgFontSize = current.lines.isEmpty() ? 0.0 : fontSum / current.lines.size();
        current.mostlyBold = !current.lines.isEmpty() && (boldCount >= (current.lines.size() / 2.0));
        current.text = normalizeSpacesLocal(text);
        paragraphs.push_back(current);
        current = ParagraphPt{};
    };

    for (int i = 0; i < lines.size(); ++i) {
        const LinePt &line = lines[i];
        if (current.lines.isEmpty()) {
            current.lines.push_back(line);
            continue;
        }
        const LinePt &prev = current.lines.last();
        bool cont = isParagraphContinuationLocal(prev, line, medianLineGap, indentThreshold);
        if (!cont) {
            const bool similarToBodyLeft = qAbs(prev.left() - typicalLeft) < 10.0 && qAbs(line.left() - typicalLeft) < 10.0;
            const double gap = line.top() - prev.bottom();
            if (similarToBodyLeft && gap <= qMax(6.0, medianLineGap * 1.2) && !endsWithTerminalPunctuationLocal(prev.text))
                cont = true;
        }
        if (cont)
            current.lines.push_back(line);
        else {
            flush();
            current.lines.push_back(line);
        }
    }
    flush();
    return paragraphs;
}

static double computeBodyFontEstimateLocal(const QVector<ParagraphPt> &paragraphs)
{
    if (paragraphs.isEmpty())
        return 10.0;
    QVector<double> fonts;
    fonts.reserve(paragraphs.size());
    for (const auto &p : paragraphs) {
        if (!p.text.isEmpty())
            fonts.push_back(p.avgFontSize);
    }
    if (fonts.isEmpty())
        return 10.0;
    std::sort(fonts.begin(), fonts.end());
    return fonts[fonts.size() / 2];
}

static BlockTypePt classifyParagraphLocal(const ParagraphPt &p, double bodyFontEstimate, bool firstBlockOnPage)
{
    const QString text = p.text.trimmed();
    if (text.isEmpty())
        return BlockTypePt::Body;

    const bool headingLike = isLikelyHeadingTextLocal(text);
    const bool larger = p.avgFontSize > bodyFontEstimate * 1.35;
    const bool somewhatLarger = p.avgFontSize > bodyFontEstimate * 1.15;

    if (headingLike && larger) {
        if (firstBlockOnPage || text.size() < 80)
            return BlockTypePt::Title;
    }
    if (headingLike && (somewhatLarger || p.mostlyBold))
        return BlockTypePt::Subtitle;
    return BlockTypePt::Body;
}

static bool isCommonAbbreviationBeforeDot(const QString &text, int dotIndex)
{
    // Adapted from splogic.txt: keep a conservative, explicit list.
    static const QSet<QString> abbreviations = {
        QStringLiteral("mr"), QStringLiteral("mrs"), QStringLiteral("ms"),
        QStringLiteral("dr"), QStringLiteral("prof"), QStringLiteral("sr"), QStringLiteral("jr"),
        QStringLiteral("vs"), QStringLiteral("etc"),
        QStringLiteral("e.g"), QStringLiteral("i.e"),
        QStringLiteral("fig"), QStringLiteral("no"),
        QStringLiteral("st"), QStringLiteral("mt"),
        QStringLiteral("inc"), QStringLiteral("ltd"), QStringLiteral("co")
    };

    int start = dotIndex - 1;
    while (start >= 0 && text[start].isLetter())
        --start;
    const QString word = text.mid(start + 1, dotIndex - start - 1).toLower();
    if (abbreviations.contains(word))
        return true;

    // Single-letter initials: "A."
    if (word.size() == 1 && word[0].isLetter())
        return true;

    return false;
}

static QVector<QPair<int, int>> splitSentenceRangesHeuristic(const QString &s, int maxSentenceLen = 240)
{
    QVector<QPair<int, int>> ranges;
    const int n = s.size();
    int start = 0;

    auto pushRange = [&](int a, int b) {
        a = qMax(0, a);
        b = qMin(n, b);
        while (a < b && s[a].isSpace())
            ++a;
        while (b > a && s[b - 1].isSpace())
            --b;
        if (b > a)
            ranges.append(qMakePair(a, b));
    };

    auto isDashLike = [](QChar c) {
        return c == QLatin1Char('-') || c == QChar(0x2013) /* – */ || c == QChar(0x2014) /* — */;
    };

    auto findClauseCutNear = [&](int a, int limit) -> int {
        // Pick a "good" cut point near limit, preferring punctuation that sounds like a natural pause.
        // Returns an index inside (a, n]; caller decides +1 rules.
        const int minKeep = qMax(a + 60, a + maxSentenceLen / 3); // avoid tiny fragments
        const int lo = qMin(qMax(minKeep, a + 1), n);
        const int hi = qMin(qMax(limit, lo), n - 1);

        auto scoreAt = [&](int j) -> int {
            const QChar cc = s[j];
            if (cc == QLatin1Char(';'))
                return 100;
            if (cc == QLatin1Char(':'))
                return 95;
            if (cc == QLatin1Char(','))
                return 90;
            if (isDashLike(cc))
                return 85;
            if (cc == QLatin1Char(')') || cc == QLatin1Char(']'))
                return 80;
            if (cc.isSpace())
                return 30;
            return 0;
        };

        int bestJ = -1;
        int bestScore = -1;
        for (int j = hi; j >= lo; --j) {
            const int sc = scoreAt(j);
            if (sc <= 0)
                continue;
            // Prefer cutting *after* punctuation/space, but avoid splitting mid-word.
            if (!s[j].isSpace() && j + 1 < n && !s[j + 1].isSpace() && !isDashLike(s[j]))
                continue;
            if (sc > bestScore) {
                bestScore = sc;
                bestJ = j;
                if (bestScore >= 95) // ';' or ':' is good enough
                    break;
            }
        }
        return bestJ;
    };

    for (int i = 0; i < n; ++i) {
        const QChar c = s[i];
        const bool punctEnd = (c == QLatin1Char('.') || c == QLatin1Char('!') || c == QLatin1Char('?'));

        if (!punctEnd)
            continue;

        int end = i + 1;
        if (c == QLatin1Char('.')) {
            // Avoid splitting abbreviations like "Dr." or "e.g."
            if (isCommonAbbreviationBeforeDot(s, i))
                continue;
            // Avoid splitting decimals like 3.14
            if (i > 0 && i + 1 < n && s[i - 1].isDigit() && s[i + 1].isDigit())
                continue;
        }

        // Boundary if next char is space/end/quote/bracket (same as splogic).
        bool boundary = false;
        if (i + 1 >= n) {
            boundary = true;
        } else {
            const QChar next = s[i + 1];
            if (next.isSpace() || next == QLatin1Char('"') || next == QLatin1Char('\'')
                || next == QLatin1Char(')') || next == QLatin1Char(']')) {
                boundary = true;
            }
        }
        if (!boundary)
            continue;

        // include trailing quotes/brackets in the sentence
        while (end < n) {
            const QChar cc = s[end];
            if (cc == QLatin1Char('"') || cc == QLatin1Char('\'') || cc == QLatin1Char(')')
                || cc == QLatin1Char(']')) {
                ++end;
            } else {
                break;
            }
        }

        pushRange(start, end);
        start = end;
        while (start < n && s[start].isSpace())
            ++start;
    }

    if (start < n)
        pushRange(start, n);

    if (maxSentenceLen > 0) {
        QVector<QPair<int, int>> out;
        for (const auto &r : ranges) {
            int a = r.first;
            const int b = r.second;
            while (b - a > maxSentenceLen) {
                int cut = -1;
                const int limit = a + maxSentenceLen;
                const int best = findClauseCutNear(a, qMin(limit, b - 1));
                if (best >= 0) {
                    cut = best + 1;
                } else {
                    cut = limit;
                }
                out.append(qMakePair(a, cut));
                a = cut;
            }
            if (b > a)
                out.append(qMakePair(a, b));
        }
        ranges = out;
    }

    return ranges;
}
} // namespace

void PdfViewerForm::rebuildSentenceCues()
{
    m_sentenceCues.clear();
    m_extractedText.clear();
    if (!m_doc)
        return;

    const int numPages = m_doc->numPages();
    for (int pageIndex = 0; pageIndex < numPages; ++pageIndex) {
        std::unique_ptr<Poppler::Page> page(m_doc->page(pageIndex));
        if (!page)
            continue;

        QVector<TextItemPt> items = extractTextItemsPt(page.get());
        QVector<LinePt> lines = groupItemsIntoLinesLocal(items);
        QVector<ParagraphPt> paragraphs = groupLinesIntoParagraphsLocal(lines);
        if (paragraphs.isEmpty())
            continue;

        const double bodyFontEstimate = computeBodyFontEstimateLocal(paragraphs);

        for (int pi = 0; pi < paragraphs.size(); ++pi) {
            const ParagraphPt &p = paragraphs[pi];
            const BlockTypePt type = classifyParagraphLocal(p, bodyFontEstimate, pi == 0);
            const QString paraText = p.text;
            if (paraText.trimmed().isEmpty())
                continue;

            // Build spans by concatenating the underlying text items in reading order.
            // (Good-enough mapping to highlight blocks/sentences.)
            QVector<TextBoxPt> flat;
            for (const auto &ln : p.lines) {
                for (const auto &it : ln.items)
                    flat.append(TextBoxPt{it.rectPt, it.text});
            }
            std::vector<TextBoxPt> flatVec;
            flatVec.reserve(static_cast<size_t>(flat.size()));
            for (const auto &x : flat)
                flatVec.push_back(x);
            const std::vector<int> order = computeReadingOrderForCue(flatVec);

            struct Span {
                int start = 0;
                int end = 0;
                QRectF rectPt;
            };
            QVector<Span> spans;
            spans.reserve(static_cast<int>(order.size()));

            QString merged;
            merged.reserve(2048);
            for (size_t oi = 0; oi < order.size(); ++oi) {
                const TextBoxPt &b = flatVec[static_cast<size_t>(order[oi])];
                if (!merged.isEmpty() && !merged.endsWith(QLatin1Char(' ')))
                    merged += QLatin1Char(' ');
                const int st = merged.size();
                merged += b.text;
                const int en = merged.size();
                spans.append(Span{st, en, b.rectPt});
            }
            merged = normalizeSpacesLocal(merged);
            if (merged.isEmpty())
                continue;

            if (type == BlockTypePt::Body) {
                const auto sentenceRanges = splitSentenceRangesHeuristic(merged);
                for (const auto &r : sentenceRanges) {
                    const QString sentence = merged.mid(r.first, r.second - r.first).trimmed();
                    if (sentence.isEmpty())
                        continue;
                    SentenceCue cue;
                    cue.pageIndex = pageIndex;
                    cue.text = sentence;
                    for (const Span &sp : spans) {
                        if (sp.end <= r.first || sp.start >= r.second)
                            continue;
                        cue.rectsPt.append(sp.rectPt);
                    }
                    if (!cue.rectsPt.isEmpty())
                        m_sentenceCues.append(std::move(cue));
                }
            } else {
                // Title / Subtitle: keep as a single unit (not sentence-split) and don't merge into body.
                SentenceCue cue;
                cue.pageIndex = pageIndex;
                cue.text = merged;
                for (const Span &sp : spans)
                    cue.rectsPt.append(sp.rectPt);
                if (!cue.rectsPt.isEmpty())
                    m_sentenceCues.append(std::move(cue));
            }

            m_extractedText += merged;
            m_extractedText += QLatin1String("\n\n");
        }
    }
    m_extractedText = m_extractedText.trimmed();
}

void PdfViewerForm::startSentenceTts(int startIndex)
{
    if (!m_ttsEngine || m_sentenceCues.isEmpty())
        return;
    m_sentenceTtsActive = true;
    m_currentCueIndex = qBound(0, startIndex, m_sentenceCues.size() - 1);
    speakNextSentence();
}

void PdfViewerForm::stopSentenceTts()
{
    m_sentenceTtsActive = false;
    m_currentCueIndex = -1;
    setPlaybackHighlight(nullptr);
    if (m_btnPlayTts)
        m_btnPlayTts->setIconPath(QStringLiteral(":/icons/play.svg"));
    if (m_ttsEngine)
        m_ttsEngine->stop();
}

void PdfViewerForm::speakNextSentence()
{
    if (!m_sentenceTtsActive || !m_ttsEngine)
        return;
    if (m_currentCueIndex < 0 || m_currentCueIndex >= m_sentenceCues.size()) {
        stopSentenceTts();
        return;
    }
    const SentenceCue &cue = m_sentenceCues[m_currentCueIndex];
    setPlaybackHighlight(&cue);
    scrollToCue(cue);
    m_ttsEngine->speak(cue.text);
}

void PdfViewerForm::setPlaybackHighlight(const SentenceCue *cue)
{
    if (!m_pagesContainer)
        return;
    for (QWidget *cw : m_pagesContainer->findChildren<QWidget *>()) {
        auto *pw = dynamic_cast<PdfPageWidget *>(cw);
        if (!pw)
            continue;
        if (cue && pw->pageIndex() == cue->pageIndex)
            pw->setPlaybackHighlightRectsPt(cue->rectsPt);
        else
            pw->clearPlaybackHighlight();
    }
}

void PdfViewerForm::scrollToCue(const SentenceCue &cue)
{
    if (!m_scrollArea || !m_pagesContainer)
        return;
    PdfPageWidget *target = nullptr;
    for (QWidget *cw : m_pagesContainer->findChildren<QWidget *>()) {
        auto *pw = dynamic_cast<PdfPageWidget *>(cw);
        if (pw && pw->pageIndex() == cue.pageIndex) {
            target = pw;
            break;
        }
    }
    if (!target)
        return;

    m_scrollArea->ensureWidgetVisible(target, 20, 20);

    if (!cue.rectsPt.isEmpty()) {
        // Aim near top of the page (good enough for page-change scrolling).
        const QPoint anchorInContainer = target->mapTo(m_pagesContainer, QPoint(0, 0));
        if (QScrollBar *sb = m_scrollArea->verticalScrollBar())
            sb->setValue(qMax(0, anchorInContainer.y() - 20));
    }
}

void PdfViewerForm::onTtsStateChanged(int state)
{
    const int prev = m_lastTtsState;
    m_lastTtsState = state;

    if (m_btnPlayTts) {
        const bool speaking = m_sentenceTtsActive && (state == TtsEngine::Speaking);
        m_btnPlayTts->setIconPath(speaking ? QStringLiteral(":/icons/pause.svg")
                                           : QStringLiteral(":/icons/play.svg"));
    }

    if (!m_sentenceTtsActive)
        return;
    if (state == TtsEngine::Ready && prev == TtsEngine::Speaking) {
        ++m_currentCueIndex;
        speakNextSentence();
    } else if (state == TtsEngine::BackendError) {
        stopSentenceTts();
    }
}

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
    m_sentenceCues.clear();
    m_sentenceTtsActive = false;
    m_currentCueIndex = -1;
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

    // Avoid extracting the entire document text up-front.
    // Sentence cues are built on-demand when the user starts TTS.
    for (int i = 0; i < numPages; ++i) {
        if (m_pdfLoadProgress && numPages > 0) {
            m_pdfLoadProgress->setValue(i + 1);
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }
    }

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
    if (m_btnSaveAudio)
        m_btnSaveAudio->setEnabled(true);
    if (m_btnZoomIn)
        m_btnZoomIn->setEnabled(true);
    if (m_btnZoomOut)
        m_btnZoomOut->setEnabled(true);

    // Don't auto-start TTS here.
}

void PdfViewerForm::clearPdf()
{
    stopSentenceTts();
    m_sentenceCues.clear();
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
    if (m_btnSaveAudio)
        m_btnSaveAudio->setEnabled(false);
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
