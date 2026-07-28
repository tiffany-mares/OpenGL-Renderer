/* Phase 8b dashboard: renders the committed native benchmark data.
 * Data of record only -- every number is fetched from dist/data/*, staged by
 * web/build.py from files committed under bench/results/ and web/data/.
 * Nothing is measured in the browser; nothing numeric is hardcoded here
 * except axis/annotation labels justified by the committed results docs
 * (the ~100k crossover line cites bench/results/2026-07-27-handoff.md). */
'use strict';

/* ---- design tokens (dataviz reference palette, dark column) ---- */
var TOKEN = {
  ink: '#ffffff', ink2: '#c3c2b7', muted: '#898781',
  grid: '#2c2c2a', baseline: '#383835', surface: '#1a1a19'
};
var STRATEGY = [ // categorical slots 1-4, fixed order, never cycled
  { key: 'sleep',      label: 'sleep_for (naive)',              color: '#3987e5' },
  { key: 'timer',      label: 'high-res timer',                 color: '#199e70' },
  { key: 'timer_spin', label: 'timer + spin (shipping default)', color: '#c98500' },
  { key: 'spin',       label: 'pure spin',                      color: '#008300' }
];
var UNCAPPED = { key: 'uncapped', label: 'uncapped (no target)', color: '#d95926' }; // slot 8
var BACKEND = [ // categorical slots 5-7
  { key: 'mutex',   label: 'mutex',   color: '#9085e9' },
  { key: 'bitmask', label: 'bitmask', color: '#e66767' },
  { key: 'seqlock', label: 'seqlock', color: '#d55181' }
];
var HIST_XCAP_MS = 18; // same cap as the committed README figure; overflow noted

var state = { rate: '144', platform: 'win11-arc' };
var DATA = null;        // { hist, pacing, handoff } after boot
var histCharts = [];    // destroyed/rebuilt on rate change

/* ---- tiny CSV splitter: simple comma CSVs with empty fields, no quoting ---- */
function parseCsv(text) {
  var lines = text.trim().split(/\r?\n/);
  var head = lines[0].split(',');
  return lines.slice(1).map(function (line) {
    var cells = line.split(',');
    var row = {};
    head.forEach(function (h, i) { row[h] = cells[i] !== undefined ? cells[i] : ''; });
    return row;
  });
}

function fatal(msg) {
  var box = document.getElementById('data-error');
  box.textContent = 'FATAL: ' + msg;
  box.style.display = 'block';
}

function el(tag, cls, text) {
  var e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text !== undefined) e.textContent = text;
  return e;
}

function msFromNs(ns) { return (Number(ns) / 1e6).toFixed(3); }
function periodMs(rate) { return Math.floor(1e9 / Number(rate)) / 1e6; } // 10^9 // rate, as the pacer computes it
function fmtHz(v) {
  return v >= 1e6 ? (v / 1e6).toFixed(v >= 1e7 ? 0 : 1) + 'M'
       : v >= 1e3 ? Math.round(v / 1e3) + 'k' : String(Math.round(v));
}

function tooltipStyle(extra) {
  var base = {
    backgroundColor: '#232322', titleColor: TOKEN.ink, bodyColor: TOKEN.ink2,
    borderColor: TOKEN.baseline, borderWidth: 1, cornerRadius: 4,
    padding: 8, displayColors: false
  };
  if (extra) Object.keys(extra).forEach(function (k) { base[k] = extra[k]; });
  return base;
}

