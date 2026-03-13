#include "custombutton.h"
#include <QPainter>
#include <QEvent>
#include <QPainterPath>

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
               objectName() == QLatin1String("btnTtsStop")) {
        r = height() / 2; // round TTS controls
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
