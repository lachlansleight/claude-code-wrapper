#!/usr/bin/env node

import fs from "fs/promises";
import path from "path";

function formatTimestamp(tsValue) {
  const date = new Date(tsValue);
  if (Number.isNaN(date.getTime())) {
    return null;
  }

  const pad = (n) => String(n).padStart(2, "0");
  const yyyy = date.getFullYear();
  const mm = pad(date.getMonth() + 1);
  const dd = pad(date.getDate());
  const hh = pad(date.getHours());
  const min = pad(date.getMinutes());

  return `${yyyy}-${mm}-${dd}_${hh}-${min}`;
}

function parseJsonLine(line, fileName, lineNumber) {
  try {
    return JSON.parse(line);
  } catch (error) {
    throw new Error(
      `Invalid JSON in ${fileName} at line ${lineNumber}: ${error.message}`,
    );
  }
}

async function processFile(fileName) {
  const fullPath = path.join(process.cwd(), fileName);
  const raw = await fs.readFile(fullPath, "utf8");
  const lines = raw
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter((line) => line.length > 0);

  if (lines.length === 0) {
    console.warn(`Skipping ${fileName}: file has no JSONL rows.`);
    return;
  }

  const first = parseJsonLine(lines[0], fileName, 1);
  const last = parseJsonLine(lines[lines.length - 1], fileName, lines.length);
  const items = lines.length;

  const agentName = fileName.split("-")[0];
  if (!agentName) {
    console.warn(`Skipping ${fileName}: could not extract agent name.`);
    return;
  }

  if (!Object.prototype.hasOwnProperty.call(first, "ts")) {
    console.warn(`Skipping ${fileName}: first row does not contain "ts".`);
    return;
  }

  if (!Object.prototype.hasOwnProperty.call(last, "delta_ms")) {
    console.warn(`Skipping ${fileName}: last row does not contain "delta_ms".`);
    return;
  }

  const formattedTs = formatTimestamp(first.ts);
  if (!formattedTs) {
    console.warn(`Skipping ${fileName}: invalid timestamp in "ts".`);
    return;
  }

  const durationSeconds = Math.round(Number(last.delta_ms) / 1000);
  if (!Number.isFinite(durationSeconds)) {
    console.warn(`Skipping ${fileName}: invalid numeric value in "delta_ms".`);
    return;
  }

  const nextName = `${agentName}_${formattedTs}_${items}i-${durationSeconds}s.log`;
  if (nextName === fileName) {
    console.log(`Unchanged: ${fileName}`);
    return;
  }

  const nextPath = path.join(process.cwd(), agentName, nextName);
  await fs.mkdir(path.join(process.cwd(), agentName), { recursive: true });
  try {
    await fs.access(nextPath);
    console.warn(`Skipping ${fileName}: target already exists (${nextName}).`);
    return;
  } catch {
    // File does not exist; safe to rename.
  }

  await fs.rename(fullPath, nextPath);
  console.log(`Renamed: ${fileName} -> ${nextName}`);
}

async function main() {
  const entries = await fs.readdir(process.cwd(), { withFileTypes: true });
  const logFiles = entries
    .filter((entry) => entry.isFile() && entry.name.endsWith(".log"))
    .map((entry) => entry.name);

  if (logFiles.length === 0) {
    console.log("No .log files found in current directory.");
    return;
  }

  for (const fileName of logFiles) {
    try {
      await processFile(fileName);
    } catch (error) {
      console.error(`Failed processing ${fileName}: ${error.message}`);
    }
  }
}

main().catch((error) => {
  console.error(`Fatal error: ${error.message}`);
  process.exitCode = 1;
});