/* ---- inline plugins (no annotation plugin dependency) ---- */
var vlinePlugin = {
  id: 'vline',
  afterDraw: function (chart, _args, opts) {
    if (!opts || opts.x == null) return;
    var x = chart.scales.x.getPixelForValue(opts.x);
    var area = chart.chartArea;
    if (x < area.left || x > area.right) return;
    var ctx = chart.ctx;
    ctx.save();
    ctx.strokeStyle = TOKEN.muted;
    ctx.setLineDash([4, 3]);
    ctx.lineWidth = 1;
    ctx.beginPath(); ctx.moveTo(x, area.top); ctx.lineTo(x, area.bottom); ctx.stroke();
    if (opts.label) {
      ctx.setLineDash([]);
      ctx.fillStyle = TOKEN.ink2;
      ctx.font = '10px system-ui, sans-serif';
      ctx.textAlign = 'left'; ctx.textBaseline = 'top';
      ctx.fillText(opts.label, x + 5, area.top + 2);
    }
    ctx.restore();
  }
};
var endLabelPlugin = { // in-ink direct labels: identity never rides on color alone
  id: 'endLabels',
  afterDatasetsDraw: function (chart, _args, opts) {
    if (!opts || !opts.enabled) return;
    chart.data.datasets.forEach(function (ds, i) {
      var meta = chart.getDatasetMeta(i);
      var pt = meta.data[meta.data.length - 1];
      if (!pt) return;
      var ctx = chart.ctx;
      ctx.save();
      ctx.fillStyle = TOKEN.ink2;
      ctx.font = '10px system-ui, sans-serif';
      ctx.textAlign = 'right'; ctx.textBaseline = 'bottom';
      ctx.fillText(ds.label, pt.x - 2, pt.y - 6);
      ctx.restore();
    });
  }
};
Chart.register(vlinePlugin, endLabelPlugin);

Chart.defaults.font.family = 'system-ui, -apple-system, "Segoe UI", sans-serif';
Chart.defaults.font.size = 11;
Chart.defaults.color = TOKEN.muted;
Chart.defaults.animation = false;

function axisStyle(extra) {
  var base = { grid: { color: TOKEN.grid }, border: { color: TOKEN.baseline },
               ticks: { color: TOKEN.muted } };
  if (extra) {
    Object.keys(extra).forEach(function (k) {
      if (k === 'ticks') Object.keys(extra.ticks).forEach(function (t) { base.ticks[t] = extra.ticks[t]; });
      else base[k] = extra[k];
    });
  }
  return base;
}

/* ================= frame-pacing histograms (small multiples) ============= */
function panelsForRate(rate) {
  if (rate === 'uncapped') {
    return [{ def: UNCAPPED, cell: DATA.hist.cells['uncapped-0'] }];
  }
  return STRATEGY.map(function (s) {
    return { def: s, cell: DATA.hist.cells[s.key + '-' + rate] };
  });
}

