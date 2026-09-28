#pragma once

#include <Arduino.h>

// Offline (no external JS/CSS), inline-SVG line chart generator: straight-line segments between
// points (deliberately NOT curve-smoothed - several series here, RSSI especially, are noisy
// instantaneous samples rather than slow trends, and smoothing implies structure the data doesn't
// have), gradient fill under the line, a light reference grid, rounded stroke joins, and native
// <title> tooltips per point instead of any JS hover library. One static function, reused for
// every historical series (battery, RSSI/SNR, noise floor, packet rates) - the caller downsamples
// to a sane point count (StatsHistory::downsample) and picks per-series colour/labels.
#ifndef SVGCHART_MAX_POINTS
  #define SVGCHART_MAX_POINTS   200   // must be >= the dashboard's downsample point cap
#endif

class SvgChart {
public:
  struct Options {
    int width = 640;
    int height = 180;
    int margin_left = 42;
    int margin_right = 12;
    int margin_top = 14;
    int margin_bottom = 22;
    const char* stroke_color = "#2e7d32";
    const char* grad_top = "#2e7d3255";     // ~33% alpha
    const char* grad_bottom = "#2e7d3200";  // transparent
    const char* value_suffix = "";          // appended to grid/point labels, e.g. "%", " mV"
    int decimals = 0;                       // decimals for grid/tooltip value labels
  };

  // x: monotonically increasing "seconds ago" per point (n entries, oldest-first is fine too -
  //    only relative spacing matters for layout). y: same length as x.
  // point_labels: optional per-point tooltip prefix (e.g. "3h ago"); pass NULL to auto-format
  //    from x[] assuming it holds "seconds ago" values.
  static String render(const uint32_t* age_secs, const float* y, int n, const char* series_id,
                        const Options& opt) {
    if (n <= 0) {
      return emptyChart(opt);
    }
    if (n > SVGCHART_MAX_POINTS) n = SVGCHART_MAX_POINTS;   // safety clamp, see SVGCHART_MAX_POINTS

    float y_min, y_max;
    computeRange(y, n, y_min, y_max);

    int plot_w = opt.width - opt.margin_left - opt.margin_right;
    int plot_h = opt.height - opt.margin_top - opt.margin_bottom;

    float px[SVGCHART_MAX_POINTS], py[SVGCHART_MAX_POINTS];
    // oldest point on the left: age_secs is "seconds ago", so the largest age is oldest (x=0)
    uint32_t max_age = age_secs[0];
    for (int i = 0; i < n; i++) if (age_secs[i] > max_age) max_age = age_secs[i];

    for (int i = 0; i < n; i++) {
      float frac_x = (max_age == 0 || n == 1) ? (n == 1 ? 0.5f : 0) : (float)(max_age - age_secs[i]) / (float)max_age;
      px[i] = opt.margin_left + frac_x * plot_w;
      float frac_y = (y_max > y_min) ? (y[i] - y_min) / (y_max - y_min) : 0.5f;
      py[i] = opt.margin_top + (1.0f - frac_y) * plot_h;
    }

    String svg;
    svg.reserve(2048 + n * 96);
    svg += "<svg class=\"chart\" viewBox=\"0 0 " + String(opt.width) + " " + String(opt.height) +
           "\" width=\"100%\" height=\"" + String(opt.height) + "\" preserveAspectRatio=\"xMidYMid meet\">";

    svg += "<defs><linearGradient id=\"g_" + String(series_id) + "\" x1=\"0\" y1=\"0\" x2=\"0\" y2=\"1\">";
    svg += "<stop offset=\"0%\" stop-color=\"" + String(opt.grad_top) + "\"/>";
    svg += "<stop offset=\"100%\" stop-color=\"" + String(opt.grad_bottom) + "\"/></linearGradient></defs>";

    // reference grid: 0/25/50/75/100% of the value range, with value labels
    for (int g = 0; g <= 4; g++) {
      float frac = g / 4.0f;
      float gy = opt.margin_top + (1.0f - frac) * plot_h;
      float gval = y_min + frac * (y_max - y_min);
      svg += "<line x1=\"" + String(opt.margin_left) + "\" y1=\"" + String(gy, 1) +
             "\" x2=\"" + String(opt.width - opt.margin_right) + "\" y2=\"" + String(gy, 1) +
             "\" stroke=\"#e0e0e0\" stroke-width=\"0.5\"/>";
      svg += "<text x=\"2\" y=\"" + String(gy + 3, 1) + "\" class=\"axis\">" +
             String(gval, opt.decimals) + opt.value_suffix + "</text>";
    }

    // x-axis: "time ago" labels along the bottom, same formatAge() used in the per-point
    // tooltips below. Point spacing in time (and thus pixels) isn't guaranteed even, so ticks
    // are picked up to a fixed candidate count but then filtered by actual pixel distance -
    // first and last are always shown, a middle candidate is only drawn if there's genuinely
    // enough room next to whichever label was drawn immediately before it and before the last
    // one, rather than assuming even spacing and letting labels overlap.
    {
      int bottom_y = opt.height - 4;
      if (n == 1) {
        svg += "<text x=\"" + String(px[0], 1) + "\" y=\"" + String(bottom_y) +
               "\" text-anchor=\"middle\" class=\"axis\">" + formatAge(age_secs[0]) + "</text>";
      } else {
        const float min_gap_px = 44.0f;   // enough room for e.g. "23h ago" at 9px font
        int n_candidates = (n < 5) ? n : 5;
        float last_label_x = -1e9f;
        for (int t = 0; t < n_candidates; t++) {
          int idx = (int)((long)t * (n - 1) / (n_candidates - 1));
          bool first = (idx == 0), last = (idx == n - 1);
          if (!first && !last && (px[idx] - last_label_x < min_gap_px || px[n - 1] - px[idx] < min_gap_px)) {
            continue;   // too close to the previous label, or to where the last label will land
          }
          const char* anchor = first ? "start" : last ? "end" : "middle";
          svg += "<text x=\"" + String(px[idx], 1) + "\" y=\"" + String(bottom_y) +
                 "\" text-anchor=\"" + String(anchor) + "\" class=\"axis\">" + formatAge(age_secs[idx]) + "</text>";
          last_label_x = px[idx];
        }
      }
    }

    // Plain straight-line segments between points, deliberately not smoothed: several of these
    // series (RSSI in particular) are inherently noisy instantaneous samples, not slow trends -
    // a smoothed curve implies structure/flow the data doesn't actually have. Straight segments
    // show real point-to-point jumps honestly instead of dressing noise up as a flowing shape.
    String pathD = "M " + String(px[0], 1) + "," + String(py[0], 1);
    for (int i = 1; i < n; i++) {
      pathD += " L " + String(px[i], 1) + "," + String(py[i], 1);
    }

    // filled area under the line, closed down to the baseline
    String fillD = pathD + " L " + String(px[n - 1], 1) + "," + String(opt.margin_top + plot_h) +
                    " L " + String(px[0], 1) + "," + String(opt.margin_top + plot_h) + " Z";
    svg += "<path d=\"" + fillD + "\" fill=\"url(#g_" + String(series_id) + ")\" stroke=\"none\"/>";

    svg += "<path d=\"" + pathD + "\" fill=\"none\" stroke=\"" + String(opt.stroke_color) +
           "\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>";

    // Native tooltips, no JS: a small circle + <title> per point. The line path above costs only
    // ~20 bytes/point, but each circle+tooltip costs ~150-200 bytes - multiplied across n points
    // and several charts on one page, that's the dominant cost of the whole HTML response (this
    // is exactly what was blowing through free heap and truncating the page mid-render). Cap the
    // circle count independent of n rather than one per point; the line itself stays full
    // resolution, only the hover markers get subsampled. Always include the last point.
    const int max_circles = 24;
    int circle_step = (n > max_circles) ? (n + max_circles - 1) / max_circles : 1;
    for (int i = 0; i < n; i += circle_step) {
      svg += "<circle cx=\"" + String(px[i], 1) + "\" cy=\"" + String(py[i], 1) +
             "\" r=\"2.5\" fill=\"" + String(opt.stroke_color) + "\"><title>" +
             formatAge(age_secs[i]) + ": " + String(y[i], opt.decimals) + opt.value_suffix +
             "</title></circle>";
    }
    if ((n - 1) % circle_step != 0) {
      int i = n - 1;
      svg += "<circle cx=\"" + String(px[i], 1) + "\" cy=\"" + String(py[i], 1) +
             "\" r=\"2.5\" fill=\"" + String(opt.stroke_color) + "\"><title>" +
             formatAge(age_secs[i]) + ": " + String(y[i], opt.decimals) + opt.value_suffix +
             "</title></circle>";
    }

    svg += "</svg>";
    return svg;
  }

