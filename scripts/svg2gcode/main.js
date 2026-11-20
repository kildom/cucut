const SVG_NS = 'http://www.w3.org/2000/svg';
const $ = (selector) => document.querySelector(selector);
const groupsBody = $('#groups-body');
const messages = $('#messages');
const fileInput = $('#file-input');
const dropZone = $('#drop-zone');
let loadedGroups = [];
let loadedName = '';
let svgWarnings = [];
let outputUrls = [];
let loadedDrillFiles = [];
let nextDrillFileId = 1;
let previewBaseView = null;
let previewView = null;
let previewPointer = null;

const defaults = { order: 1, tool: 1, size: 1.6, initial: -0.2, final: -1, steps: 4, horizontal: 300, vertical: 100 };
const CONFIG_STORAGE_KEY = 'cucut.svg2gcode.config.v1';
let rememberedConfig = { version: 1, globals: {}, drillGlobals: {}, colors: {}, drills: {} };
const unitToMm = { mm: 1, cm: 10, in: 25.4, pt: 25.4 / 72, pc: 25.4 / 6, px: 25.4 / 96, q: 0.25 };

function setMessages(items) {
  messages.replaceChildren();
  if (!items.length) {
    const li = document.createElement('li');
    li.className = 'muted';
    li.textContent = 'No issues found.';
    messages.append(li);
    return;
  }
  for (const item of items) {
    const li = document.createElement('li');
    li.className = item.type || 'error';
    li.textContent = item.text;
    messages.append(li);
  }
}

function appendMessage(item) {
  if (messages.querySelector('.muted')) messages.replaceChildren();
  const li = document.createElement('li');
  li.className = item.type || 'error';
  li.textContent = item.text;
  messages.append(li);
}

function isRecord(value) { return value && typeof value === 'object' && !Array.isArray(value); }
function normalizeConfig(value) {
  if (!isRecord(value) || value.version !== 1) throw new Error('Unsupported configuration file version.');
  return {
    version: 1,
    globals: isRecord(value.globals) ? value.globals : {},
    drillGlobals: isRecord(value.drillGlobals) ? value.drillGlobals : {},
    colors: isRecord(value.colors) ? value.colors : {},
    drills: isRecord(value.drills) ? value.drills : {},
  };
}
function loadStoredConfig() {
  try {
    const saved = localStorage.getItem(CONFIG_STORAGE_KEY);
    if (saved) rememberedConfig = normalizeConfig(JSON.parse(saved));
  } catch {
    rememberedConfig = { version: 1, globals: {}, drillGlobals: {}, colors: {}, drills: {} };
  }
}
function drillConfigBaseKey(fileName, diameter) {
  const baseName = fileName.split(/[\\/]/).pop().trim().toLowerCase();
  return `${baseName}|${diameter === null ? 'unknown' : Number(diameter).toFixed(4)}`;
}
function drillConfigKey(fileName, diameter, fileTool) {
  return `${drillConfigBaseKey(fileName, diameter)}|T${fileTool}`;
}
function applyGlobalConfig() {
  const fields = {
    rapidHeight: '#rapid-height', feedHeight: '#feed-height', mirror: '#mirror',
  };
  for (const [key, selector] of Object.entries(fields)) {
    const value = rememberedConfig.globals[key];
    if (value !== undefined) $(selector).type === 'checkbox' ? $(selector).checked = Boolean(value) : $(selector).value = value;
  }
  const drillFields = {
    rapidHeight: '#drill-rapid-height', feedHeight: '#drill-feed-height', depth: '#drill-depth', mirror: '#drill-mirror',
  };
  for (const [key, selector] of Object.entries(drillFields)) {
    const value = rememberedConfig.drillGlobals[key];
    if (value !== undefined) $(selector).type === 'checkbox' ? $(selector).checked = Boolean(value) : $(selector).value = value;
  }
}
function applyRowConfig(row, values) {
  if (!isRecord(values)) return;
  for (const [field, value] of Object.entries(values)) {
    const input = row.querySelector(`[data-field="${field}"]`);
    if (!input || value === undefined) continue;
    if (input.type === 'checkbox') input.checked = Boolean(value);
    else input.value = value;
  }
}
function saveConfiguration() {
  const globals = {
    rapidHeight: $('#rapid-height').value, feedHeight: $('#feed-height').value, mirror: $('#mirror').checked,
  };
  const drillGlobals = {
    rapidHeight: $('#drill-rapid-height').value, feedHeight: $('#drill-feed-height').value,
    depth: $('#drill-depth').value, mirror: $('#drill-mirror').checked,
  };
  const colors = { ...rememberedConfig.colors };
  for (const row of groupsBody.querySelectorAll('tr[data-group]')) {
    const group = loadedGroups[Number(row.dataset.group)];
    if (!group) continue;
    colors[group.color] = Object.fromEntries([...row.querySelectorAll('[data-field]')].map((input) => [input.dataset.field, input.type === 'checkbox' ? input.checked : input.value]));
  }
  const drills = { ...rememberedConfig.drills };
  for (const row of $('#drill-tools-body').querySelectorAll('tr[data-file-id]')) {
    const file = loadedDrillFiles.find((item) => item.id === Number(row.dataset.fileId));
    const tool = file?.tools.find((item) => item.fileTool === Number(row.dataset.fileTool));
    if (!file || !tool) continue;
    drills[drillConfigKey(file.name, tool.diameter, tool.fileTool)] = {
      toolIndex: row.querySelector('[data-field="tool-index"]').value,
      previewDiameter: row.querySelector('[data-field="preview-diameter"]').value,
      feedSpeed: row.querySelector('[data-field="feed-speed"]').value,
    };
  }
  rememberedConfig = { version: 1, globals, drillGlobals, colors, drills };
  try {
    localStorage.setItem(CONFIG_STORAGE_KEY, JSON.stringify(rememberedConfig));
    return true;
  } catch {
    return false;
  }
}
function parseLength(value) {
  const match = String(value || '').trim().match(/^([+-]?(?:\d*\.)?\d+(?:e[+-]?\d+)?)\s*(mm|cm|in|pt|pc|px|q)?$/i);
  if (!match) return null;
  const number = Number(match[1]);
  return Number.isFinite(number) ? { number, unit: (match[2] || '').toLowerCase() } : null;
}

function mmLength(value, fallback = null) {
  const parsed = parseLength(value);
  if (!parsed) return fallback;
  if (!parsed.unit) return parsed.number * unitToMm.px;
  return parsed.number * unitToMm[parsed.unit];
}

function matrix(a = 1, b = 0, c = 0, d = 1, e = 0, f = 0) { return { a, b, c, d, e, f }; }
function multiply(left, right) {
  return matrix(
    left.a * right.a + left.c * right.b,
    left.b * right.a + left.d * right.b,
    left.a * right.c + left.c * right.d,
    left.b * right.c + left.d * right.d,
    left.a * right.e + left.c * right.f + left.e,
    left.b * right.e + left.d * right.f + left.f,
  );
}
function applyMatrix(m, p) { return { x: m.a * p.x + m.c * p.y + m.e, y: m.b * p.x + m.d * p.y + m.f }; }
function parseTransform(text) {
  let result = matrix();
  const re = /([a-zA-Z]+)\s*\(([^)]*)\)/g;
  let match;
  while ((match = re.exec(text || ''))) {
    const values = match[2].trim().split(/[\s,]+/).filter(Boolean).map(Number);
    let next = matrix();
    switch (match[1]) {
      case 'matrix': if (values.length === 6) next = matrix(...values); else throw new Error('invalid matrix transform'); break;
      case 'translate': next = matrix(1, 0, 0, 1, values[0] || 0, values.length > 1 ? values[1] : 0); break;
      case 'scale': next = matrix(values[0], 0, 0, values.length > 1 ? values[1] : values[0], 0, 0); break;
      case 'rotate': {
        const angle = values[0] * Math.PI / 180, cos = Math.cos(angle), sin = Math.sin(angle);
        const rotation = matrix(cos, sin, -sin, cos);
        if (values.length > 2) next = multiply(multiply(matrix(1, 0, 0, 1, values[1], values[2]), rotation), matrix(1, 0, 0, 1, -values[1], -values[2]));
        else next = rotation;
        break;
      }
      case 'skewX': next = matrix(1, 0, Math.tan(values[0] * Math.PI / 180), 1); break;
      case 'skewY': next = matrix(1, Math.tan(values[0] * Math.PI / 180), 0, 1); break;
      default: throw new Error(`unsupported transform ${match[1]}`);
    }
    if (Object.values(next).some((v) => !Number.isFinite(v))) throw new Error('non-finite transform');
    result = multiply(result, next);
  }
  const residue = (text || '').replace(re, '').replace(/[\s,]+/g, '');
  if (residue) throw new Error('invalid transform syntax');
  return result;
}

