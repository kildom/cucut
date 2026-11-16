import { spawnSync } from "node:child_process";
import {
  mkdtempSync,
  readFileSync,
  rmSync,
  writeFileSync,
} from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, parse, resolve } from "node:path";
import { fileURLToPath } from "node:url";

type JsonValue = null | boolean | number | string | JsonValue[] | { [key: string]: JsonValue };

const filePrefix = "#FILE#";
const maximumNestingDepth = 256;

function fail(message: string): never {
  throw new Error(message);
}

function encodeValue(
  value: JsonValue,
  inputDirectory: string,
  binaryPaths: string[],
  depth = 0,
): Buffer {
  if (depth > maximumNestingDepth) {
    fail("Maximum JSON nesting depth exceeded");
  }
  if (value === null) {
    return Buffer.from([0]);
  }
  if (typeof value === "boolean") {
    const encoded = Buffer.alloc(5);
    encoded[0] = 2;
    encoded.writeUInt32LE(value ? 1 : 0, 1);
    return encoded;
  }
  if (typeof value === "number") {
    if (!Number.isFinite(value)) {
      fail("JSON numbers must be finite");
    }
    const encoded = Buffer.alloc(9);
    encoded[0] = 3;
    encoded.writeDoubleLE(value, 1);
    return encoded;
  }
  if (typeof value === "string") {
    if (value.startsWith(filePrefix)) {
      const path = value.slice(filePrefix.length);
      if (path.length === 0) {
        fail(`${filePrefix} must be followed by a file name`);
      }
      const resolvedPath = resolve(inputDirectory, path);
      const bufferIndex = binaryPaths.length + 1;
      binaryPaths.push(resolvedPath);
      const encoded = Buffer.alloc(5);
      encoded[0] = 1;
      encoded.writeUInt32LE(bufferIndex, 1);
      return encoded;
    }
    const contents = Buffer.from(value, "utf8");
    const encoded = Buffer.alloc(5 + contents.length);
    encoded[0] = 4;
    encoded.writeUInt32LE(contents.length, 1);
    contents.copy(encoded, 5);
    return encoded;
  }
  if (Array.isArray(value)) {
    const contents = Buffer.concat(
      value.map((entry) => encodeValue(entry, inputDirectory, binaryPaths, depth + 1)),
    );
    return encodeContainer(5, value.length, contents);
  }

  const entries = Object.entries(value);
  const contents = Buffer.concat(entries.map(([name, entry]) => {
    const nameBytes = Buffer.from(name, "utf8");
    const nameLength = Buffer.alloc(4);
    nameLength.writeUInt32LE(nameBytes.length);
    return Buffer.concat([
      nameLength,
      nameBytes,
      encodeValue(entry, inputDirectory, binaryPaths, depth + 1),
    ]);
  }));
  return encodeContainer(6, entries.length, contents);
}

function encodeContainer(tag: number, count: number, contents: Buffer): Buffer {
  const dataSize = 4 + contents.length;
  if (dataSize > 0xffffffff || count > 0xffffffff) {
    fail("JSON container exceeds the binJSON size limit");
  }
  const header = Buffer.alloc(9);
  header[0] = tag;
  header.writeUInt32LE(dataSize, 1);
  header.writeUInt32LE(count, 5);
  return Buffer.concat([header, contents]);
}

class BinJsonReader {
  private offset = 0;

  constructor(private readonly contents: Buffer, private readonly outputStem: string) {}

  read(): JsonValue {
    const result = this.readValue(0);
    if (this.offset !== this.contents.length) {
      fail("Unexpected trailing data in binJSON response");
    }
    return result;
  }

