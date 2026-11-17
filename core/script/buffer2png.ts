import { readFileSync } from "node:fs";
import { writeFile } from "node:fs/promises";
import { join, parse, resolve } from "node:path";
import * as mupdf from "mupdf";

interface InputFile {
  path: string;
  outputPath: string;
  filter: string;
  color: string;
  arcColor: string;
  lineColor: string;
  drawLines: boolean;
  junctionColor: string;
  backgroundPath?: string;
}

interface PathPoint {
  x: number;
  y: number;
}

interface PngOutput {
  path: string;
  contents: Uint8Array;
}

function usage(): never {
  throw new Error(
    "Usage: npx tsx core/script/buffer2png.ts [-o output.png] [-f path [--filter-color RRGGBB] [--filter-line RRGGBB] [--filter-background background.png] | -f composite-path [--filter-arc RRGGBB] [--filter-line RRGGBB] [--filter-junctions RRGGBB] [--filter-background background.png]] <file.raw> [...]",
  );
}

function parseArguments(args: string[]): InputFile[] {
  const inputs: InputFile[] = [];
  let nextOutputPath: string | undefined;
  let nextFilter: string | undefined;
  let nextColor: string | undefined;
  let nextArcColor: string | undefined;
  let nextLineColor: string | undefined;
  let nextJunctionColor: string | undefined;
  let nextBackgroundPath: string | undefined;

  for (let index = 0; index < args.length; index++) {
    const argument = args[index];
    if (["-o", "-f", "--filter-color", "--filter-arc", "--filter-line", "--filter-junctions", "--filter-background"].includes(argument)) {
      const optionValue = args[++index];
      if (!optionValue || optionValue.startsWith("-")) {
        throw new Error(`Option ${argument} requires a value`);
      }

      if (argument === "-o") {
        if (nextOutputPath !== undefined) {
          throw new Error("Only one -o option may be specified for each input file");
        }
        nextOutputPath = optionValue;
      } else if (argument === "-f") {
        if (nextFilter !== undefined) {
          throw new Error("Only one -f option may be specified for each input file");
        }
        if (optionValue !== "path" && optionValue !== "composite-path") {
          throw new Error(`Unsupported filter: ${optionValue}`);
        }
        nextFilter = optionValue;
      } else {
        if (nextFilter === undefined) {
          throw new Error(`${argument} must follow -f <filter>`);
        }
        if (argument === "--filter-background") {
          if (nextBackgroundPath !== undefined) {
            throw new Error("Only one --filter-background option may be specified for each input file");
          }
          nextBackgroundPath = optionValue;
        } else if (argument === "--filter-color") {
          if (nextFilter !== "path") {
            throw new Error(`${argument} is only supported for the path filter`);
          }
          if (nextColor !== undefined) {
            throw new Error("Only one --filter-color option may be specified for each input file");
          }
          nextColor = optionValue;
        } else if (argument === "--filter-line") {
          if (nextFilter !== "path" && nextFilter !== "composite-path") {
            throw new Error(`${argument} requires the path or composite-path filter`);
          }
          if (nextLineColor !== undefined) {
            throw new Error("Only one --filter-line option may be specified for each input file");
          }
          nextLineColor = optionValue;
        } else {
          if (nextFilter !== "composite-path") {
            throw new Error(`${argument} is only supported for the composite-path filter`);
          }
          if (argument === "--filter-arc") {
            if (nextArcColor !== undefined) {
              throw new Error("Only one --filter-arc option may be specified for each input file");
            }
            nextArcColor = optionValue;
          } else {
            if (nextJunctionColor !== undefined) {
              throw new Error("Only one --filter-junctions option may be specified for each input file");
            }
            nextJunctionColor = optionValue;
          }
        }
      }
      continue;
    }

    if (argument.startsWith("-")) {
      throw new Error(`Unknown option: ${argument}`);
    }

    const filter = nextFilter ?? "path";
    const parsedPath = parse(resolve(argument));
    inputs.push({
      path: argument,
      outputPath: nextOutputPath ?? join(parsedPath.dir, `${parsedPath.name}.png`),
      filter,
      color: nextColor ?? "ffffff",
      arcColor: nextArcColor ?? "ff0000",
      lineColor: nextLineColor ?? "ffffff",
      drawLines: filter === "composite-path" || nextLineColor !== undefined,
      junctionColor: nextJunctionColor ?? "0000ff",
      backgroundPath: nextBackgroundPath,
    });
    nextOutputPath = undefined;
    nextFilter = undefined;
    nextColor = undefined;
    nextArcColor = undefined;
    nextLineColor = undefined;
    nextJunctionColor = undefined;
    nextBackgroundPath = undefined;
  }

  if (
    nextOutputPath !== undefined ||
    nextFilter !== undefined ||
    nextColor !== undefined ||
    nextArcColor !== undefined ||
    nextLineColor !== undefined ||
    nextJunctionColor !== undefined ||
    nextBackgroundPath !== undefined
  ) {
    throw new Error("Every output, filter, or filter parameter option must be followed by an input file");
  }
  if (inputs.length === 0) {
    usage();
  }

  const outputPaths = inputs.map((input) => resolve(input.outputPath));
  if (new Set(outputPaths).size !== outputPaths.length) {
    throw new Error("Multiple input files resolve to the same output path");
  }
  return inputs;
}