function rootTransform(svg) {
  const viewBox = (svg.getAttribute('viewBox') || '').trim().split(/[\s,]+/).map(Number);
  const widthRaw = svg.getAttribute('width') || '';
  const heightRaw = svg.getAttribute('height') || '';
  const widthMm = mmLength(widthRaw);
  const heightMm = mmLength(heightRaw);
  if (viewBox.length === 4 && viewBox.every(Number.isFinite) && viewBox[2] > 0 && viewBox[3] > 0) {
    const [minX, minY, vbWidth, vbHeight] = viewBox;
    const physicalWidth = widthMm ?? vbWidth * unitToMm.px;
    const physicalHeight = heightMm ?? vbHeight * unitToMm.px;
    let sx = physicalWidth / vbWidth, sy = physicalHeight / vbHeight;
    let tx = -minX * sx, ty = -minY * sy;
    const aspect = (svg.getAttribute('preserveAspectRatio') || 'xMidYMid meet').trim().split(/\s+/);
    if (aspect[0] !== 'none') {
      const uniform = aspect.includes('slice') ? Math.max(sx, sy) : Math.min(sx, sy);
      const spareX = physicalWidth - vbWidth * uniform;
      const spareY = physicalHeight - vbHeight * uniform;
      const align = aspect[0] || 'xMidYMid';
      const ax = align.includes('xMax') ? 1 : align.includes('xMid') ? 0.5 : 0;
      const ay = align.includes('YMax') ? 1 : align.includes('YMid') ? 0.5 : 0;
      sx = sy = uniform;
      tx = spareX * ax - minX * uniform;
      ty = spareY * ay - minY * uniform;
    }
    return matrix(sx, 0, 0, sy, tx, ty);
  }
  const widthPx = parseLength(widthRaw);
  const heightPx = parseLength(heightRaw);
  const sx = widthMm && widthPx ? widthMm / (widthPx.number * (widthPx.unit ? unitToMm[widthPx.unit] / unitToMm.px : 1)) : unitToMm.px;
  const sy = heightMm && heightPx ? heightMm / (heightPx.number * (heightPx.unit ? unitToMm[heightPx.unit] / unitToMm.px : 1)) : sx;
  svgWarnings.push('The SVG has no valid viewBox; interpreted its user units as CSS pixels.');
  return matrix(sx, 0, 0, sy);
}

function elementStyle(path) {
  const chain = [];
  for (let node = path; node?.nodeType === 1; node = node.parentElement) chain.unshift(node);
  const style = { stroke: 'none', strokeWidth: '1', color: '#000000' };
  const propertyNames = { stroke: 'stroke', strokeWidth: 'stroke-width', color: 'color' };
  for (const node of chain) {
    const inline = {};
    for (const part of (node.getAttribute('style') || '').split(';')) {
      const colon = part.indexOf(':');
      if (colon > 0) inline[part.slice(0, colon).trim()] = part.slice(colon + 1).trim();
    }
    for (const [key, property] of Object.entries(propertyNames)) {
      const value = inline[property] ?? node.getAttribute(property);
      if (value && value !== 'inherit') style[key] = value;
    }
  }
  if (style.stroke === 'currentColor') style.stroke = style.color;
  return style;
}

function normalizeColor(color) {
  const value = color.trim();
  if (value.toLowerCase() === 'none') return 'none';
  const canvas = document.createElement('canvas');
  const context = canvas.getContext('2d');
  if (!context) return value.toLowerCase();
  context.fillStyle = '#010203';
  context.fillStyle = value;
  const normalized = context.fillStyle.toLowerCase();
  if (normalized === '#010203' && value.toLowerCase() !== '#010203') return value.toLowerCase();
  return normalized;
}

function pathMatrix(path, root, rootMap) {
  const chain = [];
  for (let node = path; node && node !== root.parentElement; node = node.parentElement) {
    chain.unshift(node);
    if (node === root) break;
  }
  let result = rootMap;
  for (const node of chain) {
    if (node === root) {
      const transform = node.getAttribute('transform');
      if (transform) result = multiply(result, parseTransform(transform));
      continue;
    }
    const transform = node.getAttribute('transform');
    if (transform) result = multiply(result, parseTransform(transform));
  }
  return multiply(matrix(1, 0, 0, -1), result);
}

function arcCenter(from, to, rxValue, ryValue, rotation, largeArc, sweepFlag) {
  let rx = Math.abs(rxValue), ry = Math.abs(ryValue);
  if (!rx || !ry || (Math.abs(from.x - to.x) < 1e-12 && Math.abs(from.y - to.y) < 1e-12)) return null;
  const phi = rotation * Math.PI / 180, cos = Math.cos(phi), sin = Math.sin(phi);
  const dx = (from.x - to.x) / 2, dy = (from.y - to.y) / 2;
  const x1 = cos * dx + sin * dy, y1 = -sin * dx + cos * dy;
  const lambda = x1 * x1 / (rx * rx) + y1 * y1 / (ry * ry);
  if (lambda > 1) { const scale = Math.sqrt(lambda); rx *= scale; ry *= scale; }
  const numerator = Math.max(0, rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1);
  const denominator = rx * rx * y1 * y1 + ry * ry * x1 * x1;
  const sign = largeArc === sweepFlag ? -1 : 1;
  const factor = denominator ? sign * Math.sqrt(numerator / denominator) : 0;
  const cx1 = factor * rx * y1 / ry, cy1 = factor * -ry * x1 / rx;
  const center = { x: cos * cx1 - sin * cy1 + (from.x + to.x) / 2, y: sin * cx1 + cos * cy1 + (from.y + to.y) / 2 };
  const angle = (u, v) => Math.atan2(u.x * v.y - u.y * v.x, u.x * v.x + u.y * v.y);
  const u = { x: (x1 - cx1) / rx, y: (y1 - cy1) / ry };
  const v = { x: (-x1 - cx1) / rx, y: (-y1 - cy1) / ry };
  const startAngle = angle({ x: 1, y: 0 }, u);
  let delta = angle(u, v);
  if (!sweepFlag && delta > 0) delta -= 2 * Math.PI;
  if (sweepFlag && delta < 0) delta += 2 * Math.PI;
  if (largeArc && Math.abs(delta) < Math.PI) delta += sweepFlag ? 2 * Math.PI : -2 * Math.PI;
  if (!largeArc && Math.abs(delta) > Math.PI) delta += sweepFlag ? -2 * Math.PI : 2 * Math.PI;
  return { center, radius: rx, startAngle, delta };
}

function tokenizePath(d) {
  const tokenRe = /[a-zA-Z]|[-+]?(?:\d*\.)?\d+(?:e[-+]?\d+)?/gi;
  const tokens = d.match(tokenRe) || [];
  if (d.replace(tokenRe, '').replace(/[\s,]+/g, '')) throw new Error('invalid path data');
  return tokens;
}

