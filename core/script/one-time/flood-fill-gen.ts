export type FloodFillType = "edges" | "edges-or-corners";

export type FloodFillLocation = [x: number, y: number];

const varNames = {
  "edges": 'flood4',
  "edges-or-corners": 'flood8',
};

const nextLocationPriority: FloodFillLocation[] = [
  [0, 1],
  [1, 0],
  [0, 0],
  [2, 1],
  [2, 0],
  [1, 2],
  [0, 2],
  [2, 2],
];

function isConnected(
  first: FloodFillLocation,
  second: FloodFillLocation,
  type: FloodFillType,
): boolean {
  const deltaX = Math.abs(first[0] - second[0]);
  const deltaY = Math.abs(first[1] - second[1]);
  return type === "edges-or-corners"
    ? Math.max(deltaX, deltaY) === 1
    : deltaX + deltaY === 1;
}

function floodFill(
  image: number[][],
  start: FloodFillLocation,
  type: FloodFillType,
  from: number,
  to: number,
): void {
  const pending = [start];
  image[start[0]][start[1]] = to;

  while (pending.length > 0) {
    const current = pending.pop()!;
    for (let x = 0; x < 3; x += 1) {
      for (let y = 0; y < 3; y += 1) {
        const next: FloodFillLocation = [x, y];
        if (
          image[x][y] === from &&
          isConnected(current, next, type)
        ) {
          image[x][y] = to;
          pending.push(next);
        }
      }
    }
  }
}

export function generateFloodFillTasks(
  input: number[][],
  type: FloodFillType,
): FloodFillLocation[] {
  if (
    !Array.isArray(input) ||
    input.length !== 3 ||
    Array.from(input).some(
      (column) =>
        !Array.isArray(column) ||
        column.length !== 3 ||
        Array.from(column).some((pixel) => pixel !== 0 && pixel !== 1),
    )
  ) {
    throw new TypeError("Flood-fill input must be a 3x3 array containing only 0 and 1.");
  }
  if (type !== "edges" && type !== "edges-or-corners") {
    throw new TypeError(`Unsupported flood-fill type: ${String(type)}`);
  }

  const image = input.map((column) => [...column]);
  const center: FloodFillLocation = [1, 1];
  image[center[0]][center[1]] = 0;
  floodFill(image, center, type, 0, 2);
  image[center[0]][center[1]] = 1;

  const tasks: FloodFillLocation[] = [];
  while (true) {
    const next = nextLocationPriority.find(([x, y]) => image[x][y] === 2);
    if (!next) {
      return tasks;
    }

    tasks.push([next[0], next[1]]);
    floodFill(image, next, type, 2, 1);
  }
}

let file = '';

for (let type of ["edges", "edges-or-corners"] as FloodFillType[]) {
  file += `const FloodFillJobs ${varNames[type]}[256] = {\n`;
  for (let index = 0; index < 256; index++) {
      let image = [
          [7,4,2],
          [6,8,1],
          [5,3,0],
      ].map(row => row.map(bit => 1 & (index >> bit)))
      let tasks = generateFloodFillTasks(image, type);

      let art2 = image
      .map((row, x) => row.map((value, y) => {
      }).join('')).join('▏\n▕');

      let art = '';
      for (let y = 0; y < 3; y++) {
        art += '▕';
        for (let x = 0; x < 3; x++) {
          if (x == 1 && y == 1) {
            art += '▒';
          } else if (x === tasks[0]?.[0] && y === tasks[0]?.[1]) {
            art += '⊙';
          } else if (tasks.some(c => (x === c[0] && y === c[1]))) {
            art += '+';
          } else if (image[x][y] == 1) {
            art += '█';
          } else {
            art += ' ';
          }
        }          
        art += '▏\n';
      }
      art = art.trimEnd();
      if (tasks.length == 0) {
        tasks = [[1, 1]];
      }
      file += `    /* ${art.replace(/\n/g, ' *\n     * ')} */\n`;
      file += `    { .next = { .x = ${tasks[0][0] - 1}, .y = ${tasks[0][1] - 1} }, .count = ${tasks.length - 1}`;
      if (tasks.length > 1) {
        file += `, .queue = { `;
        for (let i = 1; i < tasks.length; i++) {
          let next = tasks[i];
          file += `{ .x = ${next[0] - 1}, .y = ${next[1] - 1} }`;
          if (i < tasks.length - 1) file += ', ';
        }
        file += ` }`;
      }
      file += ` },\n`;
  }
  file += `};\n\n`;
}

console.log(file);
