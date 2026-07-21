#include "uiAnimation.h"

QSequentialAnimationGroup* uiAnimation::shake(QWidget *widget, int intensity)
{
    if (!widget) return nullptr;

    QRect originalGeometry = widget->geometry();
    QSequentialAnimationGroup *group = new QSequentialAnimationGroup();

    // 创建左右摇晃效果
    for (int i = 0; i < 3; ++i) {
        // 向右
        QPropertyAnimation *right = new QPropertyAnimation(widget, "geometry");
        right->setDuration(50);
        QRect rightGeometry = originalGeometry;
        rightGeometry.moveLeft(originalGeometry.left() + intensity);
        right->setEndValue(rightGeometry);
        group->addAnimation(right);

        // 向左
        QPropertyAnimation *left = new QPropertyAnimation(widget, "geometry");
        left->setDuration(50);
        QRect leftGeometry = originalGeometry;
        leftGeometry.moveLeft(originalGeometry.left() - intensity);
        left->setEndValue(leftGeometry);
        group->addAnimation(left);
    }

    // 恢复原位
    QPropertyAnimation *restore = new QPropertyAnimation(widget, "geometry");
    restore->setDuration(50);
    restore->setEndValue(originalGeometry);
    group->addAnimation(restore);

    return group;
}