function parsePathData(d) {
  const tokens = tokenizePath(d);
  const subpaths = [];
  let i = 0, command = null, current = { x: 0, y: 0 }, active = null;
  const isCommand = (token) => /^[a-z]$/i.test(token);
  const hasNumber = () => i < tokens.length && !isCommand(tokens[i]);
  const read = () => {
    if (!hasNumber()) throw new Error(`missing parameter for ${command}`);
    const value = Number(tokens[i++]);
    if (!Number.isFinite(value)) throw new Error('non-finite path coordinate');
    return value;
  };
  const finish = () => { if (active?.segments.length) subpaths.push(active); active = null; };
  while (i < tokens.length) {
    if (isCommand(tokens[i])) command = tokens[i++];
    else if (!command) throw new Error('path data must start with a command');
    const relative = command === command.toLowerCase();
    const upper = command.toUpperCase();
    if (upper === 'Z') {
      if (!active) throw new Error('close-path command without a subpath');
      if (Math.hypot(current.x - active.start.x, current.y - active.start.y) > 1e-12) active.segments.push({ type: 'line', from: { ...current }, to: { ...active.start } });
      current = { ...active.start }; active.closed = true; finish(); command = null; continue;
    }
    if (!['M', 'L', 'H', 'V', 'A'].includes(upper)) throw new Error(`unsupported path command ${command}`);
    if (!hasNumber()) throw new Error(`missing parameters for ${command}`);
    let first = true;
    while (hasNumber()) {
      if (upper === 'M') {
        let x = read(), y = read();
        if (relative) { x += current.x; y += current.y; }
        if (first) { finish(); active = { start: { x, y }, segments: [], closed: false }; current = { x, y }; }
        else { active.segments.push({ type: 'line', from: { ...current }, to: { x, y } }); current = { x, y }; }
      } else {
        if (!active) throw new Error('path segment appears before a move command');
        if (upper === 'L') {
          let x = read(), y = read(); if (relative) { x += current.x; y += current.y; }
          active.segments.push({ type: 'line', from: { ...current }, to: { x, y } }); current = { x, y };
        } else if (upper === 'H') {
          let x = read(); if (relative) x += current.x;
          const to = { x, y: current.y }; active.segments.push({ type: 'line', from: { ...current }, to }); current = to;
        } else if (upper === 'V') {
          let y = read(); if (relative) y += current.y;
          const to = { x: current.x, y }; active.segments.push({ type: 'line', from: { ...current }, to }); current = to;
        } else {
          const rx = read(), ry = read(), rotation = read(), large = read(), sweep = read();
          if (![0, 1].includes(large) || ![0, 1].includes(sweep)) throw new Error('arc flags must be 0 or 1');
          let x = read(), y = read(); if (relative) { x += current.x; y += current.y; }
          const to = { x, y };
          if (!rx || !ry) active.segments.push({ type: 'line', from: { ...current }, to });
          else {
            if (Math.abs(Math.abs(rx) - Math.abs(ry)) > 1e-7 * Math.max(1, Math.abs(rx), Math.abs(ry))) throw new Error('elliptical arcs cannot be converted to circular G-code arcs');
            const arc = arcCenter(current, to, rx, ry, rotation, Boolean(large), Boolean(sweep));
            if (!arc) throw new Error('full-circle or degenerate SVG arcs are not supported');
            active.segments.push({ type: 'arc', from: { ...current }, to, center: arc.center, radius: arc.radius, sweep: arc.delta });
          }
          current = to;
        }
      }
      first = false;
      if (upper === 'M') command = relative ? 'l' : 'L';
    }
  }
  finish();
  return subpaths;
}

function transformedSubpath(subpath, m, scale) {
  const det = m.a * m.d - m.b * m.c;
  const xScale = Math.hypot(m.a, m.b), yScale = Math.hypot(m.c, m.d);
  const dot = m.a * m.c + m.b * m.d;
  const segments = subpath.segments.map((segment) => {
    const from = applyMatrix(m, segment.from), to = applyMatrix(m, segment.to);
    if (segment.type === 'line') return { type: 'line', from, to };
    if (Math.abs(xScale - yScale) > 1e-7 * Math.max(xScale, yScale, 1) || Math.abs(dot) > 1e-7 * Math.max(1, xScale * yScale)) {
      throw new Error('a non-uniform or skewed transform turns this circular arc into an ellipse');
    }
    return { type: 'arc', from, to, center: applyMatrix(m, segment.center), radius: segment.radius * xScale, sweep: segment.sweep * (det < 0 ? -1 : 1) };
  });
  return { segments, closed: subpath.closed, strokeWidth: scale };
}

function angleWithin(angle, start, sweep) {
  const tau = 2 * Math.PI;
  const normalize = (a) => ((a % tau) + tau) % tau;
  if (sweep >= 0) return normalize(angle - start) <= sweep + 1e-9;
  return normalize(start - angle) <= -sweep + 1e-9;
}
function pathBounds(path) {
  const points = [];
  for (const segment of path.segments) {
    points.push(segment.from, segment.to);
    if (segment.type === 'arc') {
      const start = Math.atan2(segment.from.y - segment.center.y, segment.from.x - segment.center.x);
      for (const angle of [0, Math.PI / 2, Math.PI, Math.PI * 1.5]) {
        if (angleWithin(angle, start, segment.sweep)) points.push({ x: segment.center.x + segment.radius * Math.cos(angle), y: segment.center.y + segment.radius * Math.sin(angle) });
      }
    }
  }
  return { minX: Math.min(...points.map((p) => p.x)), minY: Math.min(...points.map((p) => p.y)), maxX: Math.max(...points.map((p) => p.x)), maxY: Math.max(...points.map((p) => p.y)) };
}

function readSvg(text) {
  svgWarnings = [];
  const doc = new DOMParser().parseFromString(text, 'image/svg+xml');
  const parserError = doc.querySelector('parsererror');
  if (parserError) throw new Error(`SVG parse failed: ${parserError.textContent.trim().replace(/\s+/g, ' ')}`);
  const root = doc.documentElement;
  if (root.localName !== 'svg') throw new Error('The selected document is not an SVG.');
  const rootMap = rootTransform(root);
  const pathElements = [...root.getElementsByTagNameNS(SVG_NS, 'path')];
  const extracted = [], errors = [];
  if (!pathElements.length) errors.push({ type: 'warning', text: 'No <path> elements were found in the SVG.' });
  pathElements.forEach((element, index) => {
    const id = element.getAttribute('id') || `path ${index + 1}`;
    try {
      const d = element.getAttribute('d');
      if (!d) throw new Error('missing d attribute');
      const style = elementStyle(element);
      const m = pathMatrix(element, root, rootMap);
      const subpaths = parsePathData(d);
      if (!subpaths.length) throw new Error('no drawable path segments found');
      const xScale = Math.hypot(m.a, m.b), yScale = Math.hypot(m.c, m.d);
      const widthValue = parseLength(style.strokeWidth);
      if (!widthValue || widthValue.number < 0) throw new Error(`invalid stroke-width "${style.strokeWidth}"`);
      let widthUnits = widthValue.number;
      if (widthValue.unit) {
        const viewBox = (root.getAttribute('viewBox') || '').trim().split(/[\s,]+/).map(Number);
        const viewportWidth = mmLength(root.getAttribute('width') || '') / unitToMm.px;
        widthUnits = widthValue.number * unitToMm[widthValue.unit] / unitToMm.px;
        if (viewBox.length === 4 && Number.isFinite(viewBox[2]) && viewportWidth) widthUnits *= viewBox[2] / viewportWidth;
      }
      const strokeWidth = widthUnits * Math.sqrt(Math.abs(m.a * m.d - m.b * m.c));
      const color = normalizeColor(style.stroke);
      const titleElement = [...element.children].find((child) => child.localName === 'title');
      const title = titleElement?.textContent.trim() || element.getAttribute('title')?.trim() || '';
      const transformed = subpaths.filter((subpath) => subpath.segments.length).map((subpath, partIndex) => {
        const path = transformedSubpath(subpath, m, strokeWidth);
        path.id = subpaths.length > 1 ? `${id} · part ${partIndex + 1}` : id;
        path.title = title;
        path.color = color;
        path.group = null;
        path.bounds = pathBounds(path);
        return path;
      });
      extracted.push(...transformed);
    } catch (error) {
      errors.push({ type: 'error', text: `${id}: ${error.message}` });
    }
  });
  const grouped = new Map();
  for (const path of extracted) {
    if (!grouped.has(path.color)) grouped.set(path.color, { color: path.color, paths: [], widths: [] });
    const group = grouped.get(path.color);
    group.paths.push(path);
    path.group = group;
    if (!group.widths.some((width) => Math.abs(width - path.strokeWidth) < 1e-6)) group.widths.push(path.strokeWidth);
  }
  return { groups: [...grouped.values()], errors };
}