function renderHistograms(rate) {
  var grid = document.getElementById('hist-grid');
  histCharts.forEach(function (c) { c.destroy(); });
  histCharts = [];
  grid.innerHTML = '';

  var panels = panelsForRate(rate);
  for (var i = 0; i < panels.length; i++) {
    if (!panels[i].cell) {
      return fatal('frametime-hist.json is missing cell ' +
                   panels[i].def.key + ' at rate ' + rate);
    }
  }
  var binMs = DATA.hist.bin_ns / 1e6; // 0.05

  /* shared scales across the multiples: one x window, one log-y ceiling */
  var maxEdge = 0, maxCount = 1;
  panels.forEach(function (p) {
    p.cell.bins.forEach(function (b, i) {
      var hi = (b + 1) * binMs;
      if (hi <= HIST_XCAP_MS && hi > maxEdge) maxEdge = hi;
      if (p.cell.counts[i] > maxCount) maxCount = p.cell.counts[i];
    });
  });
  var xmax = Math.min(HIST_XCAP_MS, Math.ceil(maxEdge));
  var nb = Math.round(xmax / binMs);
  var ymax = Math.pow(10, Math.ceil(Math.log10(maxCount)));
  var pline = rate === 'uncapped' ? null : periodMs(rate);

  panels.forEach(function (p) {
    var dense = new Array(nb).fill(null); // dense over the window: uniform bar ruler
    var overflow = 0, overflowMaxMs = 0;
    p.cell.bins.forEach(function (b, i) {
      if (b < nb) dense[b] = p.cell.counts[i];
      else {
        overflow += p.cell.counts[i];
        var hi = (b + 1) * binMs;
        if (hi > overflowMaxMs) overflowMaxMs = hi;
      }
    });
    var points = dense.map(function (c, i) { return { x: (i + 0.5) * binMs, y: c }; });

    var card = el('div', 'card');
    var title = el('h4', 'panel-title');
    var chip = el('span', 'chip'); chip.style.background = p.def.color;
    title.appendChild(chip);
    title.appendChild(document.createTextNode(p.def.label));
    card.appendChild(title);
    var box = el('div', 'chart-box');
    var cv = document.createElement('canvas');
    box.appendChild(cv);
    card.appendChild(box);
    if (overflow) {
      card.appendChild(el('p', 'overflow-note',
        overflow + ' interval(s) beyond ' + HIST_XCAP_MS + ' ms not shown (up to ' + overflowMaxMs.toFixed(1) + ' ms)'));
    }
    grid.appendChild(card);

    histCharts.push(new Chart(cv, {
      type: 'bar',
      data: { datasets: [{
        label: p.def.label, data: points,
        backgroundColor: p.def.color, borderWidth: 0,
        barPercentage: 1.0, categoryPercentage: 1.0, grouped: false
      }] },
      options: {
        responsive: true, maintainAspectRatio: false,
        scales: {
          x: axisStyle({ type: 'linear', min: 0, max: xmax,
            title: { display: true, text: 'start-to-start interval (ms)',
                     color: TOKEN.muted, font: { size: 10 } },
            ticks: { maxTicksLimit: 7 } }),
          y: axisStyle({ type: 'logarithmic', min: 0.8, max: ymax,
            title: { display: true, text: 'intervals (log)',
                     color: TOKEN.muted, font: { size: 10 } },
            ticks: { callback: function (v) {
              return [1, 10, 100, 1000, 10000, 100000].indexOf(v) >= 0
                     ? v.toLocaleString() : '';
            } } })
        },
        plugins: {
          legend: { display: false }, // single series: the panel title names it
          tooltip: tooltipStyle({ callbacks: {
            title: function (items) {
              var x = items[0].raw.x;
              return (x - binMs / 2).toFixed(2) + '–' +
                     (x + binMs / 2).toFixed(2) + ' ms';
            },
            label: function (item) {
              return item.raw.y.toLocaleString() + ' of ' +
                     p.cell.n.toLocaleString() + ' intervals';
            }
          } }),
          vline: pline == null ? { x: null }
               : { x: pline, label: pline.toFixed(3) + ' ms period' }
        }
      }
    }));
  });
}

/* ======================= pacing percentile table ======================== */
function renderPacingTable(rate) {
  var host = document.getElementById('pacing-table');
  host.innerHTML = '';
  var defs = rate === 'uncapped' ? [UNCAPPED] : STRATEGY;
  var want = rate === 'uncapped' ? '0' : rate;
  var table = el('table');
  var thead = el('thead');
  var hr = el('tr');
  ['strategy', 'p50 ms', 'p95 ms', 'p99 ms', 'max ms', 'stddev ms',
   'missed', 'cpu %'].forEach(function (h) { hr.appendChild(el('th', null, h)); });
  thead.appendChild(hr);
  table.appendChild(thead);
  var tbody = el('tbody');
  defs.forEach(function (d) {
    var row = DATA.pacing.find(function (r) {
      return r.strategy === d.key && r.rate_hz === want;
    });
    if (!row) return fatal('pacing-summary.csv is missing ' + d.key + ',' + want);
    var tr = el('tr');
    var name = el('td');
    var chip = el('span', 'chip'); chip.style.background = d.color;
    chip.style.display = 'inline-block'; chip.style.marginRight = '7px';
    chip.style.verticalAlign = 'baseline';
    name.appendChild(chip);
    name.appendChild(document.createTextNode(d.label));
    tr.appendChild(name);
    [msFromNs(row.p50_ns), msFromNs(row.p95_ns), msFromNs(row.p99_ns),
     msFromNs(row.max_ns), msFromNs(row.stddev_ns),
     Number(row.missed).toLocaleString(), Number(row.cpu_pct).toFixed(1)
    ].forEach(function (v) { tr.appendChild(el('td', null, v)); });
    tbody.appendChild(tr);
  });
  table.appendChild(tbody);
  host.appendChild(table);
  document.getElementById('pacing-title').textContent =
    rate === 'uncapped' ? 'Frame pacing — uncapped baseline (no target)'
                        : 'Frame pacing at ' + rate + ' Hz';
}

