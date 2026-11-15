import { readFileSync } from "node:fs";
import { writeFile } from "node:fs/promises";
import { join, parse, resolve } from "node:path";
import * as mupdf from "mupdf";

const rasterDataOffset = 16 + 48;
const labelPalette: readonly (readonly [number, number, number])[] = [
  [0, 0, 0],
  [0, 0, 255],
  [255, 0, 0],
  [0, 255, 0],
  [255, 255, 255],
];

interface InputFile {
  path: string;
  outputPath: string;
  filter: string;
}

interface PngOutput {
  path: string;
  contents: Uint8Array;
}

function usage(): never {
  throw new Error(
    "Usage: npx tsx core/script/raster2png.ts [-o output.png] [-f filter] <file.raster> [...]",
  );
}

function parseArguments(args: string[]): InputFile[] {
  const inputs: InputFile[] = [];
  let nextOutputPath: string | undefined;
  let nextFilter: string | undefined;

  for (let index = 0; index < args.length; index++) {
    const argument = args[index];
    if (argument === "-o" || argument === "-f") {
      const optionValue = args[++index];
      if (!optionValue || optionValue.startsWith("-")) {
        throw new Error(`Option ${argument} requires a value`);
      }

      if (argument === "-o") {
        if (nextOutputPath !== undefined) {
          throw new Error("Only one -o option may be specified for each input file");
        }
        nextOutputPath = optionValue;
      } else {
        if (nextFilter !== undefined) {
          throw new Error("Only one -f option may be specified for each input file");
        }
        nextFilter = optionValue;
      }
      continue;
    }

    if (argument.startsWith("-")) {
      throw new Error(`Unknown option: ${argument}`);
    }

    const parsedPath = parse(resolve(argument));
    inputs.push({
      path: argument,
      outputPath:
        nextOutputPath ?? join(parsedPath.dir, `${parsedPath.name}.png`),
      filter: nextFilter ?? "labels",
    });
    nextOutputPath = undefined;
    nextFilter = undefined;
  }

  if (nextOutputPath !== undefined || nextFilter !== undefined) {
    throw new Error("Every -o or -f option must be followed by an input file");
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

function readLabel(data: Buffer, offset: number, pixelBytes: number): bigint {
  if (pixelBytes === 1) {
    return BigInt(data[offset]);
  }
  if (pixelBytes <= 6) {
    return BigInt(data.readUIntLE(offset, pixelBytes));
  }
  return data.readBigUInt64LE(offset);
}

function colorMap(labels: bigint[]): Map<bigint, readonly [number, number, number]> {
  labels.sort((left, right) => (left < right ? -1 : left > right ? 1 : 0));
  const colors = new Map<bigint, readonly [number, number, number]>();

  for (let index = 0; index < labels.length; index++) {
    const palettePosition =
      labels.length === 1
        ? 0
        : (index * (labelPalette.length - 1)) / (labels.length - 1);
    const startIndex = Math.floor(palettePosition);
    const endIndex = Math.min(startIndex + 1, labelPalette.length - 1);
    const interpolation = palettePosition - startIndex;
    const startColor = labelPalette[startIndex];
    const endColor = labelPalette[endIndex];
    colors.set(
      labels[index],
      [
        Math.round(startColor[0] + (endColor[0] - startColor[0]) * interpolation),
        Math.round(startColor[1] + (endColor[1] - startColor[1]) * interpolation),
        Math.round(startColor[2] + (endColor[2] - startColor[2]) * interpolation),
      ],
    );
  }
  return colors;
}

function convertLabels(input: InputFile): PngOutput {
  if (input.filter !== "labels") {
    throw new Error(
      `${input.path}: unsupported filter "${input.filter}" (available filter: labels)`,
    );
  }

  const data = readFileSync(input.path);
  if (data.length < rasterDataOffset) {
    throw new Error(`${input.path}: file is too small to contain a raster header`);
  }

  const width = data.readInt32LE(0);
  const height = data.readInt32LE(4);
  const stride = data.readInt32LE(8);
  const pixelBytes = data.readInt32LE(12);
  if (width <= 0 || height <= 0 || pixelBytes < 1 || pixelBytes > 8) {
    throw new Error(`${input.path}: invalid raster dimensions or pixel size`);
  }

  const rowBytes = width * pixelBytes;
  const expectedSize = rasterDataOffset + stride * height;
  if (
    !Number.isSafeInteger(rowBytes) ||
    stride < rowBytes ||
    !Number.isSafeInteger(expectedSize) ||
    expectedSize !== data.length
  ) {
    throw new Error(`${input.path}: raster data size does not match its header`);
  }

  const sourceOffset = rasterDataOffset;
  const labelSet = new Set<bigint>();
  for (let y = 0; y < height; y++) {
    const rowOffset = sourceOffset + y * stride;
    for (let x = 0; x < width; x++) {
      labelSet.add(readLabel(data, rowOffset + x * pixelBytes, pixelBytes));
    }
  }
  const colors = colorMap([...labelSet]);

  const pixmap = new mupdf.Pixmap(
    mupdf.ColorSpace.DeviceRGB,
    [0, 0, width, height],
    false,
  );
  if (
    pixmap.getAlpha() ||
    pixmap.getColorSpace()?.getType() !== mupdf.ColorSpace.DeviceRGB.getType()
  ) {
    throw new Error(`${input.path}: MuPDF returned an unexpected pixmap format`);
  }

  const outputPixels = pixmap.getPixels();
  const outputStride = pixmap.getStride();
  if (outputStride < width * 3) {
    throw new Error(`${input.path}: MuPDF returned an invalid RGB stride`);
  }
  for (let y = 0; y < height; y++) {
    const sourceRowOffset = sourceOffset + y * stride;
    const outputRowOffset = y * outputStride;
    for (let x = 0; x < width; x++) {
      const label = readLabel(data, sourceRowOffset + x * pixelBytes, pixelBytes);
      const color = colors.get(label);
      if (!color) {
        throw new Error(`${input.path}: failed to map label ${label}`);
      }
      const outputOffset = outputRowOffset + x * 3;
      outputPixels[outputOffset] = color[0];
      outputPixels[outputOffset + 1] = color[1];
      outputPixels[outputOffset + 2] = color[2];
    }
  }

  return { path: input.outputPath, contents: pixmap.asPNG() };
}

async function main(): Promise<void> {
  const outputs = parseArguments(process.argv.slice(2)).map(convertLabels);
  for (const output of outputs) {
    await writeFile(output.path, output.contents);
    console.log(`Saved ${output.path}`);
  }
}

main().catch((error: unknown) => {
  console.error(error instanceof Error ? error.message : error);
  process.exitCode = 1;
});