function parseExcellon(text) {
  let unit = 'INCH';
  let unitDeclared = false;
  let suppression = 'LZ';
  let format = { integer: 2, decimal: 4 };
  let customFormat = false;
  let inBody = false;
  let absolute = true;
  let currentTool = null;
  const position = { x: 0, y: 0 };
  const tools = new Map();
  const errors = [];
  const ensureTool = (index) => {
    if (!tools.has(index)) tools.set(index, { fileTool: index, diameter: null, holes: [] });
    return tools.get(index);
  };
  const unitScale = () => unit === 'INCH' ? 25.4 : 1;
  const parseCoordinate = (raw) => {
    if (raw.includes('.')) return Number(raw) * unitScale();
    const sign = raw.startsWith('-') ? -1 : 1;
    let digits = raw.replace(/^[+-]/, '');
    if (suppression === 'TZ') digits = digits.padEnd(format.integer + format.decimal, '0');
    return sign * Number(digits) / (10 ** format.decimal) * unitScale();
  };

  for (const [lineIndex, sourceLine] of String(text).split(/\r?\n/).entries()) {
    const line = sourceLine.trim().toUpperCase();
    if (!line) continue;
    const formatMatch = line.match(/(?:FILE_)?FORMAT\s*=\s*(\d+)\s*:\s*(\d+)/);
    if (formatMatch) {
      format = { integer: Number(formatMatch[1]), decimal: Number(formatMatch[2]) };
      customFormat = true;
    }
    const mode = line.match(/\b(METRIC|INCH)(?:\s*,\s*(LZ|TZ))?(?:\s*,\s*(\d+)\.(\d+))?/);
    if (mode) {
      unit = mode[1]; unitDeclared = true;
      if (mode[2]) suppression = mode[2];
      if (mode[3]) { format = { integer: Number(mode[3]), decimal: Number(mode[4]) }; customFormat = true; }
      if (!customFormat) format = unit === 'METRIC' ? { integer: 3, decimal: 3 } : { integer: 2, decimal: 4 };
      continue;
    }
    if (line === '%') { inBody = true; continue; }
    if (/^M30\b/.test(line)) break;
    if (/\bG90\b/.test(line)) absolute = true;
    if (/\bG91\b/.test(line)) absolute = false;
    if (/\bG85\b/.test(line)) {
      errors.push(`line ${lineIndex + 1}: routed slot (G85) is not supported`);
      continue;
    }

    const toolMatch = line.match(/^T\s*(\d+)(?:C\s*([+-]?(?:\d+(?:\.\d*)?|\.\d+)))?$/);
    if (toolMatch) {
      const index = Number(toolMatch[1]);
      const tool = ensureTool(index);
      currentTool = index;
      if (toolMatch[2] !== undefined) {
        const diameter = Number(toolMatch[2]) * unitScale();
        if (!(diameter > 0) || !Number.isFinite(diameter)) errors.push(`line ${lineIndex + 1}: invalid diameter for T${index}`);
        else tool.diameter = diameter;
      }
      continue;
    }

    const coordinates = [...line.matchAll(/([XY])\s*([+-]?(?:\d+(?:\.\d*)?|\.\d+))/g)];
    if (!coordinates.length || !inBody) continue;
    if (currentTool === null) {
      errors.push(`line ${lineIndex + 1}: drill coordinates appear before a tool is selected`);
      continue;
    }
    const next = { ...position };
    for (const match of coordinates) {
      const axis = match[1].toLowerCase();
      const value = parseCoordinate(match[2]);
      if (!Number.isFinite(value)) { errors.push(`line ${lineIndex + 1}: invalid ${axis.toUpperCase()} coordinate`); continue; }
      next[axis] = absolute ? value : position[axis] + value;
    }
    position.x = next.x; position.y = next.y;
    ensureTool(currentTool).holes.push({ x: position.x, y: position.y });
  }

  const parsedTools = [...tools.values()];
  if (!parsedTools.length) throw new Error('no tool declarations or selections were found');
  if (!parsedTools.some((tool) => tool.holes.length)) throw new Error('no drill coordinates were found');
  if (errors.length) throw new Error(errors.join('; '));
  return { tools: parsedTools, warning: unitDeclared ? '' : 'No METRIC/INCH declaration found; assumed INCH.' };
}

function makeInput(field, value, options = {}) {
  const input = document.createElement('input');
  input.className = `cell-input${options.short ? ' short' : ''}`;
  input.type = 'number'; input.step = options.step || 'any'; input.value = value;
  input.dataset.field = field;
  input.setAttribute('aria-label', field);
  return input;
}

function renderGroups() {
  groupsBody.replaceChildren();
  if (!loadedGroups.length) {
    const row = document.createElement('tr'); row.className = 'empty-row';
    const cell = document.createElement('td'); cell.colSpan = 13; cell.textContent = 'No paths loaded.'; row.append(cell); groupsBody.append(row);
    $('#path-summary').textContent = 'Upload an SVG to begin';
    updateValidation();
    return;
  }
  loadedGroups.forEach((group, index) => {
    const row = document.createElement('tr'); row.dataset.group = String(index);
    const swatchCell = document.createElement('td'); swatchCell.className = 'swatch-cell';
    const swatch = document.createElement('span'); swatch.className = 'swatch'; swatch.style.setProperty('--swatch', group.color === 'none' ? 'transparent' : group.color); swatch.title = group.color;
    swatchCell.append(swatch);
    const detailsCell = document.createElement('td'); detailsCell.className = 'stroke-details';
    const details = document.createElement('span');
    details.textContent = `${group.color} · ${group.widths.map((width) => `${format(width)} mm`).join(', ')}`;
    detailsCell.append(details);
    const titles = group.paths.map((path) => path.title).filter(Boolean);
    if (titles.length) {
      detailsCell.append(document.createElement('br'));
      titles.forEach((title, titleIndex) => {
        if (titleIndex) detailsCell.append(document.createElement('br'));
        const titleElement = document.createElement('span');
        titleElement.className = 'path-title';
        titleElement.textContent = title;
        detailsCell.append(titleElement);
      });
    }
    row.append(swatchCell, detailsCell);
    const values = { ...defaults, order: index + 1, tool: index + 1 };
    for (const [field, value] of Object.entries(values)) row.append(makeCell(makeInput(field, value, { short: ['order', 'tool', 'steps'].includes(field) })));
    const originCell = document.createElement('td');
    const origin = document.createElement('input'); origin.type = 'checkbox'; origin.className = 'group-check'; origin.dataset.field = 'origin'; origin.setAttribute('aria-label', 'Use this group as origin and exclude it from output'); originCell.append(origin); row.append(originCell);
    const reverseCell = document.createElement('td');
    const reverse = document.createElement('input'); reverse.type = 'checkbox'; reverse.className = 'group-check'; reverse.dataset.field = 'reverse'; reverse.setAttribute('aria-label', 'Revert milling direction'); reverseCell.append(reverse); row.append(reverseCell);
    const stepCell = document.createElement('td'); stepCell.className = 'step-depth-cell';
    const step = document.createElement('output'); step.className = 'step-output'; step.dataset.stepOutput = 'true';
    step.textContent = format((defaults.final - defaults.initial) / defaults.steps);
    stepCell.append(step); row.append(stepCell);
    applyRowConfig(row, rememberedConfig.colors[group.color]);
    groupsBody.append(row);
  });
  $('#path-summary').textContent = `${loadedGroups.reduce((sum, group) => sum + group.paths.length, 0)} paths in ${loadedGroups.length} color groups`;
  $('#generate-button').disabled = false;
  updateValidation();
}
function makeCell(content) { const td = document.createElement('td'); td.append(content); return td; }
function renderDrillFiles() {
  const list = $('#drill-files');
  list.replaceChildren();
  list.hidden = loadedDrillFiles.length === 0;
  for (const file of loadedDrillFiles) {
    const item = document.createElement('li'); item.className = 'uploaded-file';
    const name = document.createElement('span'); name.className = 'uploaded-file-name'; name.textContent = `${file.name} · ${file.tools.length} tool(s)`;
    const remove = document.createElement('button'); remove.type = 'button'; remove.className = 'remove-file'; remove.dataset.removeDrill = String(file.id); remove.textContent = 'Remove';
    remove.setAttribute('aria-label', `Remove ${file.name}`);
    item.append(name, remove); list.append(item);
  }
  renderDrillTools();
}
function renderDrillTools() {
  const body = $('#drill-tools-body');
  body.replaceChildren();
  const entries = loadedDrillFiles.flatMap((file) => file.tools.map((tool) => ({ file, tool })));
  if (!entries.length) {
    const row = document.createElement('tr'); row.className = 'empty-row';
    const cell = document.createElement('td'); cell.colSpan = 6; cell.textContent = 'No drill tools loaded.'; row.append(cell); body.append(row);
  }
  for (const { file, tool } of entries) {
    const row = document.createElement('tr'); row.dataset.fileId = String(file.id); row.dataset.fileTool = String(tool.fileTool);
    const fileName = document.createElement('td'); fileName.className = 'file-name-cell'; fileName.textContent = file.name;
    const fileTool = document.createElement('td'); fileTool.textContent = `T${tool.fileTool}`;
    const diameter = document.createElement('td'); diameter.textContent = tool.diameter === null ? '—' : `${format(tool.diameter)} mm`;
    row.append(fileName, fileTool, diameter);
    row.append(makeCell(makeInput('tool-index', tool.fileTool, { short: true })));
    row.append(makeCell(makeInput('preview-diameter', tool.diameter ?? '', { short: true })));
    row.append(makeCell(makeInput('feed-speed', 100, { short: true })));
    const saved = rememberedConfig.drills[drillConfigKey(file.name, tool.diameter, tool.fileTool)]
      || rememberedConfig.drills[drillConfigBaseKey(file.name, tool.diameter)];
    if (saved) applyRowConfig(row, { 'tool-index': saved.toolIndex, 'preview-diameter': saved.previewDiameter, 'feed-speed': saved.feedSpeed });
    body.append(row);
  }
  const toolCount = entries.length;
  $('#drill-summary').textContent = loadedDrillFiles.length ? `${loadedDrillFiles.length} file(s) · ${toolCount} tool(s)` : 'No drill files loaded';
  updateValidation();
}
function format(number) { return Number.isFinite(number) ? Number(number.toFixed(4)).toString() : '—'; }