function readPath(input: InputFile): PathPoint[] {
  const data = readFileSync(input.path);
  if (data.length === 0 || data.length % 8 !== 0) {
    throw new Error(`${input.path}: expected a non-empty buffer of little-endian float x/y pairs`);
  }

  const points: PathPoint[] = [];
  for (let offset = 0; offset < data.length; offset += 8) {
    const x = data.readFloatLE(offset);
    const y = data.readFloatLE(offset + 4);
    if (!Number.isFinite(x) || !Number.isFinite(y) || x < 0 || y < 0) {
      throw new Error(`${input.path}: path contains an invalid coordinate at byte ${offset}`);
    }
    points.push({ x, y });
  }
  return points;
}

interface CompositePathGeometry {
  arcs: PathPoint[][];
  lines: PathPoint[][];
  junctions: PathPoint[];
}

const maximumRasterSamples = 2_000_000;

function readCompositePath(input: InputFile): CompositePathGeometry {
  const data = readFileSync(input.path);
  if (data.length < 24 || data.length % 8 !== 0) {
    throw new Error(`${input.path}: expected interleaved composite-path float x/y pairs`);
  }

  const pairCount = data.length / 8;
  if (pairCount % 2 !== 1) {
    throw new Error(`${input.path}: composite path must alternate junctions and segment centers`);
  }

  const readPair = (pairIndex: number): PathPoint => ({
    x: data.readFloatLE(pairIndex * 8),
    y: data.readFloatLE(pairIndex * 8 + 4),
  });
  const junctions: PathPoint[] = [];
  for (let pairIndex = 0; pairIndex < pairCount; pairIndex += 2) {
    const junction = readPair(pairIndex);
    if (!Number.isFinite(junction.x) || !Number.isFinite(junction.y) || junction.x < 0 || junction.y < 0) {
      throw new Error(`${input.path}: junction contains an invalid coordinate`);
    }
    junctions.push(junction);
  }

  const arcs: PathPoint[][] = [];
  const lines: PathPoint[][] = [];
  let sampleCount = junctions.length;
  for (let segment = 0; segment < junctions.length - 1; segment++) {
    const center = readPair(segment * 2 + 1);
    const begin = junctions[segment];
    const end = junctions[segment + 1];
    let samples: PathPoint[];
    if (Number.isFinite(center.x) && Number.isFinite(center.y)) {
      const radius = Math.hypot(begin.x - center.x, begin.y - center.y);
      const endRadius = Math.hypot(end.x - center.x, end.y - center.y);
      const chordLength = Math.hypot(end.x - begin.x, end.y - begin.y);
      const radiusTolerance = Math.max(1e-4, radius * 1e-5);
      if (
        radius === 0 ||
        !Number.isFinite(radius) ||
        !Number.isFinite(endRadius) ||
        Math.abs(radius - endRadius) > radiusTolerance ||
        radius + radiusTolerance < chordLength
      ) {
        throw new Error(`${input.path}: arc center does not define a valid minor arc`);
      }
      const startAngle = Math.atan2(begin.y - center.y, begin.x - center.x);
      const endAngle = Math.atan2(end.y - center.y, end.x - center.x);
      const sweep = (((endAngle - startAngle + Math.PI) % (2 * Math.PI)) + 2 * Math.PI) % (2 * Math.PI) - Math.PI;
      const steps = Math.max(1, Math.ceil(radius * Math.abs(sweep) * 2));
      if (!Number.isSafeInteger(steps) || sampleCount + steps + 1 > maximumRasterSamples) {
        throw new Error(`${input.path}: composite path requires too many raster samples`);
      }
      samples = [];
      for (let step = 0; step <= steps; step++) {
        const angle = startAngle + sweep * step / steps;
        samples.push({ x: center.x + radius * Math.cos(angle), y: center.y + radius * Math.sin(angle) });
      }
      arcs.push(samples);
    } else if (!Number.isFinite(center.x) && !Number.isFinite(center.y)) {
      const length = Math.hypot(end.x - begin.x, end.y - begin.y);
      const steps = Math.max(1, Math.ceil(length * 2));
      if (!Number.isSafeInteger(steps) || sampleCount + steps + 1 > maximumRasterSamples) {
        throw new Error(`${input.path}: composite path requires too many raster samples`);
      }
      samples = [];
      for (let step = 0; step <= steps; step++) {
        const fraction = step / steps;
        samples.push({
          x: begin.x + (end.x - begin.x) * fraction,
          y: begin.y + (end.y - begin.y) * fraction,
        });
      }
      lines.push(samples);
    } else {
      throw new Error(`${input.path}: segment center must contain either two finite values or two non-finite values`);
    }

    sampleCount += samples.length;
    if (samples.some((point) => !Number.isFinite(point.x) || !Number.isFinite(point.y) || point.x < 0 || point.y < 0)) {
      throw new Error(`${input.path}: rendered segment contains an invalid coordinate`);
    }
  }

  return { arcs, lines, junctions };
}

