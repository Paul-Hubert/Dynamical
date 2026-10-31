#include <doctest/doctest.h>

#include "imgdiff/imgdiff.h"

using namespace dy::imgdiff;

namespace {

Image flat(int w, int h, uint8_t v) {
    Image image;
    image.width = w;
    image.height = h;
    image.rgb.assign((size_t) w * h * 3, v);
    return image;
}

}

TEST_SUITE("unit") {

TEST_CASE("imgdiff: identical images pass with zero error") {
    auto r = compare(flat(100, 100, 80), flat(100, 100, 80), {});
    CHECK(r.same_size);
    CHECK(r.mae == 0.);
    CHECK(r.pixels_over == 0);
    CHECK(r.pass);
}

TEST_CASE("imgdiff: size mismatch fails") {
    auto r = compare(flat(100, 100, 80), flat(100, 50, 80), {});
    CHECK(!r.same_size);
    CHECK(!r.pass);
}

TEST_CASE("imgdiff: mean absolute error threshold") {
    // Every channel off by 1: mae 1/255 passes 1.5, off by 2 fails
    CHECK(compare(flat(10, 10, 100), flat(10, 10, 101), {}).pass);
    auto r = compare(flat(10, 10, 100), flat(10, 10, 102), {});
    CHECK(r.mae == doctest::Approx(2.));
    CHECK(!r.pass);
}

TEST_CASE("imgdiff: fraction of strongly different pixels") {
    Image a = flat(100, 100, 100), b = flat(100, 100, 100);
    // 50 pixels (0.5 %) off by 25 in one channel: at the limit, passes; the mae stays small
    for(int i = 0; i < 50; i++) b.rgb[(size_t) i * 3] = 125;
    auto r = compare(a, b, {});
    CHECK(r.pixels_over == 50);
    CHECK(r.fraction == doctest::Approx(0.005));
    CHECK(r.pass);
    // One more fails
    b.rgb[50 * 3] = 125;
    CHECK(!compare(a, b, {}).pass);
    // A difference of exactly 24 is not over the threshold
    Image c = flat(100, 100, 100);
    for(int i = 0; i < 200; i++) c.rgb[(size_t) i * 3] = 124;
    CHECK(compare(a, c, {}).pixels_over == 0);
}

TEST_CASE("imgdiff: heatmap marks differing pixels") {
    Image a = flat(4, 4, 0), b = flat(4, 4, 0);
    b.rgb[0] = 200;
    Image h = heatmap(a, b);
    CHECK(h.width == 4);
    CHECK(h.rgb[0] > 128);
    CHECK(h.rgb[3] == 0);
}

}