  // Shared <style> block (system-ui font stack, axis label styling, chart container) - include
  // once in the page <head>/<style>, reused by every chart on the page.
  static const char* sharedStyle() {
    return
      ".chart{display:block;margin:4px 0}"
      ".chart .axis{font:9px system-ui,sans-serif;fill:#888}"
      ".chart-card{border:1px solid #e0e0e0;border-radius:8px;padding:10px 12px;margin:8px 0}"
      ".chart-card h3{margin:0 0 4px;font:600 13px system-ui,sans-serif;color:#333}";
  }

private:
  static void computeRange(const float* y, int n, float& out_min, float& out_max) {
    out_min = out_max = y[0];
    for (int i = 1; i < n; i++) {
      if (y[i] < out_min) out_min = y[i];
      if (y[i] > out_max) out_max = y[i];
    }
    if (out_max <= out_min) {
      // flat/constant series - synthesize a bit of headroom so the grid isn't degenerate
      out_min -= 1.0f;
      out_max += 1.0f;
    } else {
      float pad = (out_max - out_min) * 0.08f;
      out_min -= pad;
      out_max += pad;
    }
  }

  static String formatAge(uint32_t age_secs) {
    if (age_secs < 60) return String(age_secs) + "s ago";
    if (age_secs < 3600) return String(age_secs / 60) + "m ago";
    if (age_secs < 86400) return String(age_secs / 3600) + "h ago";
    return String(age_secs / 86400) + "d ago";
  }

  static String emptyChart(const Options& opt) {
    String svg = "<svg class=\"chart\" viewBox=\"0 0 " + String(opt.width) + " " + String(opt.height) +
                 "\" width=\"100%\" height=\"" + String(opt.height) + "\">";
    svg += "<text x=\"" + String(opt.width / 2) + "\" y=\"" + String(opt.height / 2) +
           "\" text-anchor=\"middle\" class=\"axis\">no data yet</text></svg>";
    return svg;
  }
};
