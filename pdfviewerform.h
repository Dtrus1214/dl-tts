#ifndef PDFVIEWERFORM_H
#define PDFVIEWERFORM_H

#include <QWidget>
#include <QString>

#ifdef HAVE_POPPLER
#include <poppler/qt5/poppler-qt5.h>
#endif

QT_BEGIN_NAMESPACE
class QPushButton;
class QScrollArea;
class QLabel;
class QVBoxLayout;
class QHBoxLayout;
QT_END_NAMESPACE

class TtsEngine;

/**
 * PDF viewer form with Open/Close PDF and TTS playback.
 * When built with Poppler (HAVE_POPPLER), displays PDF pages and extracts text for TTS.
 */
class PdfViewerForm : public QWidget
{
    Q_OBJECT
public:
    explicit PdfViewerForm(TtsEngine *ttsEngine, QWidget *parent = nullptr);
    ~PdfViewerForm();

private slots:
    void onOpenPdf();
    void onClosePdf();
    void onPlayTts();

private:
    void setupUi();
    void loadPdf(const QString &path);
    void clearPdf();
    QString extractTextFromPdf(const QString &path);

    TtsEngine *m_ttsEngine = nullptr;
    QPushButton *m_btnOpen = nullptr;
    QPushButton *m_btnClose = nullptr;
    QPushButton *m_btnPlayTts = nullptr;
    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_pagesContainer = nullptr;
    QVBoxLayout *m_pagesLayout = nullptr;
    QString m_currentPdfPath;
    QString m_extractedText;

#ifdef HAVE_POPPLER
    Poppler::Document *m_doc = nullptr;
#endif
};

#endif // PDFVIEWERFORM_H
