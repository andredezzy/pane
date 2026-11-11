// Copyright 2026 Pane. Licensed under GPL-3.0-only.

#include "pane/ui/sheets/platform_logo.h"

#include <iterator>
#include <optional>
#include <vector>

#include "base/containers/span.h"
#include "base/no_destructor.h"
#include "cc/paint/paint_canvas.h"
#include "cc/paint/paint_filter.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_shader.h"
#include "pane/ui/design/palette.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkMatrix.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkPoint.h"
#include "third_party/skia/include/utils/SkParsePath.h"
#include "ui/color/color_provider.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/scoped_canvas.h"

namespace pane {

namespace {

// A linear gradient in the bounding box of the path it fills, from (x1, y1)
// to (x2, y2) as shares of the box (SVG's objectBoundingBox), between two
// stops, each a color at an opacity.
struct Gradient {
  struct Stop {
    float offset;
    SkColor color;
    float opacity;
  };

  float x1;
  float y1;
  float x2;
  float y2;
  Stop from;
  Stop to;
};

// A path as the SVG writes it: filled with a color or a gradient, or not,
// and stroked when it has a width, with SVG's miter joins and butt caps.
struct LogoPath {
  const char* d;
  bool filled = true;
  SkColor color = SK_ColorBLACK;
  int gradient = -1;
  SkColor stroke = SK_ColorTRANSPARENT;
  float stroke_width = 0;
};

// Windows' logo (PlatformWindows.svg): four squares in Microsoft's blue, on a
// 4875-unit square.
constexpr float kWindowsGrid = 4875;
constexpr LogoPath kWindowsPaths[] = {
    {.d = "M0 0h2311v2310H0zm2564 0h2311v2310H2564zM0 2564h2311v2311H0zm2564 "
          "0h2311v2311H2564",
     .color = SkColorSetRGB(0x00, 0x78, 0xD4)},
};

// Apple's logo (PlatformApple.svg), a template drawn in the foreground's one
// color, on a 256 by 315 box.
constexpr float kAppleWidth = 256;
constexpr float kAppleHeight = 315;
constexpr const char* kApplePath =
    "M213.803394,167.030943 C214.2452,214.609646 255.542482,230.442639 "
    "256,230.644727 C255.650812,231.761357 249.401383,253.208293 "
    "234.24263,275.361446 C221.138555,294.513969 207.538253,313.596333 "
    "186.113759,313.991545 C165.062051,314.379442 158.292752,301.507828 "
    "134.22469,301.507828 C110.163898,301.507828 102.642899,313.596301 "
    "82.7151126,314.379442 C62.0350407,315.16201 46.2873831,293.668525 "
    "33.0744079,274.586162 C6.07529317,235.552544 -14.5576169,164.286328 "
    "13.147166,116.18047 C26.9103111,92.2909053 51.5060917,77.1630356 "
    "78.2026125,76.7751096 C98.5099145,76.3877456 117.677594,90.4371851 "
    "130.091705,90.4371851 C142.497945,90.4371851 165.790755,73.5415029 "
    "190.277627,76.0228474 C200.528668,76.4495055 229.303509,80.1636878 "
    "247.780625,107.209389 C246.291825,108.132333 213.44635,127.253405 "
    "213.803394,167.030988 M174.239142,50.1987033 C185.218331,36.9088319 "
    "192.607958,18.4081019 190.591988,0 C174.766312,0.636050225 "
    "155.629514,10.5457909 144.278109,23.8283506 C134.10507,35.5906758 "
    "125.195775,54.4170275 127.599657,72.4607932 C145.239231,73.8255433 "
    "163.259413,63.4970262 174.239142,50.1987249";

// Tux (PlatformTux.svg, Larry Ewing's, by way of the Electron app): its
// paths on a 256 by 295 box, moved 10 right, as its group moves them.
constexpr float kTuxWidth = 256;
constexpr float kTuxHeight = 295;
constexpr float kTuxShift = 10;
constexpr Gradient kTuxGradients[] = {
    // tux-g-2
    {0.485477412f,
     1.15276174f,
     0.510473804f,
     0.413637237f,
     {0.0f, SkColorSetRGB(0xFF, 0xEE, 0xD7), 1.0f},
     {1.0f, SkColorSetRGB(0xBD, 0xBF, 0xC2), 1.0f}},
    // tux-g-3
    {0.544065463f,
     0.0240410545f,
     0.461753957f,
     0.905422349f,
     {0.0f, SkColorSetRGB(0xFF, 0xFF, 0xFF), 0.8f},
     {1.0f, SkColorSetRGB(0xFF, 0xFF, 0xFF), 0.0f}},
    // tux-g-4
    {0.51859653f,
     0.882477484f,
     0.479469396f,
     0.0974782136f,
     {0.0f, SkColorSetRGB(0xFF, 0xEE, 0xD7), 1.0f},
     {1.0f, SkColorSetRGB(0xBD, 0xBF, 0xC2), 1.0f}},
    // tux-g-5
    {0.499251097f,
     0.854900173f,
     0.499236843f,
     0.138109272f,
     {0.0f, SkColorSetRGB(0xFF, 0xEE, 0xD7), 1.0f},
     {1.0f, SkColorSetRGB(0xBD, 0xBF, 0xC2), 1.0f}},
    // tux-g-6
    {0.539014071f,
     0.0310177585f,
     0.459555354f,
     0.938949571f,
     {0.0f, SkColorSetRGB(0xFF, 0xFF, 0xFF), 0.65f},
     {1.0f, SkColorSetRGB(0xFF, 0xFF, 0xFF), 0.0f}},
    // tux-g-7
    {0.455928761f,
     0.0547459052f,
     0.54811359f,
     0.935235162f,
     {0.0f, SkColorSetRGB(0xFF, 0xFF, 0xFF), 0.65f},
     {1.0f, SkColorSetRGB(0xFF, 0xFF, 0xFF), 0.0f}},
    // tux-g-8
    {0.499844987f,
     0.898452442f,
     0.499844987f,
     0.406316864f,
     {0.0f, SkColorSetRGB(0xFF, 0xEE, 0xD7), 1.0f},
     {1.0f, SkColorSetRGB(0xBD, 0xBF, 0xC2), 1.0f}},
    // tux-g-9
    {0.535047131f,
     0.9997524f,
     0.427455968f,
     0.235451715f,
     {0.0f, SkColorSetRGB(0xFF, 0xEE, 0xD7), 1.0f},
     {1.0f, SkColorSetRGB(0xBD, 0xBF, 0xC2), 1.0f}},
    // tux-g-10
    {0.498413363f,
     0.132289558f,
     0.502412612f,
     0.946729694f,
     {0.0f, SkColorSetRGB(0xFF, 0xFF, 0xFF), 0.8f},
     {1.0f, SkColorSetRGB(0xFF, 0xFF, 0xFF), 0.0f}},
    // tux-g-11
    {0.499272298f,
     0.373270337f,
     0.507270446f,
     0.927824735f,
     {0.0f, SkColorSetRGB(0xFF, 0xFF, 0xFF), 0.65f},
     {1.0f, SkColorSetRGB(0xFF, 0xFF, 0xFF), 0.0f}},
    // tux-g-12
    {0.498755597f,
     0.0229900584f,
     0.498755597f,
     0.81203617f,
     {0.0f, SkColorSetRGB(0xFF, 0xFF, 0xFF), 0.65f},
     {1.0f, SkColorSetRGB(0xFF, 0xFF, 0xFF), 0.0f}},
    // tux-g-13
    {0.498334391f,
     0.0227189065f,
     0.498240398f,
     0.717989617f,
     {0.0f, SkColorSetRGB(0xFF, 0xFF, 0xFF), 0.65f},
     {1.0f, SkColorSetRGB(0xFF, 0xFF, 0xFF), 0.0f}},
    // tux-g-14
    {0.534670683f,
     0.489213861f,
     0.389488708f,
     0.980999776f,
     {0.0f, SkColorSetRGB(0xFF, 0xA6, 0x3F), 1.0f},
     {1.0f, SkColorSetRGB(0xFF, 0xFF, 0x00), 1.0f}},
    // tux-g-15
    {0.523731508f,
     1.43008909f,
     0.4757909f,
     -0.646215389f,
     {0.0f, SkColorSetRGB(0xFF, 0xEE, 0xD7), 1.0f},
     {1.0f, SkColorSetRGB(0xBD, 0xBF, 0xC2), 1.0f}},
    // tux-g-16
    {0.30580815f,
     0.340241079f,
     0.658867024f,
     0.89175349f,
     {0.0f, SkColorSetRGB(0xFF, 0xA6, 0x3F), 1.0f},
     {1.0f, SkColorSetRGB(0xFF, 0xFF, 0x00), 1.0f}},
    // tux-g-17
    {0.595715091f,
     -0.172155207f,
     0.483608522f,
     0.661184465f,
     {0.0f, SkColorSetRGB(0xFF, 0xFF, 0xFF), 0.65f},
     {1.0f, SkColorSetRGB(0xFF, 0xFF, 0xFF), 0.0f}},
    // tux-g-18
    {0.477689553f,
     0.0156481301f,
     0.513733028f,
     1.04312856f,
     {0.0f, SkColorSetRGB(0xFF, 0xFF, 0xFF), 0.65f},
     {1.0f, SkColorSetRGB(0xFF, 0xFF, 0xFF), 0.0f}},
    // tux-g-19
    {0.435495626f,
     0.045334861f,
     0.571143288f,
     0.928267174f,
     {0.0f, SkColorSetRGB(0xFF, 0xFF, 0xFF), 0.65f},
     {1.0f, SkColorSetRGB(0xFF, 0xFF, 0xFF), 0.0f}},
    // tux-g-20
    {0.497328042f,
     0.176085216f,
     0.505582487f,
     0.993854667f,
     {0.0f, SkColorSetRGB(0xFF, 0xA6, 0x3F), 1.0f},
     {1.0f, SkColorSetRGB(0xFF, 0xFF, 0x00), 1.0f}},
    // tux-g-21
    {0.501697217f,
     0.0289048531f,
     0.496802359f,
     0.941704279f,
     {0.0f, SkColorSetRGB(0xFF, 0xFF, 0xFF), 0.65f},
     {1.0f, SkColorSetRGB(0xFF, 0xFF, 0xFF), 0.0f}},
};

constexpr LogoPath kTuxPaths[] = {
    {.d = "M53.2125821,215.473804 C41.8258117,199.128278 39.6219206,145.867578 "
          "66.160442,113.084699 C79.2919595,97.3819748 82.6896249,86.4543483 "
          "83.6997416,71.6699125 C84.434372,54.8652433 71.8538272,4.81855066 "
          "119.237485,1.05357012 C167.263944,-2.80323922 164.600909,44.5804184 "
          "164.325423,69.6496791 C164.141765,90.7703016 179.844489,102.799874 "
          "190.680286,119.329056 C210.607135,149.632558 208.954216,201.791313 "
          "186.915306,230.074582 C158.999353,265.428667 135.123866,250.093259 "
          "119.237485,251.378862 C89.4849556,253.123609 88.4748389,268.918162 "
          "53.2125821,215.473804 L53.2125821,215.473804 Z",
     .color = SkColorSetRGB(0x00, 0x00, 0x00)},
    {.d = "M169.10052,122.451235 C177.365111,130.073025 198.76122,164.141508 "
          "164.876395,185.445788 C152.938652,192.88392 175.528535,221.167189 "
          "186.364333,207.484699 C205.556551,182.874582 193.343321,143.571858 "
          "181.772893,129.522053 C174.059275,119.604543 162.121532,115.747734 "
          "169.10052,122.451235 L169.10052,122.451235 Z",
     .gradient = 0},
    {.d = "M166.8048,117.859796 C180.395461,128.879251 205.097407,167.447344 "
          "169.008691,192.608434 C157.162777,200.413881 179.477174,225.115827 "
          "192.057718,212.535282 C235.676395,168.641119 190.955773,118.227111 "
          "175.528535,100.871469 C161.754216,85.719718 149.540987,104.360963 "
          "166.8048,117.859796 L166.8048,117.859796 Z",
     .color = SkColorSetRGB(0x00, 0x00, 0x00),
     .stroke = SkColorSetRGB(0x00, 0x00, 0x00),
     .stroke_width = 0.9773f},
    {.d = "M147.245267,25.0208853 C146.786123,37.60143 132.919975,48.5290565 "
          "116.298963,49.5391732 C99.6779518,50.54929 86.638263,40.9990954 "
          "87.097407,28.4185507 L87.097407,28.4185507 C87.556551,15.8380059 "
          "101.422699,4.91037946 118.043711,3.90026272 C134.664722,2.98197479 "
          "147.704411,12.4403405 147.245267,25.0208853 L147.245267,25.0208853 "
          "L147.245267,25.0208853 Z",
     .gradient = 1},
    {.d = "M107.483399,54.9570721 C107.942543,63.1298347 104.085734,70.0169942 "
          "98.7596638,70.2924806 C93.4335938,70.567967 88.7503253,64.2317802 "
          "88.2911813,56.0590176 L88.2911813,56.0590176 C87.8320374,47.8862549 "
          "91.6888467,40.9990954 97.0149167,40.723609 C102.340987,40.4481226 "
          "107.024255,46.7843094 107.483399,54.9570721 L107.483399,54.9570721 "
          "L107.483399,54.9570721 Z",
     .gradient = 2},
    {.d = "M117.125423,55.5998736 C117.30908,65.0582394 123.461609,72.5882005 "
          "130.807913,72.4045429 C138.154216,72.2208853 143.93943,64.4154378 "
          "143.755773,54.8652433 L143.755773,54.8652433 C143.572115,45.4068775 "
          "137.419586,37.8769164 130.073282,38.060574 C122.726979,38.2442316 "
          "116.849936,46.1415079 117.125423,55.5998736 L117.125423,55.5998736 "
          "L117.125423,55.5998736 Z",
     .gradient = 3},
    {.d = "M123.186123,57.7119359 C123.094294,62.9461771 125.6655,67.1703016 "
          "129.063166,67.1703016 C132.369002,67.1703016 135.215695,62.9461771 "
          "135.307524,57.8037647 L135.307524,57.8037647 C135.399353,52.5695234 "
          "132.828146,48.3453989 129.430481,48.3453989 C126.032816,48.3453989 "
          "123.277952,52.5695234 123.186123,57.7119359 L123.186123,57.7119359 "
          "L123.186123,57.7119359 Z",
     .color = SkColorSetRGB(0x00, 0x00, 0x00)},
    {.d = "M101.973672,57.8037647 C102.432816,62.119718 100.779897,65.7928697 "
          "98.3923486,66.1601849 C96.0048,66.4356713 93.7090802,63.2216635 "
          "93.2499362,58.9057102 L93.2499362,58.9057102 C92.7907922,54.5897569 "
          "94.4437105,50.9166051 96.8312591,50.54929 C99.2188078,50.2738036 "
          "101.514528,53.4878114 101.973672,57.8037647 L101.973672,57.8037647 "
          "L101.973672,57.8037647 Z",
     .color = SkColorSetRGB(0x00, 0x00, 0x00)},
    {.d = "M124.563555,54.7734145 C124.288068,57.7119359 125.6655,60.0994845 "
          "127.593905,60.2831421 C129.52231,60.4667997 131.358886,58.1710798 "
          "131.634372,55.3243872 L131.634372,55.3243872 C131.909858,52.3858658 "
          "130.532426,49.9983172 128.604022,49.8146596 C126.675617,49.631002 "
          "124.839041,51.9267219 124.563555,54.7734145 L124.563555,54.7734145 "
          "L124.563555,54.7734145 Z",
     .gradient = 4},
    {.d = "M99.9534381,55.5080448 C100.228925,57.8955935 99.2188078,60.0076557 "
          "97.7495471,60.1913133 C96.2802864,60.3749709 94.9028545,58.538395 "
          "94.6273681,56.0590176 L94.6273681,56.0590176 C94.3518817,53.6714689 "
          "95.3619984,51.5594067 96.8312591,51.3757491 C98.3005198,51.1920915 "
          "99.6779518,53.1204962 99.9534381,55.5080448 L99.9534381,55.5080448 "
          "L99.9534381,55.5080448 Z",
     .gradient = 5},
    {.d = "M71.0273681,145.68392 C77.5472125,130.899485 91.4133603,104.911936 "
          "91.6888467,84.80143 C91.6888467,68.8232199 139.531648,64.9664106 "
          "143.388458,80.9446207 C147.245267,96.9228308 156.979119,120.798317 "
          "163.223477,132.368745 C169.467835,143.847344 187.558107,180.487033 "
          "168.274061,212.443453 C150.918419,240.726722 98.3005198,263.132948 "
          "70.2009089,208.586644 C60.6507144,189.669913 62.3954615,166.25357 "
          "71.0273681,145.68392 L71.0273681,145.68392 Z",
     .gradient = 6},
    {.d = "M65.1503253,134.664465 C59.5487689,145.224776 47.9783409,172.957072 "
          "76.2616093,188.108823 C106.65694,204.270691 106.565111,237.420885 "
          "70.0172514,221.626333 C36.5915704,207.39287 51.3760062,149.724387 "
          "60.7425432,135.950068 C66.8032436,126.308045 75.986123,114.46213 "
          "65.1503253,134.664465 L65.1503253,134.664465 Z",
     .gradient = 7},
    {.d = "M69.9254226,122.726722 C61.0180296,137.235671 39.7137494,171.395983 "
          "68.2725043,189.210769 C106.65694,212.810769 95.8211424,236.31894 "
          "60.7425432,215.106488 C11.3386521,185.537617 54.7736716,125.848901 "
          "74.5168623,103.07536 C97.1067455,77.5469553 78.8328156,107.758628 "
          "69.9254226,122.726722 L69.9254226,122.726722 Z",
     .color = SkColorSetRGB(0x00, 0x00, 0x00),
     .stroke = SkColorSetRGB(0x00, 0x00, 0x00),
     .stroke_width = 1.25f},
    {.d = "M156.428146,151.285477 C156.428146,167.447344 140.90908,188.384309 "
          "114.27873,188.200652 C86.8219206,188.384309 75.1596638,167.447344 "
          "75.1596638,151.285477 C75.1596638,135.123609 93.341765,121.992092 "
          "115.747991,121.992092 C138.246045,122.08392 156.428146,135.123609 "
          "156.428146,151.285477 L156.428146,151.285477 Z",
     .gradient = 8},
    {.d = "M141.919197,100.504154 C141.643711,117.216994 130.716084,121.165632 "
          "116.941765,121.165632 C103.167446,121.165632 93.1581074,118.686255 "
          "91.9643331,100.504154 C91.9643331,89.1173833 103.167446,82.5057102 "
          "116.941765,82.5057102 C130.716084,82.4138814 141.919197,89.0255546 "
          "141.919197,100.504154 L141.919197,100.504154 Z",
     .gradient = 9},
    {.d = "M58.6304809,126.216216 C67.6297027,112.533726 86.638263,91.504932 "
          "62.2118039,129.154737 C42.3767844,160.19287 54.8655004,180.119718 "
          "61.293516,185.629446 C79.8429323,202.158628 79.1083019,213.269913 "
          "64.5075237,204.546177 C33.1939051,185.904932 39.7137494,154.499485 "
          "58.6304809,126.216216 L58.6304809,126.216216 Z",
     .gradient = 10},
    {.d = "M188.935539,131.817772 C181.130092,115.747734 156.336318,74.9757491 "
          "190.129314,122.359407 C220.89196,165.243453 199.312193,195.087811 "
          "195.455384,198.026333 C191.598574,200.964854 178.650714,206.933726 "
          "182.415695,196.557072 C186.272504,186.180418 205.372893,166.529056 "
          "188.935539,131.817772 L188.935539,131.817772 Z",
     .gradient = 11},
    {.d = "M51.8351502,258.541508 C31.2655004,247.613881 1.42114241,260.65357 "
          "12.2569401,231.084699 C14.4608311,224.381197 9.0429323,214.280029 "
          "12.5324265,207.760185 C16.6647222,199.77108 25.5721152,201.515827 "
          "30.8981852,196.189757 C36.1324265,190.680029 39.438263,181.129835 "
          "49.263944,182.599095 C58.9977961,184.068356 65.5176405,196.006099 "
          "72.3129712,210.698706 C77.3635549,221.167189 95.1783409,235.951625 "
          "93.9845665,247.70571 C92.5153058,265.704154 72.0374848,269.101819 "
          "51.8351502,258.541508 L51.8351502,258.541508 Z",
     .gradient = 12,
     .stroke = SkColorSetRGB(0xE6, 0x8C, 0x3F),
     .stroke_width = 6.25f},
    {.d = "M201.607913,189.11894 C198.485734,194.995983 185.446045,204.454348 "
          "176.72231,201.974971 C167.906746,199.587422 163.866279,186.180418 "
          "165.611026,175.987422 C167.263944,164.600652 176.72231,163.95785 "
          "188.660053,169.651235 C201.516084,175.987422 205.372893,181.313492 "
          "201.607913,189.11894 L201.607913,189.11894 Z",
     .gradient = 13},
    {.d = "M194.445267,253.490924 C209.505189,235.216994 243.022699,238.981975 "
          "220.432816,213.912714 C215.657718,208.494815 217.126979,196.924387 "
          "211.249936,191.965632 C204.362777,185.904932 196.740987,190.863687 "
          "189.761998,187.741508 C182.78301,184.343842 175.436707,177.823998 "
          "166.896629,182.415438 C158.356551,187.098706 157.438263,199.220107 "
          "156.611804,215.198317 C155.877174,226.676916 145.408691,245.869134 "
          "151.010247,256.429446 C159.091181,272.774971 180.119975,270.57108 "
          "194.445267,253.490924 L194.445267,253.490924 Z",
     .gradient = 14,
     .stroke = SkColorSetRGB(0xE6, 0x8C, 0x3F),
     .stroke_width = 6.2507f},
    {.d = "M187.925423,229.064465 C211.249936,194.628667 193.894294,194.904154 "
          "188.017251,192.241119 C182.140209,189.486255 175.987679,184.068356 "
          "169.10052,187.833337 C162.21336,191.690146 161.846045,201.607656 "
          "161.662388,214.647344 C161.386901,224.013881 153.581454,239.716605 "
          "158.264722,248.440341 C163.958107,258.633337 177.732426,243.848901 "
          "187.925423,229.064465 L187.925423,229.064465 Z",
     .gradient = 15},
    {.d = "M47.0600529,234.02322 C12.1651113,211.433337 28.5106366,203.719718 "
          "33.7448778,200.138395 C40.0810646,195.546955 40.1728934,186.731391 "
          "47.9783409,187.55785 C55.7837883,188.384309 60.375228,198.026333 "
          "65.6094693,209.964076 C69.4662786,218.504154 82.8732825,229.890924 "
          "81.8631658,239.716605 C80.5775626,251.287033 62.1199751,243.665243 "
          "47.0600529,234.02322 L47.0600529,234.02322 Z",
     .gradient = 16},
    {.d = "M199.587679,188.843453 C196.832816,193.618551 185.629703,201.148512 "
          "178.19157,199.128278 C170.569781,197.199874 167.080286,186.455905 "
          "168.641376,178.374971 C170.018808,169.192092 178.19157,168.732948 "
          "188.476395,173.324387 C199.404022,178.283142 202.801687,182.507267 "
          "199.587679,188.843453 L199.587679,188.843453 Z",
     .color = SkColorSetRGB(0x00, 0x00, 0x00)},
    {.d = "M192.057718,186.180418 C190.312971,189.486255 182.966668,194.720496 "
          "177.824255,193.343064 C172.681843,191.965632 170.110637,184.5275 "
          "170.937096,178.925944 C171.671726,172.589757 177.181454,172.222442 "
          "184.160442,175.344621 C191.690403,178.834115 194.077952,181.772636 "
          "192.057718,186.180418 L192.057718,186.180418 Z",
     .gradient = 17},
    {.d = "M97.1067455,66.3438425 C100.779897,62.9461771 109.68729,52.5695234 "
          "126.583788,63.4053211 C129.705967,65.4255546 132.277174,65.6092121 "
          "138.246045,68.1804184 C150.275617,73.1391732 144.582232,85.0769164 "
          "131.726201,89.1173833 C126.216473,90.8621304 121.257718,97.5656324 "
          "111.340209,96.9228308 C102.800131,96.4636868 100.59624,90.8621304 "
          "95.3619984,87.8317802 C86.0872903,82.597539 84.7098584,75.5267219 "
          "89.760442,71.7617413 C94.8110257,67.9967608 96.7394304,66.6193289 "
          "97.1067455,66.3438425 L97.1067455,66.3438425 Z",
     .gradient = 18,
     .stroke = SkColorSetRGB(0xE6, 0x8C, 0x3F),
     .stroke_width = 3.75f},
    {.d = "M138.429703,75.9858658 C133.379119,76.2613522 122.451493,87.1889787 "
          "110.972893,87.1889787 C99.4942942,87.1889787 92.6071346,76.5368386 "
          "90.8623875,76.5368386",
     .filled = false,
     .stroke = SkColorSetRGB(0xE6, 0x8C, 0x3F),
     .stroke_width = 2.5f},
    {.d = "M102.800131,65.4255546 C104.636707,63.7726363 110.421921,59.2730254 "
          "118.043711,63.8644651 C119.696629,64.782753 121.349547,65.7928697 "
          "123.737096,67.1703016 C128.604022,70.0169942 126.216473,74.14929 "
          "120.33943,76.7204962 C117.676395,77.8224417 113.268613,80.2099904 "
          "109.962777,80.0263328 C106.289625,79.6590176 103.810247,77.2714689 "
          "101.422699,75.7103795 C96.9230879,72.7718581 97.1985743,70.2924806 "
          "99.3106366,68.364076 C100.871726,66.8948153 102.616473,65.5173833 "
          "102.800131,65.4255546 L102.800131,65.4255546 Z",
     .gradient = 19},
};

// The edge Tux takes on dark, a blur of its outline in the edge's color, as
// SwiftUI's shadow(radius: 0.375) drew it.
constexpr float kTuxEdgeBlur = 0.375f;

const SkPath& ParsedPath(const char* d) {
  // Each path once, by its text's address: the tables are constants.
  static base::NoDestructor<std::vector<std::pair<const char*, SkPath>>> parsed;
  for (const auto& [text, path] : *parsed) {
    if (text == d) {
      return path;
    }
  }
  parsed->emplace_back(d, SkParsePath::FromSVGString(d).value_or(SkPath()));
  return parsed->back().second;
}

SkColor4f WithOpacity(SkColor color, float opacity) {
  SkColor4f color4 = SkColor4f::FromColor(color);
  color4.fA *= opacity;
  return color4;
}

void PaintPaths(gfx::Canvas& canvas,
                base::span<const LogoPath> paths,
                base::span<const Gradient> gradients,
                std::optional<SkColor> one_color) {
  for (const LogoPath& logo : paths) {
    const SkPath& path = ParsedPath(logo.d);
    if (logo.filled) {
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      if (one_color) {
        flags.setColor(*one_color);
      } else if (logo.gradient >= 0) {
        const Gradient& gradient =
            gradients[static_cast<size_t>(logo.gradient)];
        // The gradient's box is the path's own, its shares mapped onto it.
        const SkRect box = path.getBounds();
        const SkMatrix to_box = SkMatrix::Translate(box.left(), box.top()) *
                                SkMatrix::Scale(box.width(), box.height());
        const SkPoint points[] = {{gradient.x1, gradient.y1},
                                  {gradient.x2, gradient.y2}};
        const Gradient::Stop& from = gradient.from;
        const Gradient::Stop& to = gradient.to;
        const SkColor4f colors[] = {WithOpacity(from.color, from.opacity),
                                    WithOpacity(to.color, to.opacity)};
        const SkScalar positions[] = {from.offset, to.offset};
        flags.setShader(cc::PaintShader::MakeLinearGradient(
            points, colors, positions, 2, SkTileMode::kClamp,
            SkGradient::Interpolation(), 0, &to_box));
      } else {
        flags.setColor(logo.color);
      }
      canvas.DrawPath(path, flags);
    }
    if (logo.stroke_width > 0) {
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setStyle(cc::PaintFlags::kStroke_Style);
      flags.setStrokeWidth(logo.stroke_width);
      flags.setStrokeJoin(cc::PaintFlags::kMiter_Join);
      flags.setStrokeCap(cc::PaintFlags::kButt_Cap);
      flags.setColor(one_color.value_or(logo.stroke));
      canvas.DrawPath(path, flags);
    }
  }
}

}  // namespace

gfx::SizeF PlatformLogoSize(model::Platform platform) {
  switch (platform) {
    case model::Platform::kWindows:
      return gfx::SizeF(22, 22);
    case model::Platform::kMacOS:
      return gfx::SizeF(21.13f, 26);
    case model::Platform::kLinux:
      return gfx::SizeF(22.56f, 26);
  }
}

void PaintPlatformLogo(gfx::Canvas& canvas,
                       const ui::ColorProvider& colors,
                       model::Platform platform,
                       const gfx::PointF& origin) {
  const gfx::SizeF size = PlatformLogoSize(platform);
  gfx::ScopedCanvas scoped(&canvas);
  canvas.sk_canvas()->translate(origin.x(), origin.y());
  switch (platform) {
    case model::Platform::kWindows: {
      const float scale = size.width() / kWindowsGrid;
      canvas.sk_canvas()->scale(scale, scale);
      PaintPaths(canvas, kWindowsPaths, {}, std::nullopt);
      return;
    }
    case model::Platform::kMacOS: {
      canvas.sk_canvas()->scale(size.width() / kAppleWidth,
                                size.height() / kAppleHeight);
      const LogoPath apple[] = {{.d = kApplePath}};
      PaintPaths(canvas, apple, {}, colors.GetColor(kPaneColorAppleLogo));
      return;
    }
    case model::Platform::kLinux: {
      const SkColor edge = colors.GetColor(kPaneColorTuxEdge);
      const bool edged = SkColorGetA(edge) > 0;
      if (edged) {
        // The edge under the logo: its outline blurred in the edge's color.
        cc::PaintFlags layer;
        layer.setImageFilter(sk_make_sp<cc::DropShadowPaintFilter>(
            0, 0, kTuxEdgeBlur, kTuxEdgeBlur, SkColor4f::FromColor(edge),
            cc::DropShadowPaintFilter::ShadowMode::kDrawShadowAndForeground,
            nullptr));
        canvas.sk_canvas()->saveLayer(
            SkRect::MakeXYWH(-2, -2, size.width() + 4, size.height() + 4),
            layer);
      }
      canvas.sk_canvas()->scale(size.width() / kTuxWidth,
                                size.height() / kTuxHeight);
      canvas.sk_canvas()->translate(kTuxShift, 0);
      PaintPaths(canvas, kTuxPaths, kTuxGradients, std::nullopt);
      if (edged) {
        canvas.sk_canvas()->restore();
      }
      return;
    }
  }
}

}  // namespace pane
