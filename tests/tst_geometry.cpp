#include <QtTest>
#include "Geometry.h"

using namespace ql;
using namespace ql::geometry;

namespace {
const Monitor laptop{"eDP-2", {2048, 1235}, 1.25};

bool near(double a, double b, double tol = 0.01) { return qAbs(a - b) <= tol; }
bool nearRect(const QRectF &r, double x, double y, double w, double h, double tol = 0.01)
{
    return near(r.x(), x, tol) && near(r.y(), y, tol) && near(r.width(), w, tol) && near(r.height(), h, tol);
}
}

#define VERIFY_RECT(r, ex, ey, ew, eh) \
    QVERIFY2(nearRect(r, ex, ey, ew, eh), qPrintable(QStringLiteral("got %1,%2 %3x%4").arg((r).x()).arg((r).y()).arg((r).width()).arg((r).height())))

class TstGeometry : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void boxIsNinetyPercent()
    {
        QCOMPARE(box(laptop), QSize(1843, 1111));
        QCOMPARE(box({"x", {0, 0}, 1}), QSize(1, 1));
    }
    void fitAndFitDown()
    {
        QCOMPARE(fit(QSizeF(100, 50), QSizeF(400, 400)), QSizeF(400, 200));
        QCOMPARE(fitDown(QSizeF(100, 50), QSizeF(400, 400)), QSizeF(100, 50));
        QCOMPARE(fitDown(QSizeF(800, 400), QSizeF(400, 400)), QSizeF(400, 200));
    }
    void imageLayouts()
    {
        auto l = imageLayout({1920, 1080}, laptop);
        QCOMPARE(l.window, QSize(1536, 864));
        VERIFY_RECT(l.content, 0, 0, 1536, 864);

        l = imageLayout({3840, 2160}, laptop);
        QCOMPARE(l.window, QSize(1843, 1037));
        VERIFY_RECT(l.content, 0, 0.155, 1843, 1036.69); // centered in 1037

        l = imageLayout({1080, 2400}, laptop);
        QCOMPARE(l.window, QSize(500, 1111));
        QVERIFY(near(l.content.width(), 499.95) && near(l.content.height(), 1111));

        l = imageLayout({100, 3000}, laptop);
        QCOMPARE(l.window, QSize(240, 1111));
        QVERIFY(near(l.content.width(), 37.03) && near(l.content.x(), (240 - 37.03) / 2));

        l = imageLayout({16, 16}, laptop);
        QCOMPARE(l.window, QSize(240, 240));
        VERIFY_RECT(l.content, 113.6, 113.6, 12.8, 12.8);
    }
    void svgLayouts()
    {
        QCOMPARE(svgLayout({24, 24}, laptop).window, QSize(240, 240));
        QCOMPARE(svgLayout({4000, 1000}, laptop).window, QSize(1843, 461));
    }
    void pageLayouts()
    {
        QCOMPARE(pageLayout({595.28, 841.89}, laptop).window, QSize(786, 1111));
        QCOMPARE(pageLayout({841.89, 595.28}, laptop).window, QSize(1572, 1111));
    }
    void videoAndFallback()
    {
        QCOMPARE(videoLayout({1280, 720}, laptop).window, QSize(1024, 576));
        QCOMPARE(fallbackLayout({16, 9}, laptop).window, QSize(1843, 1037));
    }
    void textSizes()
    {
        QCOMPARE(textViewportWidth(120, 9.59375), 1153);
        QCOMPARE(textWindowWidth(120, 9.59375), 1185);
        QCOMPARE(textLayout(120, 48, 9.59375, 22, laptop).window, QSize(1185, 1088));
        QCOMPARE(textLayout(120, 2, 9.59375, 22, laptop).window.height(), 240);
        QCOMPARE(textLayout(40, 48, 9.59375, 22, laptop).window.width(), 417);
        const auto l = textLayout(120, 48, 9.59375, 22, laptop);
        VERIFY_RECT(l.content, 0, 0, 1185, 1088);
    }
    void tinyMonitorClampsMinToBox()
    {
        const Monitor tiny{"t", {200, 150}, 1.0};
        QCOMPARE(box(tiny), QSize(180, 135));
        const auto l = imageLayout({16, 16}, tiny);
        QCOMPARE(l.window, QSize(135, 135));
        QCOMPARE(textLayout(120, 2, 9.59375, 22, tiny).window.height(), 135);
    }
    void cards()
    {
        auto l = cardLayout(Kind::FileCard);
        QCOMPARE(l.window, QSize(440, 180));
        VERIFY_RECT(l.content, 0, 0, 440, 180);
        QCOMPARE(cardLayout(Kind::FolderCard).window, QSize(440, 180));
        QCOMPARE(cardLayout(Kind::Audio).window, QSize(520, 180));
        VERIFY_RECT(cardRectIn({240, 240}, Kind::FileCard), 0, 30, 240, 180);
        VERIFY_RECT(cardRectIn({1000, 600}, Kind::Audio), 240, 210, 520, 180);
    }
    void pageRects()
    {
        auto r = pageRect({786, 1111}, {595.28, 841.89});
        QVERIFY(near(r.x(), 0.22) && near(r.width(), 785.56) && near(r.y(), 0));
        r = pageRect({786, 1111}, {841.89, 595.28});
        QVERIFY(near(r.width(), 786) && near(r.y(), 0));
    }
    void fullscreen()
    {
        const QSize s(2048, 1280);
        VERIFY_RECT(fullscreenContent(Kind::Image, s, {1920, 1080}, 1.25), 256, 208, 1536, 864);
        VERIFY_RECT(fullscreenContent(Kind::Image, s, {3840, 2160}, 1.25), 0, 64, 2048, 1152);
        VERIFY_RECT(fullscreenContent(Kind::Video, s, {1280, 720}, 1.25), 0, 64, 2048, 1152);
        VERIFY_RECT(fullscreenContent(Kind::Svg, s, {1024, 640}, 1.25), 0, 0, 2048, 1280);
        VERIFY_RECT(fullscreenContent(Kind::Text, s, {}, 1.25), 0, 0, 2048, 1280);
        VERIFY_RECT(fullscreenContent(Kind::FileCard, s, {}, 1.25), 804, 550, 440, 180);
        const auto p = fullscreenContent(Kind::Pdf, s, {595.28, 841.89}, 1.25);
        QVERIFY(near(p.height(), 1280) && near(p.y(), 0));
    }
    void emptySizesDoNotDivideByZero()
    {
        QCOMPARE(imageLayout({}, laptop).window, QSize(240, 240));
        QCOMPARE(svgLayout({}, laptop).window, QSize(240, 240));
        QCOMPARE(pageLayout({}, laptop).window, QSize(240, 240));
        QVERIFY(pageRect({100, 100}, {}).isEmpty());
    }
    void neverLargerThanBox()
    {
        for (int d : {1, 2, 3, 7}) {
            const Monitor m{"m", {2048 + d * 13, 1235 + d * 7}, 1.0 + d / 4.0};
            const QSize b = box(m);
            for (int w = 1; w <= 9000; w += 97)
                for (int h = 1; h <= 9000; h += 89) {
                    for (const Layout &l : {imageLayout({w, h}, m), svgLayout({double(w), double(h)}, m),
                                            pageLayout({double(w), double(h)}, m), fallbackLayout({double(w), double(h)}, m)}) {
                        QVERIFY2(l.window.width() <= b.width() && l.window.height() <= b.height(),
                                 qPrintable(QStringLiteral("%1x%2").arg(w).arg(h)));
                    }
                }
        }
        const Monitor m{"m", {2048, 1235}, 2.4}; // exact box aspect
        QVERIFY(imageLayout({9215, 5555}, m).window.height() <= box(m).height());
    }
};

QTEST_APPLESS_MAIN(TstGeometry)
#include "tst_geometry.moc"