function rowSettings(row) {
  const get = (field) => row.querySelector(`[data-field="${field}"]`);
  const num = (field) => Number(get(field).value);
  return { order: num('order'), tool: num('tool'), size: num('size'), initial: num('initial'), final: num('final'), steps: num('steps'), horizontal: num('horizontal'), vertical: num('vertical'), origin: get('origin').checked, reverse: get('reverse').checked };
}
function settingsErrors(settings) {
  const errors = [];
  for (const field of ['order', 'tool', 'size', 'initial', 'final', 'steps', 'horizontal', 'vertical']) if (!Number.isFinite(settings[field])) errors.push(`${field} must be a finite number`);
  if (settings.order < 0) errors.push('order cannot be negative');
  if (!Number.isInteger(settings.tool) || settings.tool < 0) errors.push('tool index must be a non-negative integer');
  if (!(settings.size > 0)) errors.push('tool size must be greater than zero');
  if (!Number.isInteger(settings.steps) || settings.steps < 1) errors.push('steps must be a positive integer');
  if (!(settings.horizontal > 0) || !(settings.vertical > 0)) errors.push('feed speeds must be greater than zero');
  const rapid = Number($('#rapid-height').value), feed = Number($('#feed-height').value);
  if (![rapid, feed].every(Number.isFinite)) errors.push('global heights must be finite numbers');
  if (!(settings.final <= settings.initial && settings.initial <= feed && feed <= rapid)) errors.push('depth/height order is invalid');
  return errors;
}
function drillSettingsErrors(row) {
  const tool = Number(row.querySelector('[data-field="tool-index"]').value);
  const diameter = Number(row.querySelector('[data-field="preview-diameter"]').value);
  const speed = Number(row.querySelector('[data-field="feed-speed"]').value);
  const rapid = Number($('#drill-rapid-height').value);
  const feed = Number($('#drill-feed-height').value);
  const depth = Number($('#drill-depth').value);
  const errors = [];
  if (!Number.isInteger(tool) || tool < 0) errors.push('tool index must be a non-negative integer');
  if (!(diameter > 0) || !Number.isFinite(diameter)) errors.push('preview diameter must be greater than zero');
  if (!(speed > 0) || !Number.isFinite(speed)) errors.push('feed speed must be greater than zero');
  if (![rapid, feed, depth].every(Number.isFinite)) errors.push('drill heights and depth must be finite numbers');
  if (!(depth <= feed && feed <= rapid)) errors.push('drill depth/feed/rapid order is invalid');
  return errors;
}
function updateValidation() {
  const trackRows = [...groupsBody.querySelectorAll('tr[data-group]')];
  let tracksValid = true;
  for (const row of trackRows) {
    const settings = rowSettings(row); const errors = settingsErrors(settings);
    row.classList.toggle('invalid', errors.length > 0);
    const step = row.querySelector('[data-step-output]');
    step.textContent = Number.isFinite(settings.initial) && Number.isFinite(settings.final) && Number.isFinite(settings.steps) && settings.steps > 0 ? format((settings.final - settings.initial) / settings.steps) : '—';
    if (errors.length) tracksValid = false;
  }
  const drillRows = [...$('#drill-tools-body').querySelectorAll('tr[data-file-id]')];
  let drillsValid = true;
  for (const row of drillRows) {
    const errors = drillSettingsErrors(row);
    row.classList.toggle('invalid', errors.length > 0);
    if (errors.length) drillsValid = false;
  }
  $('#generate-button').disabled = !(loadedGroups.length || loadedDrillFiles.length) || !tracksValid || !drillsValid;
}

function validateAndSaveSettings() {
  updateValidation();
  saveConfiguration();
  $('#preview-panel').hidden = true;
}

function reverseSegments(segments) {
  return [...segments].reverse().map((segment) => ({ ...segment, from: segment.to, to: segment.from, ...(segment.type === 'arc' ? { sweep: -segment.sweep } : {}) }));
}
function gcodePath(path, reverse, mirror, origin) {
  let segments = reverse ? reverseSegments(path.segments) : path.segments.map((segment) => ({ ...segment }));
  const map = (p) => ({ x: (mirror ? -1 : 1) * (p.x - origin.x), y: p.y - origin.y });
  segments = segments.map((s) => ({ ...s, from: map(s.from), to: map(s.to), ...(s.type === 'arc' ? { center: map(s.center), sweep: s.sweep * (mirror ? -1 : 1) } : {}) }));
  return { segments, start: segments[0].from, end: segments[segments.length - 1].to, group: path.group, closed: path.closed };
}
function distance(a, b) { return Math.hypot(a.x - b.x, a.y - b.y); }
function routeFor(paths) {
  if (paths.length < 3) return [...paths];
  const remaining = new Set(paths), route = [];
  let current = { x: 0, y: 0 };
  while (remaining.size) {
    let best, bestDistance = Infinity;
    for (const path of remaining) {
      const d = distance(current, path.start);
      if (d < bestDistance) { best = path; bestDistance = d; }
    }
    route.push(best); remaining.delete(best); current = best.end;
  }
  const routeCost = (items) => {
    let total = distance({ x: 0, y: 0 }, items[0].start);
    for (let i = 1; i < items.length; i++) total += distance(items[i - 1].end, items[i].start);
    return total;
  };
  let improved = true, passes = 0;
  while (improved && passes++ < 12) {
    improved = false;
    for (let i = 0; i < route.length - 1; i++) {
      for (let j = i + 1; j < route.length; j++) {
        const candidate = [...route.slice(0, i), ...route.slice(i, j + 1).reverse(), ...route.slice(j + 1)];
        if (routeCost(candidate) + 1e-9 < routeCost(route)) { route.splice(0, route.length, ...candidate); improved = true; }
      }
    }
  }
  return route;
}
function routeHoles(holes) {
  if (holes.length < 3) return [...holes];
  const remaining = new Set(holes), route = [];
  let current = { x: 0, y: 0 };
  while (remaining.size) {
    let nearest, nearestDistance = Infinity;
    for (const hole of remaining) {
      const candidateDistance = distance(current, hole.start);
      if (candidateDistance < nearestDistance) { nearest = hole; nearestDistance = candidateDistance; }
    }
    route.push(nearest); remaining.delete(nearest); current = nearest.end;
  }
  let improved = true, passes = 0;
  while (improved && passes++ < 8) {
    improved = false;
    for (let i = 0; i < route.length - 1; i++) {
      const before = i ? route[i - 1].end : { x: 0, y: 0 };
      for (let j = i + 1; j < route.length; j++) {
        const after = j + 1 < route.length ? route[j + 1].start : null;
        const oldCost = distance(before, route[i].start) + (after ? distance(route[j].end, after) : 0);
        const newCost = distance(before, route[j].start) + (after ? distance(route[i].end, after) : 0);
        if (newCost + 1e-9 < oldCost) {
          for (let left = i, right = j; left < right; left++, right--) [route[left], route[right]] = [route[right], route[left]];
          improved = true;
        }
      }
    }
  }
  return route;
}
function gc(value) { const rounded = Number(value.toFixed(4)); return Object.is(rounded, -0) ? '0' : String(rounded); }
function xy(p) { return `X${gc(p.x)} Y${gc(p.y)}`; }
function zLine(z, feed) { return `G1 Z${gc(z)} F${gc(feed)}`; }

