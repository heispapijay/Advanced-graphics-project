#include "image.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

using namespace std;

// helper function to clamp values between min and max
// added this because std::clamp is C++17 and I want to be safe
float clamp_float(float val, float minVal, float maxVal) {
  if (val < minVal)
    return minVal;
  if (val > maxVal)
    return maxVal;
  return val;
}

struct Point {
  float x, y;
};

// helpers to make shapes
vector<Point> createRect(float x, float y, float w, float h) {
  vector<Point> pts;
  pts.push_back({x, y});
  pts.push_back({x + w, y});
  pts.push_back({x + w, y + h});
  pts.push_back({x, y + h});
  return pts;
}

// color structure dealing with floats for calculation
struct ColorF {
  float r, g, b, a;

  ColorF() : r(0), g(0), b(0), a(1) {}
  ColorF(float _r, float _g, float _b, float _a) : r(_r), g(_g), b(_b), a(_a) {}

  // convert to 0-255 rgba (had to check stackoverflow for this)
  RGBA toRGBA() {
    return RGBA((Byte)clamp_float(r * 255.0f, 0.0f, 255.0f),
                (Byte)clamp_float(g * 255.0f, 0.0f, 255.0f),
                (Byte)clamp_float(b * 255.0f, 0.0f, 255.0f),
                (Byte)clamp_float(a * 255.0f, 0.0f, 255.0f));
  }

  // convert from 0-255 RGBA
  static ColorF fromRGBA(RGBA c) {
    return ColorF(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
  }
};

// blending modes
enum BlendMode {
  BLEND_NORMAL,
  BLEND_MULTIPLY,
  BLEND_ADD,
  BLEND_DIFFERENCE,
  BLEND_OVERLAY
};

// function to blend two colors based on the blend mode
ColorF blend(ColorF src, ColorF dest, int mode) {
  float alpha = src.a;
  float invAlpha = 1.0f - alpha;

  float r = dest.r;
  float g = dest.g;
  float b = dest.b;

  // blend modes
  if (mode == BLEND_NORMAL) {
    r = src.r;
    g = src.g;
    b = src.b;
  } else if (mode == BLEND_MULTIPLY) {
    r = src.r * dest.r;
    g = src.g * dest.g;
    b = src.b * dest.b;
  } else if (mode == BLEND_ADD) {
    r = clamp_float(src.r + dest.r, 0, 1);
    g = clamp_float(src.g + dest.g, 0, 1);
    b = clamp_float(src.b + dest.b, 0, 1);
  } else if (mode == BLEND_DIFFERENCE) {
    r = std::abs(dest.r - src.r);
    g = std::abs(dest.g - src.g);
    b = std::abs(dest.b - src.b);
  } else if (mode == BLEND_OVERLAY) {
    float (*overlay_channel)(float, float) = [](float s, float d) {
      if (d < 0.5f) {
        return 2.0f * s * d;
      } else {
        return 1.0f - 2.0f * (1.0f - s) * (1.0f - d);
      }
    };
    r = overlay_channel(src.r, dest.r);
    g = overlay_channel(src.g, dest.g);
    b = overlay_channel(src.b, dest.b);
  }

  return ColorF(r * alpha + dest.r * invAlpha, g * alpha + dest.g * invAlpha,
                b * alpha + dest.b * invAlpha, 1.0f);
}

struct GradientStop {
  float position;
  ColorF color;
};

bool compareStops(const GradientStop &a, const GradientStop &b) {
  return a.position < b.position;
}

struct Gradient {
  bool isRadial;
  Point p1, p2;
  float radius;
  vector<GradientStop> stops;

  void addStop(float pos, ColorF col) {
    stops.push_back({pos, col});
    std::sort(stops.begin(), stops.end(), compareStops);
  }

