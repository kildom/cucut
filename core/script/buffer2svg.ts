import { readFileSync } from "node:fs";
import { writeFile } from "node:fs/promises";
import { join, parse, resolve } from "node:path";
import * as mupdf from "mupdf";

interface FilterInput {
  path: string;
  filter: "png" | "path" | "composite-path";
  opacity: number;
  color: string;
  width: number;
}

interface PathPoint {
  x: number;
  y: number;
}

interface SvgElement {
  svg: string;
  maxX: number;
  maxY: number;
}

function usage(): never {
  throw new Error(
    "Usage: npx tsx core/script/buffer2svg.ts --res <pixels-per-mm> [-o output.svg] -f png [--filter-opacity 0-100] <image.png> -f path [--filter-color RRGGBB[AA]] [--filter-width pixels] <path.raw> -f composite-path [--filter-color RRGGBB[AA]] [--filter-width pixels] <composite-path.raw> [...]",
  );
}

function parseArguments(args: string[]): { outputPath: string; resolution: number; inputs: FilterInput[] } {
  const inputs: FilterInput[] = [];
  let outputPath: string | undefined;
  let resolution: number | undefined;
  let pendingInput: Omit<FilterInput, "path"> | undefined;

  for (let index = 0; index < args.length; index++) {
    const argument = args[index];
    if (argument === "-o" || argument === "--res" || argument === "-f" ||
        argument === "--filter-opacity" || argument === "--filter-color" || argument === "--filter-width") {
      const value = args[++index];
      if (!value || value.startsWith("-")) {
        throw new Error(`Option ${argument} requires a value`);
      }

      if (argument === "-o") {
        if (outputPath !== undefined) {
          throw new Error("Only one -o option may be specified");
        }
        outputPath = value;
      } else if (argument === "--res") {
        if (resolution !== undefined) {
          throw new Error("Only one --res option may be specified");
        }
        resolution = Number(value);
        if (!Number.isFinite(resolution) || resolution <= 0) {
          throw new Error(`Resolution must be a positive number of pixels per mm: ${value}`);
        }
      } else if (argument === "-f") {
        if (pendingInput !== undefined) {
          throw new Error("Each filter must be followed by exactly one input file");
        }
        if (value !== "png" && value !== "path" && value !== "composite-path") {
          throw new Error(`Unsupported filter: ${value}`);
        }
        pendingInput = { filter: value, opacity: 100, color: "000000", width: 1 };
      } else {
        if (pendingInput === undefined) {
          throw new Error(`${argument} must follow -f <filter>`);
        }
        if (argument === "--filter-opacity") {
          if (pendingInput.filter !== "png") {
            throw new Error(`${argument} is only supported for the png filter`);
          }
          const opacity = Number(value);
          if (!Number.isFinite(opacity) || opacity < 0 || opacity > 100) {
            throw new Error(`Filter opacity must be between 0 and 100: ${value}`);
          }
          pendingInput.opacity = opacity;
        } else if (argument === "--filter-color") {
          if (pendingInput.filter === "png") {
            throw new Error(`${argument} is only supported for path filters`);
          }
          if (!/^(?:[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/.test(value.replace(/^#/, ""))) {
            throw new Error("Filter color must be a six- or eight-digit hexadecimal RGB(A) value");
          }
          pendingInput.color = value.replace(/^#/, "");
        } else {
          if (pendingInput.filter === "png") {
            throw new Error(`${argument} is only supported for path filters`);
          }
          const width = Number(value);
          if (!Number.isFinite(width) || width <= 0) {
            throw new Error(`Filter width must be a positive number of pixels: ${value}`);
          }
          pendingInput.width = width;
        }
      }
      continue;
    }

    if (argument.startsWith("-")) {
      throw new Error(`Unknown option: ${argument}`);
    }
    if (pendingInput === undefined) {
      throw new Error(`Input file must be preceded by -f <filter>: ${argument}`);
    }
    inputs.push({ ...pendingInput, path: argument });
    pendingInput = undefined;
  }

  if (pendingInput !== undefined) {
    throw new Error("Every filter or filter parameter must be followed by an input file");
  }
  if (resolution === undefined || inputs.length === 0) {
    usage();
  }

  const defaultOutput = `${parse(resolve(inputs[0].path)).name}.svg`;
  const resolvedOutput = resolve(outputPath ?? join(parse(resolve(inputs[0].path)).dir, defaultOutput));
  if (inputs.some((input) => resolve(input.path) === resolvedOutput)) {
    throw new Error("Output SVG path must not overwrite an input file");
  }
  return { outputPath: resolvedOutput, resolution, inputs };
}

function readPath(path: string): PathPoint[] {
  const data = readFileSync(path);
  if (data.length === 0 || data.length % 8 !== 0) {
    throw new Error(`${path}: expected a non-empty buffer of little-endian float x/y pairs`);
  }
  const points: PathPoint[] = [];
  for (let offset = 0; offset < data.length; offset += 8) {
    const point = { x: data.readFloatLE(offset), y: data.readFloatLE(offset + 4) };
    if (!Number.isFinite(point.x) || !Number.isFinite(point.y) || point.x < 0 || point.y < 0) {
      throw new Error(`${path}: path contains an invalid coordinate at byte ${offset}`);
    }
    points.push(point);
  }
  return points;
}

function readCompositePath(path: string): { junctions: PathPoint[]; centers: (PathPoint | undefined)[] } {
  const data = readFileSync(path);
  if (data.length < 24 || data.length % 8 !== 0 || (data.length / 8) % 2 !== 1) {
    throw new Error(`${path}: expected interleaved composite-path float x/y pairs`);
  }
  const pairCount = data.length / 8;
  const readPair = (index: number): PathPoint => ({
    x: data.readFloatLE(index * 8),
    y: data.readFloatLE(index * 8 + 4),
  });
  const junctions: PathPoint[] = [];
  const centers: (PathPoint | undefined)[] = [];
  for (let pairIndex = 0; pairIndex < pairCount; pairIndex++) {
    const point = readPair(pairIndex);
    if (pairIndex % 2 === 0) {
      if (!Number.isFinite(point.x) || !Number.isFinite(point.y) || point.x < 0 || point.y < 0) {
        throw new Error(`${path}: junction contains an invalid coordinate`);
      }
      junctions.push(point);
    } else if (Number.isFinite(point.x) && Number.isFinite(point.y)) {
      if (point.x < 0 || point.y < 0) {
        throw new Error(`${path}: arc center contains an invalid coordinate`);
      }
      centers.push(point);
    } else if (Number.isNaN(point.x) && Number.isNaN(point.y)) {
      centers.push(undefined);
    } else {
      throw new Error(`${path}: segment center must contain either two finite values or two NaN values`);
    }
  }
  const first = junctions[0];
  const last = junctions[junctions.length - 1];
  if (first.x !== last.x || first.y !== last.y) {
    throw new Error(`${path}: composite path must repeat its first junction at the end`);
  }
  return { junctions, centers };
}

function format(value: number): string {
  return Number(value.toPrecision(12)).toString();
}

function colorAttributes(color: string): string {
  const normalized = color.replace(/^#/, "");
  if (normalized.length === 6) {
    return `stroke="#${normalized}"`;
  }
  return `stroke="#${normalized.slice(0, 6)}" stroke-opacity="${format(Number.parseInt(normalized.slice(6), 16) / 255)}"`;
}

function pathElement(points: readonly PathPoint[], resolution: number, color: string, width: number): SvgElement {
  const first = points[0];
  const commands = [`M ${format(first.x / resolution)} ${format(first.y / resolution)}`];
  for (let index = 1; index < points.length; index++) {
    commands.push(`L ${format(points[index].x / resolution)} ${format(points[index].y / resolution)}`);
  }
  commands.push("Z");
  return makePathSvg(commands.join(" "), points, resolution, color, width);
}

function compositePathElement(
  inputPath: string,
  junctions: readonly PathPoint[],
  centers: readonly (PathPoint | undefined)[],
  resolution: number,
  color: string,
  width: number,
): SvgElement {
  const commands = [`M ${format(junctions[0].x / resolution)} ${format(junctions[0].y / resolution)}`];
  const boundsPoints = [...junctions];
  for (let index = 0; index < centers.length; index++) {
    const begin = junctions[index];
    const end = junctions[index + 1];
    const center = centers[index];
    if (center === undefined) {
      commands.push(`L ${format(end.x / resolution)} ${format(end.y / resolution)}`);
      continue;
    }
    const radius = Math.hypot(begin.x - center.x, begin.y - center.y);
    const endRadius = Math.hypot(end.x - center.x, end.y - center.y);
    const chord = Math.hypot(end.x - begin.x, end.y - begin.y);
    const tolerance = Math.max(1e-4, radius * 1e-5);
    if (
      radius === 0 || !Number.isFinite(radius) || !Number.isFinite(endRadius) ||
      Math.abs(radius - endRadius) > tolerance || radius + tolerance < chord
    ) {
      throw new Error(`${inputPath}: arc center does not define a valid circular arc`);
    }
    const startAngle = Math.atan2(begin.y - center.y, begin.x - center.x);
    const endAngle = Math.atan2(end.y - center.y, end.x - center.x);
    let sweep = endAngle - startAngle;
    while (sweep <= -Math.PI) sweep += Math.PI * 2;
    while (sweep > Math.PI) sweep -= Math.PI * 2;
    commands.push(
      `A ${format(radius / resolution)} ${format(radius / resolution)} 0 0 ${sweep > 0 ? 1 : 0} ${format(end.x / resolution)} ${format(end.y / resolution)}`,
    );
    for (const angle of [0, Math.PI / 2, Math.PI, Math.PI * 3 / 2]) {
      let distance = angle - startAngle;
      if (sweep > 0) {
        while (distance < 0) distance += Math.PI * 2;
        if (distance <= sweep) {
          boundsPoints.push({ x: center.x + radius * Math.cos(angle), y: center.y + radius * Math.sin(angle) });
        }
      } else {
        while (distance > 0) distance -= Math.PI * 2;
        if (distance >= sweep) {
          boundsPoints.push({ x: center.x + radius * Math.cos(angle), y: center.y + radius * Math.sin(angle) });
        }
      }
    }
  }
  commands.push("Z");
  return makePathSvg(commands.join(" "), boundsPoints, resolution, color, width);
}

function makePathSvg(
  commands: string,
  points: readonly PathPoint[],
  resolution: number,
  color: string,
  width: number,
): SvgElement {
  const strokeWidth = width / resolution;
  const padding = strokeWidth / 2;
  const svg = `<path d="${commands}" fill="none" ${colorAttributes(color)} stroke-width="${format(strokeWidth)}" stroke-linejoin="round" stroke-linecap="round"/>`;
  let maxX = 0;
  let maxY = 0;
  for (const point of points) {
    maxX = Math.max(maxX, point.x / resolution + padding);
    maxY = Math.max(maxY, point.y / resolution + padding);
  }
  return { svg, maxX, maxY };
}

function pngElement(input: FilterInput, resolution: number): SvgElement {
  const data = readFileSync(input.path);
  if (data.length < 8 || !data.subarray(0, 8).equals(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]))) {
    throw new Error(`${input.path}: expected a PNG image`);
  }
  const pixmap = new mupdf.Image(data).toPixmap();
  const width = pixmap.getWidth();
  const height = pixmap.getHeight();
  if (width <= 0 || height <= 0) {
    throw new Error(`${input.path}: invalid PNG dimensions`);
  }
  const physicalWidth = width / resolution;
  const physicalHeight = height / resolution;
  const opacity = input.opacity === 100 ? "" : ` opacity="${format(input.opacity / 100)}"`;
  const encoded = data.toString("base64");
  return {
    svg: `<image x="0" y="0" width="${format(physicalWidth)}" height="${format(physicalHeight)}" preserveAspectRatio="none" href="data:image/png;base64,${encoded}"${opacity}/>` ,
    maxX: physicalWidth,
    maxY: physicalHeight,
  };
}

function makeSvg(inputs: readonly FilterInput[], resolution: number): string {
  const elements: SvgElement[] = [];
  for (const input of inputs) {
    if (input.filter === "png") {
      elements.push(pngElement(input, resolution));
    } else if (input.filter === "path") {
      elements.push(pathElement(readPath(input.path), resolution, input.color, input.width));
    } else {
      const geometry = readCompositePath(input.path);
      elements.push(compositePathElement(input.path, geometry.junctions, geometry.centers, resolution, input.color, input.width));
    }
  }

  const maxX = Math.max(0, ...elements.map((element) => element.maxX));
  const maxY = Math.max(0, ...elements.map((element) => element.maxY));
  const contents = elements.map((element) => element.svg).join("\n  ");
  return `<?xml version="1.0" encoding="UTF-8"?>\n<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="${format(maxX)}mm" height="${format(maxY)}mm" viewBox="0 0 ${format(maxX)} ${format(maxY)}">\n  ${contents}\n</svg>\n`;
}

async function main(): Promise<void> {
  const { outputPath, resolution, inputs } = parseArguments(process.argv.slice(2));
  await writeFile(outputPath, makeSvg(inputs, resolution), "utf8");
  console.log(`Saved ${outputPath}`);
}

main().catch((error: unknown) => {
  console.error(error instanceof Error ? error.message : error);
  process.exitCode = 1;
});