function emitPath(lines, path, settings, rapid, feed) {
  const segments = path.segments;
  const start = segments[0].from;
  const depths = [settings.initial];
  if (Math.abs(settings.final - settings.initial) > 1e-10) {
    for (let step = 1; step <= settings.steps; step++) depths.push(settings.initial + (settings.final - settings.initial) * step / settings.steps);
    depths[depths.length - 1] = settings.final;
  }
  lines.push(`G0 ${xy(start)} Z${gc(rapid)}`, `G0 Z${gc(feed)}`, zLine(depths[0], settings.vertical));
  for (let pass = 0; pass < depths.length; pass++) {
    if (pass > 0) lines.push(zLine(depths[pass], settings.vertical));
    for (const segment of segments) {
      if (segment.type === 'line') lines.push(`G1 ${xy(segment.to)} F${gc(settings.horizontal)}`);
      else {
        const command = segment.sweep >= 0 ? 'G3' : 'G2';
        const i = segment.center.x - segment.from.x, j = segment.center.y - segment.from.y;
        lines.push(`${command} ${xy(segment.to)} I${gc(i)} J${gc(j)} F${gc(settings.horizontal)}`);
      }
    }
    if (pass + 1 < depths.length) {
      const current = segments[segments.length - 1].to;
      const next = segments[0].from;
      const gap = distance(current, next);
      if (gap >= 1e-7 && gap < 0.2 * settings.size) lines.push(`G1 ${xy(next)} F${gc(settings.vertical)}`);
      else if (gap >= 0.2 * settings.size) lines.push(`G0 Z${gc(rapid)}`, `G0 ${xy(next)} Z${gc(rapid)}`, `G0 Z${gc(feed)}`);
    }
  }
  lines.push(`G0 Z${gc(rapid)}`);
}

function generateFiles() {
  const rapid = Number($('#rapid-height').value), feed = Number($('#feed-height').value);
  const rows = [...groupsBody.querySelectorAll('tr[data-group]')];
  const settingsByGroup = new Map(rows.map((row) => [loadedGroups[Number(row.dataset.group)], rowSettings(row)]));
  const invalid = rows.flatMap((row) => settingsErrors(rowSettings(row)).map((message) => `${loadedGroups[Number(row.dataset.group)].color}: ${message}`));
  if (invalid.length) throw new Error(invalid.join('; '));
  const originPaths = loadedGroups.filter((group) => settingsByGroup.get(group).origin).flatMap((group) => group.paths);
  const origin = originPaths.length
    ? { x: Math.min(...originPaths.map((path) => path.bounds.minX)), y: Math.max(...originPaths.map((path) => path.bounds.maxY)) }
    : { x: 0, y: 0 };
  const mirror = $('#mirror').checked;
  const byTool = new Map();
  for (const group of loadedGroups) {
    const settings = settingsByGroup.get(group);
    if (settings.origin) continue;
    if (!byTool.has(settings.tool)) byTool.set(settings.tool, []);
    for (const path of group.paths) byTool.get(settings.tool).push({ path: gcodePath(path, settings.reverse, mirror, origin), settings });
  }
  if (!byTool.size) return [];
  const files = [];
  for (const [tool, entries] of [...byTool].sort((a, b) => a[0] - b[0])) {
    const orders = [...new Set(entries.map(({ settings }) => settings.order))].sort((a, b) => a - b);
    const lines = ['G17 G90', 'G21', `M6 T${tool}`, 'M3 S10000', 'G54'];
    for (const order of orders) {
      const ordered = routeFor(entries.filter(({ settings }) => settings.order === order).map(({ path, settings }) => ({ ...path, settings })));
      for (const path of ordered) emitPath(lines, path, path.settings, rapid, feed);
    }
    lines.push('M5', 'G17 G90', 'M2');
    files.push({ tool, filename: `tool-${tool}.gcode`, content: `${lines.join('\n')}\n` });
  }
  return files;
}

function generateDrillFiles() {
  const rapid = Number($('#drill-rapid-height').value);
  const feed = Number($('#drill-feed-height').value);
  const depth = Number($('#drill-depth').value);
  const mirror = $('#drill-mirror').checked;
  const rows = [...$('#drill-tools-body').querySelectorAll('tr[data-file-id]')];
  const byTool = new Map();
  for (const row of rows) {
    const errors = drillSettingsErrors(row);
    if (errors.length) throw new Error(`${row.querySelector('.file-name-cell').textContent}, T${row.dataset.fileTool}: ${errors.join(', ')}`);
    const file = loadedDrillFiles.find((item) => item.id === Number(row.dataset.fileId));
    const tool = file.tools.find((item) => item.fileTool === Number(row.dataset.fileTool));
    const outputTool = Number(row.querySelector('[data-field="tool-index"]').value);
    const speed = Number(row.querySelector('[data-field="feed-speed"]').value);
    if (!byTool.has(outputTool)) byTool.set(outputTool, []);
    for (const hole of tool.holes) {
      const point = { x: mirror ? -hole.x : hole.x, y: hole.y };
      byTool.get(outputTool).push({ start: point, end: point, speed });
    }
  }
  const files = [];
  for (const [tool, holes] of [...byTool].sort((a, b) => a[0] - b[0])) {
    const lines = ['G17 G90', 'G21', `M6 T${tool}`, 'M3 S10000', 'G54'];
    for (const hole of routeHoles(holes)) {
      lines.push(`G0 ${xy(hole.start)} Z${gc(rapid)}`, `G0 Z${gc(feed)}`, zLine(depth, hole.speed), zLine(feed, hole.speed), `G0 Z${gc(rapid)}`);
    }
    lines.push('M5', 'G17 G90', 'M2');
    files.push({ tool, filename: `drill-tool-${tool}.gcode`, content: `${lines.join('\n')}\n` });
  }
  return files;
}

