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

    const int r = (m_role == TitleBar) ? height() / 2 : 6;
    QRect rect = this->rect().adjusted(1, 1, -1, -1);
    QPainterPath path;
    path.addRoundedRect(rect, r, r);

    QColor bg, fg, border;
    switch (m_role) {
    case Primary:
        bg = m_hovered ? QColor(0x1a7fc4) : QColor(0x0e639c);
        if (isDown()) bg = QColor(0x094a75);
        fg = Qt::white;
        border = QColor(0x0a4d78);
        break;
    case Secondary:
        bg = m_hovered ? QColor(0x3c3c3c) : QColor(0x2d2d30);
        if (isDown()) bg = QColor(0x252526);
        fg = QColor(0xe0e0e0);
        border = QColor(0x454545);
        break;
    case Danger:
        bg = m_hovered ? QColor(0xc42b2b) : QColor(0xa12626);
        if (isDown()) bg = QColor(0x7a1e1e);
        fg = Qt::white;
        border = QColor(0x6b1a1a);
        break;
    case TitleBar:
        bg = Qt::transparent;
        if (m_hovered) bg = isDown() ? QColor(0x505050) : QColor(0x404040);
        fg = m_hovered ? QColor(0xffffff) : QColor(0xb0b0b0);
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
    if (m_role == TitleBar && text().isEmpty()) {
        // Draw × for close
        QFont f = font();
        f.setPixelSize(14);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.drawText(rect, Qt::AlignCenter, QStringLiteral("×"));
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
