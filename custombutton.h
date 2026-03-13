#ifndef CUSTOMBUTTON_H
#define CUSTOMBUTTON_H

#include <QPushButton>

class CustomButton : public QPushButton
{
    Q_OBJECT
public:
    enum ButtonRole { Primary, Secondary, Danger, TitleBar };

    explicit CustomButton(const QString &text, ButtonRole role = Primary, QWidget *parent = nullptr);
    explicit CustomButton(ButtonRole role = TitleBar, QWidget *parent = nullptr); // for icon-only (e.g. close)
    void setButtonRole(ButtonRole role);

protected:
    void enterEvent(QEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void updateStyle();
    ButtonRole m_role;
    bool m_hovered = false;
};

#endif // CUSTOMBUTTON_H