function readColor(color: string, inputPath: string): readonly [number, number, number] {
  const normalized = color.startsWith("#") ? color.slice(1) : color;
  if (!/^[0-9a-fA-F]{6}$/.test(normalized)) {
    throw new Error(`${inputPath}: filter color must be a six-digit hexadecimal RGB value`);
  }
  return [
    Number.parseInt(normalized.slice(0, 2), 16),
    Number.parseInt(normalized.slice(2, 4), 16),
    Number.parseInt(normalized.slice(4, 6), 16),
  ];
}

function loadBackground(path: string): mupdf.Pixmap {
  const image = new mupdf.Image(readFileSync(path));
  const imagePixmap = image.toPixmap();
  const pixmap = imagePixmap.convertToColorSpace(mupdf.ColorSpace.DeviceRGB, false);
  if (
    pixmap.getAlpha() ||
    pixmap.getColorSpace()?.getType() !== mupdf.ColorSpace.DeviceRGB.getType()
  ) {
    throw new Error(`${path}: MuPDF returned an unexpected background image format`);
  }
  return pixmap;
}

interface Canvas {
  pixmap: mupdf.Pixmap;
  width: number;
  height: number;
  pixels: Uint8Array | Uint8ClampedArray;
  stride: number;
}

function createCanvas(input: InputFile, pointGroups: readonly PathPoint[][]): Canvas {
  let width: number;
  let height: number;
  let pixmap: mupdf.Pixmap;
  if (input.backgroundPath !== undefined) {
    pixmap = loadBackground(input.backgroundPath);
    width = pixmap.getWidth();
    height = pixmap.getHeight();
    if (width <= 0 || height <= 0) {
      throw new Error(`${input.backgroundPath}: invalid background image dimensions`);
    }
  } else {
    let maxX = 0;
    let maxY = 0;
    for (const group of pointGroups) {
      for (const point of group) {
        maxX = Math.max(maxX, Math.round(point.x));
        maxY = Math.max(maxY, Math.round(point.y));
      }
    }
    width = maxX + 1;
    height = maxY + 1;
    const pixelBytes = width * height * 3;
    if (
      !Number.isSafeInteger(width) ||
      !Number.isSafeInteger(height) ||
      !Number.isSafeInteger(pixelBytes) ||
      pixelBytes > 0x7fffffff
    ) {
      throw new Error(`${input.path}: path requires unsupported image dimensions`);
    }
    pixmap = new mupdf.Pixmap(mupdf.ColorSpace.DeviceRGB, [0, 0, width, height], false);
    pixmap.clear(0);
  }

  const stride = pixmap.getStride();
  if (stride < width * 3) {
    throw new Error(`${input.path}: MuPDF returned an invalid RGB stride`);
  }
  return { pixmap, width, height, pixels: pixmap.getPixels(), stride };
}

