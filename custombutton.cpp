#include "custombutton.h"
#include <QPainter>
#include <QEvent>
#include <QPainterPath>
#include <QFile>
#include <QSvgRenderer>
#include <QImage>

CustomButton::CustomButton(const QString &text, ButtonRole role, QWidget *parent)
    : QPushButton(text, parent)
    , m_role(role)
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    updateStyle();
}

CustomButton::CustomButton(ButtonRole role, QWidget *parent)
    : QPushButton(parent)
    , m_role(role)
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    updateStyle();
}

void CustomButton::setButtonRole(ButtonRole role)
{
    if (m_role != role) {
        m_role = role;
        updateStyle();
    }
}

void CustomButton::setIconPath(const QString &path)
{
    if (m_iconPath == path)
        return;
    m_iconPath = path;
    m_iconPixmap = QPixmap();
    loadIcon();
    update();
}

void CustomButton::loadIcon()
{
    if (m_iconPath.isEmpty())
        return;
    const int size = 64; // load at 1x for scaling
    if (m_iconPath.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)) {
        QSvgRenderer renderer;
        if (renderer.load(m_iconPath)) {
            QImage img(size, size, QImage::Format_ARGB32);
            img.fill(Qt::transparent);
            QPainter p(&img);
            renderer.render(&p);
            p.end();
            m_iconPixmap = QPixmap::fromImage(img);
        }
    } else {
        m_iconPixmap.load(m_iconPath);
        if (!m_iconPixmap.isNull())
            m_iconPixmap = m_iconPixmap.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
}

void CustomButton::enterEvent(QEvent *event)
{
    Q_UNUSED(event);
    m_hovered = true;
    update();
}

void CustomButton::leaveEvent(QEvent *event)
{
    Q_UNUSED(event);
    m_hovered = false;
    update();
}

