import { readFileSync } from "node:fs";
import { writeFile } from "node:fs/promises";
import { join, parse, resolve } from "node:path";
import * as mupdf from "mupdf";

interface InputFile {
  path: string;
  outputPath: string;
  filter: string;
  color: string;
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
    "Usage: npx tsx core/script/buffer2png.ts [-o output.png] [-f path [--filter-color RRGGBB] [--filter-background background.png]] <file.raw> [...]",
  );
}

function parseArguments(args: string[]): InputFile[] {
  const inputs: InputFile[] = [];
  let nextOutputPath: string | undefined;
  let nextFilter: string | undefined;
  let nextColor: string | undefined;
  let nextBackgroundPath: string | undefined;

  for (let index = 0; index < args.length; index++) {
    const argument = args[index];
    if (argument === "-o" || argument === "-f" || argument === "--filter-color" || argument === "--filter-background") {
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
        nextFilter = optionValue;
      } else {
        if (nextFilter === undefined) {
          throw new Error(`${argument} must follow -f <filter>`);
        }
        if (nextFilter !== "path") {
          throw new Error(`${argument} is not supported for filter "${nextFilter}"`);
        }
        if (argument === "--filter-color") {
          if (nextColor !== undefined) {
            throw new Error("Only one --filter-color option may be specified for each input file");
          }
          nextColor = optionValue;
        } else {
          if (nextBackgroundPath !== undefined) {
            throw new Error("Only one --filter-background option may be specified for each input file");
          }
          nextBackgroundPath = optionValue;
        }
      }
      continue;
    }

    if (argument.startsWith("-")) {
      throw new Error(`Unknown option: ${argument}`);
    }

    const parsedPath = parse(resolve(argument));
    inputs.push({
      path: argument,
      outputPath: nextOutputPath ?? join(parsedPath.dir, `${parsedPath.name}.png`),
      filter: nextFilter ?? "path",
      color: nextColor ?? "ffffff",
      backgroundPath: nextBackgroundPath,
    });
    nextOutputPath = undefined;
    nextFilter = undefined;
    nextColor = undefined;
    nextBackgroundPath = undefined;
  }

  if (
    nextOutputPath !== undefined ||
    nextFilter !== undefined ||
    nextColor !== undefined ||
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

function convertPath(input: InputFile): PngOutput {
  if (input.filter !== "path") {
    throw new Error(`${input.path}: unsupported filter "${input.filter}" (available filter: path)`);
  }
  const points = readPath(input);
  const color = readColor(input.color, input.path);
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
    for (const point of points) {
      maxX = Math.max(maxX, Math.round(point.x));
      maxY = Math.max(maxY, Math.round(point.y));
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

  const outputPixels = pixmap.getPixels();
  const outputStride = pixmap.getStride();
  if (outputStride < width * 3) {
    throw new Error(`${input.path}: MuPDF returned an invalid RGB stride`);
  }
  for (const point of points) {
    const x = Math.round(point.x);
    const y = Math.round(point.y);
    if (x >= width || y >= height) {
      throw new Error(`${input.path}: path point (${point.x}, ${point.y}) is outside the background image`);
    }
    const outputOffset = y * outputStride + x * 3;
    outputPixels[outputOffset] = color[0];
    outputPixels[outputOffset + 1] = color[1];
    outputPixels[outputOffset + 2] = color[2];
  }

  return { path: input.outputPath, contents: pixmap.asPNG() };
}

async function main(): Promise<void> {
  const outputs = parseArguments(process.argv.slice(2)).map(convertPath);
  for (const output of outputs) {
    await writeFile(output.path, output.contents);
    console.log(`Saved ${output.path}`);
  }
}

main().catch((error: unknown) => {
  console.error(error instanceof Error ? error.message : error);
  process.exitCode = 1;
});
