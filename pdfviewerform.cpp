#include "pdfviewerform.h"
#include "tts/ttsengine.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QFrame>
#include <QStyle>
#include <QPainter>
#include <QMouseEvent>
#include <QClipboard>
#include <QApplication>
#include <QMenu>
#include <memory>
#include <vector>
#include <algorithm>

#ifdef HAVE_POPPLER
#include <poppler/qt5/poppler-qt5.h>
#endif

#ifdef HAVE_POPPLER
namespace {

class PdfPageWidget final : public QWidget
{
public:
    PdfPageWidget(Poppler::Document *doc,
                  int pageIndex,
                  int xRes,
                  int yRes,
                  const QImage &rendered,
                  QWidget *parent = nullptr)
        : QWidget(parent)
        , m_doc(doc)
        , m_pageIndex(pageIndex)
        , m_xRes(xRes)
        , m_yRes(yRes)
        , m_image(rendered)
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

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawImage(QPoint(0, 0), m_image);

        if (m_hasSelection) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(60, 120, 215, 85));
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
        m_hasSelection = true;
        m_selecting = true;
        m_selStart = clampToPage(e->pos());
        m_selEnd = m_selStart;
        updateHoverCursor(m_selStart);
        updateSelectionFromBoxes();
        update();
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        const QPoint pos = clampToPage(e->pos());
        if (!m_selecting) {
            updateHoverCursor(pos);
            return;
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
            update();
            return;
        }
        m_selecting = false;
        m_selEnd = clampToPage(e->pos());
        updateSelectionFromBoxes();
        update();
    }

    void mouseDoubleClickEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton)
            return;
        setFocus(Qt::MouseFocusReason);
        const QPoint pos = clampToPage(e->pos());
        updateHoverCursor(pos);

        const int idx = hitTestTextBoxIndex(pos);
        if (idx < 0)
            return;

        m_hasSelection = true;
        m_selecting = false;
        m_skipNextReleaseUpdate = true;
        selectLineAtIndex(idx);
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
        QAction *copyAct = menu.addAction(tr("Copy"));
        copyAct->setEnabled(!m_selectedText.trimmed().isEmpty());
        QAction *clearAct = menu.addAction(tr("Clear selection"));
        QAction *chosen = menu.exec(e->globalPos());
        if (chosen == copyAct) {
            copySelectionToClipboard();
        } else if (chosen == clearAct) {
            clearSelection();
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

    static bool isSentenceTerminator(QChar c)
    {
        // A pragmatic set; feel free to add more for your language needs.
        return c == QLatin1Char('.') || c == QLatin1Char('!') || c == QLatin1Char('?')
            || c == QChar(0x3002) /* 。 */ || c == QChar(0xFF01) /* ！ */ || c == QChar(0xFF1F) /* ？ */;
    }

    static bool endsSentence(const QString &t)
    {
        const QString s = t.trimmed();
        if (s.isEmpty())
            return false;
        // Skip trailing quotes/brackets.
        int i = s.size() - 1;
        while (i >= 0) {
            const QChar c = s.at(i);
            if (c.isSpace())
                --i;
            else if (c == QLatin1Char('"') || c == QLatin1Char('\'') || c == QLatin1Char(')') || c == QLatin1Char(']') || c == QLatin1Char('}'))
                --i;
            else
                break;
        }
        if (i < 0)
            return false;
        return isSentenceTerminator(s.at(i));
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

    std::vector<TextBoxItem> m_textBoxes;
    std::vector<QRect> m_selectedPixelRects;

    bool m_skipNextReleaseUpdate = false;
    bool m_hasSelection = false;
    bool m_selecting = false;
    QPoint m_selStart;
    QPoint m_selEnd;
    QString m_selectedText;
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

void PdfViewerForm::setupUi()
{
    QVBoxLayout *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(12, 12, 12, 12);
    rootLayout->setSpacing(10);

    // Toolbar: Open, Close, Play TTS
    QHBoxLayout *toolbarLayout = new QHBoxLayout();
    toolbarLayout->setSpacing(8);

#ifdef HAVE_POPPLER
    m_btnOpen = new QPushButton(tr("Open PDF"), this);
    m_btnOpen->setObjectName("btnOpenPdf");
    m_btnOpen->setMinimumHeight(32);
    connect(m_btnOpen, &QPushButton::clicked, this, &PdfViewerForm::onOpenPdf);
    toolbarLayout->addWidget(m_btnOpen);

    m_btnClose = new QPushButton(tr("Close PDF"), this);
    m_btnClose->setObjectName("btnClosePdf");
    m_btnClose->setMinimumHeight(32);
    m_btnClose->setEnabled(false);
    connect(m_btnClose, &QPushButton::clicked, this, &PdfViewerForm::onClosePdf);
    toolbarLayout->addWidget(m_btnClose);

    m_btnPlayTts = new QPushButton(tr("Play TTS"), this);
    m_btnPlayTts->setObjectName("btnPlayTts");
    m_btnPlayTts->setMinimumHeight(32);
    m_btnPlayTts->setEnabled(false);
    connect(m_btnPlayTts, &QPushButton::clicked, this, &PdfViewerForm::onPlayTts);
    toolbarLayout->addWidget(m_btnPlayTts);
#else
    QLabel *noPopplerLabel = new QLabel(
        tr("PDF support not built. Set POPPLER_DIR in CrystalTts.pro and rebuild with Poppler Qt5 development files."),
        this);
    noPopplerLabel->setWordWrap(true);
    noPopplerLabel->setAlignment(Qt::AlignCenter);
    toolbarLayout->addWidget(noPopplerLabel);
#endif
    toolbarLayout->addStretch();
    rootLayout->addLayout(toolbarLayout);

    // Scroll area for PDF pages
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setObjectName("pdfScrollArea");
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setAlignment(Qt::AlignCenter);
    m_scrollArea->setStyleSheet(
        "QScrollArea#pdfScrollArea {"
        "  background-color: #f5f5f5;"
        "  border: 1px solid #d0e4ff;"
        "  border-radius: 6px;"
        "}"
    );

    m_pagesContainer = new QWidget(this);
    m_pagesLayout = new QVBoxLayout(m_pagesContainer);
    m_pagesLayout->setContentsMargins(8, 8, 8, 8);
    m_pagesLayout->setSpacing(12);
    m_pagesLayout->setAlignment(Qt::AlignTop | Qt::AlignHCenter);

#ifdef HAVE_POPPLER
    QLabel *placeholder = new QLabel(tr("Open a PDF file to view and listen."), m_pagesContainer);
    placeholder->setObjectName("pdfPlaceholder");
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setStyleSheet("color: #6b7a8a; font-size: 13px;");
    m_pagesLayout->addWidget(placeholder);
#else
    (void)m_btnOpen;
    (void)m_btnClose;
    (void)m_btnPlayTts;
#endif

    m_scrollArea->setWidget(m_pagesContainer);
    rootLayout->addWidget(m_scrollArea);

    setWindowTitle(tr("PDF Viewer"));
    setWindowFlags(Qt::Window);
    setMinimumSize(500, 400);
    resize(640, 560);
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
    const double scale = 1.0;  // resolution scale for rendering
    const int xRes = 150 * scale;
    const int yRes = 150 * scale;

    for (int i = 0; i < numPages; ++i) {
        std::unique_ptr<Poppler::Page> page(m_doc->page(i));
        if (!page) {
            continue;
        }

        m_extractedText += page->text(QRectF());  // null rect = all text on page
        m_extractedText += QLatin1String("\n\n");

        QImage image = page->renderToImage(xRes, yRes);
        if (image.isNull())
            continue;

        QFrame *frame = new QFrame(m_pagesContainer);
        frame->setStyleSheet("QFrame { background: white; border: 1px solid #ddd; border-radius: 4px; }");
        QVBoxLayout *frameLayout = new QVBoxLayout(frame);
        frameLayout->setContentsMargins(4, 4, 4, 4);
        frameLayout->setSpacing(0);

        PdfPageWidget *pageWidget = new PdfPageWidget(m_doc, i, xRes, yRes, image, frame);
        frameLayout->addWidget(pageWidget);
        m_pagesLayout->addWidget(frame);
    }

    m_extractedText = m_extractedText.trimmed();

    if (m_btnClose)
        m_btnClose->setEnabled(true);
    if (m_btnPlayTts)
        m_btnPlayTts->setEnabled(true);

    // Auto-play TTS once PDF is loaded
    if (!m_extractedText.trimmed().isEmpty() && m_ttsEngine && m_ttsEngine->isAvailable())
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

    if (m_btnClose)
        m_btnClose->setEnabled(false);
    if (m_btnPlayTts)
        m_btnPlayTts->setEnabled(false);
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
