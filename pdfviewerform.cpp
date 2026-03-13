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

#ifdef HAVE_POPPLER
#include <poppler/qt5/poppler-qt5.h>
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

    Poppler::Document *doc = Poppler::Document::load(path);
    if (!doc) {
        QMessageBox::warning(this, tr("PDF"), tr("Could not load PDF: %1").arg(path));
        return;
    }
    if (doc->isLocked()) {
        delete doc;
        QMessageBox::warning(this, tr("PDF"), tr("PDF is locked."));
        return;
    }

    m_currentPdfPath = path;
    m_extractedText.clear();
    const int numPages = doc->numPages();
    const double scale = 1.0;  // resolution scale for rendering
    const int xRes = 150 * scale;
    const int yRes = 150 * scale;

    for (int i = 0; i < numPages; ++i) {
        Poppler::Page *page = doc->page(i);
        if (!page)
            continue;

        m_extractedText += page->text(QRectF());  // null rect = all text on page
        m_extractedText += QLatin1String("\n\n");

        QImage image = page->renderToImage(xRes, yRes);
        delete page;
        if (image.isNull())
            continue;

        QLabel *pageLabel = new QLabel(m_pagesContainer);
        pageLabel->setPixmap(QPixmap::fromImage(image));
        pageLabel->setAlignment(Qt::AlignCenter);
        pageLabel->setStyleSheet("background: white; border: 1px solid #ddd; border-radius: 4px; padding: 4px;");
        m_pagesLayout->addWidget(pageLabel);
    }

    m_extractedText = m_extractedText.trimmed();
    delete doc;

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
        Poppler::Page *page = doc->page(i);
        if (page) {
            text += page->text(QRectF());  // null rect = all text on page
            text += QLatin1String("\n\n");
            delete page;
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