function previewPathData(path) {
  const point = (value) => `${gc(value.x)} ${gc(-value.y)}`;
  const first = path.segments[0].from;
  const commands = [`M ${point(first)}`];
  for (const segment of path.segments) {
    if (segment.type === 'line') commands.push(`L ${point(segment.to)}`);
    else {
      const largeArc = Math.abs(segment.sweep) > Math.PI ? 1 : 0;
      const sweepFlag = segment.sweep < 0 ? 1 : 0;
      commands.push(`A ${gc(segment.radius)} ${gc(segment.radius)} 0 ${largeArc} ${sweepFlag} ${point(segment.to)}`);
    }
  }
  if (path.closed) commands.push('Z');
  return commands.join(' ');
}
function collectPreviewGeometry() {
  const rows = [...groupsBody.querySelectorAll('tr[data-group]')];
  const settingsByGroup = new Map(rows.map((row) => [loadedGroups[Number(row.dataset.group)], rowSettings(row)]));
  const originPaths = loadedGroups.filter((group) => settingsByGroup.get(group).origin).flatMap((group) => group.paths);
  const origin = originPaths.length
    ? { x: Math.min(...originPaths.map((path) => path.bounds.minX)), y: Math.max(...originPaths.map((path) => path.bounds.maxY)) }
    : { x: 0, y: 0 };
  const mirror = $('#mirror').checked;
  const paths = [];
  for (const group of loadedGroups) {
    const settings = settingsByGroup.get(group);
    if (settings.origin) continue;
    for (const source of group.paths) paths.push({ path: gcodePath(source, settings.reverse, mirror, origin), width: settings.size, depth: settings.final });
  }
  if (paths.length) {
    const minDepth = Math.min(...paths.map((entry) => entry.depth));
    const maxDepth = Math.max(...paths.map((entry) => entry.depth));
    for (const entry of paths) entry.color = depthColor(entry.depth, minDepth, maxDepth);
    paths.sort((a, b) => b.depth - a.depth);
  }
  const circles = [];
  const drillMirror = $('#drill-mirror').checked;
  for (const row of $('#drill-tools-body').querySelectorAll('tr[data-file-id]')) {
    const file = loadedDrillFiles.find((item) => item.id === Number(row.dataset.fileId));
    const tool = file?.tools.find((item) => item.fileTool === Number(row.dataset.fileTool));
    if (!tool) continue;
    const diameter = Number(row.querySelector('[data-field="preview-diameter"]').value);
    for (const hole of tool.holes) circles.push({ x: drillMirror ? -hole.x : hole.x, y: hole.y, diameter });
  }
  return { paths, circles };
}
function depthColor(depth, minDepth, maxDepth) {
  const shallow = [0xa9, 0xd1, 0x92];
  const deep = [0x32, 0x57, 0x1d];
  const t = maxDepth === minDepth ? 0 : (maxDepth - depth) / (maxDepth - minDepth);
  const channels = shallow.map((value, index) => Math.round(value + (deep[index] - value) * t));
  return `#${channels.map((value) => value.toString(16).padStart(2, '0')).join('')}`;
}
function previewBounds(paths, circles) {
  const bounds = { minX: Infinity, minY: Infinity, maxX: -Infinity, maxY: -Infinity };
  const add = (x, y) => {
    bounds.minX = Math.min(bounds.minX, x); bounds.minY = Math.min(bounds.minY, y);
    bounds.maxX = Math.max(bounds.maxX, x); bounds.maxY = Math.max(bounds.maxY, y);
  };
  for (const { path, width } of paths) {
    for (const segment of path.segments) {
      add(segment.from.x, -segment.from.y); add(segment.to.x, -segment.to.y);
      if (segment.type === 'arc') {
        const start = Math.atan2(segment.from.y - segment.center.y, segment.from.x - segment.center.x);
        for (const angle of [0, Math.PI / 2, Math.PI, Math.PI * 1.5]) {
          if (angleWithin(angle, start, segment.sweep)) add(segment.center.x + segment.radius * Math.cos(angle), -(segment.center.y + segment.radius * Math.sin(angle)));
        }
      }
    }
  }
  for (const circle of circles) {
    const radius = circle.diameter / 2, y = -circle.y;
    add(circle.x - radius, y - radius); add(circle.x + radius, y + radius);
  }
  add(-1, 0); add(1, 0); add(0, -1); add(0, 1);
  const maxStroke = Math.max(0.2, ...paths.map((entry) => entry.width), 0.1);
  const padding = maxStroke / 2 + 2;
  bounds.minX -= padding; bounds.minY -= padding; bounds.maxX += padding; bounds.maxY += padding;
  return { x: bounds.minX, y: bounds.minY, width: bounds.maxX - bounds.minX, height: bounds.maxY - bounds.minY };
}
function setPreviewView(view) {
  previewView = { ...view };
  $('#preview-svg').setAttribute('viewBox', `${previewView.x} ${previewView.y} ${previewView.width} ${previewView.height}`);
}
function renderBoardPreview() {
  const { paths, circles } = collectPreviewGeometry();
  const view = previewBounds(paths, circles);
  previewBaseView = { ...view };
  setPreviewView(view);
  const svg = $('#preview-svg');
  svg.replaceChildren();
  const ns = 'http://www.w3.org/2000/svg';
  const element = (name, attributes) => {
    const node = document.createElementNS(ns, name);
    for (const [key, value] of Object.entries(attributes)) node.setAttribute(key, String(value));
    return node;
  };
  svg.append(element('rect', { x: view.x, y: view.y, width: view.width, height: view.height, fill: '#f5d082' }));
  for (const { path, width, color } of paths) {
    svg.append(element('path', { d: previewPathData(path), fill: 'none', stroke: color, 'stroke-width': width, 'stroke-linecap': 'round', 'stroke-linejoin': 'round' }));
  }
  for (const circle of circles) {
    const x = circle.x, y = -circle.y, radius = circle.diameter / 2;
    svg.append(element('circle', { cx: x, cy: y, r: radius, fill: '#7a7580' }));
    svg.append(element('path', { d: `M ${gc(x - radius)} ${gc(y)} L ${gc(x + radius)} ${gc(y)} M ${gc(x)} ${gc(y - radius)} L ${gc(x)} ${gc(y + radius)}`, fill: 'none', stroke: '#ffffff', 'stroke-width': 0.1, 'stroke-linecap': 'round' }));
  }
  svg.append(element('path', { d: 'M -1 0 L 1 0 M 0 -1 L 0 1', fill: 'none', stroke: '#ff0000', 'stroke-width': 0.2, 'stroke-linecap': 'round' }));
  $('#preview-panel').hidden = false;
}
function previewScale() {
  const rect = $('#preview-svg').getBoundingClientRect();
  return Math.min(rect.width / previewView.width, rect.height / previewView.height);
}
function previewPointAt(clientX, clientY) {
  const rect = $('#preview-svg').getBoundingClientRect();
  const scale = previewScale();
  const offsetX = (rect.width - previewView.width * scale) / 2;
  const offsetY = (rect.height - previewView.height * scale) / 2;
  return { x: previewView.x + (clientX - rect.left - offsetX) / scale, y: previewView.y + (clientY - rect.top - offsetY) / scale };
}
function setupPreviewNavigation() {
  const svg = $('#preview-svg');
  svg.addEventListener('wheel', (event) => {
    if (!previewView) return;
    event.preventDefault();
    const focus = previewPointAt(event.clientX, event.clientY);
    const fractionX = Math.max(0, Math.min(1, (focus.x - previewView.x) / previewView.width));
    const fractionY = Math.max(0, Math.min(1, (focus.y - previewView.y) / previewView.height));
    const factor = event.deltaY < 0 ? 0.85 : 1.15;
    const width = Math.max(previewBaseView.width / 1000, Math.min(previewBaseView.width * 100, previewView.width * factor));
    const height = Math.max(previewBaseView.height / 1000, Math.min(previewBaseView.height * 100, previewView.height * factor));
    setPreviewView({ x: focus.x - fractionX * width, y: focus.y - fractionY * height, width, height });
  }, { passive: false });
  svg.addEventListener('pointerdown', (event) => {
    if (!previewView || event.button !== 0) return;
    svg.setPointerCapture(event.pointerId);
    previewPointer = { id: event.pointerId, x: event.clientX, y: event.clientY, view: { ...previewView } };
    svg.classList.add('panning');
  });
  svg.addEventListener('pointermove', (event) => {
    if (!previewPointer || previewPointer.id !== event.pointerId) return;
    const rect = svg.getBoundingClientRect();
    const scale = Math.min(rect.width / previewPointer.view.width, rect.height / previewPointer.view.height);
    setPreviewView({ ...previewPointer.view, x: previewPointer.view.x - (event.clientX - previewPointer.x) / scale, y: previewPointer.view.y - (event.clientY - previewPointer.y) / scale });
  });
  const endPan = (event) => {
    if (previewPointer?.id !== event.pointerId) return;
    previewPointer = null; svg.classList.remove('panning');
  };
  svg.addEventListener('pointerup', endPan);
  svg.addEventListener('pointercancel', endPan);
  $('#preview-reset').addEventListener('click', () => { if (previewBaseView) setPreviewView(previewBaseView); });
}

function showOutputs(files) {
  const panel = $('#outputs-panel'), list = $('#outputs');
  outputUrls.forEach((url) => URL.revokeObjectURL(url));
  outputUrls = [];
  list.replaceChildren(); panel.hidden = false;
  for (const file of files) {
    const li = document.createElement('li'); li.className = 'success';
    const blob = new Blob([file.content], { type: 'text/plain;charset=utf-8' });
    const url = URL.createObjectURL(blob);
    outputUrls.push(url);
    const filename = file.filename || `tool-${file.tool}.gcode`;
    const link = document.createElement('a'); link.href = url; link.download = filename; link.textContent = `Download ${filename}`;
    link.style.color = 'inherit'; link.style.fontWeight = '700';
    li.append(link, document.createTextNode(` · ${file.content.split('\n').length - 1} lines`)); list.append(li);
  }
}

async function loadFile(file) {
  if (!file) return;
  $('#status').textContent = `Reading ${file.name}…`;
  $('#outputs-panel').hidden = true;
  $('#preview-panel').hidden = true;
  try {
    const result = readSvg(await file.text());
    loadedGroups = result.groups;
    loadedName = file.name;
    $('#file-name').textContent = loadedName;
    const problemMessages = [...result.errors, ...svgWarnings.map((text) => ({ type: 'warning', text }))];
    if (result.groups.length) problemMessages.unshift({ type: 'success', text: `Loaded ${result.groups.reduce((n, group) => n + group.paths.length, 0)} supported path(s) from ${file.name}.` });
    setMessages(problemMessages);
    renderGroups();
    saveConfiguration();
    $('#status').textContent = result.groups.length ? 'Review the parameters; Origin is optional and defaults to (0, 0).' : 'No usable paths found.';
  } catch (error) {
    loadedGroups = []; loadedName = '';
    $('#file-name').textContent = 'No SVG loaded';
    renderGroups(); setMessages([{ type: 'error', text: error.message }]);
    $('#status').textContent = 'Could not load this SVG.';
  }
}