/* ==================== handoff: cost + contended sweep =================== */
function renderCostChart() {
  var cost = {};
  DATA.handoff.filter(function (r) { return r.table === 'cost'; })
    .forEach(function (r) {
      cost[r.backend] = { publish: Number(r.publish_ns), read: Number(r.read_ns) };
    });
  BACKEND.forEach(function (b) {
    if (!cost[b.key]) fatal('handoff-summary.csv has no cost row for ' + b.key);
  });
  new Chart(document.getElementById('cost-chart'), {
    type: 'bar',
    data: {
      labels: ['publish', 'read'],
      datasets: BACKEND.map(function (b) {
        return { label: b.label, data: [cost[b.key].publish, cost[b.key].read],
                 backgroundColor: b.color, borderRadius: 3, maxBarThickness: 44 };
      })
    },
    options: {
      responsive: true, maintainAspectRatio: false,
      scales: {
        x: axisStyle({ grid: { display: false } }),
        y: axisStyle({ beginAtZero: true,
          title: { display: true, text: 'ns per op (amortized)',
                   color: TOKEN.muted, font: { size: 10 } } })
      },
      plugins: {
        legend: { position: 'top', align: 'end',
                  labels: { color: TOKEN.ink2, boxWidth: 9, boxHeight: 9 } },
        tooltip: tooltipStyle({ displayColors: true, callbacks: {
          label: function (item) {
            return item.dataset.label + ': ' + item.parsed.y + ' ns/op';
          }
        } })
      }
    }
  });
}

var SWEEP_TARGETS = ['1k', '10k', '100k', '1M', 'unthrottled'];
function renderSweepChart() {
  var rows = DATA.handoff.filter(function (r) { return r.table === 'sweep'; });
  var datasets = BACKEND.map(function (b) {
    var pts = rows.filter(function (r) { return r.backend === b.key; })
      .map(function (r) {
        return { x: Number(r.achieved_hz), y: Number(r.read_p99_ns),
                 target: r.rate_hz === '0' ? 'unthrottled' : fmtHz(Number(r.rate_hz)) + '/s' };
      })
      .sort(function (a, b2) { return a.x - b2.x; });
    if (pts.length !== 5) fatal('handoff-summary.csv: expected 5 sweep rows for ' +
                                b.key + ', got ' + pts.length);
    return { label: b.label, data: pts, borderColor: b.color,
             backgroundColor: b.color, pointRadius: 4, pointHitRadius: 10,
             borderWidth: 2, tension: 0 };
  });
  new Chart(document.getElementById('sweep-chart'), {
    type: 'line',
    data: { datasets: datasets },
    options: {
      responsive: true, maintainAspectRatio: false,
      interaction: { mode: 'index', intersect: false }, // crosshair-style shared tooltip
      scales: {
        x: axisStyle({ type: 'logarithmic', min: 800, max: 2e7,
          title: { display: true, text: 'achieved publishes/s (log)',
                   color: TOKEN.muted, font: { size: 10 } },
          ticks: { callback: function (v) {
            return { 1e3: '1k', 1e4: '10k', 1e5: '100k', 1e6: '1M', 1e7: '10M' }[v] || '';
          } } }),
        y: axisStyle({ beginAtZero: true,
          title: { display: true, text: 'reader p99 (ns)',
                   color: TOKEN.muted, font: { size: 10 } } })
      },
      plugins: {
        legend: { position: 'top', align: 'end',
                  labels: { color: TOKEN.ink2, boxWidth: 9, boxHeight: 9,
                            usePointStyle: true } },
        endLabels: { enabled: true },
        /* 1e5 is the measured crossover from bench/results/2026-07-27-handoff.md:
         * "first measurable departure is at ~100,000 publishes/s". */
        vline: { x: 1e5, label: '≈100k/s — mutex p99 first departs' },
        tooltip: tooltipStyle({ displayColors: true, callbacks: {
          title: function (items) {
            return 'target ' + (items[0].raw.target || SWEEP_TARGETS[items[0].dataIndex]);
          },
          label: function (item) {
            return item.dataset.label + ': ' + item.raw.y +
                   ' ns (achieved ' + fmtHz(item.raw.x) + '/s)';
          }
        } })
      }
    }
  });
}