void CustomButton::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    int r = 6;
    if (m_role == TitleBar) {
        r = height() / 2;
    } else if (objectName() == QLatin1String("btnTtsPlay") ||
               objectName() == QLatin1String("btnTtsPause") ||
               objectName() == QLatin1String("btnTtsStop")  ||
               objectName() == QLatin1String("btnSpeaker")) {
        r = height() / 2; // round controls
    }
    QRect rect = this->rect().adjusted(1, 1, -1, -1);
    QPainterPath path;
    path.addRoundedRect(rect, r, r);

    QColor bg, fg, border;
    switch (m_role) {
    case Primary:
        bg = m_hovered ? QColor(0x4aa6ff) : QColor(0x2e8cff);
        if (isDown()) bg = QColor(0x1f6fcb);
        fg = Qt::white;
        border = QColor(0x1f6fcb);
        break;
    case Secondary:
        bg = m_hovered ? QColor(0xe1efff) : QColor(0xf3f7ff);
        if (isDown()) bg = QColor(0xd1e3ff);
        fg = QColor(0x2e4f7f);
        border = QColor(0xc0d7ff);
        break;
    case Danger:
        bg = m_hovered ? QColor(0xff6961) : QColor(0xff4c4c);
        if (isDown()) bg = QColor(0xe63b3b);
        fg = Qt::white;
        border = QColor(0xd53434);
        break;
    case TitleBar:
        bg = Qt::transparent;
        if (m_hovered) bg = isDown() ? QColor(0xb0c9ff) : QColor(0xc9dbff);
        fg = m_hovered ? QColor(0x274066) : QColor(0x3c557a);
        border = Qt::transparent;
        break;
    }

    if (bg.alpha() > 0) {
        p.fillPath(path, bg);
    }
    if (border.alpha() > 0) {
        p.setPen(QPen(border, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(rect, r, r);
    }

    p.setPen(fg);
    p.setBrush(Qt::NoBrush);
    if (text().isEmpty()) {
        if (!m_iconPixmap.isNull()) {
            QRect iconRect = rect.adjusted(4, 4, -4, -4);
            QPixmap scaled = m_iconPixmap.scaled(iconRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            QImage img = scaled.toImage().convertToFormat(QImage::Format_ARGB32);
            for (int y = 0; y < img.height(); ++y) {
                for (int x = 0; x < img.width(); ++x) {
                    QRgb px = img.pixel(x, y);
                    int a = qAlpha(px);
                    if (a > 0)
                        img.setPixel(x, y, qRgba(fg.red(), fg.green(), fg.blue(), a));
                }
            }
            p.drawImage(iconRect, img);
            return;
        }
        const QString name = objectName();
        if (m_role == TitleBar) {
            // Draw × for close
            QFont f = font();
            f.setPixelSize(14);
            f.setWeight(QFont::DemiBold);
            p.setFont(f);
            p.drawText(rect, Qt::AlignCenter, QStringLiteral("×"));
        } else if (name == QLatin1String("btnTtsPlay")) {
            // Draw play triangle
            QRectF r = rect.adjusted(rect.width() * 0.28, rect.height() * 0.22,
                                     -rect.width() * 0.28, -rect.height() * 0.22);
            QPainterPath tri;
            QPointF p1(r.left(), r.top());
            QPointF p2(r.left(), r.bottom());
            QPointF p3(r.right(), (r.top() + r.bottom()) / 2.0);
            tri.moveTo(p1);
            tri.lineTo(p2);
            tri.lineTo(p3);
            tri.closeSubpath();
            p.setBrush(fg);
            p.setPen(Qt::NoPen);
            p.drawPath(tri);
        } else if (name == QLatin1String("btnTtsPause")) {
            // Draw pause (two vertical bars)
            QRectF r = rect.adjusted(rect.width() * 0.25, rect.height() * 0.22,
                                     -rect.width() * 0.25, -rect.height() * 0.22);
            qreal w = r.width() / 3.0;
            QRectF leftBar(r.left(), r.top(), w, r.height());
            QRectF rightBar(r.right() - w, r.top(), w, r.height());
            p.setBrush(fg);
            p.setPen(Qt::NoPen);
            p.drawRect(leftBar);
            p.drawRect(rightBar);
        } else if (name == QLatin1String("btnTtsStop")) {
            // Draw stop square
            QRectF r = rect.adjusted(rect.width() * 0.25, rect.height() * 0.25,
                                     -rect.width() * 0.25, -rect.height() * 0.25);
            p.setBrush(fg);
            p.setPen(Qt::NoPen);
            p.drawRect(r);
        } else if (name == QLatin1String("btnSpeaker")) {
            // Draw simple speaker icon with waves
            QRectF r = rect.adjusted(rect.width() * 0.28, rect.height() * 0.24,
                                     -rect.width() * 0.28, -rect.height() * 0.24);
            // Speaker body (triangle)
            QPainterPath body;
            QPointF b1(r.left(), r.center().y());
            QPointF b2(r.left() + r.width() * 0.35, r.top());
            QPointF b3(r.left() + r.width() * 0.35, r.bottom());
            body.moveTo(b1);
            body.lineTo(b2);
            body.lineTo(b3);
            body.closeSubpath();
            // Waves
            QPainterPath waves;
            qreal cx = r.left() + r.width() * 0.55;
            qreal cy = r.center().y();
            qreal radius1 = r.width() * 0.28;
            qreal radius2 = r.width() * 0.42;
            QRectF arcRect1(cx - radius1, cy - radius1, radius1 * 2, radius1 * 2);
            QRectF arcRect2(cx - radius2, cy - radius2, radius2 * 2, radius2 * 2);
            waves.arcMoveTo(arcRect1, -40);
            waves.arcTo(arcRect1, -40, 80);
            waves.arcMoveTo(arcRect2, -40);
            waves.arcTo(arcRect2, -40, 80);

            p.setBrush(fg);
            p.setPen(QPen(fg, 1.4));
            p.drawPath(body);
            p.drawPath(waves);
        }
    } else {
        QFont f = font();
        f.setPixelSize(13);
        if (m_role == Primary || m_role == Danger) f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.drawText(rect, Qt::AlignCenter, text());
    }
}

void CustomButton::updateStyle()
{
    setAttribute(Qt::WA_Hover, true);
    if (m_role == TitleBar && text().isEmpty()) {
        setFixedSize(28, 28);
    }
}
