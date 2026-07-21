#ifndef UI_ANIMATION_H
#define UI_ANIMATION_H

#include <QWidget>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>

class uiAnimation
{
public:
    // 震动动画
    static QSequentialAnimationGroup* shake(QWidget *widget, int intensity = 10);
};

#endif // UI_ANIMATION_H
