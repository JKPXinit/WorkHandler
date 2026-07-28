#include "trayiconrenderer.h"
#include "issuetoast.h"

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QSignalSpy>
#include <QTest>

class TrayIconRendererTest : public QObject
{
    Q_OBJECT

private slots:
    void badgeLabels();
    void actionStates();
    void urlsUseConfiguredPort();
    void renderKeepsStableCanvas();
    void toastKeepsIssueIdentity();
};

void TrayIconRendererTest::badgeLabels()
{
    QCOMPARE(TrayIconRenderer::badgeText(0), QString());
    QCOMPARE(TrayIconRenderer::badgeText(1), QStringLiteral("1"));
    QCOMPARE(TrayIconRenderer::badgeText(9), QStringLiteral("9"));
    QCOMPARE(TrayIconRenderer::badgeText(10), QStringLiteral("10"));
    QCOMPARE(TrayIconRenderer::badgeText(99), QStringLiteral("99"));
    QCOMPARE(TrayIconRenderer::badgeText(100), QStringLiteral("99+"));
    QCOMPARE(TrayIconRenderer::badgeText(999), QStringLiteral("99+"));
}

void TrayIconRendererTest::actionStates()
{
    const TrayActionState running = TrayIconRenderer::actionState(
        TrayServerState::Running, 3);
    QVERIFY(running.openWebEnabled);
    QVERIFY(running.serverActionEnabled);
    QCOMPARE(int(running.serverAction), int(TrayServerAction::Stop));
    QVERIFY(running.markAllReadEnabled);

    const TrayActionState stopped = TrayIconRenderer::actionState(
        TrayServerState::Stopped, 0);
    QVERIFY(!stopped.openWebEnabled);
    QCOMPARE(int(stopped.serverAction), int(TrayServerAction::Start));
    QVERIFY(!stopped.markAllReadEnabled);

    const TrayActionState starting = TrayIconRenderer::actionState(
        TrayServerState::Starting, 1);
    QVERIFY(!starting.serverActionEnabled);
    QCOMPARE(int(starting.serverAction), int(TrayServerAction::Starting));
    const TrayActionState stopping = TrayIconRenderer::actionState(
        TrayServerState::Stopping, 1);
    QVERIFY(!stopping.serverActionEnabled);
    QCOMPARE(int(stopping.serverAction), int(TrayServerAction::Stopping));
    const TrayActionState error = TrayIconRenderer::actionState(
        TrayServerState::Error, 1);
    QVERIFY(error.serverActionEnabled);
    QCOMPARE(int(error.serverAction), int(TrayServerAction::Retry));
}

void TrayIconRendererTest::urlsUseConfiguredPort()
{
    QCOMPARE(TrayIconRenderer::rootUrl(9123).toString(),
             QStringLiteral("http://127.0.0.1:9123/"));
    QCOMPARE(TrayIconRenderer::issueUrl(9123, 42).toString(),
             QStringLiteral("http://127.0.0.1:9123/#/issues/42"));
    QCOMPARE(TrayIconRenderer::issueUrl(9123, 0),
             TrayIconRenderer::rootUrl(9123));
}

void TrayIconRendererTest::renderKeepsStableCanvas()
{
    QPixmap source(32, 32);
    source.fill(QColor(QStringLiteral("#24a148")));
    const QIcon icon(source);
    for (qint64 count : {qint64(0), qint64(1), qint64(9), qint64(10),
                         qint64(99), qint64(100), qint64(999)}) {
        const QPixmap rendered = TrayIconRenderer::render(
            icon, TrayServerState::Running, count, 32, 2.0);
        QCOMPARE(rendered.size(), QSize(64, 64));
        QCOMPARE(rendered.devicePixelRatio(), qreal(2.0));
        QVERIFY(!rendered.toImage().isNull());
    }
    const QImage stopped = TrayIconRenderer::render(
        icon, TrayServerState::Stopped, 0).toImage();
    const QColor stoppedCenter(stopped.pixel(stopped.width() / 2,
                                             stopped.height() / 2));
    QCOMPARE(stoppedCenter.red(), stoppedCenter.green());
    QCOMPARE(stoppedCenter.green(), stoppedCenter.blue());

    const QImage error = TrayIconRenderer::render(
        icon, TrayServerState::Error, 0).toImage();
    bool hasRedMarker = false;
    for (int y = error.height() / 2; y < error.height(); ++y) {
        for (int x = 0; x < error.width() / 2; ++x) {
            const QColor color(error.pixel(x, y));
            hasRedMarker = hasRedMarker
                || (color.red() > 180 && color.green() < 100);
        }
    }
    QVERIFY(hasRedMarker);
}

void TrayIconRendererTest::toastKeepsIssueIdentity()
{
    IssueToast first(101, QStringLiteral("First"), QStringLiteral("Content"));
    IssueToast second(202, QStringLiteral("Second"), QStringLiteral("Content"));
    QSignalSpy firstSpy(&first, &IssueToast::activated);
    QSignalSpy secondSpy(&second, &IssueToast::activated);

    first.resize(320, 120);
    second.resize(320, 120);
    QTest::mouseClick(&first, Qt::LeftButton, Qt::NoModifier,
                      first.rect().center());
    QTest::mouseClick(&second, Qt::LeftButton, Qt::NoModifier,
                      second.rect().center());

    QCOMPARE(first.issueId(), qint64(101));
    QCOMPARE(second.issueId(), qint64(202));
    QCOMPARE(firstSpy.count(), 1);
    QCOMPARE(secondSpy.count(), 1);
    QCOMPARE(firstSpy.first().first().toLongLong(), qint64(101));
    QCOMPARE(secondSpy.first().first().toLongLong(), qint64(202));
}

QTEST_MAIN(TrayIconRendererTest)

#include "trayiconrenderertest.moc"