  ColorF getColorAt(float t) const {
    if (stops.empty())
      return ColorF(0, 0, 0, 1);

    if (t <= stops.front().position)
      return stops.front().color;
    if (t >= stops.back().position)
      return stops.back().color;

    // finding the two stops t is between
    for (size_t i = 0; i < stops.size() - 1; i++) {
      if (t >= stops[i].position && t <= stops[i + 1].position) {
        float t0 = stops[i].position;
        float t1 = stops[i + 1].position;
        float f = (t - t0) / (t1 - t0);

        ColorF c1 = stops[i].color;
        ColorF c2 = stops[i + 1].color;

        // linear interpolation
        return ColorF(c1.r + (c2.r - c1.r) * f, c1.g + (c2.g - c1.g) * f,
                      c1.b + (c2.b - c1.b) * f, c1.a + (c2.a - c1.a) * f);
      }
    }
    return stops.back().color;
  }
};

// edge bucket for scanline algorithm
struct Edge {
  int yMin, yMax;
  float x;
  float mInv;
};

// compare edges by x coordinate
bool compareEdges(const Edge &a, const Edge &b) { return a.x < b.x; }

// function to fill the polygon
void drawPolygon(ColorImage &image, vector<Point> vertices, ColorF color,
                 Gradient *grad, int blendMode) {
  if (vertices.size() < 3)
    return;

  int width = image.GetWidth();
  int height = image.GetHeight();

  vector<Edge> edges;
  int globalMinY = height;
  int globalMaxY = 0;

  for (size_t i = 0; i < vertices.size(); i++) {
    Point p1 = vertices[i];
    Point p2 = vertices[(i + 1) % vertices.size()];

    if ((int)p1.y == (int)p2.y)
      continue;

    if (p1.y > p2.y) {
      Point temp = p1;
      p1 = p2;
      p2 = temp;
    }

    Edge e;
    e.yMin = (int)p1.y;
    e.yMax = (int)p2.y;
    e.x = p1.x;
    e.mInv = (p2.x - p1.x) / (p2.y - p1.y);

    if (e.yMax <= 0 || e.yMin >= height)
      continue;

    edges.push_back(e);
    if (e.yMin < globalMinY)
      globalMinY = e.yMin;
    if (e.yMax > globalMaxY)
      globalMaxY = e.yMax;
  }

  // clamp Y range
  if (globalMinY < 0)
    globalMinY = 0;
  if (globalMaxY > height)
    globalMaxY = height;

  // iiterate through scanlines
  for (int y = globalMinY; y < globalMaxY; y++) {
    // find intersections for this scanline
    vector<float> nodes;
    for (size_t i = 0; i < edges.size(); i++) {
      if (y >= edges[i].yMin && y < edges[i].yMax) {
        float intersectX =
            edges[i].x + edges[i].mInv * (float)(y - edges[i].yMin);
        nodes.push_back(intersectX);
      }
    }

    // sort x intersections
    std::sort(nodes.begin(), nodes.end());

    // fill pixels between pairs of nodes (Even-Odd rule)
    for (size_t i = 0; i < nodes.size(); i += 2) {
      if (i + 1 >= nodes.size())
        break;

      int startX = (int)nodes[i];
      int endX = (int)nodes[i + 1];

      if (startX >= width)
        continue;
      if (endX <= 0)
        continue;

      if (startX < 0)
        startX = 0;
      if (endX > width)
        endX = width;

      for (int x = startX; x < endX; x++) {
        ColorF drawColor = color;

        if (grad != nullptr) {
          float t = 0;
          if (grad->isRadial) {
            float dx = x - grad->p1.x;
            float dy = y - grad->p1.y;
            float dist = sqrt(dx * dx + dy * dy);
            t = dist / grad->radius;
          } else {
            float dx = grad->p2.x - grad->p1.x;
            float dy = grad->p2.y - grad->p1.y;
            float lenSq = dx * dx + dy * dy;
            float pdx = x - grad->p1.x;
            float pdy = y - grad->p1.y;
            t = (pdx * dx + pdy * dy) / lenSq;
          }
          t = clamp_float(t, 0.0f, 1.0f);
          drawColor = grad->getColorAt(t);
        }
        RGBA bgPixel = image.Get(x, y);
        ColorF bgColor = ColorF::fromRGBA(bgPixel);
        ColorF finalColor = blend(drawColor, bgColor, blendMode);
        image(x, y) = finalColor.toRGBA();
      }
    }
  }
}

vector<Point> createCircle(float cx, float cy, float r, int segments) {
  vector<Point> pts;
  for (int i = 0; i < segments; i++) {
    float angle = 2.0f * M_PI * (float)i / (float)segments;
    pts.push_back({cx + r * cos(angle), cy + r * sin(angle)});
  }
  return pts;
}

int main() {
  int W = 800;
  int H = 600;
  ColorImage canvas(W, H);

  // clear white background
  for (int i = 0; i < W; i++) {
    for (int j = 0; j < H; j++) {
      canvas(i, j) = RGBA(255, 255, 255);
    }
  }

  // red rectangle
  vector<Point> rect = createRect(50, 50, 200, 150);
  drawPolygon(canvas, rect, ColorF(1, 0, 0, 1), nullptr, BLEND_NORMAL);

  // circle with radial gradient
  vector<Point> circle = createCircle(400, 300, 100, 50);
  Gradient radGrad;
  radGrad.isRadial = true;
  radGrad.p1 = {400, 300};
  radGrad.radius = 100;
  radGrad.addStop(0.0, ColorF(0, 0, 1, 1));
  radGrad.addStop(1.0, ColorF(0, 0, 0, 0));
  drawPolygon(canvas, circle, ColorF(0, 0, 0, 0), &radGrad, BLEND_NORMAL);

  // triangle with linear gradient and overlay
  vector<Point> tri;
  tri.push_back({100, 400});
  tri.push_back({300, 400});
  tri.push_back({200, 250});

  Gradient linGrad;
  linGrad.isRadial = false;
  linGrad.p1 = {100, 400};
  linGrad.p2 = {300, 400};
  linGrad.addStop(0.0, ColorF(0, 1, 0, 1));
  linGrad.addStop(1.0, ColorF(1, 1, 0, 0.5));

  // using multiply blend mode so it shows up on white background
  drawPolygon(canvas, tri, ColorF(0, 0, 0, 0), &linGrad, BLEND_MULTIPLY);

  // star shape (difference mode)
  vector<Point> star;
  float cx = 600, cy = 150, rOut = 80, rIn = 30;
  for (int i = 0; i < 10; i++) {
    float r = (i % 2 == 0) ? rOut : rIn;
    float a = i * M_PI / 5.0f;
    star.push_back({cx + r * sin(a), cy - r * cos(a)});
  }
  drawPolygon(canvas, star, ColorF(1, 0.5, 0, 0.8), nullptr, BLEND_DIFFERENCE);

  canvas.Save("output.png");
  cout << "output.png for result" << endl;

  return 0;
}