function drawPoints(input: InputFile, canvas: Canvas, points: readonly PathPoint[], color: readonly [number, number, number]): void {
  for (const point of points) {
    const x = Math.round(point.x);
    const y = Math.round(point.y);
    if (x < 0 || y < 0 || x >= canvas.width || y >= canvas.height) {
      throw new Error(`${input.path}: path point (${point.x}, ${point.y}) is outside the background image`);
    }
    const offset = y * canvas.stride + x * 3;
    canvas.pixels[offset] = color[0];
    canvas.pixels[offset + 1] = color[1];
    canvas.pixels[offset + 2] = color[2];
  }
}

function sampleLineSegment(inputPath: string, begin: PathPoint, end: PathPoint): PathPoint[] {
  const length = Math.hypot(end.x - begin.x, end.y - begin.y);
  const steps = Math.max(1, Math.ceil(length * 2));
  if (!Number.isSafeInteger(steps) || steps > maximumRasterSamples) {
    throw new Error(`${inputPath}: line requires too many raster samples`);
  }
  const samples: PathPoint[] = [];
  for (let step = 0; step <= steps; step++) {
    const fraction = step / steps;
    samples.push({
      x: begin.x + (end.x - begin.x) * fraction,
      y: begin.y + (end.y - begin.y) * fraction,
    });
  }
  return samples;
}

function convertPath(input: InputFile): PngOutput {
  const points = readPath(input);
  const color = readColor(input.color, input.path);
  const lineColor = input.drawLines ? readColor(input.lineColor, input.path) : undefined;
  const canvas = createCanvas(input, [points]);
  if (lineColor !== undefined) {
    for (let index = 0; index < points.length; index++) {
      const begin = points[index];
      const end = points[(index + 1) % points.length];
      drawPoints(input, canvas, sampleLineSegment(input.path, begin, end), lineColor);
    }
  }
  drawPoints(input, canvas, points, color);
  return { path: input.outputPath, contents: canvas.pixmap.asPNG() };
}

function convertCompositePath(input: InputFile): PngOutput {
  const geometry = readCompositePath(input);
  const arcColor = readColor(input.arcColor, input.path);
  const lineColor = readColor(input.lineColor, input.path);
  const junctionColor = readColor(input.junctionColor, input.path);
  const pointGroups = [...geometry.arcs, ...geometry.lines, geometry.junctions];
  const canvas = createCanvas(input, pointGroups);

  for (const arc of geometry.arcs) {
    drawPoints(input, canvas, arc, arcColor);
  }
  for (const line of geometry.lines) {
    drawPoints(input, canvas, line, lineColor);
  }
  drawPoints(input, canvas, geometry.junctions, junctionColor);
  return { path: input.outputPath, contents: canvas.pixmap.asPNG() };
}

function convert(input: InputFile): PngOutput {
  if (input.filter === "path") {
    return convertPath(input);
  }
  if (input.filter === "composite-path") {
    return convertCompositePath(input);
  }
  throw new Error(`${input.path}: unsupported filter "${input.filter}"`);
}

async function main(): Promise<void> {
  const outputs = parseArguments(process.argv.slice(2)).map(convert);
  for (const output of outputs) {
    await writeFile(output.path, output.contents);
    console.log(`Saved ${output.path}`);
  }
}

main().catch((error: unknown) => {
  console.error(error instanceof Error ? error.message : error);
  process.exitCode = 1;
});
