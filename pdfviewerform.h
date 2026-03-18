#ifndef PDFVIEWERFORM_H
#define PDFVIEWERFORM_H

#include <QWidget>
#include <QString>
#include <QPoint>
#include <QRect>
#include <QVector>

class QEvent;
#ifdef Q_OS_WIN
#include <QByteArray>
#endif

#ifdef HAVE_POPPLER
#include <poppler/qt5/poppler-qt5.h>
#endif

QT_BEGIN_NAMESPACE
class QScrollArea;
class QLabel;
class QVBoxLayout;
class QHBoxLayout;
class QMouseEvent;
class QResizeEvent;
class QShowEvent;
class QProgressBar;
QT_END_NAMESPACE

class CustomButton;
class TtsEngine;

/**
 * PDF viewer with CrystalTts-style frameless UI (matches MainWindow).
 */
class PdfViewerForm : public QWidget
{
    Q_OBJECT
public:
    explicit PdfViewerForm(TtsEngine *ttsEngine, QWidget *parent = nullptr);
    ~PdfViewerForm();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
#if defined(Q_OS_WIN)
    bool nativeEvent(const QByteArray &eventType, void *message, long *result) override;
#endif

private slots:
    void onOpenPdf();
    void onClosePdf();
    void onPlayTts();
    void onHideWindow();
    void onToggleMaximize();
    void updateMaximizeButton();
    void layoutResizeEdgeWidgets();
#ifdef HAVE_POPPLER
    void onZoomIn();
    void onZoomOut();
#endif

private:
    void setupUi();
    void applyCrystalStyle();
    bool isInTitleBar(const QPoint &globalPos) const;

    void loadPdf(const QString &path);
    void clearPdf();
    QString extractTextFromPdf(const QString &path);
#ifdef HAVE_POPPLER
    double computeFitToWindowDpi() const;
    void rebuildPdfPageWidgets(QProgressBar *progress = nullptr, int progressStart = 0, int progressEnd = 0);
#endif

    TtsEngine *m_ttsEngine = nullptr;

    QWidget *m_centralShell = nullptr;
    QWidget *m_titleBarWidget = nullptr;
    QLabel *m_labelTitle = nullptr;
    CustomButton *m_btnWinMaximize = nullptr;
    CustomButton *m_btnWinClose = nullptr;

    CustomButton *m_btnOpenPdf = nullptr;
    CustomButton *m_btnClosePdf = nullptr;
    CustomButton *m_btnPlayTts = nullptr;
#ifdef HAVE_POPPLER
    CustomButton *m_btnZoomOut = nullptr;
    CustomButton *m_btnZoomIn = nullptr;
    double m_baseFitDpi = 150.0;
    double m_zoomFactor = 1.0;
    QProgressBar *m_pdfLoadProgress = nullptr;
#endif

    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_pagesContainer = nullptr;
    QVBoxLayout *m_pagesLayout = nullptr;
    QString m_currentPdfPath;
    QString m_extractedText;

    QPoint m_dragPosition;
    bool m_dragging = false;
    QRect m_restoredGeometry;

    QVector<QWidget *> m_resizeEdgeWidgets;

#ifdef HAVE_POPPLER
    Poppler::Document *m_doc = nullptr;
#endif
};

#endif // PDFVIEWERFORM_H
