import { spawn } from "node:child_process";
import { createWriteStream } from "node:fs";
import {
  lstat,
  mkdir,
  mkdtemp,
  readFile,
  readdir,
  rename,
  rm,
} from "node:fs/promises";
import { createInterface } from "node:readline/promises";
import { stdin, stdout } from "node:process";
import { Readable } from "node:stream";
import { pipeline } from "node:stream/promises";
import { basename, dirname, isAbsolute, join, relative, resolve } from "node:path";
import { randomUUID } from "node:crypto";

interface ToolConfig {
  name: string;
  destination: string;
  archives: Record<string, string>;
}

interface DownloadConfig {
  tools: ToolConfig[];
}

function getHostKey(): string {
  const operatingSystem: Record<string, string> = {
    linux: "linux",
    darwin: "macos",
    win32: "windows",
  };
  const architecture: Record<string, string> = {
    x64: "x86_64",
    arm64: "arm64",
  };
  const os = operatingSystem[process.platform];
  const arch = architecture[process.arch];

  if (!os || !arch) {
    throw new Error(
      `Unsupported host platform or architecture: ${process.platform}-${process.arch}`,
    );
  }

  return `${os}-${arch}`;
}

async function pathExists(path: string): Promise<boolean> {
  try {
    await lstat(path);
    return true;
  } catch (error) {
    if (
      typeof error === "object" &&
      error !== null &&
      "code" in error &&
      error.code === "ENOENT"
    ) {
      return false;
    }
    throw error;
  }
}

async function askExistingDestination(destination: string): Promise<boolean> {
  if (!stdin.isTTY || !stdout.isTTY) {
    throw new Error(
      `Cannot prompt to replace ${destination} because this process is not interactive.`,
    );
  }

  const readline = createInterface({ input: stdin, output: stdout });
  try {
    while (true) {
      const answer = (
        await readline.question(
          `${destination} already exists. Skip or replace it? [s]kip/[r]eplace: `,
        )
      )
        .trim()
        .toLowerCase();

      if (answer === "s" || answer === "skip") {
        return false;
      }
      if (answer === "r" || answer === "replace") {
        return true;
      }
      stdout.write("Enter 's' to skip or 'r' to replace.\n");
    }
  } finally {
    readline.close();
  }
}

async function runTar(args: string[]): Promise<void> {
  await new Promise<void>((resolvePromise, rejectPromise) => {
    const process = spawn("tar", args, { stdio: ["ignore", "ignore", "pipe"] });
    let stderr = "";

    process.stderr.setEncoding("utf8");
    process.stderr.on("data", (chunk: string) => {
      stderr += chunk;
    });
    process.once("error", (error) => {
      rejectPromise(
        new Error(`Could not start tar to extract the archive: ${error.message}`),
      );
    });
    process.once("close", (code) => {
      if (code === 0) {
        resolvePromise();
      } else {
        rejectPromise(
          new Error(
            `tar failed to extract the archive (exit code ${code}): ${stderr.trim()}`,
          ),
        );
      }
    });
  });
}

async function downloadArchive(url: string, archivePath: string): Promise<void> {
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`Download failed with HTTP ${response.status}: ${url}`);
  }
  if (!response.body) {
    throw new Error(`Download response did not contain a file: ${url}`);
  }

  await pipeline(Readable.fromWeb(response.body), createWriteStream(archivePath));
}

async function installTool(
  root: string,
  tool: ToolConfig,
  url: string,
): Promise<void> {
  const destination = resolve(root, tool.destination);
  const destinationRelativePath = relative(root, destination);
  if (
    destinationRelativePath === "" ||
    destinationRelativePath.startsWith("..") ||
    isAbsolute(destinationRelativePath)
  ) {
    throw new Error(`Tool destination must be inside the workspace: ${tool.destination}`);
  }

  const destinationExists = await pathExists(destination);
  if (destinationExists) {
    const destinationStat = await lstat(destination);
    if (!destinationStat.isDirectory()) {
      throw new Error(`Tool destination exists but is not a directory: ${destination}`);
    }
    if (!(await askExistingDestination(destination))) {
      stdout.write(`Skipping ${tool.name}.\n`);
      return;
    }
  }

  const parent = dirname(destination);
  await mkdir(parent, { recursive: true });
  const temporaryDirectory = await mkdtemp(join(parent, ".download-tools-"));
  const archivePath = join(temporaryDirectory, basename(new URL(url).pathname));
  const extractionDirectory = join(temporaryDirectory, "extracted");
  const stagedDirectory = join(temporaryDirectory, "install");

  try {
    stdout.write(`Downloading ${tool.name}...\n`);
    await downloadArchive(url, archivePath);
    await mkdir(extractionDirectory);
    await runTar(["-xzf", archivePath, "-C", extractionDirectory]);

    const extractedEntries = await readdir(extractionDirectory);
    if (extractedEntries.length === 0) {
      throw new Error(`${tool.name} archive did not contain any files.`);
    }
    const onlyEntryPath = join(extractionDirectory, extractedEntries[0]);
    const onlyEntryStat =
      extractedEntries.length === 1 ? await lstat(onlyEntryPath) : undefined;
    const extractedRoot =
      onlyEntryStat?.isDirectory() === true ? onlyEntryPath : extractionDirectory;

    await rename(extractedRoot, stagedDirectory);

    if (destinationExists) {
      const backupDirectory = join(
        parent,
        `.${basename(destination)}.backup-${randomUUID()}`,
      );
      await rename(destination, backupDirectory);
      try {
        await rename(stagedDirectory, destination);
      } catch (error) {
        await rename(backupDirectory, destination);
        throw error;
      }
      await rm(backupDirectory, { recursive: true, force: false });
    } else {
      await rename(stagedDirectory, destination);
    }

    stdout.write(`Installed ${tool.name} to ${tool.destination}.\n`);
  } finally {
    await rm(temporaryDirectory, { recursive: true, force: true });
  }
}

async function main(): Promise<void> {
  const root = process.cwd();
  const configPath = resolve(root, "core/script/download-tools.json");
  const config = JSON.parse(await readFile(configPath, "utf8")) as DownloadConfig;
  const hostKey = getHostKey();

  if (!Array.isArray(config.tools)) {
    throw new Error(`Invalid tool configuration: "tools" must be an array.`);
  }

  for (const tool of config.tools) {
    if (
      typeof tool !== "object" ||
      tool === null ||
      typeof tool.name !== "string" ||
      typeof tool.destination !== "string" ||
      typeof tool.archives !== "object" ||
      tool.archives === null ||
      Array.isArray(tool.archives)
    ) {
      throw new Error(`Invalid tool configuration entry: ${JSON.stringify(tool)}`);
    }
    const url = tool.archives[hostKey];
    if (typeof url !== "string" || !url.startsWith("https://")) {
      throw new Error(`No download is configured for ${tool.name} on ${hostKey}.`);
    }
    await installTool(root, tool, url);
  }
}

main().catch((error: unknown) => {
  const message = error instanceof Error ? error.message : String(error);
  console.error(`Failed to download tools: ${message}`);
  process.exitCode = 1;
});