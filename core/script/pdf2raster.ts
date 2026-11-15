import { readFileSync } from "node:fs";
import { writeFile } from "node:fs/promises";
import { join, parse, resolve } from "node:path";
import * as mupdf from "mupdf";

const auxiliaryDataSize = 48;
const rasterDataOffset = 16 + auxiliaryDataSize;
const pixelsPerInch = 25.4;
const pdfPointsPerInch = 72;
const marginMillimeters = 10;

interface SourceImage {
  path: string;
  width: number;
  height: number;
  pixels: Uint8Array;
}

interface RasterOutput {
  path: string;
  contents: Buffer;
}

function usage(): never {
  throw new Error(
    "Usage: npx tsx core/script/pdf2raster.ts <resolution-pixels-per-mm> <file.pdf> [file.pdf ...]",
  );
}

function renderPdf(path: string, scale: number): SourceImage {
  const document = mupdf.Document.openDocument(
    readFileSync(path),
    "application/pdf",
  );
  const pageCount = document.countPages();
  if (pageCount !== 1) {
    throw new Error(`${path}: expected exactly one PDF page, found ${pageCount}`);
  }

  const pixmap = document
    .loadPage(0)
    .toPixmap(mupdf.Matrix.scale(scale, scale), mupdf.ColorSpace.DeviceGray);
  if (
    pixmap.getAlpha() ||
    pixmap.getColorSpace()?.getType() !== mupdf.ColorSpace.DeviceGray.getType()
  ) {
    throw new Error(`${path}: MuPDF returned an unexpected pixmap format`);
  }

  const width = pixmap.getWidth();
  const height = pixmap.getHeight();
  const stride = pixmap.getStride();
  if (width <= 0 || height <= 0 || stride < width) {
    throw new Error(`${path}: MuPDF returned invalid image dimensions`);
  }

  const sourcePixels = pixmap.getPixels();
  const pixels = new Uint8Array(width * height);
  for (let y = 0; y < height; y++) {
    const sourceRow = y * stride;
    const outputRow = y * width;
    for (let x = 0; x < width; x++) {
      pixels[outputRow + x] = sourcePixels[sourceRow + x] <= 128 ? 1 : 0;
    }
  }

  return { path, width, height, pixels };
}

function makeRaster(
  image: SourceImage,
  left: number,
  top: number,
  width: number,
  height: number,
  outputPath: string,
): RasterOutput {
  const pixelCount = width * height;
  if (
    !Number.isSafeInteger(pixelCount) ||
    pixelCount > 0x7fffffff - rasterDataOffset
  ) {
    throw new Error(`${outputPath}: raster is too large`);
  }

  const contents = Buffer.alloc(rasterDataOffset + pixelCount);
  contents.writeInt32LE(width, 0);
  contents.writeInt32LE(height, 4);
  contents.writeInt32LE(width, 8);
  contents.writeInt32LE(1, 12);

  const data = contents.subarray(rasterDataOffset);
  for (let y = 0; y < height; y++) {
    const sourceY = top + y;
    if (sourceY < 0 || sourceY >= image.height) {
      continue;
    }

    const sourceLeft = Math.max(left, 0);
    const sourceRight = Math.min(left + width, image.width);
    if (sourceLeft >= sourceRight) {
      continue;
    }

    const sourceOffset = sourceY * image.width + sourceLeft;
    const destinationOffset = y * width + sourceLeft - left;
    data.set(
      image.pixels.subarray(sourceOffset, sourceOffset + sourceRight - sourceLeft),
      destinationOffset,
    );
  }

  return { path: outputPath, contents };
}

async function main(): Promise<void> {
  const [resolutionArgument, ...pdfPaths] = process.argv.slice(2);
  if (!resolutionArgument || pdfPaths.length === 0) {
    usage();
  }

  const resolution = Number(resolutionArgument);
  if (!Number.isFinite(resolution) || resolution <= 0) {
    throw new Error(`Resolution must be a positive number of pixels per mm: ${resolutionArgument}`);
  }

  const scale = (resolution * pixelsPerInch) / pdfPointsPerInch;
  const margin = Math.ceil(resolution * marginMillimeters);
  if (!Number.isFinite(scale) || !Number.isSafeInteger(margin)) {
    throw new Error(`Resolution is too large: ${resolutionArgument}`);
  }
  const images: SourceImage[] = [];
  let minX = Number.POSITIVE_INFINITY;
  let minY = Number.POSITIVE_INFINITY;
  let maxX = Number.NEGATIVE_INFINITY;
  let maxY = Number.NEGATIVE_INFINITY;

  for (const pdfPath of pdfPaths) {
    const image = renderPdf(pdfPath, scale);
    images.push(image);

    for (let y = 0; y < image.height; y++) {
      const rowOffset = y * image.width;
      for (let x = 0; x < image.width; x++) {
        if (image.pixels[rowOffset + x] === 1) {
          minX = Math.min(minX, x);
          minY = Math.min(minY, y);
          maxX = Math.max(maxX, x + 1);
          maxY = Math.max(maxY, y + 1);
        }
      }
    }
  }

  if (minX === Number.POSITIVE_INFINITY) {
    throw new Error("No drawing pixels (grayscale value <= 128) found in the input PDFs");
  }

  const left = minX - margin;
  const top = minY - margin;
  const width = maxX - minX + margin * 2;
  const height = maxY - minY + margin * 2;
  const outputs = images.map((image) => {
    const parsedPath = parse(resolve(image.path));
    const outputPath = join(parsedPath.dir, `${parsedPath.name}.raster`);
    return makeRaster(image, left, top, width, height, outputPath);
  });

  for (const output of outputs) {
    await writeFile(output.path, output.contents);
    console.log(`Saved ${output.path} (${width}x${height})`);
  }
}

main().catch((error: unknown) => {
  console.error(error instanceof Error ? error.message : error);
  process.exitCode = 1;
});