#ifndef CUSTOMBUTTON_H
#define CUSTOMBUTTON_H

#include <QPushButton>
#include <QPixmap>

class CustomButton : public QPushButton
{
    Q_OBJECT
public:
    enum ButtonRole { Primary, Secondary, Danger, TitleBar };

    explicit CustomButton(const QString &text, ButtonRole role = Primary, QWidget *parent = nullptr);
    explicit CustomButton(ButtonRole role = TitleBar, QWidget *parent = nullptr); // for icon-only (e.g. close)
    void setButtonRole(ButtonRole role);
    /** Set icon from file path (PNG or SVG). Resource paths like ":/icons/play.svg" supported. Clears path to use drawn icon. */
    void setIconPath(const QString &path);
    QString iconPath() const { return m_iconPath; }

protected:
    void enterEvent(QEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void updateStyle();
    void loadIcon();
    ButtonRole m_role;
    bool m_hovered = false;
    QString m_iconPath;
    QPixmap m_iconPixmap;
};

#endif // CUSTOMBUTTON_H