async function loadDrillFiles(files) {
  $('#outputs-panel').hidden = true;
  $('#preview-panel').hidden = true;
  for (const file of files) {
    try {
      const parsed = parseExcellon(await file.text());
      loadedDrillFiles.push({ id: nextDrillFileId++, name: file.name, tools: parsed.tools });
      appendMessage({ type: 'success', text: `Loaded ${parsed.tools.reduce((sum, tool) => sum + tool.holes.length, 0)} holes from ${file.name}.` });
      if (parsed.warning) appendMessage({ type: 'warning', text: `${file.name}: ${parsed.warning}` });
    } catch (error) {
      appendMessage({ type: 'error', text: `${file.name}: ${error.message}` });
    }
  }
  renderDrillFiles();
  saveConfiguration();
  $('#status').textContent = loadedDrillFiles.length ? 'Review drill settings; Generate will create files for both loaded workflows.' : 'No valid drill files loaded.';
}

loadStoredConfig();
applyGlobalConfig();

$('#download-config').addEventListener('click', () => {
  const saved = saveConfiguration();
  const blob = new Blob([`${JSON.stringify(rememberedConfig, null, 2)}\n`], { type: 'application/json;charset=utf-8' });
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a'); link.href = url; link.download = 'cucut-config.json'; link.hidden = true;
  document.body.append(link); link.click(); link.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
  appendMessage({ type: saved ? 'success' : 'warning', text: saved ? 'Downloaded current configuration.' : 'Downloaded configuration; browser storage is unavailable.' });
});
$('#upload-config-button').addEventListener('click', () => $('#config-file-input').click());
$('#config-file-input').addEventListener('change', async (event) => {
  const file = event.target.files[0]; event.target.value = '';
  if (!file) return;
  try {
    rememberedConfig = normalizeConfig(JSON.parse(await file.text()));
    applyGlobalConfig();
    renderGroups();
    renderDrillFiles();
    saveConfiguration();
    $('#outputs-panel').hidden = true;
    $('#preview-panel').hidden = true;
    setMessages([{ type: 'success', text: `Loaded configuration from ${file.name}. Matching settings were applied to loaded files.` }]);
    $('#status').textContent = `Configuration loaded from ${file.name}.`;
  } catch (error) {
    appendMessage({ type: 'error', text: `Could not load configuration: ${error.message}` });
  }
});

$('#browse-button').addEventListener('click', () => fileInput.click());
dropZone.addEventListener('click', (event) => { if (event.target === dropZone || event.target.tagName === 'STRONG' || event.target.tagName === 'SPAN') fileInput.click(); });
dropZone.addEventListener('keydown', (event) => { if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); fileInput.click(); } });
fileInput.addEventListener('change', () => loadFile(fileInput.files[0]));
for (const eventName of ['dragenter', 'dragover']) dropZone.addEventListener(eventName, (event) => { event.preventDefault(); dropZone.classList.add('dragging'); });
for (const eventName of ['dragleave', 'drop']) dropZone.addEventListener(eventName, (event) => { event.preventDefault(); dropZone.classList.remove('dragging'); });
dropZone.addEventListener('drop', (event) => loadFile(event.dataTransfer.files[0]));
const drillFileInput = $('#drill-file-input');
const drillDropZone = $('#drill-drop-zone');
$('#drill-browse-button').addEventListener('click', () => drillFileInput.click());
drillDropZone.addEventListener('click', (event) => { if (event.target === drillDropZone || event.target.tagName === 'STRONG' || event.target.tagName === 'SPAN') drillFileInput.click(); });
drillDropZone.addEventListener('keydown', (event) => { if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); drillFileInput.click(); } });
drillFileInput.addEventListener('change', () => { const files = [...drillFileInput.files]; drillFileInput.value = ''; loadDrillFiles(files); });
for (const eventName of ['dragenter', 'dragover']) drillDropZone.addEventListener(eventName, (event) => { event.preventDefault(); drillDropZone.classList.add('dragging'); });
for (const eventName of ['dragleave', 'drop']) drillDropZone.addEventListener(eventName, (event) => { event.preventDefault(); drillDropZone.classList.remove('dragging'); });
drillDropZone.addEventListener('drop', (event) => loadDrillFiles([...event.dataTransfer.files]));
$('#drill-files').addEventListener('click', (event) => {
  const button = event.target.closest('[data-remove-drill]');
  if (!button) return;
  const file = loadedDrillFiles.find((item) => item.id === Number(button.dataset.removeDrill));
  loadedDrillFiles = loadedDrillFiles.filter((item) => item.id !== Number(button.dataset.removeDrill));
  $('#outputs-panel').hidden = true;
  $('#preview-panel').hidden = true;
  renderDrillFiles();
  saveConfiguration();
  appendMessage({ type: 'warning', text: `Removed ${file?.name || 'drill file'}.` });
  $('#status').textContent = 'Drill file removed.';
});
$('#sample-button').addEventListener('click', async () => {
  const button = $('#sample-button');
  button.disabled = true;
  try {
    const directoryUrl = new URL('data/', document.baseURI);
    const listing = await fetch(directoryUrl);
    if (!listing.ok) throw new Error(`Could not read data/ directory (${listing.status}).`);
    const listingDocument = new DOMParser().parseFromString(await listing.text(), 'text/html');
    const files = [...listingDocument.querySelectorAll('a[href]')].map((anchor) => {
      const url = new URL(anchor.getAttribute('href'), listing.url || directoryUrl);
      return { url, name: decodeURIComponent(url.pathname.split('/').pop()) };
    }).filter((file) => file.url.origin === directoryUrl.origin && file.url.pathname.startsWith(directoryUrl.pathname));
    const svgFiles = files.filter((file) => file.name.toLowerCase().endsWith('.svg'));
    const drillFiles = files.filter((file) => file.name.toLowerCase().endsWith('.drl'));
    svgFiles.sort((a, b) => Number(b.name.toLowerCase() === 'pcb.svg') - Number(a.name.toLowerCase() === 'pcb.svg') || a.name.localeCompare(b.name));
    if (!svgFiles.length && !drillFiles.length) throw new Error('No .svg or .drl links found in the data/ directory listing.');
    if (svgFiles.length) {
      const response = await fetch(svgFiles[0].url);
      if (!response.ok) throw new Error(`Could not read ${svgFiles[0].name} (${response.status}).`);
      await loadFile(new File([await response.blob()], svgFiles[0].name, { type: 'image/svg+xml' }));
    }
    const existingNames = new Set(loadedDrillFiles.map((file) => file.name));
    const newDrills = drillFiles.filter((file) => !existingNames.has(file.name));
    const drillInputs = await Promise.all(newDrills.map(async (file) => {
      const response = await fetch(file.url);
      if (!response.ok) throw new Error(`Could not read ${file.name} (${response.status}).`);
      return new File([await response.blob()], file.name, { type: 'text/plain' });
    }));
    await loadDrillFiles(drillInputs);
    $('#status').textContent = `Loaded ${svgFiles.length ? 'SVG' : 'no SVG'} and ${drillInputs.length} new drill file(s) from data/.`;
    appendMessage({ type: 'success', text: `Loaded files from the data/ directory listing.` });
  } catch (error) {
    appendMessage({ type: 'error', text: `Could not load data files: ${error.message}` });
    $('#status').textContent = 'Could not load files from data/.';
  } finally {
    button.disabled = false;
  }
});
groupsBody.addEventListener('input', validateAndSaveSettings);
groupsBody.addEventListener('change', validateAndSaveSettings);
$('#rapid-height').addEventListener('input', validateAndSaveSettings);
$('#feed-height').addEventListener('input', validateAndSaveSettings);
$('#mirror').addEventListener('change', validateAndSaveSettings);
for (const selector of ['#drill-rapid-height', '#drill-feed-height', '#drill-depth']) $(selector).addEventListener('input', validateAndSaveSettings);
$('#drill-mirror').addEventListener('change', validateAndSaveSettings);
$('#drill-tools-body').addEventListener('input', validateAndSaveSettings);
$('#drill-tools-body').addEventListener('change', validateAndSaveSettings);
setupPreviewNavigation();
$('#generate-button').addEventListener('click', () => {
  $('#preview-panel').hidden = true;
  try {
    const files = [
      ...(loadedGroups.length ? generateFiles() : []),
      ...(loadedDrillFiles.length ? generateDrillFiles() : []),
    ];
    if (!files.length) throw new Error('No output paths or drill holes remain.');
    showOutputs(files);
    renderBoardPreview();
    $('#status').textContent = `Generated ${files.length} G-code file(s). Use the preview and download links below.`;
    setMessages([{ type: 'success', text: `Generated ${files.length} file(s). SVG Origin groups, if selected, were excluded.` }]);
  } catch (error) {
    $('#status').textContent = 'G-code was not generated.';
    setMessages([{ type: 'error', text: error.message }]);
  }
});