function renderHandoffTables() {
  var host = document.getElementById('handoff-tables');
  host.innerHTML = '';
  var t1 = el('table');
  t1.innerHTML = '<thead><tr><th>backend</th><th>publish ns/op</th>' +
                 '<th>read ns/op</th></tr></thead>';
  var b1 = el('tbody');
  DATA.handoff.filter(function (r) { return r.table === 'cost'; })
    .forEach(function (r) {
      var tr = el('tr');
      [r.backend, r.publish_ns, r.read_ns].forEach(function (v) {
        tr.appendChild(el('td', null, v));
      });
      b1.appendChild(tr);
    });
  t1.appendChild(b1);
  host.appendChild(t1);

  var t2 = el('table');
  t2.innerHTML = '<thead><tr><th>backend</th><th>target Hz</th>' +
                 '<th>achieved Hz</th><th>read p99 ns</th>' +
                 '<th>retries/s</th></tr></thead>';
  var b2 = el('tbody');
  DATA.handoff.filter(function (r) { return r.table === 'sweep'; })
    .forEach(function (r) {
      var tr = el('tr');
      [r.backend, r.rate_hz === '0' ? 'unthrottled' : r.rate_hz,
       r.achieved_hz, r.read_p99_ns, r.retries_per_sec].forEach(function (v) {
        tr.appendChild(el('td', null, v));
      });
      b2.appendChild(tr);
    });
  t2.appendChild(b2);
  host.appendChild(t2);
}

/* ============================ filter wiring ============================= */
function wireFilters() {
  var seg = document.getElementById('rate-seg');
  Array.prototype.forEach.call(seg.querySelectorAll('button'), function (btn) {
    btn.addEventListener('click', function () {
      if (state.rate === btn.dataset.rate) return;
      state.rate = btn.dataset.rate;
      Array.prototype.forEach.call(seg.querySelectorAll('button'), function (b) {
        b.classList.toggle('active', b === btn);
      });
      /* rate change rebuilds ONLY the rate-scoped views; entity colors are
       * keyed by strategy/backend constants, so survivors never repaint */
      renderHistograms(state.rate);
      renderPacingTable(state.rate);
    });
  });
  document.getElementById('platform').addEventListener('change', function (e) {
    state.platform = e.target.value; // single honest option today; shape ships
  });
}

/* ================================ boot ================================== */
Promise.all([
  fetch('data/frametime-hist.json'),
  fetch('data/pacing-summary.csv'),
  fetch('data/handoff-summary.csv')
]).then(function (rs) {
  rs.forEach(function (r) {
    if (!r.ok) throw new Error(r.url + ' -> HTTP ' + r.status);
  });
  return Promise.all([rs[0].json(), rs[1].text(), rs[2].text()]);
}).then(function (loaded) {
  DATA = { hist: loaded[0], pacing: parseCsv(loaded[1]),
           handoff: parseCsv(loaded[2]) };
  if (!DATA.hist.cells || !DATA.hist.bin_ns) {
    throw new Error('frametime-hist.json has no cells/bin_ns -- wrong file staged?');
  }
  wireFilters();
  renderHistograms(state.rate);
  renderPacingTable(state.rate);
  renderCostChart();
  renderSweepChart();
  renderHandoffTables();
}).catch(function (e) {
  fatal('dashboard data failed to load — ' + e.message +
        '. (fetch needs http://; for a local preview run ' +
        '`python -m http.server -d dist`, file:// will not work)');
});