  private readValue(depth: number): JsonValue {
    if (depth > maximumNestingDepth) {
      fail("Maximum binJSON nesting depth exceeded");
    }
    const tag = this.readUInt8();
    switch (tag) {
      case 0:
        return null;
      case 1: {
        const bufferIndex = this.readUInt32();
        return `${filePrefix}${this.outputStem}-${bufferIndex}.raw`;
      }
      case 2:
        return this.readUInt32() !== 0;
      case 3: {
        this.ensureAvailable(8);
        const value = this.contents.readDoubleLE(this.offset);
        this.offset += 8;
        if (!Number.isFinite(value)) {
          fail("binJSON response contains a non-finite number");
        }
        return value;
      }
      case 4: {
        const length = this.readUInt32();
        return this.readBytes(length).toString("utf8");
      }
      case 5:
      case 6:
        return this.readContainer(tag, depth);
      default:
        fail(`Unknown binJSON type ${tag}`);
    }
  }

  private readContainer(tag: 5 | 6, depth: number): JsonValue {
    const dataSize = this.readUInt32();
    const count = this.readUInt32();
    if (dataSize < 4) {
      fail("Invalid binJSON container size");
    }
    const end = this.offset + dataSize - 4;
    if (end > this.contents.length) {
      fail("Unexpected end of binJSON container");
    }

    if (tag === 5) {
      const result: JsonValue[] = [];
      for (let i = 0; i < count; i++) {
        result.push(this.readValue(depth + 1));
      }
      if (this.offset !== end) {
        fail("BinJSON array size does not match its contents");
      }
      return result;
    }

    const result: { [key: string]: JsonValue } = {};
    for (let i = 0; i < count; i++) {
      const nameLength = this.readUInt32();
      const name = this.readBytes(nameLength).toString("utf8");
      result[name] = this.readValue(depth + 1);
    }
    if (this.offset !== end) {
      fail("BinJSON object size does not match its contents");
    }
    return result;
  }

  private readUInt8(): number {
    this.ensureAvailable(1);
    return this.contents[this.offset++];
  }

  private readUInt32(): number {
    this.ensureAvailable(4);
    const value = this.contents.readUInt32LE(this.offset);
    this.offset += 4;
    return value;
  }

  private readBytes(length: number): Buffer {
    this.ensureAvailable(length);
    const result = this.contents.subarray(this.offset, this.offset + length);
    this.offset += length;
    return result;
  }

  private ensureAvailable(length: number): void {
    if (length < 0 || this.offset + length > this.contents.length) {
      fail("Unexpected end of binJSON response");
    }
  }
}

function main(): void {
  const [outputArgument, inputArgument, ...extraArguments] = process.argv.slice(2);
  if (!outputArgument || !inputArgument || extraArguments.length > 0) {
    fail("Usage: npx tsx src/cucutcli.ts <output.json> <input.json>");
  }

  const outputPath = resolve(outputArgument);
  const inputPath = resolve(inputArgument);
  const outputParts = parse(outputPath);
  const outputStem = join(outputParts.dir, outputParts.name);
  const input = JSON.parse(readFileSync(inputPath, "utf8")) as JsonValue;
  const binaryPaths: string[] = [];
  const requestContents = encodeValue(input, dirname(inputPath), binaryPaths);
  const temporaryDirectory = mkdtempSync(join(tmpdir(), "cucut-cli-"));
  const requestPath = join(temporaryDirectory, "request.raw");
  writeFileSync(requestPath, requestContents);

  try {
    const scriptDirectory = dirname(fileURLToPath(import.meta.url));
    const executablePath = resolve(scriptDirectory, "../core/build/cucut-cli");
    const result = spawnSync(
      executablePath,
      [outputStem, requestPath, ...binaryPaths],
      { encoding: "utf8" },
    );
    if (result.error) {
      fail(`Cannot run core CLI (${executablePath}): ${result.error.message}`);
    }
    if (result.status !== 0) {
      fail(result.stderr.trim() || `Core CLI exited with status ${result.status}`);
    }

    const responsePath = `${outputStem}-0.raw`;
    const responseContents = readFileSync(responsePath);
    const response = new BinJsonReader(responseContents, outputStem).read();
    writeFileSync(outputPath, `${JSON.stringify(response, null, 2)}\n`);
  } finally {
    rmSync(temporaryDirectory, { recursive: true, force: true });
  }
}

try {
  main();
} catch (error) {
  console.error(error instanceof Error ? error.message : error);
  process.exitCode = 1;
}
